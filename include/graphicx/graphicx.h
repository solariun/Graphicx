// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  C++11, no dependencies (the optional image loader bundles stb_image).
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   include/graphicx/graphicx.h
//  Layer:  Core library (the only header most programs need)
//
//  What this file does
//  -------------------
//  The whole public API of the core library:
//    gfx::Rgb, gfx::Image   a picture (8-bit RGB pixels)
//    gfx::Options           how to draw it: style, colors, size, filter
//    gfx::render()          picture -> one string of terminal text
//    gfx::Screen            animation / video: redraws only what changed
//    gfx::fit(), palette    small helpers
//
//  Three drawing styles (gfx::Style):
//    Blocks      "▀" upper half blocks with a foreground AND a background
//                color: every character cell shows 2 pixels stacked
//                vertically, in full color. The best quality.
//    AsciiColor  one ASCII character per cell, chosen by brightness from a
//                "ramp" (" .:-=+*#%@"), drawn in the cell's color. No
//                background color is used.
//    Ascii       the same characters with no color codes at all: plain
//                text that works everywhere (files, e-mail, any terminal).
//  Colors are sent as xterm 256-color palette indexes (works in practically
//  every terminal) or as 24-bit "truecolor" (gfx::ColorMode).
//
//  Quick start
//  -----------
//      #include <graphicx/graphicx.h>
//      #include <graphicx/image_io.h>     // only to load JPG / PNG files
//      #include <iostream>
//
//      int main() {
//          gfx::Image photo = gfx::loadImage("cat.jpg");
//          gfx::Options options;
//          options.columns = 80;              // 80 characters wide
//          std::cout << gfx::render(photo, options);
//      }
//
//  How it fits in
//  --------------
//  include/graphicx/graphicx.h  + src/graphicx.cpp   this core (no deps)
//  include/graphicx/image_io.h  + src/image_io.cpp   JPG/PNG/BMP/GIF loading
//  include/graphicx/terminal.h  + src/terminal.cpp   raw mode, size, keys
//  tools/ (gfx-convert, gfx-play) and examples/ are built on these.
//  To use it: build libgraphicx.a with `make`, or simply add the .cpp files
//  you need to your own build (see README.md).
//
//  Terminal notes
//  --------------
//  * Character cells are about twice as tall as they are wide, so one cell
//    stands for 1 (wide) x 2 (tall) pixels; that keeps pictures in shape.
//  * Escape sequences used: SGR "CSI 38;5;n m" / "CSI 48;5;n m" (256-color
//    foreground / background), "CSI 38;2;r;g;b m" (truecolor), "CSI 0 m"
//    (reset), cursor position "CSI row;col H", and synchronized output
//    "CSI ?2026h ... CSI ?2026l" (terminals that know it show the frame at
//    once; others ignore it).
//  * Palette entries 0-15 are never used: every terminal theme changes them.
//
//  Contributing
//  ------------
//  Keep this header the single entry point, C++11 only, and free of
//  platform code (that belongs in terminal.cpp). Run `make test` before
//  sending changes; tests/test_graphicx.cpp checks every public function.
// ============================================================================
#ifndef GRAPHICX_GRAPHICX_H
#define GRAPHICX_GRAPHICX_H

#include <cstdint>
#include <string>
#include <vector>

namespace gfx {

// ------------------------------------------------------------------ pixels --

// One 24-bit color: red, green, blue, 0-255 each.
struct Rgb {
    std::uint8_t r, g, b;
};

inline Rgb rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    Rgb c;
    c.r = r;
    c.g = g;
    c.b = b;
    return c;
}

// A picture: width x height RGB pixels, stored row by row, top row first.
// A plain value type: copy it, return it, keep it in containers.
class Image {
public:
    Image();                                     // empty (0 x 0)
    Image(int width, int height, Rgb fill = rgb(0, 0, 0));

    int width() const { return width_; }
    int height() const { return height_; }
    bool empty() const { return width_ <= 0 || height_ <= 0; }

    // Pixel access; x in [0, width), y in [0, height) (not range checked).
    Rgb get(int x, int y) const { return pixels_[static_cast<size_t>(y) * width_ + x]; }
    void set(int x, int y, Rgb c) { pixels_[static_cast<size_t>(y) * width_ + x] = c; }
    Rgb* row(int y) { return &pixels_[static_cast<size_t>(y) * width_]; }
    const Rgb* row(int y) const { return &pixels_[static_cast<size_t>(y) * width_]; }

    // Changes the size (contents become undefined; keeps memory when it can,
    // which makes reusing one Image per video frame cheap).
    void resize(int width, int height);

    // Build an Image from raw memory:
    //   fromRgb24: 3 bytes per pixel (R, G, B). stride = bytes per row
    //              (0 = width * 3). This is ffmpeg's "rgb24" format.
    //   fromRgba32: 4 bytes per pixel (R, G, B, A); alpha is ignored.
    //   fromRgb555: 16-bit pixels 0bbbbbgggggrrrrr (Game Boy Color / GBA).
    static Image fromRgb24(const std::uint8_t* data, int width, int height, int stride = 0);
    static Image fromRgba32(const std::uint8_t* data, int width, int height, int stride = 0);
    static Image fromRgb555(const std::uint16_t* data, int width, int height);

private:
    int width_;
    int height_;
    std::vector<Rgb> pixels_;
};

// ----------------------------------------------------------------- options --

// How a picture is drawn (see the file header for pictures of each).
enum class Style {
    Blocks,      // colored half blocks: 2 pixels per cell, best quality (default)
    AsciiColor,  // colored ASCII characters, no background
    Ascii        // plain ASCII characters, no escape codes at all
};

// How colors are written.
enum class ColorMode {
    Palette256,  // xterm 256-color palette (default; works almost everywhere)
    TrueColor    // 24-bit colors (modern terminals; exact colors)
};

// How the picture is resized to the character grid.
enum class Filter {
    Box,     // average all the pixels a cell covers: smooth, best for photos
             // and video (default)
    Nearest  // take one pixel: crisp edges, best for pixel art and games
};

// Everything render() and Screen need to know.  Create one, change the
// fields you care about, pass it along:
//     gfx::Options o;
//     o.style = gfx::Style::AsciiColor;
//     o.columns = 100;
struct Options {
    Options();

    Style style;          // default Style::Blocks
    ColorMode colorMode;  // default ColorMode::Palette256
    Filter filter;        // default Filter::Box

    // Size of the result in character cells.
    //   columns = 0 and rows = 0: one cell per pixel horizontally (image
    //                             width columns, height / 2 rows)
    //   only columns:  rows follow from the picture's aspect ratio
    //   only rows:     columns follow from the aspect ratio
    //   both:          exactly that size (the picture is stretched to it)
    // gfx::fit() computes the largest size that fits a terminal.
    int columns;
    int rows;

    // ASCII styles: characters from emptiest to densest.  With the usual
    // dark terminal background, dense characters read as bright.
    std::string ramp;     // default " .:-=+*#%@"
    // ASCII styles: true for light terminal backgrounds (bright = empty).
    bool invert;          // default false
};

// Name of a style for messages and command lines: "blocks", "ascii-color",
// "ascii"; parseStyle() accepts those names (returns false otherwise).
const char* styleName(Style style);
bool parseStyle(const std::string& name, Style& style);

// ------------------------------------------------------------------ render --

// The size in cells render()/Screen will use for a picture of
// imageWidth x imageHeight pixels with these options.
void gridSize(int imageWidth, int imageHeight, const Options& options, int& columns, int& rows);

// The largest grid that fits in maxColumns x maxRows cells while keeping the
// picture's proportions (cells count as 1 x 2 pixels).  Use it to fill a
// terminal:  gfx::fit(img.width(), img.height(), termCols, termRows, o.columns, o.rows);
void fit(int imageWidth, int imageHeight, int maxColumns, int maxRows, int& columns, int& rows);

// Draws the whole picture as text: one line per cell row, each line ending
// with a color reset (if any color was used) and "\n".  No cursor movement,
// so the result can be printed anywhere, saved to a file, or `cat`-ed.
// Style::Ascii output contains no escape sequences at all.
std::string render(const Image& image, const Options& options = Options());

// ------------------------------------------------------------------ Screen --

// Draws a sequence of pictures (animation, game, video) at a fixed place on
// the terminal, sending only the cells that changed since the previous call.
// That keeps the output small enough for 60 frames per second.
//
//     gfx::Screen screen(options);
//     screen.setOrigin(1, 1);               // top-left corner of the terminal
//     for (;;) {
//         std::string out = screen.draw(frame);   // escape sequences
//         write(1, out.data(), out.size());
//     }
//
// Screen does not talk to the terminal itself: you decide where the bytes go
// (stdout, a file to replay later, a socket...).
class Screen {
public:
    explicit Screen(const Options& options = Options());

    // New options: the next draw() repaints everything.
    void setOptions(const Options& options);
    const Options& options() const { return options_; }

    // Terminal position (1-based row and column) of the picture's top-left
    // cell.  Default (1, 1).  Changing it repaints everything.
    void setOrigin(int row, int column);

    // Wrap every frame in "CSI ?2026h ... CSI ?2026l" (synchronized output,
    // default true).  Turn it off if you add your own text to the same frame
    // and wrap it yourself.
    void setSynchronized(bool on) { synchronized_ = on; }

    // Returns the escape sequences that update the terminal from the previous
    // frame to `frame`: cursor moves, colors and characters of the changed
    // cells only (the first frame, or after invalidate(), sends all cells).
    // Always ends with a color reset.
    std::string draw(const Image& frame);
    // Same, appending to `out` (lets you reuse one string, no allocation).
    void draw(const Image& frame, std::string& out);

    // Forget what is on the terminal: the next draw() sends every cell.
    // Call it after clearing the screen or when something overwrote it.
    void invalidate();
    // Same for a rectangle of cells (0-based, relative to the picture), e.g.
    // after drawing a message over part of it.
    void invalidateCells(int row, int column, int rows, int columns);

    // Grid size used by the last draw() (0 before the first one).
    int columns() const { return columns_; }
    int rows() const { return rows_; }

private:
    void prepare(const Image& frame);

    Options options_;
    int originRow_, originColumn_;
    bool synchronized_;
    int columns_, rows_;
    int imageWidth_, imageHeight_;
    std::vector<int> xStart_, xEnd_;  // source pixel span of each cell column
    std::vector<int> yStart_, yEnd_;  // source pixel span of each half row
    std::vector<std::uint64_t> previous_;  // what each cell shows now
};

// ----------------------------------------------------------------- palette --

// The xterm 256-color palette entry `index` (16-255; 16-231 = 6x6x6 color
// cube, 232-255 = gray ramp).
Rgb paletteColor(int index);
// The palette entry (16-255) that looks closest to `color`.  Fast: uses a
// 32K-entry table built on first use.
int nearestPaletteIndex(Rgb color);
// Perceived brightness of a color, 0-255 (Rec. 601 luma).
int brightness(Rgb color);

}  // namespace gfx

#endif
