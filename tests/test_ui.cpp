// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   tests/test_ui.cpp
//
//  What this file does
//  -------------------
//  Unit tests for the drawing and interface layers: color.h, canvas.h,
//  ui.h, widgets.h and the input parser in terminal.h.  No terminal is
//  needed: widgets are drawn into a Surface and checked cell by cell, and
//  input is fed to gfx::terminal::parseEvent as raw bytes.
//
//      make test        (runs test-graphicx and test-ui)
// ============================================================================
#include <cmath>
#include <cstdio>
#include <string>

#include "graphicx/canvas.h"
#include "graphicx/image_io.h"
#include "graphicx/png.h"
#include "graphicx/color.h"
#include "graphicx/terminal.h"
#include "graphicx/ui.h"
#include "graphicx/widgets.h"

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

using gfx::Rgb;
using gfx::rect;

const Rgb kBlack = gfx::rgb(0, 0, 0);
const Rgb kWhite = gfx::rgb(255, 255, 255);
const Rgb kRed = gfx::rgb(255, 0, 0);

int countColor(const gfx::Image& img, Rgb c) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.get(x, y) == c) ++n;
    return n;
}

void testColor() {
    CHECK(gfx::hex(0x123456) == gfx::rgb(0x12, 0x34, 0x56));
    CHECK(gfx::hsv(0, 1, 1) == kRed);
    CHECK(gfx::hsv(120, 1, 1) == gfx::rgb(0, 255, 0));
    CHECK(gfx::lerp(kBlack, kWhite, 0.5) == gfx::rgb(128, 128, 128));
    CHECK(gfx::lerp(kBlack, kWhite, 2) == kWhite);  // clamped
    CHECK(gfx::colormap(gfx::Colormap::Grey, 0) == kBlack);
    CHECK(gfx::colormap(gfx::Colormap::Grey, 1) == kWhite);
    CHECK(gfx::colormap(gfx::Colormap::Grey, std::nan("")) == kBlack);
    CHECK(gfx::colormapTable(gfx::Colormap::Turbo).size() == 256);
    gfx::Colormap m;
    CHECK(gfx::parseColormap("inferno", m) && m == gfx::Colormap::Inferno);
    CHECK(!gfx::parseColormap("nope", m));
    CHECK(gfx::allColormaps().size() == 9);
    for (size_t i = 0; i < gfx::allColormaps().size(); ++i) {
        gfx::Colormap back;
        CHECK(gfx::parseColormap(gfx::colormapName(gfx::allColormaps()[i]), back) && back == gfx::allColormaps()[i]);
    }
    CHECK(gfx::theme("cyan").name == "cyan");
    CHECK(gfx::theme("unknown").name == "amber");
    CHECK(gfx::themeNames().size() == 11);
}

void testCanvasShapes() {
    gfx::Image img(20, 10, kBlack);
    gfx::Canvas c(img);
    c.pixel(-1, 0, kWhite);  // clipped silently
    c.pixel(25, 3, kWhite);
    CHECK(countColor(img, kWhite) == 0);

    c.line(0, 0, 19, 0, kWhite);
    CHECK(countColor(img, kWhite) == 20);
    c.clear(kBlack);
    c.line(0, 0, 9, 9, kWhite);  // diagonal: one pixel per row
    CHECK(countColor(img, kWhite) == 10);

    c.clear(kBlack);
    c.rect(rect(2, 2, 5, 4), kWhite);
    CHECK(countColor(img, kWhite) == 2 * 5 + 2 * 2);
    c.fillRect(rect(2, 2, 5, 4), kRed);
    CHECK(countColor(img, kRed) == 20);

    c.clear(kBlack);
    c.setClip(rect(0, 0, 10, 10));
    c.fillRect(rect(0, 0, 20, 10), kWhite);
    CHECK(countColor(img, kWhite) == 100);  // right half clipped
    c.resetClip();

    gfx::Image big(41, 41, kBlack);
    gfx::Canvas d(big);
    d.fillCircle(20, 20, 10, kWhite);
    int area = countColor(big, kWhite);
    CHECK(area > 300 && area < 360);  // pi * 10.5^2 = 346 (pixel centres within r + 0.5)
    d.clear(kBlack);
    d.circle(20, 20, 10, kWhite);
    CHECK(big.get(30, 20) == kWhite && big.get(10, 20) == kWhite && big.get(20, 10) == kWhite);
    CHECK(big.get(20, 20) == kBlack);

    d.clear(kBlack);
    std::vector<gfx::Point> tri;
    tri.push_back(gfx::point(0, 0));
    tri.push_back(gfx::point(40, 0));
    tri.push_back(gfx::point(0, 40));
    d.fillPolygon(tri, kWhite);
    int t = countColor(big, kWhite);
    CHECK(t > 780 && t < 900);  // ~ 40*40/2 + edges
    CHECK(big.get(5, 5) == kWhite && big.get(35, 35) == kBlack);

    d.clear(kBlack);
    d.fillEllipse(20, 20, 15, 5, kWhite);
    CHECK(big.get(34, 20) == kWhite && big.get(20, 26) == kBlack);

    d.clear(kBlack);
    d.pie(20, 20, 15, 0, 90, kWhite);  // top-right quarter
    CHECK(big.get(28, 12) == kWhite && big.get(12, 28) == kBlack);

    d.clear(kBlack);
    d.lineAA(0, 0.5, 40, 0.5, kWhite);  // half-covered rows: blended
    CHECK(big.get(20, 0).r > 60 && big.get(20, 0).r < 200);

    d.clear(kBlack);
    d.gradient(rect(0, 0, 41, 1), kBlack, kWhite);
    CHECK(big.get(0, 0) == kBlack && big.get(40, 0) == kWhite);

    d.clear(kBlack);
    d.thickLine(5, 20, 35, 20, 5, kWhite);
    CHECK(big.get(20, 18) == kWhite && big.get(20, 22) == kWhite && big.get(20, 25) == kBlack);
}

void testCanvasText() {
    CHECK(gfx::textWidth("AB") == 11);
    CHECK(gfx::textWidth("AB", gfx::Font::Font3x5, 2) == 14);
    CHECK(gfx::textHeight() == 7);
    gfx::Image img(60, 10, kBlack);
    gfx::Canvas c(img);
    int end = c.text(0, 0, "~", kWhite);  // last glyph of the table exists
    CHECK(end == 6);
    CHECK(countColor(img, kWhite) > 0);
    c.clear(kBlack);
    c.text(0, 0, "1", kWhite, gfx::Font::Font3x5);
    CHECK(countColor(img, kWhite) == 8);  // 010 110 010 010 111 = 1+2+1+1+3
}

void testCanvasImages() {
    gfx::Image src(2, 2, kWhite);
    src.set(0, 0, kRed);
    gfx::Image dst(10, 10, kBlack);
    gfx::Canvas c(dst);
    c.drawImage(src, 8, 8);
    CHECK(dst.get(8, 8) == kRed && dst.get(9, 9) == kWhite);
    c.drawImage(src, rect(0, 0, 4, 4), false);
    CHECK(dst.get(0, 0) == kRed && dst.get(1, 1) == kRed && dst.get(3, 3) == kWhite);
    c.clear(kBlack);
    c.drawSprite(src, 0, 0, kWhite);
    CHECK(dst.get(0, 0) == kRed && dst.get(1, 1) == kBlack);

    gfx::Image col(1, 4, kBlack);
    col.set(0, 0, kWhite);
    gfx::Canvas s(col);
    s.scroll(rect(0, 0, 1, 4), 0, 1, kRed);
    CHECK(col.get(0, 0) == kRed && col.get(0, 1) == kWhite && col.get(0, 2) == kBlack);
}

void testUtf8() {
    std::string s;
    gfx::ui::appendUtf8(s, 0x2580);
    CHECK(s == "\xE2\x96\x80");
    size_t pos = 0;
    CHECK(gfx::ui::nextCodePoint(s, pos) == 0x2580 && pos == 3);
    CHECK(gfx::ui::columns("a\xE2\x96\x80" "b") == 3);
    CHECK(gfx::ui::truncate("hello world", 5) == "hell\xE2\x80\xA6");
    CHECK(gfx::ui::truncate("hi", 5) == "hi");
    std::vector<std::string> lines = gfx::ui::wrap("the quick brown fox", 9);
    CHECK(lines.size() == 2 && lines[0] == "the quick" && lines[1] == "brown fox");
    lines = gfx::ui::wrap("abcdefghij", 4);
    CHECK(lines.size() == 3 && lines[0] == "abcd" && lines[2] == "ij");
    lines = gfx::ui::wrap("a\nb", 10);
    CHECK(lines.size() == 2 && lines[1] == "b");
}

void testPainter() {
    gfx::ui::Surface s(20, 6, kBlack);
    gfx::ui::Painter p(s);
    p.box(rect(0, 0, 20, 6), kWhite, gfx::ui::Border::Rounded, " T ");
    CHECK(s.at(0, 0).ch == 0x256D && s.at(19, 5).ch == 0x256F);
    CHECK(s.rowText(0).find(" T ") != std::string::npos);

    gfx::ui::Painter inner = p.sub(rect(1, 1, 5, 2));
    inner.text(0, 0, "abcdefgh", kRed);  // clipped at 5 columns
    CHECK(s.at(1, 1).ch == 'a' && s.at(5, 1).ch == 'e' && s.at(6, 1).ch == ' ');
    CHECK(s.at(1, 1).fg == kRed && s.at(1, 1).bg == kBlack);
    gfx::ui::Painter nested = inner.sub(rect(3, 0, 10, 1));  // can't escape its parent
    nested.text(0, 0, "XXXXXXXX", kWhite);
    CHECK(s.at(4, 1).ch == 'X' && s.at(5, 1).ch == 'X' && s.at(6, 1).ch == ' ');

    p.fill(rect(0, 0, 20, 6), kBlack);
    p.bar(rect(0, 0, 10, 1), 0.55, kWhite, kBlack);  // 5.5 cells
    CHECK(s.at(4, 0).ch == 0x2588 && s.at(5, 0).ch == 0x258C && s.at(6, 0).ch == ' ');
    p.columnBar(rect(0, 2, 1, 4), 0.5, kWhite, kBlack);  // two full cells from the bottom
    CHECK(s.at(0, 5).ch == 0x2588 && s.at(0, 4).ch == 0x2588 && s.at(0, 3).ch == ' ');

    p.text(rect(0, 1, 20, 1), "mid", kWhite, gfx::Align::Center);
    CHECK(s.rowText(1).substr(8, 3) == "mid");
}

void testPixelModes() {
    // A 2x4 image: left column white, right column black.
    gfx::Image img(2, 4, kBlack);
    for (int y = 0; y < 4; ++y) img.set(0, y, kWhite);
    gfx::ui::Surface s(1, 1, kBlack);
    gfx::ui::Painter p(s);
    p.image(rect(0, 0, 1, 1), img, gfx::ui::PixelMode::Braille, gfx::Filter::Nearest);
    CHECK(s.at(0, 0).ch == 0x2800 + 0x47);  // dots 1, 2, 3, 7 (left column)
    CHECK(s.at(0, 0).fg == kWhite && s.at(0, 0).bg == kBlack);
    p.image(rect(0, 0, 1, 1), img, gfx::ui::PixelMode::Quadrants, gfx::Filter::Box);
    CHECK(s.at(0, 0).ch == 0x258C);  // ▌ left half
    p.image(rect(0, 0, 1, 1), img, gfx::ui::PixelMode::HalfBlocks, gfx::Filter::Box);
    CHECK(s.at(0, 0).ch == ' ' || s.at(0, 0).ch == 0x2580);  // averaged: same gray top and bottom
    gfx::Image flat(2, 2, kRed);
    p.image(rect(0, 0, 1, 1), flat, gfx::ui::PixelMode::Quadrants);
    CHECK(s.at(0, 0).ch == ' ' && s.at(0, 0).bg == kRed);
}

void testDotCanvas() {
    gfx::ui::DotCanvas d(3, 2);
    CHECK(d.dotWidth() == 6 && d.dotHeight() == 8);
    d.set(0, 0, kRed);
    d.set(1, 3, kRed);
    CHECK(d.get(0, 0) && d.get(1, 3) && !d.get(1, 0));
    d.set(100, 100, kRed);  // ignored
    gfx::ui::Surface s(3, 2, kBlack);
    gfx::ui::Painter p(s);
    d.paint(p);
    CHECK(s.at(0, 0).ch == 0x2800 + 0x01 + 0x80 && s.at(0, 0).fg == kRed);
    CHECK(s.at(1, 0).ch == ' ');
    d.clear();
    d.line(0, 0, 5, 7, kWhite);
    CHECK(d.get(0, 0) && d.get(5, 7));
}

void testDisplay() {
    gfx::ui::Surface s(4, 2, kBlack);
    gfx::ui::Painter p(s);
    p.text(0, 0, "ab", kWhite);
    gfx::ui::Display d(gfx::ColorMode::TrueColor);
    std::string out;
    d.render(s, out);
    CHECK(out.find("\x1b[1;1H") != std::string::npos);
    CHECK(out.find("ab") != std::string::npos);
    CHECK(out.find("38;2;255;255;255") != std::string::npos);
    out.clear();
    d.render(s, out);  // nothing changed: no cell output
    CHECK(out.find('H') == std::string::npos || out.find("ab") == std::string::npos);
    CHECK(out.find("ab") == std::string::npos);
    p.put(3, 1, 'z', kRed, kBlack, gfx::ui::Bold);
    out.clear();
    d.render(s, out);
    CHECK(out.find("\x1b[2;4H") != std::string::npos && out.find('z') != std::string::npos);
    CHECK(out.find("\x1b[0;1;38;2;255;0;0;48;2;0;0;0m") != std::string::npos);
    gfx::ui::Display d256(gfx::ColorMode::Palette256);
    out.clear();
    d256.render(s, out);
    CHECK(out.find("38;5;196") != std::string::npos);
}

void testLayout() {
    std::vector<gfx::ui::Size> sizes;
    sizes.push_back(gfx::ui::fixed(10));
    sizes.push_back(gfx::ui::flex(1));
    sizes.push_back(gfx::ui::flex(1));
    std::vector<gfx::Rect> r = gfx::ui::splitColumns(rect(0, 0, 31, 5), sizes, 1);
    CHECK(r.size() == 3 && r[0].width == 10 && r[1].x == 11 && r[1].width + r[2].width == 19);
    CHECK(r[2].right() == 31);
    sizes.clear();
    sizes.push_back(gfx::ui::percent(50));
    sizes.push_back(gfx::ui::flex());
    r = gfx::ui::splitRows(rect(0, 0, 10, 20), sizes);
    CHECK(r[0].height == 10 && r[1].y == 10 && r[1].height == 10);
    sizes.clear();
    sizes.push_back(gfx::ui::fixed(30));
    sizes.push_back(gfx::ui::fixed(30));
    r = gfx::ui::splitRows(rect(0, 0, 10, 40), sizes);  // too big: shrunk
    CHECK(r[0].height + r[1].height == 40);
    gfx::Rect c = gfx::ui::centered(rect(0, 0, 10, 10), 4, 2);
    CHECK(c.x == 3 && c.y == 4);
}

void testInputParser() {
    using namespace gfx::terminal;
    Event e;
    std::string in = "a\x1b[A\x1b[1;5C\x1b[5~\x1b[<0;10;5M\x1b[<0;10;5m\x1b[<64;3;3M\x03\xE2\x82\xAC\x1bx\x1b[Z\x1bOP";
    CHECK(parseEvent(in, e) && e.isChar('a'));
    CHECK(parseEvent(in, e) && e.isKey(KeyUp) && !e.ctrl);
    CHECK(parseEvent(in, e) && e.isKey(KeyRight) && e.ctrl);
    CHECK(parseEvent(in, e) && e.isKey(KeyPageUp));
    CHECK(parseEvent(in, e) && e.type == EventType::Mouse && e.button == MouseButton::Left &&
          e.action == MouseAction::Press && e.x == 9 && e.y == 4);
    CHECK(parseEvent(in, e) && e.type == EventType::Mouse && e.action == MouseAction::Release);
    CHECK(parseEvent(in, e) && e.type == EventType::Mouse && e.button == MouseButton::WheelUp);
    CHECK(parseEvent(in, e) && e.key == 'c' && e.ctrl);
    CHECK(parseEvent(in, e) && e.key == 0x20AC);
    CHECK(parseEvent(in, e) && e.key == 'x' && e.alt);
    CHECK(parseEvent(in, e) && e.isKey(KeyBackTab));
    CHECK(parseEvent(in, e) && e.isKey(KeyF1));
    CHECK(in.empty() && !parseEvent(in, e));
    std::string partial = "\x1b[1;";
    CHECK(!parseEvent(partial, e) && partial == "\x1b[1;");  // waits for the rest
    std::string esc = "\x1b";
    CHECK(parseEvent(esc, e) && e.isKey(KeyEscape));
    e = Event();
    e.type = EventType::Key;
    e.key = KeyF5;
    e.ctrl = true;
    CHECK(keyName(e) == "Ctrl+F5");
}

gfx::terminal::Event key(int k) {
    gfx::terminal::Event e;
    e.type = gfx::terminal::EventType::Key;
    e.key = k;
    return e;
}

gfx::terminal::Event click(int x, int y, gfx::terminal::MouseAction a = gfx::terminal::MouseAction::Press) {
    gfx::terminal::Event e;
    e.type = gfx::terminal::EventType::Mouse;
    e.button = gfx::terminal::MouseButton::Left;
    e.action = a;
    e.x = x;
    e.y = y;
    return e;
}

void testWidgets() {
    using namespace gfx::ui;
    App app(gfx::theme("amber"));
    int pressed = 0;
    Button b("Go");
    b.bounds = rect(0, 0, 8, 1);
    b.onPress = [&]() { ++pressed; };
    Slider sl("Vol", 0, 10, 5);
    sl.step = 1;
    sl.bounds = rect(0, 2, 30, 1);
    RadioGroup rg;
    rg.options.push_back("AM");
    rg.options.push_back("FM");
    rg.options.push_back("USB");
    rg.bounds = rect(0, 4, 30, 1);
    int mode = -1;
    rg.onChange = [&](int i) { mode = i; };
    TextInput ti("freq");
    ti.bounds = rect(0, 6, 20, 1);
    std::string submitted;
    ti.onSubmit = [&](const std::string& s) { submitted = s; };
    app.add(b);
    app.add(sl);
    app.add(rg);
    app.add(ti);

    CHECK(app.focused() == &b);
    app.dispatch(key(gfx::terminal::KeyEnter));
    CHECK(pressed == 1);
    app.dispatch(click(3, 0));
    app.dispatch(click(3, 0, gfx::terminal::MouseAction::Release));
    CHECK(pressed == 2);

    app.dispatch(key(gfx::terminal::KeyTab));
    CHECK(app.focused() == &sl);
    app.dispatch(key(gfx::terminal::KeyRight));
    CHECK(sl.value == 6);
    app.dispatch(key(gfx::terminal::KeyEnd));
    CHECK(sl.value == 10);

    app.dispatch(key(gfx::terminal::KeyTab));
    app.dispatch(key(gfx::terminal::KeyRight));
    CHECK(rg.selected == 1 && mode == 1);
    // Click on "USB": options are " AM " " FM " " USB " separated by 1.
    app.dispatch(click(11, 4));
    CHECK(rg.selected == 2);

    app.focus(ti);
    app.dispatch(key('1'));
    app.dispatch(key('4'));
    app.dispatch(key('5'));
    app.dispatch(key(gfx::terminal::KeyBackspace));
    app.dispatch(key(gfx::terminal::KeyEnter));
    CHECK(submitted == "14");

    bool quitBound = false;
    app.bindKey('q', [&]() { quitBound = true; });
    app.focus(b);
    app.dispatch(key('q'));
    CHECK(quitBound);

    // Menu popup: Down + Enter picks the second item and closes.
    Menu m;
    m.addItem("One");
    m.addSeparator();
    m.addItem("Two", "F2");
    int picked = -1;
    m.onPick = [&](int i) { picked = i; };
    m.place(5, 5, rect(0, 0, 80, 24));
    app.openPopup(m);
    CHECK(app.popupOpen());
    app.dispatch(key(gfx::terminal::KeyDown));
    app.dispatch(key(gfx::terminal::KeyEnter));
    CHECK(picked == 2 && !app.popupOpen());

    // Drawing: a Meter and a Plot render without touching outside bounds.
    Surface s(40, 10, gfx::theme("amber").background);
    Painter p(s);
    Meter meter;
    meter.min = 0;
    meter.max = 10;
    meter.update(5);
    meter.label = "S";
    meter.bounds = rect(0, 0, 20, 2);
    Painter mp = p.sub(meter.bounds);
    meter.draw(mp, app.theme(), false);
    CHECK(s.at(2, 0).ch == 0x25AE);  // lit segment
    CHECK(s.at(19, 0).ch == 0x25AF);  // unlit

    Plot plot;
    plot.bounds = rect(0, 2, 40, 8);
    Plot::Series series;
    series.color = kRed;
    series.useColor = true;
    series.fill = false;
    for (int i = 0; i < 100; ++i) series.y.push_back(std::sin(i / 10.0));
    plot.series.push_back(series);
    plot.xMin = 0;
    plot.xMax = 100;
    Painter pp = p.sub(plot.bounds);
    plot.draw(pp, app.theme(), false);
    int dots = 0;
    for (int y = 2; y < 10; ++y)
        for (int x = 0; x < 40; ++x)
            if (s.at(x, y).ch >= 0x2800 && s.at(x, y).ch <= 0x28FF && s.at(x, y).fg == kRed) ++dots;
    CHECK(dots > 20);
    CHECK(plot.columnOf(50) > plot.plotArea().x && plot.columnOf(50) < plot.plotArea().right());
    CHECK(std::fabs(plot.xAt(plot.columnOf(50)) - 50) < 100.0 / plot.plotArea().width + 1e-9);

    Waterfall wf;
    wf.bounds = rect(0, 0, 10, 3);
    wf.low = 0;
    wf.high = 1;
    std::vector<float> row(20, 1.0f);
    wf.addRow(row);
    Surface ws(10, 3, kBlack);
    Painter wp(ws);
    wf.draw(wp, app.theme(), false);
    CHECK(ws.at(5, 0).fg == gfx::colormap(gfx::Colormap::Classic, 1.0));  // newest row on top

    BigText big("12.5");
    int bw, bh;
    big.measure(bw, bh);
    CHECK(bw == 8 && bh == 3);  // 3x5 font in quadrants: 15+1 px wide, 5+1 high
}

bool sameImage(const gfx::Image& a, const gfx::Image& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.get(x, y) != b.get(x, y)) return false;
    return true;
}

void testPng() {
    // Colour noise (incompressible), a gradient (compressible), a grey image.
    gfx::Image noise(37, 23);
    unsigned seed = 12345;
    for (int y = 0; y < noise.height(); ++y)
        for (int x = 0; x < noise.width(); ++x) {
            seed = seed * 1103515245u + 12345u;
            noise.set(x, y, gfx::rgb(static_cast<std::uint8_t>(seed >> 16), static_cast<std::uint8_t>(seed >> 8), static_cast<std::uint8_t>(seed >> 24)));
        }
    gfx::Image grad(300, 200);
    gfx::Canvas(grad).gradient(rect(0, 0, 300, 200), gfx::Colormap::Turbo);
    gfx::Image grey(180, 90);
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 180; ++x) {
            std::uint8_t v = static_cast<std::uint8_t>((x * 7 + y * 3) & 0xFF);
            grey.set(x, y, gfx::rgb(v, v, v));
        }
    const gfx::Image* images[3] = {&noise, &grad, &grey};
    for (int i = 0; i < 3; ++i) {
        std::string png = gfx::encodePng(*images[i]);
        CHECK(png.size() > 8 && png.compare(1, 3, "PNG") == 0);
        gfx::Image back = gfx::loadImageFromMemory(png.data(), png.size());
        CHECK(sameImage(back, *images[i]));
    }
    std::string g = gfx::encodePng(grad);
    CHECK(g.size() < static_cast<size_t>(300 * 200 * 3) / 10);  // gradient compresses well
    std::string gr = gfx::encodePng(grey);
    CHECK(gr[25] == 0);  // colour type 0: written as greyscale
    CHECK(gfx::encodePng(gfx::Image()).empty());
    std::string error;
    CHECK(!gfx::savePng("/nonexistent-dir/x.png", grad, error) && !error.empty());
}

}  // namespace

// Tabs: padded names when they fit, compact (no padding) when they do not;
// the mouse finds the same tab either way.
void testTabs() {
    using namespace gfx::ui;
    const char* names[] = {"Bands", "Memories", "Log", "Heard", "Monitor"};
    for (int round = 0; round < 2; ++round) {
        const int width = round == 0 ? 42 : 32;  // padded needs 28 names + 5 * 3 - 1 = 42
        Tabs tabs;
        tabs.tabs.assign(names, names + 5);
        tabs.bounds = rect(0, 0, width, 1);
        Surface s(width, 1, kBlack);
        Painter p(s);
        tabs.draw(p, gfx::theme("amber"), false);
        if (round == 0) {
            CHECK(s.at(0, 0).ch == ' ' && s.at(1, 0).ch == 'B');
            CHECK(s.at(34, 0).ch == 'M' && s.at(40, 0).ch == 'r');  // " Monitor " at 33-41
        } else {
            CHECK(s.at(0, 0).ch == 'B');
            CHECK(s.at(25, 0).ch == 'M' && s.at(31, 0).ch == 'r');  // compact: all five fit
        }
        int selected = -1;
        tabs.onChange = [&selected](int i) { selected = i; };
        tabs.handle(click(round == 0 ? 38 : 28, 0));
        CHECK(tabs.selected == 4 && selected == 4);
    }
}

int main() {
    testPng();
    testColor();
    testCanvasShapes();
    testCanvasText();
    testCanvasImages();
    testUtf8();
    testPainter();
    testPixelModes();
    testDotCanvas();
    testDisplay();
    testLayout();
    testInputParser();
    testWidgets();
    testTabs();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
