// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  C++11, no dependencies (the optional image loader bundles stb_image).
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   tools/gfx-convert.cpp
//  Layer:  Tools
//
//  What this file does
//  -------------------
//  Command line converter: picture file (JPG, PNG, BMP, GIF, ...) -> terminal
//  text in any of the three styles, printed or saved to a file.
//
//      gfx-convert photo.jpg                       # colored half blocks
//      gfx-convert -s ascii-color photo.jpg
//      gfx-convert -s ascii -w 100 -o photo.txt photo.jpg
//      cat photo.txt
//
//  How it fits in
//  --------------
//  A thin front end over gfx::loadImage() (image_io.h) and gfx::render()
//  (graphicx.h): parse options, load, size, render, write.  It is also the
//  easiest way to try every library option from the shell.
//
//  Contributing
//  ------------
//  Keep the options in sync with gfx::Options and the README.
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "graphicx/graphicx.h"
#include "graphicx/image_io.h"
#include "graphicx/terminal.h"

namespace {

void usage() {
    std::printf(
        "gfx-convert - show a picture in the terminal, or save it as terminal text\n"
        "\n"
        "usage: gfx-convert [options] IMAGE\n"
        "\n"
        "  -s, --style S       blocks (default) | ascii-color | ascii\n"
        "  -w, --width N       width in characters (default: terminal width, 80 if not a terminal)\n"
        "  -H, --height N      height in lines (default: from the picture's proportions)\n"
        "      --fit           fit width AND height in the terminal\n"
        "      --truecolor     24-bit colors instead of the 256-color palette\n"
        "      --nearest       crisp resizing for pixel art (default: smooth)\n"
        "      --invert        ASCII styles: for light terminal backgrounds\n"
        "      --ramp CHARS    ASCII styles: characters from empty to dense\n"
        "  -o, --output FILE   write to FILE instead of the terminal\n"
        "  -h, --help\n"
        "\n"
        "formats: JPEG, PNG, BMP, GIF, TGA, PSD, HDR, PIC, PNM\n");
}

}  // namespace

int main(int argc, char** argv) {
    gfx::Options options;
    std::string input, output;
    bool fitTerminal = false;

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
        } else if ((a == "-w" || a == "--width") && hasValue) {
            options.columns = std::atoi(argv[++i]);
        } else if ((a == "-H" || a == "--height") && hasValue) {
            options.rows = std::atoi(argv[++i]);
        } else if (a == "--fit") {
            fitTerminal = true;
        } else if (a == "--truecolor") {
            options.colorMode = gfx::ColorMode::TrueColor;
        } else if (a == "--nearest") {
            options.filter = gfx::Filter::Nearest;
        } else if (a == "--invert") {
            options.invert = true;
        } else if (a == "--ramp" && hasValue) {
            options.ramp = argv[++i];
        } else if ((a == "-o" || a == "--output") && hasValue) {
            output = argv[++i];
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "error: unknown option %s\n\n", a.c_str());
            usage();
            return 2;
        } else {
            input = a;
        }
    }
    if (input.empty()) {
        usage();
        return 2;
    }

    gfx::Image image;
    try {
        image = gfx::loadImage(input);
    } catch (const gfx::ImageError& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    // Default size: as wide as the terminal (80 columns when writing to a
    // file or a pipe); --fit also limits the height.
    bool toTerminal = output.empty() && gfx::terminal::isTerminal();
    if (fitTerminal && toTerminal) {
        gfx::terminal::Size s = gfx::terminal::size();
        gfx::fit(image.width(), image.height(), s.columns, s.rows - 1, options.columns, options.rows);
    } else if (options.columns <= 0 && options.rows <= 0) {
        options.columns = toTerminal ? gfx::terminal::size().columns : 80;
        if (options.columns > image.width()) options.columns = image.width();  // never enlarge by default
    }

    std::string text = gfx::render(image, options);
    if (output.empty()) {
        std::fwrite(text.data(), 1, text.size(), stdout);
        return 0;
    }
    std::ofstream file(output.c_str(), std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        std::fprintf(stderr, "error: cannot write %s\n", output.c_str());
        return 1;
    }
    int columns, rows;
    gfx::gridSize(image.width(), image.height(), options, columns, rows);
    std::fprintf(stderr, "%s: %dx%d pixels -> %dx%d characters, %s, %zu bytes\n", output.c_str(), image.width(),
                 image.height(), columns, rows, gfx::styleName(options.style), text.size());
    return 0;
}
