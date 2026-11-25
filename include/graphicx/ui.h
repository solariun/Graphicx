// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   include/graphicx/ui.h
//
//  What this file does
//  -------------------
//  The cell layer of the user interface toolkit:
//    gfx::ui::Surface   a grid of character cells (character, foreground,
//                       background, attributes): what the screen should show
//    gfx::ui::Painter   draws on a Surface inside a clipped rectangle with
//                       local coordinates: text, fills, lines and boxes with
//                       box-drawing borders, shadows, and pictures
//                       (gfx::Image) as half blocks, quadrants (2x2 pixels per
//                       cell), braille (2x4 dots per cell) or ASCII
//    gfx::ui::DotCanvas a braille "pixel" canvas for crisp plots: 2x4 dots
//                       per cell, one color per cell, drawn over whatever
//                       background is there
//    gfx::ui::Display   turns a Surface into the escape sequences that update
//                       the terminal, sending only the cells that changed
//    layout helpers     split a rectangle into rows / columns by fixed size,
//                       percentage or flexible share
//  The widgets (buttons, sliders, lists, charts, waterfalls ...) are in
//  widgets.h and build on this file.
//
//  Example
//  -------
//      gfx::terminal::Session session;
//      gfx::ui::Surface surface(80, 24);
//      gfx::ui::Display display;
//      gfx::ui::Painter p(surface);
//      p.fill(gfx::hex(0x101010));
//      p.box(gfx::rect(0, 0, 80, 24), gfx::hex(0xffb000), gfx::ui::Border::Rounded, " Hello ");
//      p.text(2, 2, "Graphicx UI", gfx::hex(0xffffff), gfx::ui::Bold);
//      std::string out;
//      display.render(surface, out);
//      session.write(out);
//
//  Terminal notes
//  --------------
//  * Every glyph is assumed to be one column wide (box drawing, blocks,
//    braille and Latin text are); double-width CJK / emoji are not handled.
//  * Colors are 24-bit in the Surface; the Display sends them as truecolor
//    or as the nearest xterm-256 color (ColorMode).
// ============================================================================
#ifndef GRAPHICX_UI_H
#define GRAPHICX_UI_H

#include <cstdint>
#include <string>
#include <vector>

#include "canvas.h"
#include "color.h"
#include "graphicx.h"

namespace gfx {
namespace ui {

// Text attributes (combine with |).
enum Attr : std::uint8_t {
    Plain = 0,
    Bold = 1,
    Dim = 2,
    Italic = 4,
    Underline = 8,
    Reverse = 16,
    Blink = 32,
    Strike = 64
};

struct Cell {
    std::uint32_t ch;  // Unicode code point (' ' = empty)
    Rgb fg;
    Rgb bg;
    std::uint8_t attrs;
};
bool operator==(const Cell& a, const Cell& b);
inline bool operator!=(const Cell& a, const Cell& b) { return !(a == b); }

// ----------------------------------------------------------------- UTF-8 --

void appendUtf8(std::string& out, std::uint32_t codePoint);
// Decodes the code point at text[pos] and advances pos (invalid bytes give
// U+FFFD).
std::uint32_t nextCodePoint(const std::string& text, size_t& pos);
// Number of columns `text` takes (= code points; see the terminal notes).
int columns(const std::string& text);
// At most `width` columns; adds "…" when it cut something (and width > 1).
std::string truncate(const std::string& text, int width);
// Word wrap into lines of at most `width` columns ('\n' starts a new line).
std::vector<std::string> wrap(const std::string& text, int width);

// --------------------------------------------------------------- Surface --

class Surface {
public:
    Surface(int width = 0, int height = 0, Rgb background = rgb(0, 0, 0));

    int width() const { return width_; }
    int height() const { return height_; }
    void resize(int width, int height, Rgb background = rgb(0, 0, 0));
    void clear(Rgb background, Rgb foreground = rgb(255, 255, 255));

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
    Cell& at(int x, int y) { return cells_[static_cast<size_t>(y) * width_ + x]; }
    const Cell& at(int x, int y) const { return cells_[static_cast<size_t>(y) * width_ + x]; }

    // The text of row y (characters only), for tests and screen readers.
    std::string rowText(int y) const;

private:
    int width_, height_;
    std::vector<Cell> cells_;
};

// --------------------------------------------------------------- Painter --

enum class Border {
    None,     // nothing (the box still reserves its frame cells)
    Single,   // ┌─┐
    Double,   // ╔═╗
    Rounded,  // ╭─╮
    Heavy,    // ┏━┓
    Dashed,   // ┌╌┐
    Ascii,    // +-+
    Block     // ▛▀▜ (thick, half-block frame)
};

// How Painter::image turns pixels into cells.
enum class PixelMode {
    HalfBlocks,  // ▀ with two colors: 1x2 pixels per cell (default, best color)
    Quadrants,   // ▖▗▘▝▚... two colors: 2x2 pixels per cell
    Braille,     // ⠿ dots, two colors: 2x4 pixels per cell (finest detail)
    Ascii        // characters by brightness, colored: 1x2 pixels per cell
};
// Pixels per cell for a mode (columns x rows of pixels).
void pixelsPerCell(PixelMode mode, int& across, int& down);

class Painter {
public:
    // Whole surface.
    explicit Painter(Surface& surface);
    // `area` of the surface (absolute coordinates), clipped to it.
    Painter(Surface& surface, const Rect& area);

    // A painter for a rectangle given in this painter's coordinates.
    Painter sub(const Rect& local) const;

    int width() const { return area_.width; }
    int height() const { return area_.height; }
    Rect bounds() const { return rect(0, 0, area_.width, area_.height); }
    const Rect& area() const { return area_; }  // absolute position and size
    Surface& surface() { return surface_; }

    bool inside(int x, int y) const;
    // Cell at local (x, y), or nullptr outside.
    Cell* cell(int x, int y);

    // ---- cells
    void put(int x, int y, std::uint32_t ch, Rgb fg, Rgb bg, std::uint8_t attrs = Plain);
    // Keeps the background already there.
    void put(int x, int y, std::uint32_t ch, Rgb fg, std::uint8_t attrs = Plain);
    void setBackground(int x, int y, Rgb bg);
    void setForeground(int x, int y, Rgb fg);
    // Every cell of r: character ch in fg over bg.
    void fill(const Rect& r, Rgb bg, std::uint32_t ch = ' ', Rgb fg = rgb(255, 255, 255));
    void fill(Rgb bg) { fill(bounds(), bg); }
    // Only the backgrounds of r (text stays).
    void tint(const Rect& r, Rgb bg);
    // Darkens the backgrounds and foregrounds of r (factor 0..1).
    void darken(const Rect& r, double factor);

    // ---- text (one line; '\n' is not interpreted).  Returns the column after.
    int text(int x, int y, const std::string& text, Rgb fg, std::uint8_t attrs = Plain);
    int text(int x, int y, const std::string& text, Rgb fg, Rgb bg, std::uint8_t attrs = Plain);
    // Inside r (vertically centered), truncated with "…" when too long.
    void text(const Rect& r, const std::string& text, Rgb fg, Align align, std::uint8_t attrs = Plain);
    // Word-wrapped into r; returns the number of lines used.
    int paragraph(const Rect& r, const std::string& text, Rgb fg, std::uint8_t attrs = Plain);

    // ---- lines and boxes
    void hline(int x, int y, int length, Rgb fg, Border border = Border::Single);
    void vline(int x, int y, int length, Rgb fg, Border border = Border::Single);
    // Frame on the edge of r, optional title in it (" Title " looks best).
    void box(const Rect& r, Rgb fg, Border border = Border::Single, const std::string& title = std::string(),
             Align titleAlign = Align::Left, std::uint8_t titleAttrs = Bold);
    // Frame + background inside.
    void panel(const Rect& r, Rgb fg, Rgb bg, Border border = Border::Rounded, const std::string& title = std::string());
    // A drop shadow right of and below r (darkens what is there).
    void shadow(const Rect& r);

    // ---- pictures: `image` scaled into r (in cells) with the given mode.
    void image(const Rect& r, const Image& image, PixelMode mode = PixelMode::HalfBlocks, Filter filter = Filter::Box);

    // ---- horizontal bar with 1/8-cell resolution: `fraction` 0..1 of r's
    // width filled with fg over bg (progress bars, meters).
    void bar(const Rect& r, double fraction, Rgb fg, Rgb bg);
    // Vertical bar from the bottom of r, 1/8-cell resolution.
    void columnBar(const Rect& r, double fraction, Rgb fg, Rgb bg);

private:
    Surface& surface_;
    Rect area_;  // absolute origin and size (local (0,0) = area_.x, area_.y)
    Rect clip_;  // absolute: drawing allowed here (area within the parents)
};

// ------------------------------------------------------------- DotCanvas --

// Braille plotting canvas: dotWidth() = 2 x columns, dotHeight() = 4 x rows.
// Each cell has one color (the last dot drawn in it).  paint() writes only
// the cells that have dots, keeping the background below.
class DotCanvas {
public:
    DotCanvas(int columns = 0, int rows = 0);
    void resize(int columns, int rows);
    void clear();

    int columns() const { return columns_; }
    int rows() const { return rows_; }
    int dotWidth() const { return columns_ * 2; }
    int dotHeight() const { return rows_ * 4; }

    void set(int x, int y, Rgb color);
    void unset(int x, int y);
    bool get(int x, int y) const;
    void line(int x0, int y0, int x1, int y1, Rgb color);
    // Every dot of column x between y0 and y1 (inclusive).
    void column(int x, int y0, int y1, Rgb color);

    void paint(Painter& painter, int x = 0, int y = 0, std::uint8_t attrs = Plain) const;

private:
    int columns_, rows_;
    std::vector<std::uint8_t> bits_;  // braille bit pattern per cell
    std::vector<Rgb> colors_;
};

// --------------------------------------------------------------- Display --

class Display {
public:
    explicit Display(ColorMode mode = ColorMode::TrueColor);

    void setColorMode(ColorMode mode);
    ColorMode colorMode() const { return mode_; }

    // Appends what updates the terminal (from row 1, column 1) to show
    // `surface`: the changed cells only (everything on the first call, after
    // a size change, or after invalidate()), wrapped in synchronized output.
    void render(const Surface& surface, std::string& out);
    // Forget what the terminal shows (after it was cleared or overwritten).
    void invalidate();

    // Cursor after the frame: hidden (default) or shown at (x, y), 0-based
    // (text input fields).
    void hideCursor() { cursorVisible_ = false; }
    void showCursor(int x, int y) {
        cursorVisible_ = true;
        cursorX_ = x;
        cursorY_ = y;
    }

private:
    ColorMode mode_;
    int width_, height_;
    std::vector<Cell> shown_;
    std::vector<bool> valid_;
    bool cursorVisible_;
    int cursorX_, cursorY_;
};

// ---------------------------------------------------------------- layout --

// A size along one axis for split(): fixed cells, percent of the total, or
// a share of what is left after fixed and percent sizes (flex).
struct Size {
    enum Kind { Fixed, Percent, Flex } kind;
    double value;
};
inline Size fixed(int cells) {
    Size s = {Size::Fixed, static_cast<double>(cells)};
    return s;
}
inline Size percent(double pct) {
    Size s = {Size::Percent, pct};
    return s;
}
inline Size flex(double share = 1) {
    Size s = {Size::Flex, share};
    return s;
}

// Splits r top to bottom (rows) or left to right (columns), with `gap`
// cells between the parts.  Always returns sizes.size() rectangles that
// exactly cover r (minus gaps); parts that don't fit get width/height 0.
std::vector<Rect> splitRows(const Rect& r, const std::vector<Size>& sizes, int gap = 0);
std::vector<Rect> splitColumns(const Rect& r, const std::vector<Size>& sizes, int gap = 0);
// r centered in `outer` (clipped).
Rect centered(const Rect& outer, int width, int height);

}  // namespace ui
}  // namespace gfx

#endif
