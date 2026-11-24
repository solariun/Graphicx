// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  C++11, no dependencies (the optional image loader bundles stb_image).
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   src/graphicx.cpp
//  Layer:  Core library
//
//  What this file does
//  -------------------
//  Implementation of everything declared in include/graphicx/graphicx.h:
//  Image, Options, grid sizing, resampling, the xterm-256 palette lookup,
//  the three cell styles, render() and Screen.
//
//  How it fits in
//  --------------
//  Pure computation: no I/O, no platform calls.  Both render() and
//  Screen::draw() go through the same steps for every character cell:
//    1. sample():   the cell's top and bottom pixel colors, from the source
//                   picture resized to the grid (Box average or Nearest);
//    2. shadeCell(): the cell's look for the chosen style: two colors (half
//                   block) or one character + one color (ASCII styles);
//    3. emit:       append the escape sequences / characters to a string.
//  render() writes every cell, line by line, with no cursor movement.
//  Screen remembers what every cell shows (a 64-bit key per cell) and only
//  emits cells whose key changed, each preceded by a cursor move when it does
//  not directly follow the previously written cell.
//
//  Terminal notes
//  --------------
//  * The "▀" (U+2580, UPPER HALF BLOCK) character is drawn with the top pixel
//    as foreground and the bottom pixel as background; when both are equal
//    a space with that background is cheaper to send.
//  * Every frame and every render() line ends with "CSI 0 m" so colors never
//    leak into whatever the program prints next.
//
//  Contributing
//  ------------
//  Screen::draw() runs once per video frame on up to ~20,000 cells: keep the
//  per-cell path free of allocations (strings are appended to, never built).
//  `make test` exercises all of this file.
// ============================================================================
#include "graphicx/graphicx.h"

#include <algorithm>
#include <cmath>

namespace gfx {

namespace {

// Six intensity levels of the xterm 6x6x6 color cube (entries 16-231).
const int kCubeLevels[6] = {0, 95, 135, 175, 215, 255};

const char kUpperHalf[] = "\xE2\x96\x80";  // U+2580, UTF-8
const std::uint64_t kInvalid = ~0ull;      // "unknown" cell key

// Perceptual color distance ("redmean" approximation): cheap and much
// closer to what the eye sees than plain RGB distance.
long colorDistance(const Rgb& a, const Rgb& b) {
    long rmean = (static_cast<long>(a.r) + b.r) / 2;
    long dr = static_cast<long>(a.r) - b.r;
    long dg = static_cast<long>(a.g) - b.g;
    long db = static_cast<long>(a.b) - b.b;
    return (((512 + rmean) * dr * dr) >> 8) + 4 * dg * dg + (((767 - rmean) * db * db) >> 8);
}

// Nearest-palette lookup that is exact but almost as fast as a table.
//
// Colors are grouped in 64 x 64 x 64 bins (6 bits per channel, each bin
// spans 4 values per channel).  For every bin we store, sorted, the palette
// entries that are nearest at any lattice point of the bin AND of its
// neighbors (a 4 x 4 x 4 lattice around it), in one shared list.  The
// regions where each entry is nearest are wider than that neighborhood, so
// the candidates contain the exact answer (a brute-force test over millions
// of colors finds no difference with a full search).
// Most bins have a single candidate (returned directly); for the few that
// straddle a boundary, the candidates are compared on the real color.
//
// Built once, on first use (C++11 guarantees a thread-safe one-time
// initialization of function-local statics): about 0.2 s, ~2 MB.
struct PaletteTable {
    // Bin b's candidates are list[first[b]] .. list[first[b] + count[b] - 1]
    // (palette index - 16, ascending).
    std::vector<std::uint32_t> first;
    std::vector<std::uint8_t> count;
    std::vector<std::uint8_t> list;
};

int nearestBySearch(const Rgb& color, const Rgb* palette) {
    int best = 0;
    long bestDistance = -1;
    for (int i = 0; i < 240; ++i) {
        long d = colorDistance(color, palette[i]);
        if (bestDistance < 0 || d < bestDistance) {  // ties: lowest index wins
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

const Rgb* paletteColors() {
    static const std::vector<Rgb> colors = [] {
        std::vector<Rgb> c(240);
        for (int i = 0; i < 240; ++i) c[static_cast<size_t>(i)] = paletteColor(16 + i);
        return c;
    }();
    return &colors[0];
}

const PaletteTable& paletteTable() {
    static const PaletteTable table = [] {
        const Rgb* palette = paletteColors();
        // Nearest entry at every lattice point 0, 4, 8, ..., 252, 255 (65 per axis).
        const int n = 65;
        std::vector<std::uint8_t> lattice(static_cast<size_t>(n * n * n));
        for (int b = 0; b < n; ++b) {
            for (int g = 0; g < n; ++g) {
                for (int r = 0; r < n; ++r) {
                    Rgb c = rgb(static_cast<std::uint8_t>(std::min(255, r * 4)),
                                static_cast<std::uint8_t>(std::min(255, g * 4)),
                                static_cast<std::uint8_t>(std::min(255, b * 4)));
                    lattice[static_cast<size_t>((b * n + g) * n + r)] =
                        static_cast<std::uint8_t>(nearestBySearch(c, palette));
                }
            }
        }
        PaletteTable t;
        t.first.assign(1 << 18, 0);
        t.count.assign(1 << 18, 0);
        t.list.reserve(1 << 19);
        for (int b = 0; b < 64; ++b) {
            for (int g = 0; g < 64; ++g) {
                for (int r = 0; r < 64; ++r) {
                    std::uint8_t found[64];
                    int count = 0;
                    for (int db = -1; db <= 2; ++db) {
                        for (int dg = -1; dg <= 2; ++dg) {
                            for (int dr = -1; dr <= 2; ++dr) {
                                int cr = std::max(0, std::min(n - 1, r + dr));
                                int cg = std::max(0, std::min(n - 1, g + dg));
                                int cb = std::max(0, std::min(n - 1, b + db));
                                std::uint8_t v = lattice[static_cast<size_t>((cb * n + cg) * n + cr)];
                                bool seen = false;
                                for (int i = 0; i < count; ++i) seen = seen || found[i] == v;
                                if (!seen) found[count++] = v;
                            }
                        }
                    }
                    std::sort(found, found + count);
                    size_t bin = static_cast<size_t>(r | (g << 6) | (b << 12));
                    t.first[bin] = static_cast<std::uint32_t>(t.list.size());
                    t.count[bin] = static_cast<std::uint8_t>(count);
                    t.list.insert(t.list.end(), found, found + count);
                }
            }
        }
        return t;
    }();
    return table;
}

// A color as the value that ends up in the escape sequence: the palette
// index (Palette256) or 0xRRGGBB (TrueColor).  Equal keys = equal output.
inline std::uint32_t colorKey(Rgb c, ColorMode mode) {
    if (mode == ColorMode::TrueColor) {
        return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | c.b;
    }
    return static_cast<std::uint32_t>(nearestPaletteIndex(c));
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

// SGR color sequence for a key: "CSI 38;5;n m" / "CSI 48;5;n m" or
// "CSI 38;2;r;g;b m" / "CSI 48;2;r;g;b m".
void appendColor(std::string& out, std::uint32_t key, bool foreground, ColorMode mode) {
    out += foreground ? "\x1b[38;" : "\x1b[48;";
    if (mode == ColorMode::TrueColor) {
        out += "2;";
        appendInt(out, (key >> 16) & 0xFF);
        out.push_back(';');
        appendInt(out, (key >> 8) & 0xFF);
        out.push_back(';');
        appendInt(out, key & 0xFF);
    } else {
        out += "5;";
        appendInt(out, key);
    }
    out.push_back('m');
}

void appendMove(std::string& out, int row, int column) {
    out += "\x1b[";
    appendInt(out, static_cast<unsigned>(row));
    out.push_back(';');
    appendInt(out, static_cast<unsigned>(column));
    out.push_back('H');
}

Rgb average(Rgb a, Rgb b) {
    return rgb(static_cast<std::uint8_t>((a.r + b.r + 1) / 2), static_cast<std::uint8_t>((a.g + b.g + 1) / 2),
               static_cast<std::uint8_t>((a.b + b.b + 1) / 2));
}

// Source pixel span of every grid position along one axis: position i of
// `count` covers source pixels [start[i], end[i]) of `size`.  Nearest uses
// one pixel; Box all pixels the position overlaps (at least one).
void buildSpans(int size, int count, Filter filter, std::vector<int>& start, std::vector<int>& end) {
    start.resize(static_cast<size_t>(count));
    end.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        int s = static_cast<int>(static_cast<long long>(i) * size / count);
        int e = static_cast<int>(static_cast<long long>(i + 1) * size / count);
        if (s >= size) s = size - 1;
        if (filter == Filter::Nearest || e <= s) e = s + 1;
        start[static_cast<size_t>(i)] = s;
        end[static_cast<size_t>(i)] = e;
    }
}

// Color of the grid "pixel" covering source columns [x0, x1) and rows
// [y0, y1): the single pixel, or the average of the area.
Rgb sample(const Image& image, int x0, int x1, int y0, int y1) {
    if (x1 - x0 == 1 && y1 - y0 == 1) return image.get(x0, y0);
    unsigned long r = 0, g = 0, b = 0;
    for (int y = y0; y < y1; ++y) {
        const Rgb* p = image.row(y);
        for (int x = x0; x < x1; ++x) {
            r += p[x].r;
            g += p[x].g;
            b += p[x].b;
        }
    }
    unsigned long n = static_cast<unsigned long>((x1 - x0) * (y1 - y0));
    return rgb(static_cast<std::uint8_t>((r + n / 2) / n), static_cast<std::uint8_t>((g + n / 2) / n),
               static_cast<std::uint8_t>((b + n / 2) / n));
}

// What one cell shows.
struct Cell {
    std::uint32_t fg;  // foreground color key (Blocks, AsciiColor)
    std::uint32_t bg;  // background color key (Blocks only)
    char glyph;        // character (ASCII styles; Blocks uses kUpperHalf)
};

Cell shadeCell(Rgb top, Rgb bottom, const Options& o) {
    Cell cell;
    if (o.style == Style::Blocks) {
        cell.fg = colorKey(top, o.colorMode);
        cell.bg = colorKey(bottom, o.colorMode);
        cell.glyph = 0;
        return cell;
    }
    Rgb mean = average(top, bottom);
    static const std::string kDefaultRamp(" .:-=+*#%@");
    // Both branches are lvalues of the same type: no copy per cell.
    const std::string& ramp = o.ramp.empty() ? kDefaultRamp : o.ramp;
    int steps = static_cast<int>(ramp.size());
    int index = (brightness(mean) * (steps - 1) + 127) / 255;
    if (o.invert) index = steps - 1 - index;
    cell.glyph = ramp[static_cast<size_t>(index)];
    cell.fg = o.style == Style::AsciiColor ? colorKey(mean, o.colorMode) : 0;
    cell.bg = 0;
    return cell;
}

// 64-bit identity of a cell's look, for the Screen diff.  Color keys are at
// most 24 bits; bit 62 separates the ASCII styles from half blocks.
inline std::uint64_t cellKey(const Cell& c, Style style) {
    if (style == Style::Blocks) return (static_cast<std::uint64_t>(c.fg) << 24) | c.bg;
    return (1ull << 62) | (static_cast<std::uint64_t>(c.fg) << 8) | static_cast<unsigned char>(c.glyph);
}

// Writes one cell's look, sending colors only when they change.
// haveFg/haveBg/curFg/curBg track the terminal's current colors.
void emitCell(std::string& out, const Cell& c, const Options& o, bool& haveFg, bool& haveBg, std::uint32_t& curFg,
              std::uint32_t& curBg) {
    if (o.style == Style::Blocks) {
        if (!haveBg || curBg != c.bg) {
            appendColor(out, c.bg, false, o.colorMode);
            curBg = c.bg;
            haveBg = true;
        }
        if (c.fg == c.bg) {
            out.push_back(' ');  // one color: a space with that background
            return;
        }
        if (!haveFg || curFg != c.fg) {
            appendColor(out, c.fg, true, o.colorMode);
            curFg = c.fg;
            haveFg = true;
        }
        out += kUpperHalf;
        return;
    }
    // A space shows no foreground, so its color is never sent.
    if (o.style == Style::AsciiColor && c.glyph != ' ' && (!haveFg || curFg != c.fg)) {
        appendColor(out, c.fg, true, o.colorMode);
        curFg = c.fg;
        haveFg = true;
    }
    out.push_back(c.glyph);
}

}  // namespace

// ------------------------------------------------------------------- Image --

Image::Image() : width_(0), height_(0) {}

Image::Image(int width, int height, Rgb fill)
    : width_(std::max(0, width)), height_(std::max(0, height)),
      pixels_(static_cast<size_t>(std::max(0, width)) * static_cast<size_t>(std::max(0, height)), fill) {}

void Image::resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    pixels_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_));
}

Image Image::fromRgb24(const std::uint8_t* data, int width, int height, int stride) {
    Image image(width, height);
    if (stride <= 0) stride = width * 3;
    for (int y = 0; y < image.height(); ++y) {
        const std::uint8_t* p = data + static_cast<size_t>(y) * stride;
        Rgb* out = image.row(y);
        for (int x = 0; x < image.width(); ++x, p += 3) out[x] = rgb(p[0], p[1], p[2]);
    }
    return image;
}

Image Image::fromRgba32(const std::uint8_t* data, int width, int height, int stride) {
    Image image(width, height);
    if (stride <= 0) stride = width * 4;
    for (int y = 0; y < image.height(); ++y) {
        const std::uint8_t* p = data + static_cast<size_t>(y) * stride;
        Rgb* out = image.row(y);
        for (int x = 0; x < image.width(); ++x, p += 4) out[x] = rgb(p[0], p[1], p[2]);
    }
    return image;
}

Image Image::fromRgb555(const std::uint16_t* data, int width, int height) {
    Image image(width, height);
    for (int y = 0; y < image.height(); ++y) {
        const std::uint16_t* p = data + static_cast<size_t>(y) * width;
        Rgb* out = image.row(y);
        for (int x = 0; x < image.width(); ++x) {
            int r = p[x] & 31, g = (p[x] >> 5) & 31, b = (p[x] >> 10) & 31;
            out[x] = rgb(static_cast<std::uint8_t>((r << 3) | (r >> 2)), static_cast<std::uint8_t>((g << 3) | (g >> 2)),
                         static_cast<std::uint8_t>((b << 3) | (b >> 2)));
        }
    }
    return image;
}

// ----------------------------------------------------------------- Options --

Options::Options()
    : style(Style::Blocks),
      colorMode(ColorMode::Palette256),
      filter(Filter::Box),
      columns(0),
      rows(0),
      ramp(" .:-=+*#%@"),
      invert(false) {}

const char* styleName(Style style) {
    switch (style) {
        case Style::AsciiColor: return "ascii-color";
        case Style::Ascii: return "ascii";
        default: return "blocks";
    }
}

bool parseStyle(const std::string& name, Style& style) {
    if (name == "blocks" || name == "full" || name == "color") {
        style = Style::Blocks;
    } else if (name == "ascii-color" || name == "asciicolor") {
        style = Style::AsciiColor;
    } else if (name == "ascii" || name == "ascii-bw" || name == "bw") {
        style = Style::Ascii;
    } else {
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ sizing --

void gridSize(int imageWidth, int imageHeight, const Options& options, int& columns, int& rows) {
    columns = rows = 0;
    if (imageWidth <= 0 || imageHeight <= 0) return;
    int c = options.columns, r = options.rows;
    if (c <= 0 && r <= 0) {
        c = imageWidth;
        r = (imageHeight + 1) / 2;
    } else if (r <= 0) {
        r = static_cast<int>(std::floor(static_cast<double>(c) * imageHeight / (2.0 * imageWidth) + 0.5));
    } else if (c <= 0) {
        c = static_cast<int>(std::floor(static_cast<double>(r) * 2.0 * imageWidth / imageHeight + 0.5));
    }
    columns = std::max(1, c);
    rows = std::max(1, r);
}

void fit(int imageWidth, int imageHeight, int maxColumns, int maxRows, int& columns, int& rows) {
    columns = rows = 0;
    if (imageWidth <= 0 || imageHeight <= 0 || maxColumns <= 0 || maxRows <= 0) return;
    double scale = std::min(static_cast<double>(maxColumns) / imageWidth,
                            static_cast<double>(maxRows) * 2.0 / imageHeight);
    int pixelHeight = static_cast<int>(imageHeight * scale);
    columns = std::min(maxColumns, std::max(1, static_cast<int>(imageWidth * scale)));
    rows = std::min(maxRows, std::max(1, (pixelHeight + 1) / 2));
}

// ----------------------------------------------------------------- palette --

Rgb paletteColor(int index) {
    if (index < 16) index = 16;
    if (index > 255) index = 255;
    if (index >= 232) {
        std::uint8_t v = static_cast<std::uint8_t>(8 + (index - 232) * 10);
        return rgb(v, v, v);
    }
    int i = index - 16;
    return rgb(static_cast<std::uint8_t>(kCubeLevels[i / 36]), static_cast<std::uint8_t>(kCubeLevels[(i / 6) % 6]),
               static_cast<std::uint8_t>(kCubeLevels[i % 6]));
}

int nearestPaletteIndex(Rgb c) {
    const PaletteTable& t = paletteTable();
    size_t bin = static_cast<size_t>((c.r >> 2) | ((c.g >> 2) << 6) | ((c.b >> 2) << 12));
    const std::uint8_t* candidates = &t.list[t.first[bin]];
    int count = t.count[bin];
    int best = candidates[0];
    if (count == 1) return 16 + best;  // the usual case: one candidate
    // Bin on a boundary: compare the candidates on the real color (in index
    // order, strict "<", so ties pick the lowest index like a full search).
    const Rgb* palette = paletteColors();
    long bestDistance = colorDistance(c, palette[best]);
    for (int k = 1; k < count; ++k) {
        long d = colorDistance(c, palette[candidates[k]]);
        if (d < bestDistance) {
            bestDistance = d;
            best = candidates[k];
        }
    }
    return 16 + best;
}

int brightness(Rgb c) { return (299 * c.r + 587 * c.g + 114 * c.b) / 1000; }

// ------------------------------------------------------------------ render --

std::string render(const Image& image, const Options& options) {
    std::string out;
    int columns, rows;
    gridSize(image.width(), image.height(), options, columns, rows);
    if (columns == 0) return out;

    std::vector<int> xs, xe, ys, ye;
    buildSpans(image.width(), columns, options.filter, xs, xe);
    buildSpans(image.height(), rows * 2, options.filter, ys, ye);
    out.reserve(static_cast<size_t>(columns) * rows * (options.style == Style::Ascii ? 1 : 12));

    const bool colored = options.style != Style::Ascii;
    for (int r = 0; r < rows; ++r) {
        bool haveFg = false, haveBg = false;
        std::uint32_t curFg = 0, curBg = 0;
        for (int c = 0; c < columns; ++c) {
            size_t cx = static_cast<size_t>(c);
            Rgb top = sample(image, xs[cx], xe[cx], ys[static_cast<size_t>(2 * r)], ye[static_cast<size_t>(2 * r)]);
            Rgb bottom =
                sample(image, xs[cx], xe[cx], ys[static_cast<size_t>(2 * r + 1)], ye[static_cast<size_t>(2 * r + 1)]);
            emitCell(out, shadeCell(top, bottom, options), options, haveFg, haveBg, curFg, curBg);
        }
        out += colored ? "\x1b[0m\n" : "\n";
    }
    return out;
}

// ------------------------------------------------------------------ Screen --

Screen::Screen(const Options& options)
    : options_(options),
      originRow_(1),
      originColumn_(1),
      synchronized_(true),
      columns_(0),
      rows_(0),
      imageWidth_(0),
      imageHeight_(0) {}

void Screen::setOptions(const Options& options) {
    options_ = options;
    columns_ = rows_ = 0;  // forces prepare() to rebuild tables and invalidate
}

void Screen::setOrigin(int row, int column) {
    if (row == originRow_ && column == originColumn_) return;
    originRow_ = std::max(1, row);
    originColumn_ = std::max(1, column);
    invalidate();
}

void Screen::invalidate() { std::fill(previous_.begin(), previous_.end(), kInvalid); }

void Screen::invalidateCells(int row, int column, int rows, int columns) {
    for (int r = std::max(0, row); r < std::min(rows_, row + rows); ++r) {
        for (int c = std::max(0, column); c < std::min(columns_, column + columns); ++c) {
            previous_[static_cast<size_t>(r) * columns_ + c] = kInvalid;
        }
    }
}

// Recomputes the grid and sampling tables when the picture size or the
// options changed; a new grid starts fully invalid.
void Screen::prepare(const Image& frame) {
    int columns, rows;
    gridSize(frame.width(), frame.height(), options_, columns, rows);
    if (columns == columns_ && rows == rows_ && frame.width() == imageWidth_ && frame.height() == imageHeight_) return;
    columns_ = columns;
    rows_ = rows;
    imageWidth_ = frame.width();
    imageHeight_ = frame.height();
    if (columns_ > 0) {
        buildSpans(imageWidth_, columns_, options_.filter, xStart_, xEnd_);
        buildSpans(imageHeight_, rows_ * 2, options_.filter, yStart_, yEnd_);
    }
    previous_.assign(static_cast<size_t>(columns_) * rows_, kInvalid);
}

std::string Screen::draw(const Image& frame) {
    std::string out;
    draw(frame, out);
    return out;
}

void Screen::draw(const Image& frame, std::string& out) {
    prepare(frame);
    if (synchronized_) out += "\x1b[?2026h";
    if (options_.style != Style::Blocks) out += "\x1b[0m";  // ASCII: start from the default colors

    bool haveFg = false, haveBg = false;
    std::uint32_t curFg = 0, curBg = 0;
    int cursorRow = -1, cursorColumn = -1;
    for (int r = 0; r < rows_; ++r) {
        const int y0 = yStart_[static_cast<size_t>(2 * r)], y1 = yEnd_[static_cast<size_t>(2 * r)];
        const int y2 = yStart_[static_cast<size_t>(2 * r + 1)], y3 = yEnd_[static_cast<size_t>(2 * r + 1)];
        for (int c = 0; c < columns_; ++c) {
            size_t cx = static_cast<size_t>(c);
            Rgb top = sample(frame, xStart_[cx], xEnd_[cx], y0, y1);
            Rgb bottom = sample(frame, xStart_[cx], xEnd_[cx], y2, y3);
            Cell cell = shadeCell(top, bottom, options_);
            std::uint64_t key = cellKey(cell, options_.style);
            std::uint64_t& previous = previous_[static_cast<size_t>(r) * columns_ + c];
            if (previous == key) continue;
            previous = key;

            int row = originRow_ + r, column = originColumn_ + c;
            if (row != cursorRow || column != cursorColumn) appendMove(out, row, column);
            emitCell(out, cell, options_, haveFg, haveBg, curFg, curBg);
            cursorRow = row;
            cursorColumn = column + 1;
        }
    }
    out += "\x1b[0m";
    if (synchronized_) out += "\x1b[?2026l";
}

}  // namespace gfx
