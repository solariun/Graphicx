// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  C++11, no dependencies (the optional image loader bundles stb_image).
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   src/terminal.cpp
//  Layer:  Terminal helpers (optional part of the library, POSIX: macOS/Linux)
//
//  What this file does
//  -------------------
//  Implementation of include/graphicx/terminal.h with termios (input mode),
//  ioctl(TIOCGWINSZ) (size), poll() (key timeouts) and signal handlers
//  (resize and interrupt flags).
//
//  How it fits in
//  --------------
//  Used by tools/ and examples/.  The saved terminal settings are restored
//  by the Session destructor, so return normally (don't call exit()) to
//  leave the terminal in a good state.
//
//  Terminal notes
//  --------------
//  Arrow keys arrive as "ESC [ A" .. "ESC [ D" (or "ESC O A" .. in
//  application cursor mode); other escape sequences are consumed and
//  ignored.
//
//  Contributing
//  ------------
//  Only async-signal-safe work in the signal handlers (setting a flag).
// ============================================================================
#include "graphicx/terminal.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace gfx {
namespace terminal {

namespace {

volatile sig_atomic_t gResized = 0;
volatile sig_atomic_t gInterrupted = 0;
termios gSaved;

void onResize(int) { gResized = 1; }
void onInterrupt(int) { gInterrupted = 1; }

// Reads whatever is available within timeoutMs.
void readAvailable(std::string& into, int timeoutMs) {
    pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, timeoutMs) <= 0 || !(pfd.revents & POLLIN)) return;
    char buf[256];
    ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) into.append(buf, static_cast<size_t>(n));
}

}  // namespace

Size size() {
    Size s = {80, 24};
    winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        s.columns = ws.ws_col;
        s.rows = ws.ws_row;
    }
    return s;
}

bool isTerminal() { return isatty(STDOUT_FILENO) != 0; }

Session::Session(bool alternateScreen) : alternateScreen_(alternateScreen), restoreInput_(false) {
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &gSaved) == 0) {
        termios raw = gSaved;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        restoreInput_ = true;
    }
    struct sigaction sa;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = onResize;
    sigaction(SIGWINCH, &sa, 0);
    sa.sa_handler = onInterrupt;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGHUP, &sa, 0);
    write(std::string(alternateScreen_ ? "\x1b[?1049h" : "") + "\x1b[?25l\x1b[?7l\x1b[0m\x1b[2J\x1b[H");
}

Session::~Session() {
    write(std::string("\x1b[0m\x1b[?7h\x1b[?25h") + (alternateScreen_ ? "\x1b[?1049l" : "\r\n"));
    if (restoreInput_) tcsetattr(STDIN_FILENO, TCSANOW, &gSaved);
}

void Session::write(const std::string& bytes) {
    const char* p = bytes.data();
    size_t left = bytes.size();
    while (left > 0) {
        ssize_t n = ::write(STDOUT_FILENO, p, left);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
}

int Session::readKey(int timeoutMs) {
    if (pending_.empty()) readAvailable(pending_, timeoutMs);
    if (pending_.empty()) return KeyNone;

    unsigned char c = static_cast<unsigned char>(pending_[0]);
    if (c != 0x1B) {
        pending_.erase(0, 1);
        return c == '\n' ? KeyEnter : static_cast<int>(c);
    }
    if (pending_.size() == 1) readAvailable(pending_, 30);  // Esc key or a sequence?
    if (pending_.size() == 1 || (pending_[1] != '[' && pending_[1] != 'O')) {
        pending_.erase(0, 1);
        return KeyEscape;
    }
    // CSI / SS3 sequence: parameters until a final byte 0x40-0x7E.
    size_t end = 2;
    while (end < pending_.size() && (pending_[end] < 0x40 || pending_[end] > 0x7E)) ++end;
    if (end >= pending_.size()) {
        pending_.clear();  // incomplete: drop it
        return KeyNone;
    }
    char final = pending_[end];
    pending_.erase(0, end + 1);
    switch (final) {
        case 'A': return KeyUp;
        case 'B': return KeyDown;
        case 'C': return KeyRight;
        case 'D': return KeyLeft;
        default: return KeyNone;
    }
}

bool Session::resized() {
    if (!gResized) return false;
    gResized = 0;
    return true;
}

bool Session::interrupted() { return gInterrupted != 0; }

}  // namespace terminal
}  // namespace gfx
