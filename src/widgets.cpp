// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   src/widgets.cpp
//
//  What this file does
//  -------------------
//  Implementation of widgets.h: the App (layers, focus, mouse capture, key
//  bindings, main loop) and every widget.
//
//  Contributing
//  ------------
//  * A widget draws only inside its Painter (already clipped to bounds) and
//    takes its colors from the Theme, so every theme works everywhere.
//  * Mouse coordinates in handle() are local (App subtracts bounds.x/y).
//  * Keep keyboard and mouse equivalent: everything clickable should also
//    be reachable with Tab, arrows, Enter and Space.
// ============================================================================
#include "graphicx/widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include <poll.h>
#include <time.h>

namespace gfx {
namespace ui {

using terminal::EventType;
using terminal::MouseAction;
using terminal::MouseButton;

namespace {

double nan() { return std::numeric_limits<double>::quiet_NaN(); }

std::string fmt(const char* format, double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), format, v);
    return buf;
}

bool isPress(const Event& e) { return e.type == EventType::Mouse && e.action == MouseAction::Press && e.button == MouseButton::Left; }
bool isWheel(const Event& e, int& direction) {
    if (e.type != EventType::Mouse) return false;
    if (e.button == MouseButton::WheelUp) direction = 1;
    else if (e.button == MouseButton::WheelDown) direction = -1;
    else return false;
    return true;
}
bool isActivate(const Event& e) { return e.isKey(terminal::KeyEnter) || e.isChar(' '); }

double monotonicSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Text color of a widget: dim when disabled.
Rgb textColor(const Widget& w, const Theme& t) { return w.enabled ? t.text : t.dim; }

}  // namespace

// ------------------------------------------------------------------ Widget --

Widget::Widget() : bounds(rect(0, 0, 0, 0)), visible(true), enabled(true), app_(0) {}
Widget::~Widget() {
    if (app_) app_->remove(*this);
}

// --------------------------------------------------------------------- App --

App::App(const Theme& theme)
    : autoFocus(true),
      theme_(theme),
      focus_(0),
      capture_(0),
      needLayout_(true),
      quit_(false),
      cursorWanted_(false),
      cursorX_(0),
      cursorY_(0) {}

void App::add(Widget& w) {
    if (std::find(widgets_.begin(), widgets_.end(), &w) != widgets_.end()) return;
    widgets_.push_back(&w);
    w.app_ = this;
    if (autoFocus && !focus_ && w.focusable()) focus_ = &w;
}

void App::remove(Widget& w) {
    widgets_.erase(std::remove(widgets_.begin(), widgets_.end(), &w), widgets_.end());
    for (size_t i = 0; i < popups_.size(); ++i) {
        if (popups_[i] == &w) {
            popups_.erase(popups_.begin() + static_cast<long>(i));
            modal_.erase(modal_.begin() + static_cast<long>(i));
            break;
        }
    }
    if (focus_ == &w) focus_ = 0;
    if (capture_ == &w) capture_ = 0;
    w.app_ = 0;
}

void App::openPopup(Widget& w, bool modal) {
    closePopup(w);
    popups_.push_back(&w);
    modal_.push_back(modal);
    w.app_ = this;
    focus_ = &w;
}

void App::closePopup(Widget& w) {
    for (size_t i = 0; i < popups_.size(); ++i) {
        if (popups_[i] == &w) {
            popups_.erase(popups_.begin() + static_cast<long>(i));
            modal_.erase(modal_.begin() + static_cast<long>(i));
            if (focus_ == &w) {
                focus_ = popups_.empty() ? 0 : popups_.back();
                if (!focus_ && autoFocus) {
                    std::vector<Widget*> f = focusables();
                    if (!f.empty()) focus_ = f[0];
                }
            }
            if (capture_ == &w) capture_ = 0;
            return;
        }
    }
}

void App::focus(Widget& w) {
    if (w.focusable()) focus_ = &w;
}

std::vector<Widget*> App::focusables() const {
    std::vector<Widget*> out;
    for (size_t i = 0; i < widgets_.size(); ++i)
        if (widgets_[i]->visible && widgets_[i]->enabled && widgets_[i]->focusable() && !widgets_[i]->bounds.empty())
            out.push_back(widgets_[i]);
    return out;
}

void App::focusNext(bool backwards) {
    std::vector<Widget*> f = focusables();
    if (f.empty()) return;
    std::vector<Widget*>::iterator it = std::find(f.begin(), f.end(), focus_);
    long i = it == f.end() ? -1 : it - f.begin();
    long n = static_cast<long>(f.size());
    i = backwards ? (i <= 0 ? n - 1 : i - 1) : (i + 1) % n;
    focus_ = f[static_cast<size_t>(i)];
}

void App::bindKey(int key, std::function<void()> action, bool ctrl, bool alt) {
    Binding b;
    b.key = key;
    b.ctrl = ctrl;
    b.alt = alt;
    b.action = action;
    bindings_.push_back(b);
}

Widget* App::widgetAt(int x, int y) {
    for (size_t i = widgets_.size(); i-- > 0;) {
        Widget* w = widgets_[i];
        if (w->visible && w->bounds.contains(x, y)) return w;
    }
    return 0;
}

bool App::dispatch(const Event& e) {
    if (e.type == EventType::Resize) {
        needLayout_ = true;
        display_.invalidate();
        return true;
    }
    if (e.type == EventType::Mouse) {
        // Drag / release go to the widget that got the press.
        Widget* target = 0;
        if ((e.action == MouseAction::Drag || e.action == MouseAction::Release) && capture_) {
            target = capture_;
        } else if (!popups_.empty()) {
            Widget* top = popups_.back();
            if (top->bounds.contains(e.x, e.y)) {
                target = top;
            } else if (e.action == MouseAction::Press) {
                if (!modal_.back()) closePopup(*top);  // click outside closes a menu
                return true;
            } else {
                return true;
            }
        } else {
            target = widgetAt(e.x, e.y);
        }
        if (e.action == MouseAction::Release) capture_ = 0;
        if (!target || !target->enabled) return false;
        if (e.action == MouseAction::Press && e.button != MouseButton::WheelUp && e.button != MouseButton::WheelDown) {
            capture_ = target;
            if (target->focusable()) focus_ = target;
        }
        Event local = e;
        local.x -= target->bounds.x;
        local.y -= target->bounds.y;
        if (target->handle(local)) return true;
        if (onUnhandled) onUnhandled(e);
        return false;
    }
    if (e.type != EventType::Key) return false;

    Widget* target = !popups_.empty() ? popups_.back() : focus_;
    if (target && target->visible && target->enabled && target->handle(e)) return true;
    if (popups_.empty()) {
        if (e.isKey(terminal::KeyTab) && !e.ctrl && !e.alt) {
            focusNext(false);
            return true;
        }
        if (e.isKey(terminal::KeyBackTab)) {
            focusNext(true);
            return true;
        }
    } else if (e.isKey(terminal::KeyEscape) && !modal_.back()) {
        closePopup(*popups_.back());
        return true;
    }
    for (size_t i = 0; i < bindings_.size(); ++i) {
        const Binding& b = bindings_[i];
        if (b.key == e.key && b.ctrl == e.ctrl && b.alt == e.alt) {
            b.action();
            return true;
        }
    }
    if (onUnhandled) onUnhandled(e);
    return false;
}

void App::frame(std::string& out) {
    terminal::Size s = terminal::size();
    if (s.columns != surface_.width() || s.rows != surface_.height()) {
        surface_.resize(s.columns, s.rows, theme_.background);
        needLayout_ = true;
    }
    if (needLayout_) {
        needLayout_ = false;
        if (onLayout) onLayout(rect(0, 0, s.columns, s.rows));
    }
    surface_.clear(theme_.background, theme_.text);
    Painter screen(surface_);
    cursorWanted_ = false;
    if (onDrawBackground) onDrawBackground(screen);
    for (size_t i = 0; i < widgets_.size(); ++i) {
        Widget* w = widgets_[i];
        if (!w->visible || w->bounds.empty()) continue;
        Painter p = screen.sub(w->bounds);
        w->draw(p, theme_, w == focus_ && popups_.empty());
    }
    for (size_t i = 0; i < popups_.size(); ++i) {
        Widget* w = popups_[i];
        if (modal_[i]) screen.darken(screen.bounds(), 0.55);
        screen.shadow(w->bounds);
        Painter p = screen.sub(w->bounds);
        w->draw(p, theme_, w == focus_);
    }
    if (onDrawOverlay) onDrawOverlay(screen);
    if (cursorWanted_) display_.showCursor(cursorX_, cursorY_);
    else display_.hideCursor();
    display_.render(surface_, out);
}

void App::run(terminal::Session& session, int fps, std::function<void()> onTick) {
    double period = 1.0 / std::max(1, fps), next = monotonicSeconds();
    std::string out;
    while (!quit_ && !terminal::Session::interrupted()) {
        double now = monotonicSeconds();
        if (now >= next) {
            next = std::max(next + period, now);
            if (onTick) onTick();
            out.clear();
            frame(out);
            session.write(out);
        }
        int wait = static_cast<int>((next - monotonicSeconds()) * 1000);
        Event e;
        if (session.readEvent(e, std::max(0, wait))) {
            dispatch(e);
            // Drain queued events quickly (drags, wheel bursts).
            while (session.readEvent(e, 0)) dispatch(e);
        }
    }
}

// ---------------------------------------------------------------- displays --

Label::Label(const std::string& t, Align a) : text(t), align(a), wrap(false), useColor(false), color(rgb(255, 255, 255)), attrs(Plain) {}

void Label::draw(Painter& p, const Theme& t, bool) {
    Rgb c = useColor ? color : textColor(*this, t);
    if (wrap) p.paragraph(p.bounds(), text, c, attrs);
    else p.text(rect(0, 0, p.width(), 1), text, c, align, attrs);
}

Panel::Panel(const std::string& t, Border b) : title(t), border(b), filled(true) {}

void Panel::draw(Painter& p, const Theme& t, bool) {
    if (filled) p.fill(t.panel);
    std::string head = title.empty() ? title : " " + title + " ";
    p.box(p.bounds(), t.border, border, head);
    if (!title.empty() && p.width() > 4) p.text(rect(2, 0, p.width() - 4, 1), head, t.accent, Align::Left, Bold);
}

void Separator::draw(Painter& p, const Theme& t, bool) {
    if (vertical) p.vline(0, 0, p.height(), t.border);
    else p.hline(0, 0, p.width(), t.border);
}

void StatusBar::addLeft(const std::string& text) {
    Segment s = {text, rgb(0, 0, 0), rgb(0, 0, 0), false};
    left.push_back(s);
}
void StatusBar::addLeft(const std::string& text, Rgb fg, Rgb bg) {
    Segment s = {text, fg, bg, true};
    left.push_back(s);
}
void StatusBar::addRight(const std::string& text) {
    Segment s = {text, rgb(0, 0, 0), rgb(0, 0, 0), false};
    right.push_back(s);
}
void StatusBar::addRight(const std::string& text, Rgb fg, Rgb bg) {
    Segment s = {text, fg, bg, true};
    right.push_back(s);
}

void StatusBar::draw(Painter& p, const Theme& t, bool) {
    p.fill(rect(0, 0, p.width(), 1), t.panel);
    int x = 0;
    for (size_t i = 0; i < left.size(); ++i) {
        const Segment& s = left[i];
        std::string text = " " + s.text + " ";
        x = p.text(x, 0, text, s.useColors ? s.fg : t.text, s.useColors ? s.bg : t.panel) ;
    }
    int rx = p.width();
    for (size_t i = right.size(); i-- > 0;) {
        const Segment& s = right[i];
        std::string text = " " + s.text + " ";
        rx -= columns(text);
        if (rx < x) break;
        p.text(rx, 0, text, s.useColors ? s.fg : t.dim, s.useColors ? s.bg : t.panel);
    }
}

Led::Led(const std::string& l) : label(l), on(false), useColor(false), color(rgb(0, 255, 0)) {}

void Led::draw(Painter& p, const Theme& t, bool) {
    Rgb c = on ? (useColor ? color : t.good) : scale(t.dim, 0.6);
    p.put(0, 0, on ? 0x25CF : 0x25CB, c);  // ● / ○
    if (!label.empty()) p.text(2, 0, label, on ? t.text : t.dim);
}

void Spinner::draw(Painter& p, const Theme& t, bool) {
    static const std::uint32_t frames[10] = {0x280B, 0x2819, 0x2839, 0x2838, 0x283C, 0x2834, 0x2826, 0x2827, 0x2807, 0x280F};
    if (active) ++step_;
    p.put(0, 0, active ? frames[(step_ / 2) % 10] : 0x2800, t.accent);
    if (!label.empty()) p.text(2, 0, label, t.text);
}

void ProgressBar::draw(Painter& p, const Theme& t, bool) {
    int x = 0;
    if (!label.empty()) x = p.text(0, 0, label + " ", t.text);
    std::string pct = showPercent ? fmt(" %3.0f%%", std::max(0.0, std::min(1.0, value)) * 100) : std::string();
    int w = p.width() - x - columns(pct);
    p.bar(rect(x, 0, std::max(0, w), 1), value, t.accent, scale(t.panel, 1.4));
    if (showPercent) p.text(p.width() - columns(pct), 0, pct, t.text);
}

Meter::Meter()
    : min(0), max(1), value(0), peak(nan()), warnAt(nan()), badAt(nan()), segmented(true) {}

void Meter::update(double v, double decay) {
    value = v;
    if (std::isnan(peak) || v > peak) peak = v;
    else peak = std::max(v, peak - decay);
}

void Meter::draw(Painter& p, const Theme& t, bool) {
    int x = 0;
    if (!label.empty()) x = p.text(0, 0, label, t.dim, Bold) + 1;
    int vw = valueText.empty() ? 0 : columns(valueText) + 1;
    int w = p.width() - x - vw;
    if (w <= 0) return;
    double range = max - min;
    if (range <= 0) range = 1;
    double f = (value - min) / range;
    if (!(f > 0)) f = 0;
    f = std::min(1.0, f);
    Rgb empty = gfx::scale(t.panel, 1.6);
    for (int i = 0; i < w; ++i) {
        double cellValue = min + range * (i + 0.5) / w;
        Rgb c = t.good;
        if (!std::isnan(badAt) && cellValue >= badAt) c = t.bad;
        else if (!std::isnan(warnAt) && cellValue >= warnAt) c = t.warn;
        double fill = f * w - i;  // how much of this cell is lit
        if (segmented) {
            p.put(x + i, 0, fill >= 0.5 ? 0x25AE : 0x25AF, fill >= 0.5 ? c : empty);  // ▮ / ▯
        } else {
            int k = fill >= 1 ? 8 : (fill <= 0 ? 0 : static_cast<int>(fill * 8 + 0.5));
            p.put(x + i, 0, k == 0 ? ' ' : (k == 8 ? 0x2588 : 0x2590 - static_cast<std::uint32_t>(k)), c, empty);
        }
    }
    if (!std::isnan(peak)) {
        double pf = (peak - min) / range;
        int px = static_cast<int>(std::max(0.0, std::min(1.0, pf)) * w - 0.5);
        if (px >= 0 && px < w) p.put(x + px, 0, 0x2503, t.text);  // ┃
    }
    if (vw > 0) p.text(p.width() - vw + 1, 0, valueText, t.accent, Bold);
    if (p.height() >= 2) {
        int used = -1;  // last column taken by a label: skip labels that would overlap
        for (size_t i = 0; i < scale.size(); ++i) {
            double sf = (scale[i].first - min) / range;
            int sx = x + static_cast<int>(std::max(0.0, std::min(1.0, sf)) * (w - 1) + 0.5);
            int lw = columns(scale[i].second);
            int lx = std::min(std::max(x, sx - lw / 2), x + w - lw);
            if (lx <= used) continue;
            p.text(lx, 1, scale[i].second, t.dim);
            used = lx + lw;
        }
    }
}

void Sparkline::push(double v, size_t keep) {
    values.push_back(v);
    if (values.size() > keep) values.erase(values.begin(), values.begin() + static_cast<long>(values.size() - keep));
}

void Sparkline::draw(Painter& p, const Theme& t, bool) {
    int w = p.width(), h = std::max(1, p.height());
    if (values.empty() || w <= 0) return;
    size_t first = values.size() > static_cast<size_t>(w) ? values.size() - w : 0;
    double lo = min, hi = max;
    if (lo == hi) {
        lo = hi = values[first];
        for (size_t i = first; i < values.size(); ++i) {
            lo = std::min(lo, values[i]);
            hi = std::max(hi, values[i]);
        }
        if (hi == lo) hi = lo + 1;
    }
    int x = w - static_cast<int>(values.size() - first);
    for (size_t i = first; i < values.size(); ++i, ++x) {
        double f = (values[i] - lo) / (hi - lo);
        p.columnBar(rect(x, 0, 1, h), f, useColor ? color : t.accent, t.background);
    }
}

void BarChart::draw(Painter& p, const Theme& t, bool) {
    if (bars.empty()) return;
    double lo = min, hi = max;
    if (lo == hi) {
        lo = 0;
        hi = bars[0].value;
        for (size_t i = 0; i < bars.size(); ++i) hi = std::max(hi, bars[i].value);
        if (hi <= lo) hi = lo + 1;
    }
    int h = p.height() - 1;  // last row: labels
    int x = 0;
    for (size_t i = 0; i < bars.size() && x < p.width(); ++i) {
        double f = (bars[i].value - lo) / (hi - lo);
        p.columnBar(rect(x, 0, barWidth, h), f, t.accent, t.background);
        p.text(rect(x, h, barWidth + gap, 1), bars[i].label, t.dim, Align::Left);
        x += barWidth + gap;
    }
}

// -------------------------------------------------------------------- Plot --

Plot::Plot()
    : xMin(0),
      xMax(1),
      yMin(0),
      yMax(0),
      showAxes(true),
      showGrid(true),
      xTicks(4),
      yTicks(4),
      maxDecimation(true) {}

Rect Plot::plotArea() const {
    if (!showAxes) return rect(0, 0, bounds.width, bounds.height);
    int left = 7;  // y labels
    return rect(left, 0, std::max(0, bounds.width - left), std::max(0, bounds.height - 1));
}

double Plot::xAt(int column) const {
    Rect a = plotArea();
    if (a.width <= 1) return xMin;
    return xMin + (xMax - xMin) * (column - a.x + 0.5) / a.width;
}

int Plot::columnOf(double x) const {
    Rect a = plotArea();
    if (xMax == xMin) return a.x;
    return a.x + static_cast<int>(std::floor((x - xMin) / (xMax - xMin) * a.width));
}

void Plot::draw(Painter& p, const Theme& t, bool) {
    Rect a = plotArea();
    if (a.width <= 0 || a.height <= 0) return;

    // y range
    double lo = yMin, hi = yMax;
    if (lo == hi) {
        bool any = false;
        for (size_t s = 0; s < series.size(); ++s)
            for (size_t i = 0; i < series[s].y.size(); ++i) {
                double v = series[s].y[i];
                if (std::isnan(v)) continue;
                if (!any) lo = hi = v;
                lo = std::min(lo, v);
                hi = std::max(hi, v);
                any = true;
            }
        if (hi == lo) hi = lo + 1;
    }

    // Shaded bands (background), then grid.
    for (size_t b = 0; b < bands.size(); ++b) {
        int c0 = std::max(a.x, columnOf(std::min(bands[b].x0, bands[b].x1)));
        int c1 = std::min(a.right() - 1, columnOf(std::max(bands[b].x0, bands[b].x1)));
        if (c1 >= c0) p.tint(rect(c0, a.y, c1 - c0 + 1, a.height), bands[b].color);
    }
    Rgb grid = lerp(t.background, t.dim, 0.35);
    if (showGrid) {
        for (int i = 1; i < yTicks; ++i) {
            int y = a.y + a.height * i / yTicks;
            for (int x = a.x; x < a.right(); ++x) p.put(x, y, 0x2508, grid);  // ┈
        }
        for (int i = 1; i < xTicks; ++i) {
            int x = a.x + a.width * i / xTicks;
            for (int y = a.y; y < a.bottom(); ++y) p.put(x, y, 0x250A, grid);  // ┊
        }
    }

    // Series as braille dots.
    DotCanvas dots(a.width, a.height);
    int dw = dots.dotWidth(), dh = dots.dotHeight();
    for (size_t s = 0; s < series.size(); ++s) {
        const Series& se = series[s];
        if (se.y.empty()) continue;
        Rgb color = se.useColor ? se.color : t.accent;
        int prevY = -1;
        size_t n = se.y.size();
        for (int dx = 0; dx < dw; ++dx) {
            // Points covered by this dot column.
            size_t i0 = static_cast<size_t>(static_cast<double>(dx) * n / dw);
            size_t i1 = std::max(i0 + 1, static_cast<size_t>(static_cast<double>(dx + 1) * n / dw));
            i1 = std::min(i1, n);
            double v = se.y[std::min(i0, n - 1)];
            if (maxDecimation)
                for (size_t i = i0; i < i1; ++i)
                    if (!std::isnan(se.y[i]) && (std::isnan(v) || se.y[i] > v)) v = se.y[i];
            if (std::isnan(v)) {
                prevY = -1;
                continue;
            }
            double f = (v - lo) / (hi - lo);
            f = std::max(0.0, std::min(1.0, f));
            int y = dh - 1 - static_cast<int>(f * (dh - 1) + 0.5);
            if (se.fill) dots.column(dx, y, dh - 1, lerp(t.background, color, 0.35));
            if (prevY >= 0) dots.line(dx - 1, prevY, dx, y, color);
            else dots.set(dx, y, color);
            prevY = y;
        }
    }
    Painter area = p.sub(a);
    dots.paint(area);

    // Markers on top.
    for (size_t m = 0; m < markers.size(); ++m) {
        int x = columnOf(markers[m].x);
        if (x < a.x || x >= a.right()) continue;
        for (int y = a.y; y < a.bottom(); ++y) {
            Cell* c = p.cell(x, y);
            if (c && c->ch == 0x20) p.put(x, y, 0x2502, markers[m].color);  // │ where empty
            else if (c) c->bg = lerp(c->bg, markers[m].color, 0.35);
        }
        if (!markers[m].label.empty()) {
            int lw = columns(markers[m].label);
            int lx = std::min(std::max(a.x, x - lw / 2), a.right() - lw);
            p.text(lx, a.y, markers[m].label, t.accentText, markers[m].color, Bold);
        }
    }

    if (showAxes) {
        for (int i = 0; i <= yTicks; ++i) {
            int y = a.y + (a.height - 1) * i / std::max(1, yTicks);
            double v = hi - (hi - lo) * i / std::max(1, yTicks);
            std::string label = formatY ? formatY(v) : fmt("%.4g", v);
            p.text(rect(0, y, a.x - 1, 1), label, t.dim, Align::Right);
        }
        for (int i = 0; i <= xTicks; ++i) {
            double v = xMin + (xMax - xMin) * i / std::max(1, xTicks);
            std::string label = formatX ? formatX(v) : fmt("%.4g", v);
            int lw = columns(label);
            int x = a.x + (a.width - 1) * i / std::max(1, xTicks) - lw / 2;
            x = std::max(a.x, std::min(x, a.right() - lw));
            p.text(x, a.bottom(), label, t.dim);
        }
    }
}

bool Plot::handle(const Event& e) {
    if (e.type != EventType::Mouse || !onMouse) return false;
    Rect a = plotArea();
    double x = xAt(e.x);
    double f = a.height > 0 ? 1.0 - static_cast<double>(e.y - a.y) / a.height : 0;
    double y = yMin + (yMax - yMin) * f;
    onMouse(x, y, e);
    return true;
}

// --------------------------------------------------------------- Waterfall --

Waterfall::Waterfall() : low(-110), high(-30), useThemeMap(true), map(Colormap::Classic), tableMap_(Colormap::Classic), hasTable_(false) {}

void Waterfall::ensureSize() {
    int w = std::max(1, bounds.width), h = std::max(1, bounds.height * 2);
    if (history_.width() == w && history_.height() == h) return;
    Image next(w, h, rgb(0, 0, 0));
    // Keep what fits of the old history (stretched horizontally).
    if (!history_.empty()) {
        Canvas c(next);
        Image old = history_;
        c.drawImage(old, rect(0, 0, w, std::min(h, old.height())), false);
    }
    history_ = next;
}

void Waterfall::clear() { history_ = Image(); }

void Waterfall::addRow(const std::vector<float>& values) {
    ensureSize();
    if (values.empty()) return;
    Colormap m = map;
    if (!hasTable_ || tableMap_ != m) {
        table_ = colormapTable(m, 256);
        tableMap_ = m;
        hasTable_ = true;
    }
    Canvas c(history_);
    c.scroll(rect(0, 0, history_.width(), history_.height()), 0, 1, rgb(0, 0, 0));
    int w = history_.width();
    size_t n = values.size();
    Rgb* row = history_.row(0);
    double span = high - low;
    if (span == 0) span = 1;
    for (int x = 0; x < w; ++x) {
        size_t i0 = static_cast<size_t>(static_cast<double>(x) * n / w);
        size_t i1 = std::min(n, std::max(i0 + 1, static_cast<size_t>(static_cast<double>(x + 1) * n / w)));
        float v = values[std::min(i0, n - 1)];
        for (size_t i = i0; i < i1; ++i) v = std::max(v, values[i]);
        double f = (v - low) / span;
        int k = static_cast<int>(std::max(0.0, std::min(1.0, f)) * 255 + 0.5);
        row[x] = table_[static_cast<size_t>(k)];
    }
}

void Waterfall::draw(Painter& p, const Theme& t, bool) {
    if (useThemeMap && (!hasTable_ || tableMap_ != t.colormap)) {
        map = t.colormap;  // next rows use the theme's map
    }
    ensureSize();
    p.image(p.bounds(), history_, PixelMode::HalfBlocks, Filter::Nearest);
    for (size_t m = 0; m < markers.size(); ++m) {
        int x = static_cast<int>(markers[m].x * p.width());
        if (x < 0 || x >= p.width()) continue;
        for (int y = 0; y < p.height(); ++y) {
            Cell* c = p.cell(x, y);
            if (!c) continue;
            c->fg = lerp(c->fg, markers[m].color, 0.6);
            c->bg = lerp(c->bg, markers[m].color, 0.6);
        }
    }
}

bool Waterfall::handle(const Event& e) {
    if (e.type != EventType::Mouse || !onMouse || bounds.width <= 0) return false;
    onMouse((e.x + 0.5) / bounds.width, e);
    return true;
}

// ----------------------------------------------------------------- BigText --

BigText::BigText(const std::string& t)
    : text(t), font(Font::Font3x5), mode(PixelMode::Quadrants), align(Align::Left), useColor(false), color(rgb(255, 255, 255)) {}

void BigText::measure(int& width, int& height) const {
    int across, down;
    pixelsPerCell(mode, across, down);
    int pw = textWidth(text, font, 1) + 1, ph = textHeight(font, 1) + 1;
    width = (pw + across - 1) / across;
    height = (ph + down - 1) / down;
}

void BigText::draw(Painter& p, const Theme& t, bool) {
    int w, h;
    measure(w, h);
    int across, down;
    pixelsPerCell(mode, across, down);
    Rgb bg = p.cell(0, 0) ? p.cell(0, 0)->bg : t.background;
    Image img(w * across, h * down, bg);
    Canvas c(img);
    c.text(0, 0, text, useColor ? color : t.accent, font, 1);
    int x = 0;
    if (align == Align::Center) x = (p.width() - w) / 2;
    else if (align == Align::Right) x = p.width() - w;
    p.image(rect(x, 0, w, h), img, mode, Filter::Nearest);
}

void ImageView::draw(Painter& p, const Theme&, bool) {
    if (image.empty()) return;
    Rect r = p.bounds();
    if (keepAspect) {
        int across, down;
        pixelsPerCell(mode, across, down);
        double s = std::min(static_cast<double>(r.width * across) / image.width(), static_cast<double>(r.height * down) / image.height());
        int w = std::max(1, static_cast<int>(image.width() * s / across)), h = std::max(1, static_cast<int>(image.height() * s / down));
        r = centered(r, w, h);
    }
    p.image(r, image, mode, filter);
}

// ---------------------------------------------------------------- controls --

Button::Button(const std::string& l) : label(l), active(false), brackets(true) {}

void Button::draw(Painter& p, const Theme& t, bool focused) {
    Rgb fg = enabled ? (active ? t.accentText : t.text) : t.dim;
    Rgb bg = active ? t.accent : (focused ? lerp(t.panel, t.focus, 0.3) : lerp(t.panel, t.text, 0.08));
    std::string text = brackets ? "[ " + label + " ]" : " " + label + " ";
    p.fill(rect(0, 0, p.width(), 1), bg);
    p.text(rect(0, 0, p.width(), 1), text, fg, Align::Center, focused || active ? Bold : Plain);
    if (focused && p.height() == 1 && !active) {
        // Underline the label under focus so it reads in every theme.
        for (int x = 0; x < p.width(); ++x)
            if (Cell* c = p.cell(x, 0)) c->attrs |= Underline;
    }
}

bool Button::handle(const Event& e) {
    if (isActivate(e) || (e.type == EventType::Mouse && e.action == MouseAction::Release && e.button == MouseButton::Left &&
                          e.x >= 0 && e.y >= 0 && e.x < bounds.width && e.y < bounds.height)) {
        if (onPress) onPress();
        return true;
    }
    return isPress(e);  // the press itself is used (capture), action on release
}

Toggle::Toggle(const std::string& l, bool c) : label(l), checked(c) {}

void Toggle::draw(Painter& p, const Theme& t, bool focused) {
    Rgb box = focused ? t.focus : t.border;
    p.text(0, 0, "[", box);
    p.put(1, 0, checked ? 0x2714 : ' ', t.accent, Bold);  // ✔
    p.text(2, 0, "]", box);
    p.text(4, 0, label, textColor(*this, t), focused ? Bold : Plain);
}

bool Toggle::handle(const Event& e) {
    if (isActivate(e) || isPress(e)) {
        checked = !checked;
        if (onChange) onChange(checked);
        return true;
    }
    return false;
}

RadioGroup::RadioGroup() : selected(0), vertical(false) {}

int RadioGroup::optionAt(int x, int y) const {
    if (vertical) return y >= 0 && y < static_cast<int>(options.size()) ? y : -1;
    int pos = 0;
    for (size_t i = 0; i < options.size(); ++i) {
        int w = columns(options[i]) + 2;
        if (x >= pos && x < pos + w) return static_cast<int>(i);
        pos += w + 1;
    }
    return -1;
}

void RadioGroup::select(int i) {
    if (i < 0 || i >= static_cast<int>(options.size())) return;
    if (i < static_cast<int>(disabled.size()) && disabled[static_cast<size_t>(i)]) return;
    if (i == selected) return;
    selected = i;
    if (onChange) onChange(i);
}

void RadioGroup::draw(Painter& p, const Theme& t, bool focused) {
    int x = 0;
    for (size_t i = 0; i < options.size(); ++i) {
        bool off = (i < disabled.size() && disabled[i]) || !enabled;
        bool sel = static_cast<int>(i) == selected;
        if (vertical) {
            Rgb c = off ? scale(t.dim, 0.7) : (sel ? t.accent : t.text);
            p.text(0, static_cast<int>(i), sel ? "(\xE2\x80\xA2) " : "( ) ", focused && sel ? t.focus : t.border);
            p.text(4, static_cast<int>(i), options[i], c, sel ? Bold : Plain);
        } else {
            std::string text = " " + options[i] + " ";
            Rgb fg = off ? scale(t.dim, 0.7) : (sel ? t.accentText : t.text);
            Rgb bg = sel ? (focused ? t.focus : t.accent) : lerp(t.panel, t.text, 0.08);
            x = p.text(x, 0, text, fg, bg, sel ? Bold : Plain) + 1;
        }
    }
}

bool RadioGroup::handle(const Event& e) {
    if (e.type == EventType::Mouse) {
        int dir;
        if (isWheel(e, dir)) {
            select(selected - dir);
            return true;
        }
        if (e.action == MouseAction::Press && e.button == MouseButton::Left) {
            select(optionAt(e.x, e.y));
            return true;
        }
        return false;
    }
    int prev = vertical ? terminal::KeyUp : terminal::KeyLeft, next = vertical ? terminal::KeyDown : terminal::KeyRight;
    if (e.isKey(prev) || e.isKey(next)) {
        int step = e.isKey(prev) ? -1 : 1;
        for (int i = selected + step; i >= 0 && i < static_cast<int>(options.size()); i += step) {
            if (i < static_cast<int>(disabled.size()) && disabled[static_cast<size_t>(i)]) continue;
            select(i);
            break;
        }
        return true;
    }
    return false;
}

Slider::Slider(const std::string& l, double mn, double mx, double v) : label(l), min(mn), max(mx), value(v), step(0), labelWidth(0) {}

Rect Slider::track() const {
    int lw = label.empty() ? 0 : (labelWidth > 0 ? labelWidth : columns(label) + 1);
    std::string v = format ? format(value) : fmt("%.2g", value);
    int vw = 8;
    (void)v;
    return rect(lw, 0, std::max(1, bounds.width - lw - vw), 1);
}

void Slider::set(double v) {
    if (step > 0) v = min + std::floor((v - min) / step + 0.5) * step;
    v = std::max(std::min(min, max), std::min(std::max(min, max), v));
    if (v == value) return;
    value = v;
    if (onChange) onChange(value);
}

void Slider::draw(Painter& p, const Theme& t, bool focused) {
    Rect tr = track();
    if (!label.empty()) p.text(0, 0, label, focused ? t.focus : textColor(*this, t), focused ? Bold : Plain);
    double f = max == min ? 0 : (value - min) / (max - min);
    f = std::max(0.0, std::min(1.0, f));
    int knob = tr.x + static_cast<int>(f * (tr.width - 1) + 0.5);
    for (int x = tr.x; x < tr.right(); ++x) p.put(x, 0, x < knob ? 0x2501 : 0x2500, x < knob ? t.accent : t.border);  // ━ ─
    p.put(knob, 0, 0x25C6, focused ? t.focus : t.accent, Bold);  // ◆
    std::string v = format ? format(value) : fmt("%.2g", value);
    p.text(rect(tr.right() + 1, 0, std::max(0, p.width() - tr.right() - 1), 1), v, t.text, Align::Right);
}

bool Slider::handle(const Event& e) {
    double stepSize = step > 0 ? step : (max - min) / 50.0;
    if (e.type == EventType::Mouse) {
        int dir;
        if (isWheel(e, dir)) {
            set(value + dir * stepSize);
            return true;
        }
        if ((e.action == MouseAction::Press || e.action == MouseAction::Drag) && e.button == MouseButton::Left) {
            Rect tr = track();
            double f = tr.width > 1 ? static_cast<double>(e.x - tr.x) / (tr.width - 1) : 0;
            set(min + (max - min) * std::max(0.0, std::min(1.0, f)));
            return true;
        }
        return e.action == MouseAction::Release;
    }
    if (e.isKey(terminal::KeyLeft) || e.isKey(terminal::KeyDown)) {
        set(value - stepSize * (e.shift ? 10 : 1));
        return true;
    }
    if (e.isKey(terminal::KeyRight) || e.isKey(terminal::KeyUp)) {
        set(value + stepSize * (e.shift ? 10 : 1));
        return true;
    }
    if (e.isKey(terminal::KeyHome)) {
        set(min);
        return true;
    }
    if (e.isKey(terminal::KeyEnd)) {
        set(max);
        return true;
    }
    return false;
}

TextInput::TextInput(const std::string& ph) : placeholder(ph), maxLength(0), cursor_(0), scroll_(0) {}

namespace {

std::vector<std::uint32_t> codePoints(const std::string& s) {
    std::vector<std::uint32_t> cps;
    for (size_t i = 0; i < s.size();) cps.push_back(nextCodePoint(s, i));
    return cps;
}

std::string fromCodePoints(const std::vector<std::uint32_t>& cps) {
    std::string s;
    for (size_t i = 0; i < cps.size(); ++i) appendUtf8(s, cps[i]);
    return s;
}

}  // namespace

void TextInput::setText(const std::string& s) {
    text = s;
    cursor_ = codePoints(s).size();
}

void TextInput::draw(Painter& p, const Theme& t, bool focused) {
    int x = 0;
    if (!label.empty()) x = p.text(0, 0, label + " ", focused ? t.focus : t.text, focused ? Bold : Plain);
    int w = p.width() - x;
    if (w <= 0) return;
    Rgb bg = focused ? lerp(t.panel, t.focus, 0.18) : lerp(t.panel, t.text, 0.06);
    p.fill(rect(x, 0, w, 1), bg);
    std::vector<std::uint32_t> cps = codePoints(text);
    cursor_ = std::min(cursor_, cps.size());
    if (cursor_ < scroll_) scroll_ = cursor_;
    if (cursor_ >= scroll_ + static_cast<size_t>(w)) scroll_ = cursor_ - static_cast<size_t>(w) + 1;
    if (cps.empty() && !focused) {
        p.text(x, 0, truncate(placeholder, w), t.dim, bg, Italic);
    } else {
        for (size_t i = scroll_; i < cps.size() && static_cast<int>(i - scroll_) < w; ++i)
            p.put(x + static_cast<int>(i - scroll_), 0, cps[i], t.text, bg);
    }
    if (focused && app()) app()->setCursor(p.area().x + x + static_cast<int>(cursor_ - scroll_), p.area().y);
}

bool TextInput::handle(const Event& e) {
    if (e.type == EventType::Mouse) {
        if (isPress(e)) {
            int x = label.empty() ? 0 : columns(label) + 1;
            size_t pos = scroll_ + static_cast<size_t>(std::max(0, e.x - x));
            cursor_ = std::min(pos, codePoints(text).size());
        }
        return true;
    }
    if (e.type != EventType::Key) return false;
    std::vector<std::uint32_t> cps = codePoints(text);
    bool changed = false;
    if (e.isKey(terminal::KeyEnter)) {
        if (onSubmit) onSubmit(text);
        return true;
    }
    if (e.isKey(terminal::KeyEscape)) {
        if (onCancel) onCancel();
        return static_cast<bool>(onCancel);
    }
    if (e.isKey(terminal::KeyLeft)) {
        if (cursor_ > 0) --cursor_;
    } else if (e.isKey(terminal::KeyRight)) {
        if (cursor_ < cps.size()) ++cursor_;
    } else if (e.isKey(terminal::KeyHome) || (e.ctrl && e.key == 'a')) {
        cursor_ = 0;
    } else if (e.isKey(terminal::KeyEnd) || (e.ctrl && e.key == 'e')) {
        cursor_ = cps.size();
    } else if (e.isKey(terminal::KeyBackspace)) {
        if (cursor_ > 0) {
            cps.erase(cps.begin() + static_cast<long>(cursor_ - 1));
            --cursor_;
            changed = true;
        }
    } else if (e.isKey(terminal::KeyDelete)) {
        if (cursor_ < cps.size()) {
            cps.erase(cps.begin() + static_cast<long>(cursor_));
            changed = true;
        }
    } else if (e.ctrl && e.key == 'u') {
        cps.clear();
        cursor_ = 0;
        changed = true;
    } else if (!e.ctrl && !e.alt && e.key >= 32 && e.key < terminal::KeyUp) {
        std::uint32_t cp = static_cast<std::uint32_t>(e.key);
        if (accept && !accept(cp)) return true;
        if (maxLength && cps.size() >= maxLength) return true;
        cps.insert(cps.begin() + static_cast<long>(cursor_), cp);
        ++cursor_;
        changed = true;
    } else {
        return false;  // Tab, arrows up/down, function keys: not ours
    }
    if (changed) {
        text = fromCodePoints(cps);
        if (onChange) onChange(text);
    }
    return true;
}

// ----------------------------------------------------------------- ListBox --

ListBox::ListBox() : selected(-1), border(false), top_(0) {}

void ListBox::setItems(const std::vector<std::string>& texts) {
    items.clear();
    for (size_t i = 0; i < texts.size(); ++i) {
        Item it = {texts[i], std::string(), false, rgb(255, 255, 255)};
        items.push_back(it);
    }
    if (selected >= static_cast<int>(items.size())) selected = static_cast<int>(items.size()) - 1;
}

Rect ListBox::listArea() const {
    Rect r = rect(0, 0, bounds.width, bounds.height);
    return border ? inset(r, 1) : r;
}

void ListBox::ensureVisible() {
    int h = listArea().height;
    if (h <= 0 || selected < 0) return;
    if (selected < top_) top_ = selected;
    if (selected >= top_ + h) top_ = selected - h + 1;
}

void ListBox::draw(Painter& p, const Theme& t, bool focused) {
    if (border) p.box(p.bounds(), focused ? t.focus : t.border, Border::Rounded, title.empty() ? title : " " + title + " ");
    Rect a = listArea();
    int n = static_cast<int>(items.size());
    top_ = std::max(0, std::min(top_, std::max(0, n - a.height)));
    bool scrollBar = n > a.height && a.height > 0;
    int textRight = a.right() - (scrollBar ? 1 : 0);  // keep the bar's column free
    for (int row = 0; row < a.height && top_ + row < n; ++row) {
        const Item& it = items[static_cast<size_t>(top_ + row)];
        bool sel = top_ + row == selected;
        Rgb fg = sel ? t.accentText : (it.useColor ? it.color : t.text);
        Rgb bg = sel ? (focused ? t.focus : lerp(t.panel, t.accent, 0.5)) : (border ? t.panel : t.background);
        int y = a.y + row;
        p.fill(rect(a.x, y, a.width, 1), bg);
        int rw = it.right.empty() ? 0 : columns(it.right) + 1;
        p.text(rect(a.x + 1, y, textRight - a.x - 1 - rw, 1), it.text, fg, Align::Left, sel ? Bold : Plain);
        if (rw) p.text(textRight - rw, y, it.right, sel ? fg : t.dim);
    }
    // Scroll bar when the list is longer than the view.
    if (n > a.height && a.height > 0) {
        int thumb = std::max(1, a.height * a.height / n);
        int pos = (a.height - thumb) * top_ / std::max(1, n - a.height);
        for (int y = 0; y < a.height; ++y) p.put(a.right() - 1, a.y + y, y >= pos && y < pos + thumb ? 0x2588 : 0x2502, t.border);
    }
}

bool ListBox::handle(const Event& e) {
    int n = static_cast<int>(items.size());
    int old = selected;
    Rect a = listArea();
    if (e.type == EventType::Mouse) {
        int dir;
        if (isWheel(e, dir)) {
            top_ = std::max(0, std::min(top_ - dir * 3, std::max(0, n - a.height)));
            return true;
        }
        if (isPress(e)) {
            int i = top_ + e.y - a.y;
            if (i >= 0 && i < n && e.y >= a.y && e.y < a.bottom()) {
                if (i == selected && onActivate) onActivate(i);
                selected = i;
            }
        }
        if (selected != old && onSelect) onSelect(selected);
        return true;
    }
    if (e.isKey(terminal::KeyUp)) selected = std::max(0, selected - 1);
    else if (e.isKey(terminal::KeyDown)) selected = std::min(n - 1, selected + 1);
    else if (e.isKey(terminal::KeyPageUp)) selected = std::max(0, selected - std::max(1, a.height - 1));
    else if (e.isKey(terminal::KeyPageDown)) selected = std::min(n - 1, selected + std::max(1, a.height - 1));
    else if (e.isKey(terminal::KeyHome)) selected = n > 0 ? 0 : -1;
    else if (e.isKey(terminal::KeyEnd)) selected = n - 1;
    else if (isActivate(e) && selected >= 0) {
        if (onActivate) onActivate(selected);
        return true;
    } else {
        return false;
    }
    ensureVisible();
    if (selected != old && onSelect) onSelect(selected);
    return true;
}

// ------------------------------------------------------------------- Table --

Table::Table() : selected(-1), border(true), top_(0) {}

void Table::addColumn(const std::string& t, int width, Align align) {
    Column c = {t, width, align};
    columns.push_back(c);
}

void Table::draw(Painter& p, const Theme& t, bool focused) {
    Rect a = p.bounds();
    if (border) {
        p.box(a, focused ? t.focus : t.border, Border::Rounded, title.empty() ? title : " " + title + " ");
        a = inset(a, 1);
    }
    if (a.empty() || columns.empty()) return;
    // Column widths: fixed ones first, the rest shared.
    std::vector<Size> sizes;
    for (size_t i = 0; i < columns.size(); ++i) sizes.push_back(columns[i].width > 0 ? fixed(columns[i].width) : flex());
    std::vector<Rect> cols = splitColumns(rect(a.x, a.y, a.width, 1), sizes, 1);
    for (size_t i = 0; i < columns.size(); ++i) p.text(cols[i], columns[i].title, t.accent, columns[i].align, Bold | Underline);
    int h = a.height - 1, n = static_cast<int>(rows.size());
    if (selected >= 0) {
        if (selected < top_) top_ = selected;
        if (selected >= top_ + h) top_ = selected - h + 1;
    }
    top_ = std::max(0, std::min(top_, std::max(0, n - h)));
    for (int r = 0; r < h && top_ + r < n; ++r) {
        int y = a.y + 1 + r;
        bool sel = top_ + r == selected;
        if (sel) p.fill(rect(a.x, y, a.width, 1), focused ? t.focus : lerp(t.panel, t.accent, 0.5));
        const std::vector<std::string>& row = rows[static_cast<size_t>(top_ + r)];
        for (size_t c = 0; c < columns.size() && c < row.size(); ++c) {
            Rect cr = rect(cols[c].x, y, cols[c].width, 1);
            p.text(cr, row[c], sel ? t.accentText : t.text, columns[c].align);
        }
    }
}

bool Table::handle(const Event& e) {
    int n = static_cast<int>(rows.size()), old = selected;
    int headerY = border ? 2 : 1;
    int h = bounds.height - headerY - (border ? 1 : 0);
    if (e.type == EventType::Mouse) {
        int dir;
        if (isWheel(e, dir)) {
            top_ = std::max(0, std::min(top_ - dir * 3, std::max(0, n - h)));
            return true;
        }
        if (isPress(e)) {
            int i = top_ + e.y - headerY;
            if (e.y >= headerY && i >= 0 && i < n) {
                if (i == selected && onActivate) onActivate(i);
                selected = i;
            }
        }
        if (selected != old && onSelect) onSelect(selected);
        return true;
    }
    if (e.isKey(terminal::KeyUp)) selected = std::max(0, selected - 1);
    else if (e.isKey(terminal::KeyDown)) selected = std::min(n - 1, selected + 1);
    else if (e.isKey(terminal::KeyPageUp)) selected = std::max(0, selected - std::max(1, h - 1));
    else if (e.isKey(terminal::KeyPageDown)) selected = std::min(n - 1, selected + std::max(1, h - 1));
    else if (e.isKey(terminal::KeyHome)) selected = n > 0 ? 0 : -1;
    else if (e.isKey(terminal::KeyEnd)) selected = n - 1;
    else if (isActivate(e) && selected >= 0) {
        if (onActivate) onActivate(selected);
        return true;
    } else {
        return false;
    }
    if (selected != old && onSelect) onSelect(selected);
    return true;
}

// -------------------------------------------------------------------- Tabs --

// Tabs pad each name with a space on both sides; when they would not fit the
// width they drop the padding (compact).
int Tabs::padding() const {
    int total = 0;
    for (size_t i = 0; i < tabs.size(); ++i) total += columns(tabs[i]) + 3;
    return total - 1 <= bounds.width ? 1 : 0;
}

int Tabs::tabAt(int x) const {
    int pos = 0, pad = padding();
    for (size_t i = 0; i < tabs.size(); ++i) {
        int w = columns(tabs[i]) + 2 * pad;
        if (x >= pos && x < pos + w) return static_cast<int>(i);
        pos += w + 1;
    }
    return -1;
}

void Tabs::draw(Painter& p, const Theme& t, bool focused) {
    int x = 0;
    const std::string pad(static_cast<size_t>(padding()), ' ');
    for (size_t i = 0; i < tabs.size(); ++i) {
        bool sel = static_cast<int>(i) == selected;
        Rgb fg = sel ? t.accentText : t.text;
        Rgb bg = sel ? (focused ? t.focus : t.accent) : lerp(t.panel, t.text, 0.06);
        x = p.text(x, 0, pad + tabs[i] + pad, fg, bg, sel ? Bold : Plain);
        x = p.text(x, 0, " ", t.border) ;
    }
    if (p.height() > 1) p.hline(0, 1, p.width(), t.border);
}

bool Tabs::handle(const Event& e) {
    int old = selected, n = static_cast<int>(tabs.size());
    if (isPress(e)) {
        int i = tabAt(e.x);
        if (i >= 0) selected = i;
    } else if (e.isKey(terminal::KeyLeft)) {
        selected = (selected + n - 1) % std::max(1, n);
    } else if (e.isKey(terminal::KeyRight)) {
        selected = (selected + 1) % std::max(1, n);
    } else {
        return e.type == EventType::Mouse;
    }
    if (selected != old && onChange) onChange(selected);
    return true;
}

// -------------------------------------------------------------------- Menu --

Menu::Menu() : selected(0) {}

void Menu::addItem(const std::string& text, const std::string& shortcut, bool checked) {
    Item it = {text, shortcut, false, false, checked};
    items.push_back(it);
}

void Menu::addSeparator() {
    Item it = {std::string(), std::string(), true, true, false};
    items.push_back(it);
}

void Menu::place(int x, int y, const Rect& screen) {
    int w = columns(title) + 4, h = static_cast<int>(items.size()) + 2;
    for (size_t i = 0; i < items.size(); ++i) w = std::max(w, columns(items[i].text) + columns(items[i].shortcut) + 7);
    w = std::min(w, screen.width);
    h = std::min(h, screen.height);
    x = std::max(screen.x, std::min(x, screen.right() - w));
    y = std::max(screen.y, std::min(y, screen.bottom() - h));
    bounds = rect(x, y, w, h);
    if (selected < 0 || selected >= static_cast<int>(items.size()) || items[static_cast<size_t>(selected)].separator) {
        selected = -1;
        move(1);
    }
}

void Menu::move(int dir) {
    int n = static_cast<int>(items.size());
    for (int k = 0; k < n; ++k) {
        selected = (selected + dir + n) % n;
        const Item& it = items[static_cast<size_t>(selected)];
        if (!it.separator && !it.disabled) return;
    }
}

void Menu::draw(Painter& p, const Theme& t, bool) {
    p.panel(p.bounds(), t.focus, t.panel, Border::Rounded, title.empty() ? title : " " + title + " ");
    for (size_t i = 0; i < items.size(); ++i) {
        int y = 1 + static_cast<int>(i);
        if (y >= p.height() - 1) break;
        const Item& it = items[i];
        if (it.separator) {
            p.hline(1, y, p.width() - 2, t.border);
            continue;
        }
        bool sel = static_cast<int>(i) == selected;
        Rgb fg = it.disabled ? t.dim : (sel ? t.accentText : t.text);
        if (sel) p.fill(rect(1, y, p.width() - 2, 1), t.accent);
        p.put(2, y, it.checked ? 0x2713 : ' ', sel ? fg : t.accent);  // ✓
        int sw = columns(it.shortcut);
        p.text(rect(4, y, p.width() - 6 - sw, 1), it.text, fg, Align::Left, sel ? Bold : Plain);
        if (sw) p.text(p.width() - 2 - sw, y, it.shortcut, sel ? fg : t.dim);
    }
}

bool Menu::handle(const Event& e) {
    App* a = app();
    if (e.type == EventType::Mouse) {
        int i = e.y - 1;
        bool onItem = i >= 0 && i < static_cast<int>(items.size()) && !items[static_cast<size_t>(i)].separator &&
                      !items[static_cast<size_t>(i)].disabled;
        if (onItem && (e.action == MouseAction::Move || e.action == MouseAction::Drag || e.action == MouseAction::Press)) selected = i;
        if (onItem && e.action == MouseAction::Release) {
            if (a) a->closePopup(*this);
            if (onPick) onPick(i);
        }
        return true;
    }
    if (e.isKey(terminal::KeyUp)) move(-1);
    else if (e.isKey(terminal::KeyDown)) move(1);
    else if (isActivate(e) && selected >= 0) {
        int pick = selected;
        if (a) a->closePopup(*this);
        if (onPick) onPick(pick);
    } else if (e.isKey(terminal::KeyEscape)) {
        if (a) a->closePopup(*this);
    } else {
        return true;  // a menu swallows everything else while open
    }
    return true;
}

// ------------------------------------------------------------------ Dialog --

Dialog::Dialog() : buttons(1, "OK"), selected(0) {}

void Dialog::place(const Rect& screen, int width) {
    width = std::min(width, screen.width - 2);
    int lines = static_cast<int>(wrap(message, width - 4).size());
    bounds = centered(screen, width, lines + 5);
}

int Dialog::buttonAt(int x, int y) const {
    if (y != bounds.height - 2) return -1;
    int total = 0;
    for (size_t i = 0; i < buttons.size(); ++i) total += columns(buttons[i]) + 4 + 1;
    int pos = (bounds.width - total) / 2;
    for (size_t i = 0; i < buttons.size(); ++i) {
        int w = columns(buttons[i]) + 4;
        if (x >= pos && x < pos + w) return static_cast<int>(i);
        pos += w + 1;
    }
    return -1;
}

void Dialog::draw(Painter& p, const Theme& t, bool) {
    p.panel(p.bounds(), t.focus, t.panel, Border::Double, title.empty() ? title : " " + title + " ");
    p.paragraph(rect(2, 2, p.width() - 4, p.height() - 5), message, t.text);
    int total = 0;
    for (size_t i = 0; i < buttons.size(); ++i) total += columns(buttons[i]) + 4 + 1;
    int x = (p.width() - total) / 2;
    for (size_t i = 0; i < buttons.size(); ++i) {
        bool sel = static_cast<int>(i) == selected;
        x = p.text(x, p.height() - 2, "[ " + buttons[i] + " ]", sel ? t.accentText : t.text, sel ? t.accent : t.panel,
                   sel ? Bold : Plain) + 1;
    }
}

bool Dialog::handle(const Event& e) {
    App* a = app();
    int n = static_cast<int>(buttons.size());
    int pick = -2;
    if (e.type == EventType::Mouse) {
        int i = buttonAt(e.x, e.y);
        if (i >= 0 && e.action == MouseAction::Press) selected = i;
        if (i >= 0 && e.action == MouseAction::Release) pick = i;
    } else if (e.isKey(terminal::KeyLeft) || e.isKey(terminal::KeyBackTab)) {
        selected = (selected + n - 1) % std::max(1, n);
    } else if (e.isKey(terminal::KeyRight) || e.isKey(terminal::KeyTab)) {
        selected = (selected + 1) % std::max(1, n);
    } else if (isActivate(e)) {
        pick = selected;
    } else if (e.isKey(terminal::KeyEscape)) {
        pick = -1;
    }
    if (pick != -2) {
        if (a) a->closePopup(*this);
        if (onClose) onClose(pick);
    }
    return true;
}

}  // namespace ui
}  // namespace gfx
