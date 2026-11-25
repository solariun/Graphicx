// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   include/graphicx/canvas.h
//
//  What this file does
//  -------------------
//  gfx::Canvas draws on a gfx::Image, pixel by pixel: points, lines (plain,
//  anti-aliased, thick, dashed), rectangles (plain, filled, rounded),
//  circles, ellipses, arcs and pie slices, triangles, polygons and polylines,
//  gradients, other images (plain, scaled, alpha) and text in two built-in
//  bitmap fonts (5x7 and 3x5, any integer scale).  Every operation is
//  clipped to the image and to an optional clip rectangle.
//
//  How it fits in
//  --------------
//  Draw with a Canvas, then show the Image with gfx::render / gfx::Screen
//  (graphicx.h), or place it in a user interface region with
//  gfx::ui::Surface::drawImage (ui.h), which can also use braille dots
//  (2x4 pixels per cell) or quadrants (2x2) for finer detail.
//
//  Example
//  -------
//      gfx::Image img(160, 96);
//      gfx::Canvas c(img);
//      c.clear(gfx::hex(0x101018));
//      c.fillCircle(40, 48, 30, gfx::hex(0xffb000));
//      c.lineAA(0, 95, 159, 0, gfx::hex(0x40d0ff));
//      c.text(90, 10, "HELLO", gfx::hex(0xffffff), gfx::Font::Font5x7, 2);
//      std::cout << gfx::render(img);
// ============================================================================
#ifndef GRAPHICX_CANVAS_H
#define GRAPHICX_CANVAS_H

#include <string>
#include <vector>

#include "color.h"
#include "graphicx.h"

namespace gfx {

// Integer point and rectangle (x, y = top-left; width, height may be 0).
struct Point {
    int x, y;
};
inline Point point(int x, int y) {
    Point p;
    p.x = x;
    p.y = y;
    return p;
}

struct Rect {
    int x, y, width, height;
    int right() const { return x + width; }    // one past the last column
    int bottom() const { return y + height; }  // one past the last row
    bool empty() const { return width <= 0 || height <= 0; }
    bool contains(int px, int py) const { return px >= x && py >= y && px < right() && py < bottom(); }
};
inline Rect rect(int x, int y, int width, int height) {
    Rect r;
    r.x = x;
    r.y = y;
    r.width = width;
    r.height = height;
    return r;
}
Rect intersect(const Rect& a, const Rect& b);
// r shrunk by `by` on every side (grows with a negative value).
Rect inset(const Rect& r, int by);

enum class Font {
    Font5x7,  // 5x7 pixels in a 6x8 box: full printable ASCII
    Font3x5   // 3x5 pixels in a 4x6 box: digits, upper-case letters, . : - + / %
};
// Size of `text` in pixels (width includes letter spacing, minus the last).
int textWidth(const std::string& text, Font font = Font::Font5x7, int scale = 1);
int textHeight(Font font = Font::Font5x7, int scale = 1);

enum class Align { Left, Center, Right };

class Canvas {
public:
    explicit Canvas(Image& image);

    Image& image() { return image_; }
    int width() const { return image_.width(); }
    int height() const { return image_.height(); }

    // Drawing is limited to this rectangle (and the image).  resetClip()
    // = the whole image.
    void setClip(const Rect& clip);
    void resetClip();
    const Rect& clip() const { return clip_; }

    void clear(Rgb color);

    // ---- points and lines
    void pixel(int x, int y, Rgb color);
    void pixel(int x, int y, Rgb color, double alpha);  // blended
    Rgb get(int x, int y) const;                          // black outside
    void hline(int x0, int x1, int y, Rgb color);
    void vline(int x, int y0, int y1, Rgb color);
    void line(int x0, int y0, int x1, int y1, Rgb color);
    // Anti-aliased (Xiaolin Wu), sub-pixel end points.
    void lineAA(double x0, double y0, double x1, double y1, Rgb color);
    // `thickness` pixels wide, round ends.
    void thickLine(double x0, double y0, double x1, double y1, double thickness, Rgb color);
    // on / off pattern lengths in pixels.
    void dashedLine(int x0, int y0, int x1, int y1, Rgb color, int on = 4, int off = 3);
    void polyline(const std::vector<Point>& points, Rgb color, bool antialiased = false);

    // ---- shapes
    void rect(const Rect& r, Rgb color);
    void fillRect(const Rect& r, Rgb color);
    void fillRect(const Rect& r, Rgb color, double alpha);
    void roundRect(const Rect& r, int radius, Rgb color);
    void fillRoundRect(const Rect& r, int radius, Rgb color);
    void circle(int cx, int cy, int radius, Rgb color);
    void fillCircle(int cx, int cy, int radius, Rgb color);
    void circleAA(double cx, double cy, double radius, Rgb color);
    void ellipse(int cx, int cy, int rx, int ry, Rgb color);
    void fillEllipse(int cx, int cy, int rx, int ry, Rgb color);
    // Angles in degrees, 0 = right (3 o'clock), increasing counter-clockwise.
    void arc(double cx, double cy, double radius, double startDeg, double endDeg, Rgb color);
    void pie(double cx, double cy, double radius, double startDeg, double endDeg, Rgb color);
    void triangle(Point a, Point b, Point c, Rgb color);
    void fillTriangle(Point a, Point b, Point c, Rgb color);
    void polygon(const std::vector<Point>& points, Rgb color);
    void fillPolygon(const std::vector<Point>& points, Rgb color);  // even-odd rule

    // ---- fills
    // Horizontal (left = from, right = to) or vertical (top = from).
    void gradient(const Rect& r, Rgb from, Rgb to, bool vertical = false);
    // Every column/row colored by the map, t from 0 to 1.
    void gradient(const Rect& r, Colormap map, bool vertical = false);

    // ---- images
    void drawImage(const Image& source, int x, int y);
    void drawImage(const Image& source, int x, int y, double alpha);
    // Scaled to `target` (nearest neighbour, or bilinear when smooth).
    void drawImage(const Image& source, const Rect& target, bool smooth = true);
    // Pixels equal to `transparent` are skipped (sprites).
    void drawSprite(const Image& source, int x, int y, Rgb transparent);

    // ---- text: returns the x after the text
    int text(int x, int y, const std::string& text, Rgb color, Font font = Font::Font5x7, int scale = 1);
    // Aligned inside `box` (vertically centered).
    void text(const Rect& box, const std::string& text, Rgb color, Align align, Font font = Font::Font5x7,
              int scale = 1);

    // ---- scrolling (waterfalls, oscilloscopes): content moves by dy rows /
    // dx columns inside r; the uncovered strip is filled with `fill`.
    void scroll(const Rect& r, int dx, int dy, Rgb fill);

private:
    void span(int x0, int x1, int y, Rgb color);  // clipped horizontal run
    bool inClip(int x, int y) const { return x >= clip_.x && y >= clip_.y && x < clip_.right() && y < clip_.bottom(); }

    Image& image_;
    Rect clip_;
};

}  // namespace gfx

#endif
