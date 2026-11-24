// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   examples/hello.cpp
//  Layer:  Examples
//
//  What this file does
//  -------------------
//  The smallest complete program: build a picture in memory (a color
//  gradient, no files needed) and print it in all three styles.
//
//      make examples && ./bin/example-hello
//
//  What to learn here
//  ------------------
//  gfx::Image (create, set pixels), gfx::Options (style, size) and
//  gfx::render() (picture -> text you can print or save).
// ============================================================================
#include <iostream>

#include "graphicx/graphicx.h"

int main() {
    // 1. A 64 x 32 picture: hue from left to right, darker towards the bottom.
    gfx::Image picture(64, 32);
    for (int y = 0; y < picture.height(); ++y) {
        for (int x = 0; x < picture.width(); ++x) {
            int r = 255 * x / picture.width();
            int g = 255 * (picture.width() - x) / picture.width();
            int b = 255 * y / picture.height();
            picture.set(x, y, gfx::rgb(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                       static_cast<std::uint8_t>(255 - b)));
        }
    }

    // 2. Print it three times, once per style.  Each render() is complete
    //    text: lines ending in "\n", colors reset at the end of each line.
    gfx::Options options;
    options.columns = 64;  // characters wide; the height follows (16 lines)

    options.style = gfx::Style::Blocks;
    std::cout << "Blocks (half blocks, 2 pixels per character):\n" << gfx::render(picture, options) << "\n";

    options.style = gfx::Style::AsciiColor;
    std::cout << "AsciiColor (colored characters):\n" << gfx::render(picture, options) << "\n";

    options.style = gfx::Style::Ascii;
    std::cout << "Ascii (plain text):\n" << gfx::render(picture, options);
    return 0;
}
