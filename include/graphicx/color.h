// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   include/graphicx/color.h
//
//  What this file does
//  -------------------
//  Color helpers shared by the canvas (canvas.h) and the user interface
//  toolkit (ui.h, widgets.h):
//    * mixing: lerp, blend (alpha), scale, from HSV, from 0xRRGGBB
//    * colormaps: value 0..1 -> color, for waterfalls, heat maps and
//      gradients (Classic SDR, Turbo, Inferno, Viridis, Ice, Grey, Amber,
//      Green, ...)
//    * themes: named color sets for interfaces (amber, green, cyan, ...)
//
//  Example
//  -------
//      gfx::Rgb c = gfx::colormap(gfx::Colormap::Turbo, 0.7);   // orange-red
//      gfx::Rgb half = gfx::lerp(gfx::hex(0x000000), c, 0.5);
// ============================================================================
#ifndef GRAPHICX_COLOR_H
#define GRAPHICX_COLOR_H

#include <string>
#include <vector>

#include "graphicx.h"

namespace gfx {

// 0xRRGGBB -> Rgb.
Rgb hex(unsigned value);
// Hue 0..360 degrees, saturation and value 0..1.
Rgb hsv(double hue, double saturation, double value);
// a + (b - a) * t, t in 0..1 (clamped).
Rgb lerp(Rgb a, Rgb b, double t);
// `top` over `bottom` with opacity alpha 0..1.
inline Rgb blend(Rgb bottom, Rgb top, double alpha) { return lerp(bottom, top, alpha); }
// Every channel multiplied by factor (clamped): 0.5 = half as bright.
Rgb scale(Rgb c, double factor);
inline bool operator==(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
inline bool operator!=(Rgb a, Rgb b) { return !(a == b); }

// ------------------------------------------------------------- colormaps --

enum class Colormap {
    Classic,   // black - blue - cyan - yellow - red - white (SDR waterfalls)
    Turbo,     // Google's improved rainbow
    Inferno,   // perceptually uniform: black - purple - orange - yellow
    Viridis,   // perceptually uniform: purple - teal - yellow (color-blind safe)
    Ice,       // black - deep blue - cyan - white
    Grey,      // black - white
    Amber,     // black - amber - white
    Green,     // black - phosphor green - white
    Hot        // black - red - yellow - white
};

// Color of value t (0..1, clamped) in the map.
Rgb colormap(Colormap map, double t);
// A 256-entry table of the map (fast lookups in inner loops).
std::vector<Rgb> colormapTable(Colormap map, int entries = 256);
const char* colormapName(Colormap map);
bool parseColormap(const std::string& name, Colormap& map);
// All maps, in declaration order (for menus).
std::vector<Colormap> allColormaps();

// ----------------------------------------------------------------- themes --

// Colors of a user interface (see ui.h / widgets.h).
struct Theme {
    std::string name;
    Rgb background;   // screen background
    Rgb panel;        // panels / frames background
    Rgb text;         // normal text
    Rgb dim;          // secondary text, labels, grid lines
    Rgb accent;       // highlights, values, active elements
    Rgb accentText;   // text drawn on accent background
    Rgb border;       // frame lines
    Rgb focus;        // focused widget border / selection
    Rgb good;         // OK / on (green-ish)
    Rgb warn;         // warning (amber-ish)
    Rgb bad;          // error / overload (red-ish)
    Colormap colormap;  // matching map for plots and waterfalls
};

// Built-in themes: "amber" (default), "orange", "lime", "green", "cyan",
// "vfd", "blue", "violet", "rose", "mono", "light".
Theme theme(const std::string& name);
std::vector<std::string> themeNames();

}  // namespace gfx

#endif
