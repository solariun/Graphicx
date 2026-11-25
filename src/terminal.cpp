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

#include <vector>

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

Event::Event()
    : type(EventType::None),
      key(KeyNone),
      shift(false),
      alt(false),
      ctrl(false),
      button(MouseButton::None),
      action(MouseAction::Press),
      x(0),
      y(0) {}

Session::Session(bool alternateScreen) : alternateScreen_(alternateScreen), restoreInput_(false), mouse_(false) {
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
    if (mouse_) enableMouse(false);
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

// ------------------------------------------------------------------ events --

void Session::enableMouse(bool on) {
    // 1000: clicks, 1002: drags (motion with a button held), 1006: SGR
    // coordinates (no 223-column limit).
    write(on ? "\x1b[?1000h\x1b[?1002h\x1b[?1006h" : "\x1b[?1006l\x1b[?1002l\x1b[?1000l");
    mouse_ = on;
}

namespace {

// Modifier parameter of xterm sequences: 1 + (shift 1 | alt 2 | ctrl 4).
void applyModifier(Event& e, int param) {
    if (param < 2) return;
    int bits = param - 1;
    e.shift = (bits & 1) != 0;
    e.alt = (bits & 2) != 0;
    e.ctrl = (bits & 4) != 0;
}

// Splits "a;b;c" into integers (missing values = 0).
std::vector<int> splitParams(const std::string& text) {
    std::vector<int> values(1, 0);
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == ';' || c == ':') values.push_back(0);
        else if (c >= '0' && c <= '9') values.back() = values.back() * 10 + (c - '0');
    }
    return values;
}

// Number of bytes of the UTF-8 sequence starting with lead byte c.
int utf8Length(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;  // stray continuation byte: taken as one
}

int decodeUtf8(const std::string& s, size_t n) {
    unsigned char c = static_cast<unsigned char>(s[0]);
    if (n == 1) return c;
    int cp = c & (0x7F >> n);
    for (size_t i = 1; i < n; ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    return cp;
}

}  // namespace

bool parseEvent(std::string& pending_, Event& e) {
    e = Event();
    if (pending_.empty()) return false;
    unsigned char c = static_cast<unsigned char>(pending_[0]);

    if (c != 0x1B) {
        int n = utf8Length(c);
        if (pending_.size() < static_cast<size_t>(n)) return false;  // wait for the rest
        e.type = EventType::Key;
        if (n > 1) {
            e.key = decodeUtf8(pending_, static_cast<size_t>(n));
        } else if (c == '\r' || c == '\n') {
            e.key = KeyEnter;
        } else if (c == '\t') {
            e.key = KeyTab;
        } else if (c == 127 || c == 8) {
            e.key = KeyBackspace;
        } else if (c == 0) {
            e.key = ' ';
            e.ctrl = true;
        } else if (c < 32) {
            e.key = 'a' + c - 1;  // Ctrl+A = 1 ... Ctrl+Z = 26
            e.ctrl = true;
        } else {
            e.key = c;
        }
        pending_.erase(0, static_cast<size_t>(n));
        return true;
    }

    if (pending_.size() == 1) {
        e.type = EventType::Key;
        e.key = KeyEscape;
        pending_.erase(0, 1);
        return true;
    }
    char second = pending_[1];
    if (second != '[' && second != 'O') {
        // Alt+key: Esc followed by the key.
        pending_.erase(0, 1);
        if (!parseEvent(pending_, e)) {
            e.type = EventType::Key;
            e.key = KeyEscape;
            return true;
        }
        e.alt = true;
        return true;
    }

    // CSI ( "Esc [" ) or SS3 ( "Esc O" ): parameters, then a final byte.
    size_t end = 2;
    while (end < pending_.size() && (static_cast<unsigned char>(pending_[end]) < 0x40 || static_cast<unsigned char>(pending_[end]) > 0x7E)) ++end;
    if (end >= pending_.size()) return false;  // incomplete
    std::string params = pending_.substr(2, end - 2);
    char final = pending_[end];
    pending_.erase(0, end + 1);

    e.type = EventType::Key;
    if (!params.empty() && params[0] == '<' && (final == 'M' || final == 'm')) {
        // SGR mouse: "<b;x;y" + M (press / motion) or m (release).
        std::vector<int> v = splitParams(params.substr(1));
        if (v.size() < 3) return parseEvent(pending_, e);
        int b = v[0];
        e.type = EventType::Mouse;
        e.x = v[1] - 1;
        e.y = v[2] - 1;
        e.shift = (b & 4) != 0;
        e.alt = (b & 8) != 0;
        e.ctrl = (b & 16) != 0;
        if (b & 64) {
            e.button = (b & 1) ? MouseButton::WheelDown : MouseButton::WheelUp;
            e.action = MouseAction::Press;
        } else {
            int which = b & 3;
            e.button = which == 0 ? MouseButton::Left : which == 1 ? MouseButton::Middle : which == 2 ? MouseButton::Right : MouseButton::None;
            if (b & 32) e.action = e.button == MouseButton::None ? MouseAction::Move : MouseAction::Drag;
            else e.action = final == 'M' ? MouseAction::Press : MouseAction::Release;
        }
        return true;
    }

    std::vector<int> v = splitParams(params);
    if (v.size() >= 2) applyModifier(e, v[1]);
    switch (final) {
        case 'A': e.key = KeyUp; return true;
        case 'B': e.key = KeyDown; return true;
        case 'C': e.key = KeyRight; return true;
        case 'D': e.key = KeyLeft; return true;
        case 'H': e.key = KeyHome; return true;
        case 'F': e.key = KeyEnd; return true;
        case 'Z': e.key = KeyBackTab; e.shift = true; return true;
        case 'P': e.key = KeyF1; return true;
        case 'Q': e.key = KeyF2; return true;
        case 'R': e.key = KeyF3; return true;
        case 'S': e.key = KeyF4; return true;
        case 'u':  // kitty keyboard protocol: code;modifiers u
            e.key = v[0] == 13 ? static_cast<int>(KeyEnter) : v[0] == 9 ? static_cast<int>(KeyTab)
                  : v[0] == 27 ? static_cast<int>(KeyEscape) : v[0] == 127 ? static_cast<int>(KeyBackspace) : v[0];
            return true;
        case '~':
            switch (v[0]) {
                case 1: case 7: e.key = KeyHome; return true;
                case 4: case 8: e.key = KeyEnd; return true;
                case 2: e.key = KeyInsert; return true;
                case 3: e.key = KeyDelete; return true;
                case 5: e.key = KeyPageUp; return true;
                case 6: e.key = KeyPageDown; return true;
                case 11: case 12: case 13: case 14: case 15: e.key = KeyF1 + (v[0] - 11); return true;
                case 17: case 18: case 19: case 20: case 21: e.key = KeyF6 + (v[0] - 17); return true;
                case 23: case 24: e.key = KeyF11 + (v[0] - 23); return true;
                default: break;
            }
            break;
        default: break;
    }
    // Something we don't handle (focus, paste brackets, reports): skip it.
    return parseEvent(pending_, e);
}

bool Session::readEvent(Event& event, int timeoutMs) {
    event = Event();
    if (resized()) {
        event.type = EventType::Resize;
        return true;
    }
    if (pending_.empty()) readAvailable(pending_, timeoutMs);
    if (pending_.empty()) return false;
    // A lone Esc, or a sequence cut by the read: wait briefly for the rest.
    if (pending_[0] == 0x1B && pending_.size() == 1) readAvailable(pending_, 30);
    if (parseEvent(pending_, event)) return true;
    if (!pending_.empty()) {
        readAvailable(pending_, 30);
        if (parseEvent(pending_, event)) return true;
        pending_.clear();  // garbage: drop it
    }
    return false;
}

std::string keyName(const Event& e) {
    std::string name;
    if (e.ctrl) name += "Ctrl+";
    if (e.alt) name += "Alt+";
    if (e.shift && e.key >= KeyUp) name += "Shift+";
    static const char* const special[] = {"Up", "Down", "Left", "Right", "Home", "End", "PgUp", "PgDn",
                                          "Ins", "Del", "Shift+Tab", "F1", "F2", "F3", "F4", "F5",
                                          "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
    if (e.key >= KeyUp && e.key <= KeyF12) return name + special[e.key - KeyUp];
    switch (e.key) {
        case KeyEnter: return name + "Enter";
        case KeyTab: return name + "Tab";
        case KeyEscape: return name + "Esc";
        case KeyBackspace: return name + "Backspace";
        case ' ': return name + "Space";
        default: break;
    }
    // Code point -> UTF-8.
    int cp = e.key;
    if (cp < 0) return name + "?";
    if (cp < 0x80) {
        name.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        name.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        name.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        name.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        name.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        name.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        name.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        name.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        name.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        name.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return name;
}

bool Session::resized() {
    if (!gResized) return false;
    gResized = 0;
    return true;
}

bool Session::interrupted() { return gInterrupted != 0; }

}  // namespace terminal
}  // namespace gfx
