// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  C++11, no dependencies (the optional image loader bundles stb_image).
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   tests/test_graphicx.cpp
//  Layer:  Tests
//
//  What this file does
//  -------------------
//  Unit tests for the core library (include/graphicx/graphicx.h): palette,
//  sizing, the three styles, both color modes, filters, invert, Screen
//  diffs and invalidation.  No test framework needed: `make test` builds and
//  runs it; it prints one line per check and exits non-zero on failure.
//
//  Contributing
//  ------------
//  Add a CHECK for every behavior you add or fix.  Tests must not need a
//  terminal or any file (they run in CI and in pipes).
// ============================================================================
#include <cstdio>
#include <string>

#include "graphicx/graphicx.h"

namespace {

int failures = 0;
int checks = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        ++checks;                                                      \
        if (!(cond)) {                                                 \
            ++failures;                                                \
            std::printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                              \
    } while (0)

int count(const std::string& s, const std::string& what) {
    int n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size())) ++n;
    return n;
}

gfx::Image solid(int w, int h, gfx::Rgb c) { return gfx::Image(w, h, c); }

void testPalette() {
    gfx::Rgb black = gfx::paletteColor(16), white = gfx::paletteColor(231), gray = gfx::paletteColor(232);
    CHECK(black.r == 0 && black.g == 0 && black.b == 0);
    CHECK(white.r == 255 && white.g == 255 && white.b == 255);
    CHECK(gray.r == 8 && gray.g == 8 && gray.b == 8);
    CHECK(gfx::nearestPaletteIndex(gfx::rgb(255, 0, 0)) == 196);
    CHECK(gfx::nearestPaletteIndex(gfx::rgb(0, 0, 0)) == 16);
    CHECK(gfx::nearestPaletteIndex(gfx::rgb(255, 255, 255)) == 231);
    CHECK(gfx::brightness(gfx::rgb(255, 255, 255)) == 255);
    CHECK(gfx::brightness(gfx::rgb(0, 0, 0)) == 0);
}

void testImage() {
    const std::uint16_t px[2] = {0x7FFF, 0x001F};  // white, red
    gfx::Image img = gfx::Image::fromRgb555(px, 2, 1);
    CHECK(img.width() == 2 && img.height() == 1);
    CHECK(img.get(0, 0).r == 255 && img.get(0, 0).b == 255);
    CHECK(img.get(1, 0).r == 255 && img.get(1, 0).g == 0);
    const std::uint8_t rgb24[6] = {1, 2, 3, 4, 5, 6};
    gfx::Image b = gfx::Image::fromRgb24(rgb24, 2, 1);
    CHECK(b.get(1, 0).r == 4 && b.get(1, 0).b == 6);
    const std::uint8_t rgba[8] = {1, 2, 3, 0, 4, 5, 6, 255};
    gfx::Image c = gfx::Image::fromRgba32(rgba, 2, 1);
    CHECK(c.get(0, 0).g == 2 && c.get(1, 0).g == 5);
    CHECK(gfx::Image().empty());
}

void testSizing() {
    gfx::Options o;
    int c, r;
    gfx::gridSize(160, 144, o, c, r);
    CHECK(c == 160 && r == 72);
    o.columns = 80;
    gfx::gridSize(160, 144, o, c, r);
    CHECK(c == 80 && r == 36);
    o.columns = 0;
    o.rows = 36;
    gfx::gridSize(160, 144, o, c, r);
    CHECK(c == 80 && r == 36);
    gfx::fit(160, 144, 100, 40, c, r);
    CHECK(c == 88 && r == 40);
    gfx::fit(240, 160, 240, 80, c, r);
    CHECK(c == 240 && r == 80);
    std::string name = gfx::styleName(gfx::Style::AsciiColor);
    gfx::Style s;
    CHECK(gfx::parseStyle(name, s) && s == gfx::Style::AsciiColor);
    CHECK(gfx::parseStyle("full", s) && s == gfx::Style::Blocks);
    CHECK(gfx::parseStyle("bw", s) && s == gfx::Style::Ascii);
    CHECK(!gfx::parseStyle("nope", s));
}

void testRender() {
    gfx::Options o;
    o.style = gfx::Style::Ascii;
    std::string plain = gfx::render(solid(10, 4, gfx::rgb(255, 255, 255)), o);
    CHECK(plain.find('\x1b') == std::string::npos);  // no escapes at all
    CHECK(count(plain, "\n") == 2);                    // 4 pixel rows = 2 lines
    CHECK(plain == "@@@@@@@@@@\n@@@@@@@@@@\n");
    o.invert = true;
    CHECK(gfx::render(solid(2, 2, gfx::rgb(255, 255, 255)), o) == "  \n");

    // Blocks: a two-color cell uses the half block, a one-color cell a space.
    gfx::Image split(1, 2);
    split.set(0, 0, gfx::rgb(255, 0, 0));
    split.set(0, 1, gfx::rgb(0, 0, 255));
    gfx::Options blocks;
    std::string b = gfx::render(split, blocks);
    CHECK(b == "\x1b[48;5;21m\x1b[38;5;196m\xE2\x96\x80\x1b[0m\n");
    std::string uniform = gfx::render(solid(3, 2, gfx::rgb(255, 0, 0)), blocks);
    CHECK(uniform == "\x1b[48;5;196m   \x1b[0m\n");

    gfx::Options tc;
    tc.colorMode = gfx::ColorMode::TrueColor;
    CHECK(gfx::render(split, tc).find("\x1b[38;2;255;0;0m") != std::string::npos);

    gfx::Options color;
    color.style = gfx::Style::AsciiColor;
    std::string ac = gfx::render(solid(2, 2, gfx::rgb(255, 0, 0)), color);
    CHECK(ac.find("\x1b[48;") == std::string::npos);  // never a background
    CHECK(ac.find("\x1b[38;5;196m") != std::string::npos);

    // Box filter averages: black/white rows -> one mid-gray cell; Nearest
    // keeps one pixel.
    gfx::Image stripes(2, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 2; ++x) stripes.set(x, y, (y % 2) ? gfx::rgb(255, 255, 255) : gfx::rgb(0, 0, 0));
    gfx::Options box;
    box.style = gfx::Style::Ascii;
    box.columns = 1;
    box.rows = 1;
    std::string mid = gfx::render(stripes, box);
    CHECK(mid.size() == 2 && mid[0] != ' ' && mid[0] != '@');
    box.filter = gfx::Filter::Nearest;
    CHECK(gfx::render(stripes, box) == " \n");  // rows 0 and 2: black
}

void testScreen() {
    gfx::Options o;
    o.style = gfx::Style::Ascii;
    gfx::Screen screen(o);
    screen.setOrigin(5, 10);
    gfx::Image img = solid(4, 2, gfx::rgb(255, 255, 255));
    std::string first = screen.draw(img);
    CHECK(first.find("\x1b[?2026h") == 0);
    CHECK(first.find("\x1b[5;10H@@@@") != std::string::npos);
    CHECK(screen.columns() == 4 && screen.rows() == 1);
    std::string second = screen.draw(img);
    CHECK(second.find('H') == std::string::npos);   // nothing changed: no cursor moves
    CHECK(second.find('@') == std::string::npos);

    img.set(2, 0, gfx::rgb(0, 0, 0));
    img.set(2, 1, gfx::rgb(0, 0, 0));
    std::string third = screen.draw(img);
    CHECK(third.find("\x1b[5;12H ") != std::string::npos);  // only cell 2 rewritten
    CHECK(count(third, "H") == 1);

    screen.invalidateCells(0, 0, 1, 2);
    std::string fourth = screen.draw(img);
    CHECK(fourth.find("\x1b[5;10H@@") != std::string::npos);
    screen.setSynchronized(false);
    CHECK(screen.draw(img).find("2026") == std::string::npos);
    screen.invalidate();
    CHECK(count(screen.draw(img), "@") == 3);
}

}  // namespace

int main() {
    testPalette();
    testImage();
    testSizing();
    testRender();
    testScreen();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
