// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   examples/canvas.cpp
//
//  What this file does
//  -------------------
//  Draws one picture with gfx::Canvas (shapes, anti-aliased lines, a
//  polygon, gradients, colormaps, both fonts) and prints it four times, once
//  per pixel mode of gfx::ui::Painter::image (half blocks, quadrants,
//  braille, ASCII), so you can compare them.
//
//      ./bin/example-canvas            (truecolor)
//      ./bin/example-canvas 256        (xterm-256 colors)
// ============================================================================
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "graphicx/canvas.h"
#include "graphicx/ui.h"

int main(int argc, char** argv) {
    bool palette = argc > 1 && std::strcmp(argv[1], "256") == 0;

    gfx::Image img(160, 96);
    gfx::Canvas c(img);
    c.gradient(gfx::rect(0, 0, 160, 96), gfx::hex(0x101030), gfx::hex(0x000000), true);
    c.fillCircle(30, 34, 22, gfx::hex(0xffb000));
    c.circle(30, 34, 26, gfx::hex(0xffffff));
    c.fillRoundRect(gfx::rect(62, 12, 40, 28), 6, gfx::hex(0x3080ff));
    c.pie(130, 30, 22, 30, 300, gfx::hex(0xff4060));
    std::vector<gfx::Point> star;
    for (int i = 0; i < 10; ++i) {
        double a = -3.14159265 / 2 + i * 3.14159265 / 5;
        double r = i % 2 ? 9 : 20;
        star.push_back(gfx::point(82 + static_cast<int>(r * std::cos(a)), 66 + static_cast<int>(r * std::sin(a))));
    }
    c.fillPolygon(star, gfx::hex(0x40e080));
    for (int i = 0; i < 12; ++i) c.lineAA(0, 95 - i * 3.0, 159, 60 - i * 5.0, gfx::colormap(gfx::Colormap::Turbo, i / 11.0));
    c.thickLine(110, 60, 150, 90, 4, gfx::hex(0xffffff));
    c.gradient(gfx::rect(4, 84, 60, 8), gfx::Colormap::Inferno);
    c.text(4, 60, "GRAPHICX", gfx::hex(0xffffff), gfx::Font::Font5x7);
    c.text(4, 72, "CANVAS 3X5", gfx::hex(0xffd080), gfx::Font::Font3x5);

    const gfx::ui::PixelMode modes[4] = {gfx::ui::PixelMode::HalfBlocks, gfx::ui::PixelMode::Quadrants,
                                         gfx::ui::PixelMode::Braille, gfx::ui::PixelMode::Ascii};
    const char* names[4] = {"half blocks (1x2 pixels per cell)", "quadrants (2x2)", "braille (2x4)", "ascii"};
    for (int m = 0; m < 4; ++m) {
        int across, down;
        gfx::ui::pixelsPerCell(modes[m], across, down);
        int w = 160 / across, h = 96 / down;
        gfx::ui::Surface s(w, h);
        gfx::ui::Painter p(s);
        p.image(p.bounds(), img, modes[m], gfx::Filter::Box);
        std::string out;
        gfx::ui::Display d(palette ? gfx::ColorMode::Palette256 : gfx::ColorMode::TrueColor);
        d.render(s, out);
        // Display positions the cursor absolutely; for printing in sequence
        // we print row by row instead.
        std::printf("\n%s\n", names[m]);
        for (int y = 0; y < s.height(); ++y) {
            std::string line;
            for (int x = 0; x < s.width(); ++x) {
                const gfx::ui::Cell& cell = s.at(x, y);
                char sgr[64];
                if (palette)
                    std::snprintf(sgr, sizeof(sgr), "\x1b[38;5;%d;48;5;%dm", gfx::nearestPaletteIndex(cell.fg), gfx::nearestPaletteIndex(cell.bg));
                else
                    std::snprintf(sgr, sizeof(sgr), "\x1b[38;2;%d;%d;%d;48;2;%d;%d;%dm", cell.fg.r, cell.fg.g, cell.fg.b, cell.bg.r, cell.bg.g, cell.bg.b);
                line += sgr;
                gfx::ui::appendUtf8(line, cell.ch);
            }
            std::cout << line << "\x1b[0m\n";
        }
    }
    return 0;
}
