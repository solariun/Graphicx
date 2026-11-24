// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   examples/show-image.cpp
//  Layer:  Examples
//
//  What this file does
//  -------------------
//  Load a picture file and show it as large as the terminal allows.
//
//      make examples && ./bin/example-show-image photo.jpg
//
//  What to learn here
//  ------------------
//  gfx::loadImage() (image_io.h), gfx::terminal::size() and gfx::fit() to
//  fill the terminal while keeping the picture's proportions.
// ============================================================================
#include <iostream>

#include "graphicx/graphicx.h"
#include "graphicx/image_io.h"
#include "graphicx/terminal.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " IMAGE\n";
        return 2;
    }
    try {
        gfx::Image picture = gfx::loadImage(argv[1]);

        // Largest size that fits (leave one line for the shell prompt).
        gfx::terminal::Size terminal = gfx::terminal::size();
        gfx::Options options;
        gfx::fit(picture.width(), picture.height(), terminal.columns, terminal.rows - 1, options.columns,
                 options.rows);

        std::cout << gfx::render(picture, options);
    } catch (const gfx::ImageError& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
