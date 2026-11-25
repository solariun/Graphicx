// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   src/ui.cpp
//
//  What this file does
//  -------------------
//  Implementation of ui.h: UTF-8 helpers, Surface, Painter (text, boxes,
//  bars, pictures in four pixel modes), DotCanvas (braille plotting),
//  Display (diff renderer) and the layout helpers.
//
//  Terminal concepts
//  -----------------
//  * Quadrants: U+2596..U+259F plus the half blocks; a 4-bit pattern (top
//    left 1, top right 2, bottom left 4, bottom right 8) picks the glyph.
//  * Braille: U+2800 + an 8-bit dot pattern.  Dot bits by (column, row):
//    (0,0)=0x01 (0,1)=0x02 (0,2)=0x04 (1,0)=0x08 (1,1)=0x10 (1,2)=0x20
//    (0,3)=0x40 (1,3)=0x80.
//  * Two-color cells (quadrants, braille) split the cell's pixels into the
//    brighter and the darker group (around the mean brightness); the
//    foreground is the average of the bright group, the background the
//    average of the dark one.
//  * Display sends a full SGR ("CSI 0;attrs;fg;bg m") whenever the style
//    changes between two cells written, and cursor moves only when the next
//    changed cell is not the one right after the previous.
// ============================================================================
#include "graphicx/ui.h"

#include <algorithm>
#include <cmath>

namespace gfx {
namespace ui {

namespace {

const std::uint32_t kQuadrants[16] = {0x20,   0x2598, 0x259D, 0x2580, 0x2596, 0x258C, 0x259E, 0x259B,
                                      0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588};
const std::uint8_t kBrailleBit[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};  // [row][col]

// Frame glyphs: top-left, top-right, bottom-left, bottom-right, top,
// bottom, left, right.
struct BorderGlyphs {
    std::uint32_t tl, tr, bl, br, top, bottom, left, right;
};

BorderGlyphs glyphs(Border b) {
    BorderGlyphs g;
    switch (b) {
        case Border::Double: g = {0x2554, 0x2557, 0x255A, 0x255D, 0x2550, 0x2550, 0x2551, 0x2551}; break;
        case Border::Rounded: g = {0x256D, 0x256E, 0x2570, 0x256F, 0x2500, 0x2500, 0x2502, 0x2502}; break;
        case Border::Heavy: g = {0x250F, 0x2513, 0x2517, 0x251B, 0x2501, 0x2501, 0x2503, 0x2503}; break;
        case Border::Dashed: g = {0x250C, 0x2510, 0x2514, 0x2518, 0x254C, 0x254C, 0x254E, 0x254E}; break;
        case Border::Ascii: g = {'+', '+', '+', '+', '-', '-', '|', '|'}; break;
        case Border::Block: g = {0x259B, 0x259C, 0x2599, 0x259F, 0x2580, 0x2584, 0x258C, 0x2590}; break;
        case Border::None: g = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}; break;
        default: g = {0x250C, 0x2510, 0x2514, 0x2518, 0x2500, 0x2500, 0x2502, 0x2502}; break;
    }
    return g;
}

Rgb averageOf(const Rgb* px, const bool* use, int n, bool want) {
    unsigned r = 0, g = 0, b = 0, k = 0;
    for (int i = 0; i < n; ++i) {
        if (use[i] != want) continue;
        r += px[i].r;
        g += px[i].g;
        b += px[i].b;
        ++k;
    }
    if (k == 0) return rgb(0, 0, 0);
    return rgb(static_cast<std::uint8_t>((r + k / 2) / k), static_cast<std::uint8_t>((g + k / 2) / k),
               static_cast<std::uint8_t>((b + k / 2) / k));
}

// Splits n pixels into bright (true) / dark (false) around the mean
// brightness.  Returns false when they are all alike (one color).
bool splitTwo(const Rgb* px, int n, bool* bright) {
    int lum[8], sum = 0, lo = 255, hi = 0;
    for (int i = 0; i < n; ++i) {
        lum[i] = brightness(px[i]);
        sum += lum[i];
        lo = std::min(lo, lum[i]);
        hi = std::max(hi, lum[i]);
    }
    if (hi - lo < 4) {
        for (int i = 0; i < n; ++i) bright[i] = false;
        return false;
    }
    int mean2 = sum * 2;  // compare lum * n * 2 against sum * 2 (no rounding)
    for (int i = 0; i < n; ++i) bright[i] = lum[i] * n * 2 > mean2;
    return true;
}

void appendInt(std::string& out, unsigned v) {
    char buf[12];
    int n = 0;
    do {
        buf[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v > 0);
    while (n > 0) out.push_back(buf[--n]);
}

void appendColor(std::string& out, Rgb c, bool fg, ColorMode mode) {
    out += fg ? ";38;" : ";48;";
    if (mode == ColorMode::TrueColor) {
        out += "2;";
        appendInt(out, c.r);
        out.push_back(';');
        appendInt(out, c.g);
        out.push_back(';');
        appendInt(out, c.b);
    } else {
        out += "5;";
        appendInt(out, static_cast<unsigned>(nearestPaletteIndex(c)));
    }
}

}  // namespace

bool operator==(const Cell& a, const Cell& b) { return a.ch == b.ch && a.fg == b.fg && a.bg == b.bg && a.attrs == b.attrs; }

// ------------------------------------------------------------------- UTF-8 --

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::uint32_t nextCodePoint(const std::string& s, size_t& pos) {
    unsigned char c = static_cast<unsigned char>(s[pos]);
    int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (n == 0 || pos + static_cast<size_t>(n) > s.size()) {
        ++pos;
        return 0xFFFD;
    }
    std::uint32_t cp = n == 1 ? c : (c & (0x7F >> n));
    for (int i = 1; i < n; ++i) {
        unsigned char d = static_cast<unsigned char>(s[pos + static_cast<size_t>(i)]);
        if ((d & 0xC0) != 0x80) {
            ++pos;
            return 0xFFFD;
        }
        cp = (cp << 6) | (d & 0x3F);
    }
    pos += static_cast<size_t>(n);
    return cp;
}

int columns(const std::string& text) {
    int n = 0;
    for (size_t i = 0; i < text.size();) {
        nextCodePoint(text, i);
        ++n;
    }
    return n;
}

std::string truncate(const std::string& text, int width) {
    if (width <= 0) return std::string();
    if (columns(text) <= width) return text;
    std::string out;
    size_t i = 0;
    for (int n = 0; n < width - 1 && i < text.size(); ++n) appendUtf8(out, nextCodePoint(text, i));
    appendUtf8(out, 0x2026);  // …
    return out;
}

std::vector<std::string> wrap(const std::string& text, int width) {
    std::vector<std::string> lines;
    if (width <= 0) return lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        // Greedy word wrap of one paragraph.
        std::string line;
        size_t i = 0;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            i = sp == std::string::npos ? para.size() : sp + 1;
            while (columns(word) > width) {  // a word longer than the line: hard split
                if (!line.empty()) {
                    lines.push_back(line);
                    line.clear();
                }
                std::string head;
                size_t k = 0;
                for (int n = 0; n < width; ++n) appendUtf8(head, nextCodePoint(word, k));
                lines.push_back(head);
                word = word.substr(k);
            }
            if (line.empty()) line = word;
            else if (columns(line) + 1 + columns(word) <= width) line += " " + word;
            else {
                lines.push_back(line);
                line = word;
            }
        }
        lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

// ----------------------------------------------------------------- Surface --

Surface::Surface(int width, int height, Rgb background) : width_(0), height_(0) { resize(width, height, background); }

void Surface::resize(int width, int height, Rgb background) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    Cell c = {' ', rgb(255, 255, 255), background, Plain};
    cells_.assign(static_cast<size_t>(width_) * height_, c);
}

void Surface::clear(Rgb background, Rgb foreground) {
    Cell c = {' ', foreground, background, Plain};
    std::fill(cells_.begin(), cells_.end(), c);
}

std::string Surface::rowText(int y) const {
    std::string s;
    if (y < 0 || y >= height_) return s;
    for (int x = 0; x < width_; ++x) appendUtf8(s, at(x, y).ch);
    return s;
}

// ----------------------------------------------------------------- Painter --

Painter::Painter(Surface& surface)
    : surface_(surface), area_(rect(0, 0, surface.width(), surface.height())), clip_(area_) {}

Painter::Painter(Surface& surface, const Rect& area)
    : surface_(surface), area_(area), clip_(intersect(area, rect(0, 0, surface.width(), surface.height()))) {}

Painter Painter::sub(const Rect& local) const {
    Painter p(surface_, rect(area_.x + local.x, area_.y + local.y, local.width, local.height));
    p.clip_ = intersect(p.clip_, clip_);  // never outside this painter
    return p;
}

bool Painter::inside(int x, int y) const {
    return x >= 0 && y >= 0 && x < area_.width && y < area_.height && clip_.contains(area_.x + x, area_.y + y);
}

Cell* Painter::cell(int x, int y) {
    if (!inside(x, y)) return 0;
    return &surface_.at(area_.x + x, area_.y + y);
}

void Painter::put(int x, int y, std::uint32_t ch, Rgb fg, Rgb bg, std::uint8_t attrs) {
    Cell* c = cell(x, y);
    if (!c) return;
    c->ch = ch;
    c->fg = fg;
    c->bg = bg;
    c->attrs = attrs;
}

void Painter::put(int x, int y, std::uint32_t ch, Rgb fg, std::uint8_t attrs) {
    Cell* c = cell(x, y);
    if (!c) return;
    c->ch = ch;
    c->fg = fg;
    c->attrs = attrs;
}

void Painter::setBackground(int x, int y, Rgb bg) {
    if (Cell* c = cell(x, y)) c->bg = bg;
}

void Painter::setForeground(int x, int y, Rgb fg) {
    if (Cell* c = cell(x, y)) c->fg = fg;
}

void Painter::fill(const Rect& r, Rgb bg, std::uint32_t ch, Rgb fg) {
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x) put(x, y, ch, fg, bg, Plain);
}

void Painter::tint(const Rect& r, Rgb bg) {
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x) setBackground(x, y, bg);
}

void Painter::darken(const Rect& r, double factor) {
    for (int y = r.y; y < r.bottom(); ++y) {
        for (int x = r.x; x < r.right(); ++x) {
            if (Cell* c = cell(x, y)) {
                c->bg = scale(c->bg, factor);
                c->fg = scale(c->fg, factor);
            }
        }
    }
}

int Painter::text(int x, int y, const std::string& s, Rgb fg, std::uint8_t attrs) {
    for (size_t i = 0; i < s.size();) put(x++, y, nextCodePoint(s, i), fg, attrs);
    return x;
}

int Painter::text(int x, int y, const std::string& s, Rgb fg, Rgb bg, std::uint8_t attrs) {
    for (size_t i = 0; i < s.size();) put(x++, y, nextCodePoint(s, i), fg, bg, attrs);
    return x;
}

void Painter::text(const Rect& r, const std::string& s, Rgb fg, Align align, std::uint8_t attrs) {
    if (r.empty()) return;
    std::string t = truncate(s, r.width);
    int w = columns(t);
    int x = r.x;
    if (align == Align::Center) x += (r.width - w) / 2;
    else if (align == Align::Right) x = r.right() - w;
    text(x, r.y + (r.height - 1) / 2, t, fg, attrs);
}

int Painter::paragraph(const Rect& r, const std::string& s, Rgb fg, std::uint8_t attrs) {
    std::vector<std::string> lines = wrap(s, r.width);
    int n = std::min(static_cast<int>(lines.size()), r.height);
    for (int i = 0; i < n; ++i) text(r.x, r.y + i, lines[static_cast<size_t>(i)], fg, attrs);
    return n;
}

void Painter::hline(int x, int y, int length, Rgb fg, Border border) {
    std::uint32_t ch = glyphs(border).top;
    for (int i = 0; i < length; ++i) put(x + i, y, ch, fg);
}

void Painter::vline(int x, int y, int length, Rgb fg, Border border) {
    std::uint32_t ch = glyphs(border).left;
    for (int i = 0; i < length; ++i) put(x, y + i, ch, fg);
}

void Painter::box(const Rect& r, Rgb fg, Border border, const std::string& title, Align titleAlign, std::uint8_t titleAttrs) {
    if (r.width < 2 || r.height < 2) return;
    if (border != Border::None) {
        BorderGlyphs g = glyphs(border);
        for (int x = r.x + 1; x < r.right() - 1; ++x) {
            put(x, r.y, g.top, fg);
            put(x, r.bottom() - 1, g.bottom, fg);
        }
        for (int y = r.y + 1; y < r.bottom() - 1; ++y) {
            put(r.x, y, g.left, fg);
            put(r.right() - 1, y, g.right, fg);
        }
        put(r.x, r.y, g.tl, fg);
        put(r.right() - 1, r.y, g.tr, fg);
        put(r.x, r.bottom() - 1, g.bl, fg);
        put(r.right() - 1, r.bottom() - 1, g.br, fg);
    }
    if (!title.empty() && r.width > 4) text(rect(r.x + 2, r.y, r.width - 4, 1), title, fg, titleAlign, titleAttrs);
}

void Painter::panel(const Rect& r, Rgb fg, Rgb bg, Border border, const std::string& title) {
    fill(r, bg);
    box(r, fg, border, title);
}

void Painter::shadow(const Rect& r) {
    darken(rect(r.right(), r.y + 1, 2, r.height), 0.4);
    darken(rect(r.x + 2, r.bottom(), r.width, 1), 0.4);
}

void Painter::bar(const Rect& r, double fraction, Rgb fg, Rgb bg) {
    if (r.empty()) return;
    if (!(fraction > 0)) fraction = 0;  // also NaN
    fraction = std::min(1.0, fraction);
    int eighths = static_cast<int>(std::floor(fraction * r.width * 8 + 0.5));
    for (int x = 0; x < r.width; ++x) {
        int k = std::max(0, std::min(8, eighths - x * 8));
        std::uint32_t ch = k == 0 ? ' ' : (k == 8 ? 0x2588 : 0x2590 - static_cast<std::uint32_t>(k));
        for (int y = r.y; y < r.bottom(); ++y) put(r.x + x, y, ch, fg, bg);
    }
}

void Painter::columnBar(const Rect& r, double fraction, Rgb fg, Rgb bg) {
    if (r.empty()) return;
    if (!(fraction > 0)) fraction = 0;
    fraction = std::min(1.0, fraction);
    int eighths = static_cast<int>(std::floor(fraction * r.height * 8 + 0.5));
    for (int i = 0; i < r.height; ++i) {  // i = 0 is the bottom row
        int k = std::max(0, std::min(8, eighths - i * 8));
        std::uint32_t ch = k == 0 ? ' ' : 0x2580 + static_cast<std::uint32_t>(k);
        for (int x = r.x; x < r.right(); ++x) put(x, r.bottom() - 1 - i, ch, fg, bg);
    }
}

void pixelsPerCell(PixelMode mode, int& across, int& down) {
    switch (mode) {
        case PixelMode::Quadrants: across = 2; down = 2; break;
        case PixelMode::Braille: across = 2; down = 4; break;
        default: across = 1; down = 2; break;
    }
}

void Painter::image(const Rect& r, const Image& img, PixelMode mode, Filter filter) {
    if (r.empty() || img.empty()) return;
    int across, down;
    pixelsPerCell(mode, across, down);
    int pw = r.width * across, ph = r.height * down;

    // Source span of every target pixel (box average or nearest).
    std::vector<int> xs(static_cast<size_t>(pw) + 1), ys(static_cast<size_t>(ph) + 1);
    for (int i = 0; i <= pw; ++i) xs[static_cast<size_t>(i)] = static_cast<int>(static_cast<long long>(i) * img.width() / pw);
    for (int i = 0; i <= ph; ++i) ys[static_cast<size_t>(i)] = static_cast<int>(static_cast<long long>(i) * img.height() / ph);
    struct Sampler {
        const Image& img;
        const std::vector<int>& xs;
        const std::vector<int>& ys;
        Filter filter;
        Rgb operator()(int px, int py) const {
            int x0 = xs[static_cast<size_t>(px)], x1 = std::max(x0 + 1, xs[static_cast<size_t>(px) + 1]);
            int y0 = ys[static_cast<size_t>(py)], y1 = std::max(y0 + 1, ys[static_cast<size_t>(py) + 1]);
            x0 = std::min(x0, img.width() - 1);
            y0 = std::min(y0, img.height() - 1);
            x1 = std::min(x1, img.width());
            y1 = std::min(y1, img.height());
            if (filter == Filter::Nearest || (x1 - x0 == 1 && y1 - y0 == 1)) return img.get(x0, y0);
            unsigned long rr = 0, gg = 0, bb = 0, n = 0;
            for (int y = y0; y < y1; ++y) {
                const Rgb* row = img.row(y);
                for (int x = x0; x < x1; ++x, ++n) {
                    rr += row[x].r;
                    gg += row[x].g;
                    bb += row[x].b;
                }
            }
            return rgb(static_cast<std::uint8_t>(rr / n), static_cast<std::uint8_t>(gg / n), static_cast<std::uint8_t>(bb / n));
        }
    } sample = {img, xs, ys, filter};

    static const char kRamp[] = " .:-=+*#%@";
    Rgb px[8];
    bool bright[8];
    for (int cy = 0; cy < r.height; ++cy) {
        for (int cx = 0; cx < r.width; ++cx) {
            int x = r.x + cx, y = r.y + cy;
            int bx = cx * across, by = cy * down;
            switch (mode) {
                case PixelMode::HalfBlocks: {
                    Rgb top = sample(bx, by), bottom = sample(bx, by + 1);
                    if (top == bottom) put(x, y, ' ', top, bottom);
                    else put(x, y, 0x2580, top, bottom);
                    break;
                }
                case PixelMode::Ascii: {
                    Rgb top = sample(bx, by), bottom = sample(bx, by + 1);
                    Rgb mean = lerp(top, bottom, 0.5);
                    int i = (brightness(mean) * 9 + 127) / 255;
                    put(x, y, static_cast<std::uint32_t>(kRamp[i]), mean);
                    break;
                }
                case PixelMode::Quadrants: {
                    px[0] = sample(bx, by);
                    px[1] = sample(bx + 1, by);
                    px[2] = sample(bx, by + 1);
                    px[3] = sample(bx + 1, by + 1);
                    if (!splitTwo(px, 4, bright)) {
                        put(x, y, ' ', px[0], averageOf(px, bright, 4, false));
                        break;
                    }
                    int pattern = (bright[0] ? 1 : 0) | (bright[1] ? 2 : 0) | (bright[2] ? 4 : 0) | (bright[3] ? 8 : 0);
                    put(x, y, kQuadrants[pattern], averageOf(px, bright, 4, true), averageOf(px, bright, 4, false));
                    break;
                }
                case PixelMode::Braille: {
                    for (int row = 0; row < 4; ++row)
                        for (int col = 0; col < 2; ++col) px[row * 2 + col] = sample(bx + col, by + row);
                    if (!splitTwo(px, 8, bright)) {
                        put(x, y, ' ', px[0], averageOf(px, bright, 8, false));
                        break;
                    }
                    std::uint32_t bits = 0;
                    for (int row = 0; row < 4; ++row)
                        for (int col = 0; col < 2; ++col)
                            if (bright[row * 2 + col]) bits |= kBrailleBit[row][col];
                    put(x, y, 0x2800 + bits, averageOf(px, bright, 8, true), averageOf(px, bright, 8, false));
                    break;
                }
            }
        }
    }
}

// --------------------------------------------------------------- DotCanvas --

DotCanvas::DotCanvas(int columns, int rows) : columns_(0), rows_(0) { resize(columns, rows); }

void DotCanvas::resize(int columns, int rows) {
    columns_ = std::max(0, columns);
    rows_ = std::max(0, rows);
    bits_.assign(static_cast<size_t>(columns_) * rows_, 0);
    colors_.assign(static_cast<size_t>(columns_) * rows_, rgb(255, 255, 255));
}

void DotCanvas::clear() { std::fill(bits_.begin(), bits_.end(), 0); }

void DotCanvas::set(int x, int y, Rgb color) {
    if (x < 0 || y < 0 || x >= dotWidth() || y >= dotHeight()) return;
    size_t i = static_cast<size_t>(y / 4) * columns_ + x / 2;
    bits_[i] |= kBrailleBit[y % 4][x % 2];
    colors_[i] = color;
}

void DotCanvas::unset(int x, int y) {
    if (x < 0 || y < 0 || x >= dotWidth() || y >= dotHeight()) return;
    bits_[static_cast<size_t>(y / 4) * columns_ + x / 2] &= static_cast<std::uint8_t>(~kBrailleBit[y % 4][x % 2]);
}

bool DotCanvas::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= dotWidth() || y >= dotHeight()) return false;
    return (bits_[static_cast<size_t>(y / 4) * columns_ + x / 2] & kBrailleBit[y % 4][x % 2]) != 0;
}

void DotCanvas::line(int x0, int y0, int x1, int y1, Rgb color) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        set(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void DotCanvas::column(int x, int y0, int y1, Rgb color) {
    if (y0 > y1) std::swap(y0, y1);
    for (int y = std::max(0, y0); y <= std::min(dotHeight() - 1, y1); ++y) set(x, y, color);
}

void DotCanvas::paint(Painter& p, int x, int y, std::uint8_t attrs) const {
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < columns_; ++c) {
            size_t i = static_cast<size_t>(r) * columns_ + c;
            if (bits_[i]) p.put(x + c, y + r, 0x2800 + bits_[i], colors_[i], attrs);
        }
    }
}

// ----------------------------------------------------------------- Display --

Display::Display(ColorMode mode) : mode_(mode), width_(0), height_(0), cursorVisible_(false), cursorX_(0), cursorY_(0) {}

void Display::setColorMode(ColorMode mode) {
    if (mode != mode_) invalidate();
    mode_ = mode;
}

void Display::invalidate() { std::fill(valid_.begin(), valid_.end(), false); }

void Display::render(const Surface& s, std::string& out) {
    if (s.width() != width_ || s.height() != height_) {
        width_ = s.width();
        height_ = s.height();
        shown_.assign(static_cast<size_t>(width_) * height_, Cell());
        valid_.assign(static_cast<size_t>(width_) * height_, false);
    }
    out += "\x1b[?2026h";
    bool haveStyle = false;
    Cell style = Cell();
    int curX = -1, curY = -1;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            size_t i = static_cast<size_t>(y) * width_ + x;
            const Cell& c = s.at(x, y);
            if (valid_[i] && shown_[i] == c) continue;
            shown_[i] = c;
            valid_[i] = true;
            if (x != curX || y != curY) {
                out += "\x1b[";
                appendInt(out, static_cast<unsigned>(y + 1));
                out.push_back(';');
                appendInt(out, static_cast<unsigned>(x + 1));
                out.push_back('H');
            }
            // A space shows no foreground: keep whatever color is current.
            bool blank = c.ch == ' ' && !(c.attrs & (Underline | Reverse | Strike));
            bool attrsChange = !haveStyle || style.attrs != c.attrs;
            bool fgChange = !blank && (attrsChange || style.fg != c.fg);
            bool bgChange = attrsChange || style.bg != c.bg;
            if (attrsChange) {
                // New attributes: reset, then everything.
                out += "\x1b[0";
                if (c.attrs & Bold) out += ";1";
                if (c.attrs & Dim) out += ";2";
                if (c.attrs & Italic) out += ";3";
                if (c.attrs & Underline) out += ";4";
                if (c.attrs & Blink) out += ";5";
                if (c.attrs & Reverse) out += ";7";
                if (c.attrs & Strike) out += ";9";
                appendColor(out, c.fg, true, mode_);
                appendColor(out, c.bg, false, mode_);
                out.push_back('m');
                style = c;
                haveStyle = true;
            } else if (fgChange || bgChange) {
                // Same attributes: only the colors that differ.  appendColor
                // writes ";38;..." so the leading ';' is dropped once.
                std::string sgr;
                if (fgChange) appendColor(sgr, c.fg, true, mode_);
                if (bgChange) appendColor(sgr, c.bg, false, mode_);
                out += "\x1b[";
                out.append(sgr, 1, std::string::npos);
                out.push_back('m');
                if (fgChange) style.fg = c.fg;
                if (bgChange) style.bg = c.bg;
            }
            appendUtf8(out, c.ch < 0x20 ? 0x20 : c.ch);  // never send control characters
            curX = x + 1;
            curY = y;
        }
    }
    out += "\x1b[0m";
    if (cursorVisible_ && cursorX_ >= 0 && cursorY_ >= 0 && cursorX_ < width_ && cursorY_ < height_) {
        out += "\x1b[";
        appendInt(out, static_cast<unsigned>(cursorY_ + 1));
        out.push_back(';');
        appendInt(out, static_cast<unsigned>(cursorX_ + 1));
        out += "H\x1b[?25h";
    } else {
        out += "\x1b[?25l";
    }
    out += "\x1b[?2026l";
}

// ------------------------------------------------------------------ layout --

namespace {

std::vector<int> splitLengths(int total, const std::vector<Size>& sizes, int gap) {
    std::vector<int> len(sizes.size(), 0);
    if (sizes.empty()) return len;
    int avail = std::max(0, total - gap * static_cast<int>(sizes.size() - 1));
    int used = 0;
    double flexSum = 0;
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (sizes[i].kind == Size::Fixed) len[i] = static_cast<int>(sizes[i].value);
        else if (sizes[i].kind == Size::Percent) len[i] = static_cast<int>(avail * sizes[i].value / 100.0);
        else flexSum += std::max(0.0, sizes[i].value);
        len[i] = std::max(0, len[i]);
        used += len[i];
    }
    // Fixed / percent parts that don't fit are shrunk from the end.
    for (size_t i = sizes.size(); i-- > 0 && used > avail;) {
        int cut = std::min(len[i], used - avail);
        len[i] -= cut;
        used -= cut;
    }
    int left = avail - used;
    if (flexSum > 0 && left > 0) {
        int given = 0;
        size_t lastFlex = 0;
        for (size_t i = 0; i < sizes.size(); ++i) {
            if (sizes[i].kind != Size::Flex) continue;
            len[i] = static_cast<int>(left * std::max(0.0, sizes[i].value) / flexSum);
            given += len[i];
            lastFlex = i;
        }
        len[lastFlex] += left - given;  // rounding remainder
    }
    return len;
}

}  // namespace

std::vector<Rect> splitRows(const Rect& r, const std::vector<Size>& sizes, int gap) {
    std::vector<int> len = splitLengths(r.height, sizes, gap);
    std::vector<Rect> out;
    int y = r.y;
    for (size_t i = 0; i < len.size(); ++i) {
        out.push_back(rect(r.x, y, r.width, len[i]));
        y += len[i] + gap;
    }
    return out;
}

std::vector<Rect> splitColumns(const Rect& r, const std::vector<Size>& sizes, int gap) {
    std::vector<int> len = splitLengths(r.width, sizes, gap);
    std::vector<Rect> out;
    int x = r.x;
    for (size_t i = 0; i < len.size(); ++i) {
        out.push_back(rect(x, r.y, len[i], r.height));
        x += len[i] + gap;
    }
    return out;
}

Rect centered(const Rect& outer, int width, int height) {
    width = std::min(width, outer.width);
    height = std::min(height, outer.height);
    return rect(outer.x + (outer.width - width) / 2, outer.y + (outer.height - height) / 2, width, height);
}

}  // namespace ui
}  // namespace gfx
