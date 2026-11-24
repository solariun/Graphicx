// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   examples/animation.cpp
//  Layer:  Examples
//
//  What this file does
//  -------------------
//  A full-screen animation (a moving "plasma" with a bouncing ball) at 30
//  frames per second.  Keys: 1 / 2 / 3 switch style, q quits.
//
//      make examples && ./bin/example-animation
//
//  What to learn here
//  ------------------
//  gfx::Screen: draw a new picture every frame and send only the cells that
//  changed (fast enough for games and video); gfx::terminal::Session for the
//  full-screen mode and keys; reacting to terminal resizes.
// ============================================================================
#include <chrono>
#include <cmath>

#include "graphicx/graphicx.h"
#include "graphicx/terminal.h"

int main() {
    gfx::terminal::Session session;  // full screen until the end of main()
    gfx::Options options;
    gfx::Screen screen(options);
    gfx::Image frame;

    const std::chrono::milliseconds frameTime(33);  // ~30 fps
    for (int tick = 0;; ++tick) {
        // Picture size = terminal size in "pixels": columns x (rows * 2).
        gfx::terminal::Size size = gfx::terminal::size();
        if (gfx::terminal::Session::resized()) session.write("\x1b[2J");
        frame.resize(size.columns, size.rows * 2);

        // Plasma: sums of sine waves give every pixel a moving color.
        double t = tick * 0.08;
        for (int y = 0; y < frame.height(); ++y) {
            gfx::Rgb* row = frame.row(y);
            for (int x = 0; x < frame.width(); ++x) {
                double v = std::sin(x * 0.10 + t) + std::sin(y * 0.15 - t) + std::sin((x + y) * 0.07 + t * 0.5);
                row[x] = gfx::rgb(static_cast<std::uint8_t>(128 + 120 * std::sin(v)),
                                  static_cast<std::uint8_t>(128 + 120 * std::sin(v + 2.1)),
                                  static_cast<std::uint8_t>(128 + 120 * std::sin(v + 4.2)));
            }
        }
        // A white ball bouncing around.
        int bx = static_cast<int>((std::sin(t * 0.7) * 0.4 + 0.5) * frame.width());
        int by = static_cast<int>((std::fabs(std::sin(t * 1.3)) * -0.6 + 0.8) * frame.height());
        for (int y = by - 6; y <= by + 6; ++y)
            for (int x = bx - 6; x <= bx + 6; ++x)
                if (x >= 0 && y >= 0 && x < frame.width() && y < frame.height() &&
                    (x - bx) * (x - bx) + (y - by) * (y - by) <= 36)
                    frame.set(x, y, gfx::rgb(255, 255, 255));

        // Only the cells that changed since the last frame are sent.
        session.write(screen.draw(frame));

        // Wait for the next frame, handling keys.
        int key = session.readKey(static_cast<int>(frameTime.count()));
        if (key == 'q' || key == gfx::terminal::KeyEscape || gfx::terminal::Session::interrupted()) break;
        if (key >= '1' && key <= '3') {
            const gfx::Style styles[3] = {gfx::Style::Blocks, gfx::Style::AsciiColor, gfx::Style::Ascii};
            options.style = styles[key - '1'];
            screen.setOptions(options);  // repaints everything next frame
            session.write("\x1b[0m\x1b[2J");
        }
    }
    return 0;
}
