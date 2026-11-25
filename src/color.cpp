// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   src/color.cpp
//
//  What this file does
//  -------------------
//  Implementation of color.h: color math, colormaps (as lists of control
//  points interpolated linearly) and the built-in interface themes.
//
//  Contributing
//  ------------
//  A new colormap = a new enum value in color.h, its control points in
//  stops() below, and its name in kMapNames.  Keep the control points evenly
//  spaced (they are spread over 0..1 in order).
// ============================================================================
#include "graphicx/color.h"

#include <algorithm>
#include <cmath>

namespace gfx {

namespace {

std::uint8_t clamp8(double v) { return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5)); }

const char* const kMapNames[] = {"classic", "turbo", "inferno", "viridis", "ice", "grey", "amber", "green", "hot"};
const int kMapCount = 9;

// Control points of each map, evenly spread over 0..1.
const std::vector<unsigned>& stops(Colormap map) {
    static const std::vector<unsigned> classic = {0x000000, 0x000060, 0x0030c0, 0x00a0e0, 0x30e0a0,
                                                  0xe0e020, 0xff8000, 0xff2000, 0xffffff};
    static const std::vector<unsigned> turbo = {0x30123b, 0x4145ab, 0x4675ed, 0x39a2fc, 0x1bcfd4, 0x24eca6,
                                                0x61fc6c, 0xa4fc3b, 0xd1e834, 0xf3c63a, 0xfe9b2d, 0xf36315,
                                                0xd93806, 0xb11901, 0x7a0402};
    static const std::vector<unsigned> inferno = {0x000004, 0x1b0c41, 0x4a0c6b, 0x781c6d, 0xa52c60,
                                                  0xcf4446, 0xed6925, 0xfb9b06, 0xf7d13d, 0xfcffa4};
    static const std::vector<unsigned> viridis = {0x440154, 0x482878, 0x3e4989, 0x31688e, 0x26828e,
                                                  0x1f9e89, 0x35b779, 0x6ece58, 0xb5de2b, 0xfde725};
    static const std::vector<unsigned> ice = {0x000000, 0x001030, 0x003080, 0x0070c0, 0x40b0f0, 0xa0e0ff, 0xffffff};
    static const std::vector<unsigned> grey = {0x000000, 0xffffff};
    static const std::vector<unsigned> amber = {0x000000, 0x301000, 0x803800, 0xd07000, 0xffb000, 0xffe080, 0xffffff};
    static const std::vector<unsigned> green = {0x000000, 0x002808, 0x006020, 0x10a040, 0x40e060, 0xb0ffb0, 0xffffff};
    static const std::vector<unsigned> hot = {0x000000, 0x600000, 0xc00000, 0xff4000, 0xffa000, 0xffff40, 0xffffff};
    switch (map) {
        case Colormap::Turbo: return turbo;
        case Colormap::Inferno: return inferno;
        case Colormap::Viridis: return viridis;
        case Colormap::Ice: return ice;
        case Colormap::Grey: return grey;
        case Colormap::Amber: return amber;
        case Colormap::Green: return green;
        case Colormap::Hot: return hot;
        default: return classic;
    }
}

}  // namespace

Rgb hex(unsigned v) {
    return rgb(static_cast<std::uint8_t>((v >> 16) & 0xFF), static_cast<std::uint8_t>((v >> 8) & 0xFF),
               static_cast<std::uint8_t>(v & 0xFF));
}

Rgb hsv(double h, double s, double v) {
    h = std::fmod(h, 360.0);
    if (h < 0) h += 360.0;
    s = std::max(0.0, std::min(1.0, s));
    v = std::max(0.0, std::min(1.0, v));
    double c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1)), m = v - c;
    double r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    return rgb(clamp8((r + m) * 255), clamp8((g + m) * 255), clamp8((b + m) * 255));
}

Rgb lerp(Rgb a, Rgb b, double t) {
    t = std::max(0.0, std::min(1.0, t));
    return rgb(clamp8(a.r + (b.r - a.r) * t), clamp8(a.g + (b.g - a.g) * t), clamp8(a.b + (b.b - a.b) * t));
}

Rgb scale(Rgb c, double f) { return rgb(clamp8(c.r * f), clamp8(c.g * f), clamp8(c.b * f)); }

Rgb colormap(Colormap map, double t) {
    const std::vector<unsigned>& s = stops(map);
    if (!(t > 0)) return hex(s.front());  // also catches NaN
    if (t >= 1) return hex(s.back());
    double pos = t * (s.size() - 1);
    size_t i = static_cast<size_t>(pos);
    return lerp(hex(s[i]), hex(s[i + 1]), pos - i);
}

std::vector<Rgb> colormapTable(Colormap map, int entries) {
    std::vector<Rgb> table(static_cast<size_t>(std::max(2, entries)));
    for (size_t i = 0; i < table.size(); ++i) table[i] = colormap(map, static_cast<double>(i) / (table.size() - 1));
    return table;
}

const char* colormapName(Colormap map) {
    int i = static_cast<int>(map);
    return i >= 0 && i < kMapCount ? kMapNames[i] : "classic";
}

bool parseColormap(const std::string& name, Colormap& map) {
    for (int i = 0; i < kMapCount; ++i) {
        if (name == kMapNames[i]) {
            map = static_cast<Colormap>(i);
            return true;
        }
    }
    if (name == "gray" || name == "greyscale") {
        map = Colormap::Grey;
        return true;
    }
    return false;
}

std::vector<Colormap> allColormaps() {
    std::vector<Colormap> maps;
    for (int i = 0; i < kMapCount; ++i) maps.push_back(static_cast<Colormap>(i));
    return maps;
}

// ----------------------------------------------------------------- themes --

namespace {

// A dark theme from one main hue: text and accents in that hue.
Theme darkTheme(const char* name, unsigned accent, unsigned text, Colormap map) {
    Theme t;
    t.name = name;
    t.background = hex(0x0a0a0a);
    t.panel = hex(0x141414);
    t.accent = hex(accent);
    t.text = hex(text);
    t.dim = lerp(hex(0x0a0a0a), hex(text), 0.5);
    t.accentText = hex(0x000000);
    t.border = lerp(hex(0x0a0a0a), hex(accent), 0.45);
    t.focus = hex(accent);
    t.good = hex(0x40d060);
    t.warn = hex(0xffb000);
    t.bad = hex(0xff4040);
    t.colormap = map;
    return t;
}

}  // namespace

Theme theme(const std::string& name) {
    if (name == "orange") return darkTheme("orange", 0xff8020, 0xffc090, Colormap::Hot);
    if (name == "lime") return darkTheme("lime", 0xb0f020, 0xe0ffa0, Colormap::Viridis);
    if (name == "green") return darkTheme("green", 0x30e060, 0xa0ffb0, Colormap::Green);
    if (name == "cyan") return darkTheme("cyan", 0x20d0f0, 0xa0f0ff, Colormap::Ice);
    if (name == "vfd") {
        Theme t = darkTheme("vfd", 0x40ffd0, 0x90ffe8, Colormap::Ice);
        t.background = hex(0x041410);
        t.panel = hex(0x08201a);
        return t;
    }
    if (name == "blue") return darkTheme("blue", 0x4090ff, 0xb0d0ff, Colormap::Ice);
    if (name == "violet") return darkTheme("violet", 0xb070ff, 0xe0c8ff, Colormap::Inferno);
    if (name == "rose") return darkTheme("rose", 0xff6090, 0xffc0d0, Colormap::Inferno);
    if (name == "mono") {
        Theme t = darkTheme("mono", 0xffffff, 0xd0d0d0, Colormap::Grey);
        t.good = t.warn = t.bad = hex(0xffffff);
        return t;
    }
    if (name == "light") {
        Theme t;
        t.name = "light";
        t.background = hex(0xf4f4f0);
        t.panel = hex(0xe8e8e2);
        t.text = hex(0x202020);
        t.dim = hex(0x808080);
        t.accent = hex(0x0060c0);
        t.accentText = hex(0xffffff);
        t.border = hex(0xa0a0a0);
        t.focus = hex(0x0060c0);
        t.good = hex(0x108030);
        t.warn = hex(0xb07000);
        t.bad = hex(0xc02020);
        t.colormap = Colormap::Viridis;
        return t;
    }
    return darkTheme("amber", 0xffb000, 0xffd890, Colormap::Classic);
}

std::vector<std::string> themeNames() {
    static const char* const names[] = {"amber", "orange", "lime", "green", "cyan", "vfd",
                                        "blue",  "violet", "rose", "mono",  "light"};
    return std::vector<std::string>(names, names + 11);
}

}  // namespace gfx
