// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  C++11, no dependencies (the optional image loader bundles stb_image).
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   include/graphicx/terminal.h
//  Layer:  Terminal helpers (optional part of the library, POSIX: macOS/Linux)
//
//  What this file does
//  -------------------
//  The few terminal chores an animation or player needs, so examples stay
//  short:
//    gfx::terminal::size()      columns x rows of the terminal
//    gfx::terminal::Session     full-screen mode while it exists: alternate
//                               screen, hidden cursor, keys without Enter;
//                               everything is restored when it goes away
//    Session::readKey()         one key press, with a timeout
//    Session::write()           send bytes (e.g. gfx::Screen output)
//
//      gfx::terminal::Session session;          // full screen from here on
//      for (;;) {
//          session.write(screen.draw(frame));
//          if (session.readKey(16) == 'q') break;
//      }                                         // terminal restored here
//
//  How it fits in
//  --------------
//  The core (graphicx.h) never touches the terminal; it only produces text.
//  The tools (gfx-convert, gfx-play) and examples use this file.  Programs
//  with their own terminal handling (like TBoyEmu) don't need it.
//
//  Terminal notes
//  --------------
//  * Input mode: canonical mode and echo are switched off (keys arrive one
//    by one, unseen); Ctrl+C still raises SIGINT, which only sets a flag
//    (interrupted()) so the program can exit cleanly.
//  * "CSI ?1049h" switches to the alternate screen (the shell's screen
//    comes back on exit), "CSI ?25l" hides the cursor, "CSI ?7l" stops
//    line wrapping at the right edge.
//  * The Esc key and the start of an arrow-key sequence send the same byte;
//    readKey() waits 30 ms for more bytes to tell them apart.
//
//  Contributing
//  ------------
//  Keep it small and dependency-free.  Windows is not supported (it would
//  need a separate implementation of this file).
// ============================================================================
#ifndef GRAPHICX_TERMINAL_H
#define GRAPHICX_TERMINAL_H

#include <string>

namespace gfx {
namespace terminal {

struct Size {
    int columns;
    int rows;
};

// Size of the terminal on standard output (80 x 24 if unknown).
Size size();
// True when standard output is a terminal (not a file or a pipe).
bool isTerminal();

// Special values returned by Session::readKey() (printable keys are returned
// as their character code, e.g. 'q', ' ').
enum Key {
    KeyNone = -1,    // timeout: no key
    KeyEnter = 13,
    KeyEscape = 27,
    KeyUp = 1000,
    KeyDown,
    KeyLeft,
    KeyRight
};

// Full-screen terminal mode for the lifetime of the object (RAII).
class Session {
public:
    // alternateScreen: draw on a separate screen and give the shell's screen
    // back on exit (false = draw over the current screen).
    explicit Session(bool alternateScreen = true);
    ~Session();

    // Writes all bytes to standard output.
    void write(const std::string& bytes);
    // Waits up to timeoutMs milliseconds (0 = just check, -1 = forever) and
    // returns the next key, or KeyNone.
    int readKey(int timeoutMs);

    // Set when the terminal was resized since the last call (clears it).
    static bool resized();
    // Set after Ctrl+C / SIGTERM / SIGHUP.
    static bool interrupted();

private:
    Session(const Session&);
    Session& operator=(const Session&);

    bool alternateScreen_;
    bool restoreInput_;
    std::string pending_;  // input bytes not consumed yet
};

}  // namespace terminal
}  // namespace gfx

#endif
