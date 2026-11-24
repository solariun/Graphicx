// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  C++11, no dependencies (the optional image loader bundles stb_image).
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   tools/gfx-play.cpp
//  Layer:  Tools
//
//  What this file does
//  -------------------
//  A demo video player for the terminal (picture only, no sound).  Any file
//  ffmpeg can read (MP4, MKV, WebM, GIF, ...) plays at its real frame rate
//  in any of the three styles:
//
//      gfx-play movie.mp4
//      gfx-play -s ascii-color --loop clip.webm
//
//  Keys: space = pause / resume, q or Esc = quit.
//
//  How it fits in
//  --------------
//  1. ffprobe tells the video's size, frame rate and duration.
//  2. gfx::fit() picks the largest character grid that fits the terminal
//     (keeping proportions, one line left for the status bar).
//  3. ffmpeg decodes and scales every frame to exactly that grid in pixels
//     (columns x rows*2) and writes raw RGB24 bytes into a pipe.
//  4. Each frame becomes a gfx::Image; gfx::Screen sends only the cells that
//     changed; gfx::terminal::Session handles the screen and keys.
//  Timing: frame n is due at start + n / fps.  When drawing falls behind,
//  frames are read but not drawn (dropped), so the video never slows down.
//  A terminal resize restarts ffmpeg at the current position with the new
//  grid size.
//
//  Terminal notes
//  --------------
//  The amount of text per frame is the limit: full-color half blocks at a
//  large size can need a few MB/s.  ASCII styles and smaller windows are much
//  lighter.  Terminals with synchronized output (kitty, iTerm2, WezTerm...)
//  show each frame without tearing.
//
//  Contributing
//  ------------
//  ffmpeg / ffprobe are started with fork + exec (no shell), so file names
//  with spaces or quotes are safe.  Requires ffmpeg in the PATH (macOS:
//  `brew install ffmpeg`, Debian/Ubuntu: `apt install ffmpeg`) or --ffmpeg /
//  --ffprobe paths.
// ============================================================================
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "graphicx/graphicx.h"
#include "graphicx/terminal.h"

namespace {

typedef std::chrono::steady_clock Clock;

// A child process whose standard output we read (fork + exec, no shell).
class Process {
public:
    Process() : pid_(-1), fd_(-1) {}
    ~Process() { close(); }

    bool start(const std::vector<std::string>& args) {
        close();
        int pipeFds[2];
        if (pipe(pipeFds) != 0) return false;
        pid_ = fork();
        if (pid_ < 0) return false;
        if (pid_ == 0) {
            // Child: stdout -> pipe, stdin and stderr -> /dev/null.
            dup2(pipeFds[1], STDOUT_FILENO);
            ::close(pipeFds[0]);
            ::close(pipeFds[1]);
            FILE* null = std::fopen("/dev/null", "r+");
            if (null) {
                dup2(fileno(null), STDIN_FILENO);
                dup2(fileno(null), STDERR_FILENO);
            }
            std::vector<char*> argv;
            for (size_t i = 0; i < args.size(); ++i) argv.push_back(const_cast<char*>(args[i].c_str()));
            argv.push_back(0);
            execvp(argv[0], &argv[0]);
            _exit(127);  // program not found
        }
        ::close(pipeFds[1]);
        fd_ = pipeFds[0];
        return true;
    }

    // Reads exactly `size` bytes; false at end of stream.
    bool readExactly(void* buffer, size_t size) {
        char* p = static_cast<char*>(buffer);
        while (size > 0) {
            ssize_t n = ::read(fd_, p, size);
            if (n <= 0) return false;
            p += n;
            size -= static_cast<size_t>(n);
        }
        return true;
    }

    std::string readAll() {
        std::string out;
        char buf[4096];
        for (;;) {
            ssize_t n = ::read(fd_, buf, sizeof(buf));
            if (n <= 0) break;
            out.append(buf, static_cast<size_t>(n));
        }
        return out;
    }

    // Stops the process (if still running) and reaps it; returns its exit code.
    int close() {
        int status = 0;
        if (fd_ >= 0) ::close(fd_);
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            waitpid(pid_, &status, 0);
        }
        fd_ = -1;
        pid_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    int wait() {
        int status = 0;
        if (fd_ >= 0) ::close(fd_);
        if (pid_ > 0) waitpid(pid_, &status, 0);
        fd_ = -1;
        pid_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

private:
    pid_t pid_;
    int fd_;
};

struct VideoInfo {
    int width, height;
    double fps;
    double duration;  // seconds, 0 if unknown
};

// "30000/1001" -> 29.97
double parseRate(const std::string& s) {
    size_t slash = s.find('/');
    double num = std::atof(s.substr(0, slash).c_str());
    double den = slash == std::string::npos ? 1.0 : std::atof(s.substr(slash + 1).c_str());
    return den > 0 ? num / den : 0;
}

bool probe(const std::string& ffprobe, const std::string& path, VideoInfo& info) {
    std::vector<std::string> args;
    args.push_back(ffprobe);
    args.push_back("-v");
    args.push_back("error");
    args.push_back("-select_streams");
    args.push_back("v:0");
    args.push_back("-show_entries");
    args.push_back("stream=width,height,avg_frame_rate,r_frame_rate:format=duration");
    args.push_back("-of");
    args.push_back("default=noprint_wrappers=1");
    args.push_back(path);
    Process p;
    if (!p.start(args)) return false;
    std::string out = p.readAll();
    p.wait();

    info.width = info.height = 0;
    info.fps = info.duration = 0;
    double avg = 0, real = 0;
    size_t start = 0;
    while (start < out.size()) {
        size_t end = out.find('\n', start);
        std::string line = out.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? out.size() : end + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "width") info.width = std::atoi(value.c_str());
        if (key == "height") info.height = std::atoi(value.c_str());
        if (key == "avg_frame_rate") avg = parseRate(value);
        if (key == "r_frame_rate") real = parseRate(value);
        if (key == "duration") info.duration = std::atof(value.c_str());
    }
    info.fps = avg > 0 ? avg : (real > 0 ? real : 25.0);
    return info.width > 0 && info.height > 0;
}

bool startDecoder(Process& decoder, const std::string& ffmpeg, const std::string& path, int width, int height,
                  double startSeconds) {
    char scale[64], seek[32];
    std::snprintf(scale, sizeof(scale), "scale=%d:%d:flags=area", width, height);
    std::snprintf(seek, sizeof(seek), "%.3f", startSeconds);
    std::vector<std::string> args;
    args.push_back(ffmpeg);
    args.push_back("-v");
    args.push_back("error");
    args.push_back("-nostdin");
    if (startSeconds > 0) {
        args.push_back("-ss");
        args.push_back(seek);
    }
    args.push_back("-i");
    args.push_back(path);
    args.push_back("-an");  // no sound: this is a picture-only demo
    args.push_back("-sn");
    args.push_back("-vf");
    args.push_back(scale);
    args.push_back("-pix_fmt");
    args.push_back("rgb24");
    args.push_back("-f");
    args.push_back("rawvideo");
    args.push_back("-");
    return decoder.start(args);
}

void usage() {
    std::printf(
        "gfx-play - play a video in the terminal (picture only, no sound)\n"
        "\n"
        "usage: gfx-play [options] VIDEO\n"
        "\n"
        "  -s, --style S      blocks (default) | ascii-color | ascii\n"
        "      --truecolor    24-bit colors instead of the 256-color palette\n"
        "      --invert       ASCII styles: for light terminal backgrounds\n"
        "      --contrast P   contrast change in percent, e.g. 10%% or -10%%\n"
        "      --loop         start again at the end\n"
        "      --ffmpeg PATH  ffmpeg program (default: ffmpeg from the PATH)\n"
        "      --ffprobe PATH ffprobe program (default: ffprobe from the PATH)\n"
        "  -h, --help\n"
        "\n"
        "keys: space = pause / resume, q or Esc = quit\n"
        "needs ffmpeg: macOS `brew install ffmpeg`, Debian/Ubuntu `apt install ffmpeg`\n");
}

std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

int main(int argc, char** argv) {
    gfx::Options options;
    options.filter = gfx::Filter::Nearest;  // ffmpeg already scaled to the grid
    std::string path, ffmpeg = "ffmpeg", ffprobe = "ffprobe";
    bool loop = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        bool hasValue = i + 1 < argc;
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if ((a == "-s" || a == "--style") && hasValue) {
            if (!gfx::parseStyle(argv[++i], options.style)) {
                std::fprintf(stderr, "error: unknown style '%s' (blocks, ascii-color, ascii)\n", argv[i]);
                return 2;
            }
        } else if (a == "--truecolor") {
            options.colorMode = gfx::ColorMode::TrueColor;
        } else if (a == "--invert") {
            options.invert = true;
        } else if (a == "--contrast" && hasValue) {
            if (!gfx::parsePercent(argv[++i], options.contrast)) {
                std::fprintf(stderr, "error: --contrast takes a percentage, e.g. 10%% or -10%%\n");
                return 2;
            }
        } else if (a == "--loop") {
            loop = true;
        } else if (a == "--ffmpeg" && hasValue) {
            ffmpeg = argv[++i];
        } else if (a == "--ffprobe" && hasValue) {
            ffprobe = argv[++i];
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "error: unknown option %s\n\n", a.c_str());
            usage();
            return 2;
        } else {
            path = a;
        }
    }
    if (path.empty()) {
        usage();
        return 2;
    }
    if (!gfx::terminal::isTerminal()) {
        std::fprintf(stderr, "error: gfx-play needs a terminal\n");
        return 2;
    }

    VideoInfo info;
    if (!probe(ffprobe, path, info)) {
        std::fprintf(stderr,
                     "error: cannot read video information from %s\n"
                     "       (is ffmpeg installed? macOS: brew install ffmpeg, Linux: apt install ffmpeg)\n",
                     path.c_str());
        return 1;
    }

    const std::string name = baseName(path);
    gfx::Screen screen(options);
    std::vector<std::uint8_t> buffer;
    gfx::Image frame;
    Process decoder;
    int pixelWidth = 0, pixelHeight = 0;
    const Clock::duration frameTime =
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / info.fps));

    gfx::terminal::Session session;

    // (Re)starts decoding at `position` seconds for the current terminal size.
    auto restart = [&](double position) -> bool {
        gfx::terminal::Size s = gfx::terminal::size();
        int columns, rows;
        gfx::fit(info.width, info.height, s.columns, s.rows - 1, columns, rows);
        pixelWidth = columns;
        pixelHeight = rows * 2;
        buffer.resize(static_cast<size_t>(pixelWidth) * pixelHeight * 3);
        gfx::Options o = screen.options();
        o.columns = columns;
        o.rows = rows;
        screen.setOptions(o);
        screen.setOrigin(1 + (s.rows - 1 - rows) / 2, 1 + (s.columns - columns) / 2);
        session.write("\x1b[0m\x1b[2J");
        return startDecoder(decoder, ffmpeg, path, pixelWidth, pixelHeight, position);
    };
    if (!restart(0)) return 1;

    long frameIndex = 0;            // frames read since `startTime`
    double startPosition = 0;       // video time (s) of frame 0 of this run
    Clock::time_point startTime = Clock::now();
    bool paused = false;
    long drawn = 0, dropped = 0;
    int dropStreak = 0;
    std::string out, status;

    for (;;) {
        if (gfx::terminal::Session::interrupted()) break;
        double position = startPosition + frameIndex / info.fps;
        if (gfx::terminal::Session::resized()) {
            startPosition = position;
            frameIndex = 0;
            startTime = Clock::now();
            if (!restart(position)) break;
        }

        // Keys (and, while paused, waiting).
        int key = session.readKey(paused ? 100 : 0);
        if (key == 'q' || key == gfx::terminal::KeyEscape) break;
        if (key == ' ') {
            paused = !paused;
            if (!paused) {  // resume: restart the clock at the current frame
                startPosition = position;
                frameIndex = 0;
                startTime = Clock::now();
            }
        }
        if (paused) {
            session.write("\x1b[" + std::to_string(gfx::terminal::size().rows) + ";1H\x1b[0m\x1b[2K\x1b[7m " + name +
                          " | PAUSED - space resumes, q quits \x1b[0m");
            continue;
        }

        // Next frame from ffmpeg.
        if (!decoder.readExactly(&buffer[0], buffer.size())) {
            decoder.wait();
            if (!loop) break;
            startPosition = 0;
            frameIndex = 0;
            startTime = Clock::now();
            if (!restart(0)) break;
            continue;
        }
        ++frameIndex;

        // Draw it unless we are more than a frame late (then drop it, but
        // never more than 10 in a row so the picture keeps moving).
        Clock::time_point due = startTime + frameTime * frameIndex;
        Clock::time_point now = Clock::now();
        if (now > due + frameTime && dropStreak < 10) {
            ++dropped;
            ++dropStreak;
            continue;
        }
        dropStreak = 0;
        frame = gfx::Image::fromRgb24(&buffer[0], pixelWidth, pixelHeight);
        out.clear();
        screen.draw(frame, out);

        char line[256];
        std::snprintf(line, sizeof(line), " %s | %.1f / %.1f s | %.2f fps | drawn %ld dropped %ld | %s | space pause, q quit ",
                      name.c_str(), position, info.duration, info.fps, drawn, dropped,
                      gfx::styleName(options.style));
        status = line;
        gfx::terminal::Size s = gfx::terminal::size();
        out += "\x1b[" + std::to_string(s.rows) + ";1H\x1b[0m\x1b[2K\x1b[7m" +
               status.substr(0, static_cast<size_t>(s.columns)) + "\x1b[0m";
        session.write(out);
        ++drawn;

        // Wait for the next frame's due time, reacting to keys meanwhile.
        for (;;) {
            Clock::duration left = due - Clock::now();
            long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(left).count());
            if (ms <= 0) break;
            int k = session.readKey(static_cast<int>(ms));
            if (k == gfx::terminal::KeyNone) continue;
            if (k == 'q' || k == gfx::terminal::KeyEscape) return 0;
            if (k == ' ') {
                paused = true;
                startPosition += frameIndex / info.fps;
                frameIndex = 0;
                break;
            }
        }
    }
    decoder.close();
    return 0;
}
