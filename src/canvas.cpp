// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   src/canvas.cpp
//
//  What this file does
//  -------------------
//  Implementation of gfx::Canvas (canvas.h).  The algorithms are the
//  classic ones:
//    * line:        Bresenham (integer only)
//    * lineAA:      Xiaolin Wu (two pixels per step, blended by coverage)
//    * circle:      midpoint circle; filled shapes are drawn as spans
//    * ellipse:     midpoint ellipse (two regions)
//    * polygons:    scanline fill with the even-odd rule
//    * arcs / pies: sampled per pixel by angle (simple and robust)
//
//  Contributing
//  ------------
//  Every write goes through pixel(), span() or the clip check, so new
//  primitives get clipping for free if they use those.
// ============================================================================
#include "graphicx/canvas.h"

#include <algorithm>
#include <cmath>

#include "font_data.h"

namespace gfx {

namespace {

const double kPi = 3.14159265358979323846;

double fpart(double x) { return x - std::floor(x); }

// Normalises an angle in degrees to [0, 360).
double norm360(double a) {
    a = std::fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

// True when angle a (degrees) lies on the arc going counter-clockwise from
// start to end.
bool angleIn(double a, double start, double end) {
    if (end - start >= 360.0) return true;
    a = norm360(a);
    start = norm360(start);
    end = norm360(end);
    return start <= end ? (a >= start && a <= end) : (a >= start || a <= end);
}

int glyphIndex5x7(char ch) {
    unsigned char c = static_cast<unsigned char>(ch);
    return (c >= 0x20 && c <= 0x7E) ? c - 0x20 : '?' - 0x20;
}

int glyphIndex3x5(char ch) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 'A');
    if (c == '{') c = '(';
    if (c == '}') c = ')';
    if (c == '|') c = '!';
    return (c >= 0x20 && c <= 0x5F) ? c - 0x20 : '?' - 0x20;
}

}  // namespace

Rect intersect(const Rect& a, const Rect& b) {
    int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    int x1 = std::min(a.right(), b.right()), y1 = std::min(a.bottom(), b.bottom());
    return rect(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
}

Rect inset(const Rect& r, int by) { return rect(r.x + by, r.y + by, std::max(0, r.width - 2 * by), std::max(0, r.height - 2 * by)); }

int textWidth(const std::string& text, Font font, int scale) {
    if (text.empty()) return 0;
    int advance = font == Font::Font5x7 ? 6 : 4;
    return (static_cast<int>(text.size()) * advance - 1) * std::max(1, scale);
}

int textHeight(Font font, int scale) { return (font == Font::Font5x7 ? 7 : 5) * std::max(1, scale); }

// -------------------------------------------------------------------- core --

Canvas::Canvas(Image& image) : image_(image) { resetClip(); }

void Canvas::setClip(const Rect& clip) { clip_ = intersect(clip, gfx::rect(0, 0, image_.width(), image_.height())); }

void Canvas::resetClip() { clip_ = gfx::rect(0, 0, image_.width(), image_.height()); }

void Canvas::clear(Rgb color) { fillRect(clip_, color); }

void Canvas::pixel(int x, int y, Rgb color) {
    if (inClip(x, y)) image_.set(x, y, color);
}

void Canvas::pixel(int x, int y, Rgb color, double alpha) {
    if (!inClip(x, y) || alpha <= 0) return;
    image_.set(x, y, alpha >= 1 ? color : blend(image_.get(x, y), color, alpha));
}

Rgb Canvas::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= image_.width() || y >= image_.height()) return rgb(0, 0, 0);
    return image_.get(x, y);
}

void Canvas::span(int x0, int x1, int y, Rgb color) {
    if (y < clip_.y || y >= clip_.bottom()) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(x0, clip_.x);
    x1 = std::min(x1, clip_.right() - 1);
    if (x0 > x1) return;
    Rgb* row = image_.row(y);
    std::fill(row + x0, row + x1 + 1, color);
}

void Canvas::hline(int x0, int x1, int y, Rgb color) { span(x0, x1, y, color); }

void Canvas::vline(int x, int y0, int y1, Rgb color) {
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; ++y) pixel(x, y, color);
}

void Canvas::line(int x0, int y0, int x1, int y1, Rgb color) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixel(x0, y0, color);
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

void Canvas::lineAA(double x0, double y0, double x1, double y1, Rgb color) {
    bool steep = std::fabs(y1 - y0) > std::fabs(x1 - x0);
    if (steep) {
        std::swap(x0, y0);
        std::swap(x1, y1);
    }
    if (x0 > x1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    double dx = x1 - x0, dy = y1 - y0;
    double gradient = dx == 0 ? 1.0 : dy / dx;
    // Plots (x, y) with coverage c, undoing the steep swap.
    struct Plot {
        Canvas* canvas;
        bool steep;
        Rgb color;
        void operator()(int x, int y, double c) const {
            if (steep) canvas->pixel(y, x, color, c);
            else canvas->pixel(x, y, color, c);
        }
    } plot = {this, steep, color};

    int xStart = static_cast<int>(std::floor(x0 + 0.5));
    int xEnd = static_cast<int>(std::floor(x1 + 0.5));
    double y = y0 + gradient * (xStart - x0);
    for (int x = xStart; x <= xEnd; ++x) {
        int iy = static_cast<int>(std::floor(y));
        double f = fpart(y);
        plot(x, iy, 1 - f);
        plot(x, iy + 1, f);
        y += gradient;
    }
}

void Canvas::thickLine(double x0, double y0, double x1, double y1, double thickness, Rgb color) {
    if (thickness <= 1.0) {
        lineAA(x0, y0, x1, y1, color);
        return;
    }
    // Distance from each pixel to the segment, with a 1-pixel soft edge.
    double r = thickness / 2.0;
    int minX = static_cast<int>(std::floor(std::min(x0, x1) - r - 1)), maxX = static_cast<int>(std::ceil(std::max(x0, x1) + r + 1));
    int minY = static_cast<int>(std::floor(std::min(y0, y1) - r - 1)), maxY = static_cast<int>(std::ceil(std::max(y0, y1) + r + 1));
    minX = std::max(minX, clip_.x);
    minY = std::max(minY, clip_.y);
    maxX = std::min(maxX, clip_.right() - 1);
    maxY = std::min(maxY, clip_.bottom() - 1);
    double vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            double t = len2 > 0 ? ((x - x0) * vx + (y - y0) * vy) / len2 : 0;
            t = std::max(0.0, std::min(1.0, t));
            double px = x0 + t * vx - x, py = y0 + t * vy - y;
            double d = std::sqrt(px * px + py * py);
            double cover = r + 0.5 - d;
            if (cover > 0) pixel(x, y, color, std::min(1.0, cover));
        }
    }
}

void Canvas::dashedLine(int x0, int y0, int x1, int y1, Rgb color, int on, int off) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, step = 0, period = std::max(1, on + off);
    for (;;) {
        if (step % period < on) pixel(x0, y0, color);
        ++step;
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

void Canvas::polyline(const std::vector<Point>& points, Rgb color, bool antialiased) {
    for (size_t i = 1; i < points.size(); ++i) {
        if (antialiased) lineAA(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y, color);
        else line(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y, color);
    }
    if (points.size() == 1) pixel(points[0].x, points[0].y, color);
}

// ------------------------------------------------------------------ shapes --

void Canvas::rect(const Rect& r, Rgb color) {
    if (r.empty()) return;
    span(r.x, r.right() - 1, r.y, color);
    span(r.x, r.right() - 1, r.bottom() - 1, color);
    vline(r.x, r.y, r.bottom() - 1, color);
    vline(r.right() - 1, r.y, r.bottom() - 1, color);
}

void Canvas::fillRect(const Rect& r, Rgb color) {
    for (int y = r.y; y < r.bottom(); ++y) span(r.x, r.right() - 1, y, color);
}

void Canvas::fillRect(const Rect& r, Rgb color, double alpha) {
    Rect c = intersect(r, clip_);
    for (int y = c.y; y < c.bottom(); ++y)
        for (int x = c.x; x < c.right(); ++x) pixel(x, y, color, alpha);
}

void Canvas::roundRect(const Rect& r, int radius, Rgb color) {
    if (r.empty()) return;
    radius = std::max(0, std::min(radius, std::min(r.width, r.height) / 2));
    int x0 = r.x + radius, x1 = r.right() - 1 - radius, y0 = r.y + radius, y1 = r.bottom() - 1 - radius;
    span(x0, x1, r.y, color);
    span(x0, x1, r.bottom() - 1, color);
    vline(r.x, y0, y1, color);
    vline(r.right() - 1, y0, y1, color);
    // Midpoint circle quarters at the four corners.
    int x = radius, y = 0, err = 1 - radius;
    while (x >= y) {
        pixel(x1 + x, y1 + y, color); pixel(x1 + y, y1 + x, color);
        pixel(x0 - x, y1 + y, color); pixel(x0 - y, y1 + x, color);
        pixel(x1 + x, y0 - y, color); pixel(x1 + y, y0 - x, color);
        pixel(x0 - x, y0 - y, color); pixel(x0 - y, y0 - x, color);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

void Canvas::fillRoundRect(const Rect& r, int radius, Rgb color) {
    if (r.empty()) return;
    radius = std::max(0, std::min(radius, std::min(r.width, r.height) / 2));
    for (int y = r.y; y < r.bottom(); ++y) {
        int inset = 0;
        int dy = 0;
        if (y < r.y + radius) dy = r.y + radius - y;
        else if (y > r.bottom() - 1 - radius) dy = y - (r.bottom() - 1 - radius);
        if (dy > 0) inset = radius - static_cast<int>(std::floor(std::sqrt(static_cast<double>(radius * radius - dy * dy)) + 0.5));
        span(r.x + inset, r.right() - 1 - inset, y, color);
    }
}

void Canvas::circle(int cx, int cy, int radius, Rgb color) {
    int x = radius, y = 0, err = 1 - radius;
    while (x >= y) {
        pixel(cx + x, cy + y, color); pixel(cx + y, cy + x, color);
        pixel(cx - y, cy + x, color); pixel(cx - x, cy + y, color);
        pixel(cx - x, cy - y, color); pixel(cx - y, cy - x, color);
        pixel(cx + y, cy - x, color); pixel(cx + x, cy - y, color);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

void Canvas::fillCircle(int cx, int cy, int radius, Rgb color) {
    for (int dy = -radius; dy <= radius; ++dy) {
        int dx = static_cast<int>(std::floor(std::sqrt(static_cast<double>(radius * radius - dy * dy)) + 0.5));
        span(cx - dx, cx + dx, cy + dy, color);
    }
}

void Canvas::circleAA(double cx, double cy, double radius, Rgb color) {
    int x0 = static_cast<int>(std::floor(cx - radius - 1)), x1 = static_cast<int>(std::ceil(cx + radius + 1));
    int y0 = static_cast<int>(std::floor(cy - radius - 1)), y1 = static_cast<int>(std::ceil(cy + radius + 1));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            double d = std::fabs(std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - radius);
            if (d < 1) pixel(x, y, color, 1 - d);
        }
    }
}

void Canvas::ellipse(int cx, int cy, int rx, int ry, Rgb color) {
    if (rx <= 0 || ry <= 0) {
        line(cx - rx, cy - ry, cx + rx, cy + ry, color);
        return;
    }
    // Sampled by angle with enough steps to leave no gaps.
    int steps = std::max(16, static_cast<int>(2 * kPi * std::max(rx, ry)));
    int px = cx + rx, py = cy;
    for (int i = 1; i <= steps; ++i) {
        double a = 2 * kPi * i / steps;
        int x = cx + static_cast<int>(std::floor(rx * std::cos(a) + 0.5));
        int y = cy + static_cast<int>(std::floor(ry * std::sin(a) + 0.5));
        line(px, py, x, y, color);
        px = x;
        py = y;
    }
}

void Canvas::fillEllipse(int cx, int cy, int rx, int ry, Rgb color) {
    if (rx <= 0 || ry <= 0) return;
    for (int dy = -ry; dy <= ry; ++dy) {
        double t = 1.0 - static_cast<double>(dy * dy) / (static_cast<double>(ry) * ry);
        int dx = static_cast<int>(std::floor(rx * std::sqrt(std::max(0.0, t)) + 0.5));
        span(cx - dx, cx + dx, cy + dy, color);
    }
}

void Canvas::arc(double cx, double cy, double radius, double startDeg, double endDeg, Rgb color) {
    int x0 = static_cast<int>(std::floor(cx - radius - 1)), x1 = static_cast<int>(std::ceil(cx + radius + 1));
    int y0 = static_cast<int>(std::floor(cy - radius - 1)), y1 = static_cast<int>(std::ceil(cy + radius + 1));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            double dx = x - cx, dy = cy - y;  // y up for angles
            double d = std::fabs(std::sqrt(dx * dx + dy * dy) - radius);
            if (d >= 1) continue;
            if (angleIn(std::atan2(dy, dx) * 180 / kPi, startDeg, endDeg)) pixel(x, y, color, 1 - d);
        }
    }
}

void Canvas::pie(double cx, double cy, double radius, double startDeg, double endDeg, Rgb color) {
    int x0 = static_cast<int>(std::floor(cx - radius)), x1 = static_cast<int>(std::ceil(cx + radius));
    int y0 = static_cast<int>(std::floor(cy - radius)), y1 = static_cast<int>(std::ceil(cy + radius));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            double dx = x - cx, dy = cy - y;
            double d = std::sqrt(dx * dx + dy * dy);
            if (d > radius + 0.5) continue;
            if (d < 0.5 || angleIn(std::atan2(dy, dx) * 180 / kPi, startDeg, endDeg))
                pixel(x, y, color, std::min(1.0, radius + 0.5 - d));
        }
    }
}

void Canvas::triangle(Point a, Point b, Point c, Rgb color) {
    line(a.x, a.y, b.x, b.y, color);
    line(b.x, b.y, c.x, c.y, color);
    line(c.x, c.y, a.x, a.y, color);
}

void Canvas::fillTriangle(Point a, Point b, Point c, Rgb color) {
    std::vector<Point> p;
    p.push_back(a);
    p.push_back(b);
    p.push_back(c);
    fillPolygon(p, color);
}

void Canvas::polygon(const std::vector<Point>& points, Rgb color) {
    if (points.empty()) return;
    polyline(points, color);
    line(points.back().x, points.back().y, points.front().x, points.front().y, color);
}

void Canvas::fillPolygon(const std::vector<Point>& points, Rgb color) {
    if (points.size() < 3) {
        polyline(points, color);
        return;
    }
    int minY = points[0].y, maxY = points[0].y;
    for (size_t i = 1; i < points.size(); ++i) {
        minY = std::min(minY, points[i].y);
        maxY = std::max(maxY, points[i].y);
    }
    minY = std::max(minY, clip_.y);
    maxY = std::min(maxY, clip_.bottom() - 1);
    std::vector<double> xs;
    for (int y = minY; y <= maxY; ++y) {
        // Sample at the pixel centre: an edge counts when it crosses y + 0.5.
        double sy = y + 0.5;
        xs.clear();
        for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            double y0 = points[j].y, y1 = points[i].y;
            if ((y0 <= sy && y1 > sy) || (y1 <= sy && y0 > sy)) {
                double t = (sy - y0) / (y1 - y0);
                xs.push_back(points[j].x + t * (points[i].x - points[j].x));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int x0 = static_cast<int>(std::ceil(xs[k] - 0.5)), x1 = static_cast<int>(std::floor(xs[k + 1] - 0.5));
            if (x1 >= x0) span(x0, x1, y, color);
        }
    }
    polygon(points, color);  // crisp outline so thin polygons stay visible
}

// ------------------------------------------------------------------- fills --

void Canvas::gradient(const Rect& r, Rgb from, Rgb to, bool vertical) {
    int n = vertical ? r.height : r.width;
    for (int i = 0; i < n; ++i) {
        Rgb c = lerp(from, to, n > 1 ? static_cast<double>(i) / (n - 1) : 0);
        if (vertical) span(r.x, r.right() - 1, r.y + i, c);
        else vline(r.x + i, r.y, r.bottom() - 1, c);
    }
}

void Canvas::gradient(const Rect& r, Colormap map, bool vertical) {
    int n = vertical ? r.height : r.width;
    for (int i = 0; i < n; ++i) {
        Rgb c = colormap(map, n > 1 ? static_cast<double>(i) / (n - 1) : 0);
        if (vertical) span(r.x, r.right() - 1, r.y + i, c);
        else vline(r.x + i, r.y, r.bottom() - 1, c);
    }
}

// ------------------------------------------------------------------ images --

void Canvas::drawImage(const Image& source, int x, int y) {
    Rect target = intersect(gfx::rect(x, y, source.width(), source.height()), clip_);
    for (int ty = target.y; ty < target.bottom(); ++ty) {
        const Rgb* src = source.row(ty - y) + (target.x - x);
        std::copy(src, src + target.width, image_.row(ty) + target.x);
    }
}

void Canvas::drawImage(const Image& source, int x, int y, double alpha) {
    Rect target = intersect(gfx::rect(x, y, source.width(), source.height()), clip_);
    for (int ty = target.y; ty < target.bottom(); ++ty)
        for (int tx = target.x; tx < target.right(); ++tx) pixel(tx, ty, source.get(tx - x, ty - y), alpha);
}

void Canvas::drawImage(const Image& source, const Rect& target, bool smooth) {
    if (source.empty() || target.empty()) return;
    Rect area = intersect(target, clip_);
    double sx = static_cast<double>(source.width()) / target.width;
    double sy = static_cast<double>(source.height()) / target.height;
    for (int y = area.y; y < area.bottom(); ++y) {
        double fy = (y - target.y + 0.5) * sy - 0.5;
        for (int x = area.x; x < area.right(); ++x) {
            double fx = (x - target.x + 0.5) * sx - 0.5;
            if (!smooth) {
                int ix = std::min(source.width() - 1, std::max(0, static_cast<int>(fx + 0.5)));
                int iy = std::min(source.height() - 1, std::max(0, static_cast<int>(fy + 0.5)));
                image_.set(x, y, source.get(ix, iy));
                continue;
            }
            int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
            double tx = fx - x0, ty = fy - y0;
            int x1 = std::min(source.width() - 1, std::max(0, x0 + 1)), y1 = std::min(source.height() - 1, std::max(0, y0 + 1));
            x0 = std::min(source.width() - 1, std::max(0, x0));
            y0 = std::min(source.height() - 1, std::max(0, y0));
            Rgb top = lerp(source.get(x0, y0), source.get(x1, y0), tx);
            Rgb bottom = lerp(source.get(x0, y1), source.get(x1, y1), tx);
            image_.set(x, y, lerp(top, bottom, ty));
        }
    }
}

void Canvas::drawSprite(const Image& source, int x, int y, Rgb transparent) {
    Rect target = intersect(gfx::rect(x, y, source.width(), source.height()), clip_);
    for (int ty = target.y; ty < target.bottom(); ++ty) {
        for (int tx = target.x; tx < target.right(); ++tx) {
            Rgb c = source.get(tx - x, ty - y);
            if (c != transparent) image_.set(tx, ty, c);
        }
    }
}

// -------------------------------------------------------------------- text --

int Canvas::text(int x, int y, const std::string& text, Rgb color, Font font, int scale) {
    scale = std::max(1, scale);
    for (size_t i = 0; i < text.size(); ++i) {
        if (font == Font::Font5x7) {
            const unsigned char* g = fontdata::kFont5x7[glyphIndex5x7(text[i])];
            for (int col = 0; col < 5; ++col)
                for (int row = 0; row < 7; ++row)
                    if (g[col] & (1 << row)) fillRect(gfx::rect(x + col * scale, y + row * scale, scale, scale), color);
            x += 6 * scale;
        } else {
            const unsigned char* g = fontdata::kFont3x5[glyphIndex3x5(text[i])];
            for (int row = 0; row < 5; ++row)
                for (int col = 0; col < 3; ++col)
                    if (g[row] & (4 >> col)) fillRect(gfx::rect(x + col * scale, y + row * scale, scale, scale), color);
            x += 4 * scale;
        }
    }
    return x;
}

void Canvas::text(const Rect& box, const std::string& s, Rgb color, Align align, Font font, int scale) {
    int w = textWidth(s, font, scale), h = textHeight(font, scale);
    int x = box.x;
    if (align == Align::Center) x = box.x + (box.width - w) / 2;
    else if (align == Align::Right) x = box.right() - w;
    Rect saved = clip_;
    setClip(intersect(box, saved));
    text(x, box.y + (box.height - h) / 2, s, color, font, scale);
    clip_ = saved;
}

// ------------------------------------------------------------------ scroll --

void Canvas::scroll(const Rect& area, int dx, int dy, Rgb fill) {
    Rect r = intersect(area, clip_);
    if (r.empty()) return;
    if (std::abs(dx) >= r.width || std::abs(dy) >= r.height) {
        fillRect(r, fill);
        return;
    }
    // Copy rows in the order that does not overwrite unread source rows.
    if (dy > 0) {
        for (int y = r.bottom() - 1; y >= r.y + dy; --y) std::copy(image_.row(y - dy) + r.x, image_.row(y - dy) + r.right(), image_.row(y) + r.x);
        fillRect(gfx::rect(r.x, r.y, r.width, dy), fill);
    } else if (dy < 0) {
        for (int y = r.y; y < r.bottom() + dy; ++y) std::copy(image_.row(y - dy) + r.x, image_.row(y - dy) + r.right(), image_.row(y) + r.x);
        fillRect(gfx::rect(r.x, r.bottom() + dy, r.width, -dy), fill);
    }
    if (dx != 0) {
        for (int y = r.y; y < r.bottom(); ++y) {
            Rgb* row = image_.row(y);
            if (dx > 0) {
                std::copy_backward(row + r.x, row + r.right() - dx, row + r.right());
                std::fill(row + r.x, row + r.x + dx, fill);
            } else {
                std::copy(row + r.x - dx, row + r.right(), row + r.x);
                std::fill(row + r.right() + dx, row + r.right(), fill);
            }
        }
    }
}

}  // namespace gfx
