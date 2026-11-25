// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   include/graphicx/widgets.h
//
//  What this file does
//  -------------------
//  The widget layer of the user interface toolkit, on top of ui.h:
//
//    gfx::ui::App        owns the screen Surface and Display, the Theme, the
//                        widgets (in layers: normal, then popups/modals),
//                        keyboard focus (Tab / Shift+Tab, click), mouse
//                        routing (press, drag capture, wheel), global key
//                        bindings, and produces each frame's output.
//    gfx::ui::Widget     base class: bounds, visible, enabled, focusable,
//                        draw(), handle().
//
//  Controls (interactive)
//    Button, Toggle (check box), RadioGroup / segmented buttons, Slider,
//    TextInput, ListBox, Table, Tabs, Menu (popup), Dialog (modal message
//    with buttons)
//
//  Displays
//    Label, Panel (titled frame), Separator, StatusBar, Led, Spinner,
//    ProgressBar, Meter (segmented level meter with peak hold, zones and a
//    scale: S-meters, VU meters), Sparkline, BarChart, Plot (line/area
//    chart with axes, grid, several series, vertical markers and shaded
//    bands: oscilloscopes, spectrum analysers), Waterfall (scrolling
//    spectrogram with a colormap), BigText (large text from the built-in
//    fonts), ImageView (any gfx::Image)
//
//  Example
//  -------
//      gfx::terminal::Session session;
//      session.enableMouse(true);
//      gfx::ui::App app(gfx::theme("amber"));
//      gfx::ui::Button quit("Quit");
//      quit.onPress = [&]() { app.quit(); };
//      gfx::ui::Slider volume("Volume", 0, 100, 50);
//      app.add(volume);
//      app.add(quit);
//      app.onLayout = [&](const gfx::Rect& screen) {
//          volume.bounds = gfx::rect(2, 2, 40, 1);
//          quit.bounds = gfx::rect(2, 4, 10, 1);
//      };
//      app.run(session);              // until app.quit() or Ctrl+C
//
//  How it fits in
//  --------------
//  Widgets are plain objects owned by the program; the App only keeps
//  pointers (add() / remove()).  Positions are set by the program (usually
//  in App::onLayout with the ui::split* helpers).  Programs with their own
//  main loop call App::dispatch(event) and App::frame(out) instead of run().
// ============================================================================
#ifndef GRAPHICX_WIDGETS_H
#define GRAPHICX_WIDGETS_H

#include <functional>
#include <string>
#include <vector>

#include "color.h"
#include "terminal.h"
#include "ui.h"

namespace gfx {
namespace ui {

using terminal::Event;

class App;

// ------------------------------------------------------------------ Widget --

class Widget {
public:
    Widget();
    virtual ~Widget();

    Rect bounds;   // in screen cells (set by the program's layout)
    bool visible;  // hidden widgets are neither drawn nor get events
    bool enabled;  // disabled widgets are drawn dimmed and get no events
    std::string tooltip;  // shown in the App's status line while focused (optional)

    virtual bool focusable() const { return false; }
    // Draws inside `p` (local coordinates, clipped to bounds).
    virtual void draw(Painter& p, const Theme& theme, bool focused) = 0;
    // Mouse events arrive with x / y relative to bounds.  Return true when
    // the event was used.
    virtual bool handle(const Event& event) {
        (void)event;
        return false;
    }

    App* app() const { return app_; }

private:
    friend class App;
    App* app_;
};

// --------------------------------------------------------------------- App --

class App {
public:
    explicit App(const Theme& theme = gfx::theme("amber"));

    void setTheme(const Theme& theme) { theme_ = theme; }
    const Theme& theme() const { return theme_; }
    void setColorMode(ColorMode mode) { display_.setColorMode(mode); }

    // Widgets, drawn in the order added (later ones on top).  The App does
    // not own them.
    void add(Widget& widget);
    void remove(Widget& widget);
    // Popups / modal widgets (menus, dialogs): drawn last; while one is
    // open, keys go to it and clicks outside it close it (unless modal).
    void openPopup(Widget& widget, bool modal = false);
    void closePopup(Widget& widget);
    bool popupOpen() const { return !popups_.empty(); }

    void focus(Widget& widget);
    // No widget focused: keys go straight to the bindings / onUnhandled
    // (programs whose main view is not a widget, like a radio's tuning).
    void clearFocus() { focus_ = 0; }
    Widget* focused() const { return focus_; }
    // When false (default true), add() does not give the focus to the first
    // focusable widget.
    bool autoFocus;
    void focusNext(bool backwards = false);

    // Global keys, checked after the focused widget declined the key.
    void bindKey(int key, std::function<void()> action, bool ctrl = false, bool alt = false);
    // Called on every event nobody used (optional).
    std::function<void(const Event&)> onUnhandled;

    // Recomputes the layout on the next frame (called on resize).
    void relayout() { needLayout_ = true; }
    // Called with the screen rectangle when the size changes.
    std::function<void(const Rect& screen)> onLayout;
    // Called before widgets are drawn each frame (paint backgrounds, update
    // values) and after (overlays).
    std::function<void(Painter& p)> onDrawBackground;
    std::function<void(Painter& p)> onDrawOverlay;

    // Routes one event (keys, mouse, resize).  Returns true if used.
    bool dispatch(const Event& event);
    // Draws everything into the surface and appends the terminal update.
    void frame(std::string& out);
    // Forget what is on the terminal (after something else wrote to it).
    void invalidate() { display_.invalidate(); }

    // Simple main loop: frames at `fps`, events in between; returns after
    // quit() or Ctrl+C.  `onTick` (optional) runs before every frame.
    void run(terminal::Session& session, int fps = 30, std::function<void()> onTick = std::function<void()>());
    void quit() { quit_ = true; }
    bool quitting() const { return quit_; }

    Surface& surface() { return surface_; }
    Display& display() { return display_; }
    // Text input fields put the terminal cursor here while focused.
    void setCursor(int x, int y) {
        cursorWanted_ = true;
        cursorX_ = x;
        cursorY_ = y;
    }

private:
    struct Binding {
        int key;
        bool ctrl, alt;
        std::function<void()> action;
    };
    Widget* widgetAt(int x, int y);
    std::vector<Widget*> focusables() const;

    Theme theme_;
    Surface surface_;
    Display display_;
    std::vector<Widget*> widgets_;
    std::vector<Widget*> popups_;
    std::vector<bool> modal_;
    std::vector<Binding> bindings_;
    Widget* focus_;
    Widget* capture_;  // receives drags / release after a press
    bool needLayout_;
    bool quit_;
    bool cursorWanted_;
    int cursorX_, cursorY_;
};

// ------------------------------------------------------------- displays --

class Label : public Widget {
public:
    explicit Label(const std::string& text = std::string(), Align align = Align::Left);
    std::string text;
    Align align;
    bool wrap;            // word-wrap over several rows (default false)
    bool useColor;        // color below instead of the theme's text color
    Rgb color;
    std::uint8_t attrs;
    void draw(Painter& p, const Theme& t, bool focused);
};

// A titled frame; place child widgets inside inner().
class Panel : public Widget {
public:
    explicit Panel(const std::string& title = std::string(), Border border = Border::Rounded);
    std::string title;
    Border border;
    bool filled;  // paint the theme's panel color inside (default true)
    Rect inner() const { return inset(bounds, 1); }
    void draw(Painter& p, const Theme& t, bool focused);
};

class Separator : public Widget {
public:
    explicit Separator(bool vertical = false) : vertical(vertical) {}
    bool vertical;
    void draw(Painter& p, const Theme& t, bool focused);
};

// One row of segments: left-aligned, then right-aligned ones.
class StatusBar : public Widget {
public:
    struct Segment {
        std::string text;
        Rgb fg, bg;
        bool useColors;  // false = theme colors
    };
    std::vector<Segment> left, right;
    void clear() {
        left.clear();
        right.clear();
    }
    void addLeft(const std::string& text);
    void addLeft(const std::string& text, Rgb fg, Rgb bg);
    void addRight(const std::string& text);
    void addRight(const std::string& text, Rgb fg, Rgb bg);
    void draw(Painter& p, const Theme& t, bool focused);
};

// ● label: on (colored) / off (dim).
class Led : public Widget {
public:
    explicit Led(const std::string& label = std::string());
    std::string label;
    bool on;
    bool useColor;
    Rgb color;  // when useColor (default: theme good)
    void draw(Painter& p, const Theme& t, bool focused);
};

class Spinner : public Widget {
public:
    Spinner() : active(true), step_(0) {}
    std::string label;
    bool active;
    void draw(Painter& p, const Theme& t, bool focused);

private:
    unsigned step_;
};

class ProgressBar : public Widget {
public:
    ProgressBar() : value(0), showPercent(true) {}
    double value;  // 0..1
    bool showPercent;
    std::string label;  // drawn left of the bar
    void draw(Painter& p, const Theme& t, bool focused);
};

// Level meter: a horizontal bar with colored zones, peak hold and a scale.
class Meter : public Widget {
public:
    Meter();
    double min, max;          // value range
    double value;             // current level
    double peak;              // peak marker (NaN = none); see holdPeak()
    double warnAt, badAt;     // zone starts (values); above = warn / bad colors
    std::string label;        // left of the bar ("S", "dB" ...)
    std::string valueText;    // right of the bar (e.g. "S9+10")
    // Scale marks drawn on the row below the bar when height >= 2.
    std::vector<std::pair<double, std::string> > scale;
    bool segmented;           // ▮▮▮ blocks instead of a smooth bar
    // Peak hold with decay: call each frame with the new value.
    void update(double newValue, double decayPerCall = 0.5);
    void draw(Painter& p, const Theme& t, bool focused);
};

class Sparkline : public Widget {
public:
    Sparkline() : min(0), max(0), useColor(false), color(rgb(255, 255, 255)) {}
    std::vector<double> values;  // newest last; drawn right-aligned
    double min, max;             // min == max: automatic range
    bool useColor;
    Rgb color;
    void push(double v, size_t keep = 256);
    void draw(Painter& p, const Theme& t, bool focused);
};

class BarChart : public Widget {
public:
    BarChart() : min(0), max(0), barWidth(2), gap(1) {}
    struct Bar {
        std::string label;
        double value;
    };
    std::vector<Bar> bars;
    double min, max;  // equal: automatic
    int barWidth, gap;
    void draw(Painter& p, const Theme& t, bool focused);
};

// Line / area chart.  Series are drawn with braille dots (2x4 per cell),
// with optional axes (labels left and bottom) and grid lines.
class Plot : public Widget {
public:
    Plot();
    struct Series {
        std::vector<double> y;   // evenly spread over [xMin, xMax]
        Rgb color;
        bool fill;               // dots from the line down to the bottom
        bool useColor;           // false = theme accent
    };
    struct Marker {              // vertical line at x, optional label on top
        double x;
        Rgb color;
        std::string label;
    };
    struct BandArea {            // shaded background between x0 and x1
        double x0, x1;
        Rgb color;
    };
    std::vector<Series> series;
    std::vector<Marker> markers;
    std::vector<BandArea> bands;
    double xMin, xMax, yMin, yMax;  // yMin == yMax: automatic y range
    bool showAxes, showGrid;
    int xTicks, yTicks;             // grid divisions
    std::function<std::string(double)> formatX, formatY;  // axis labels
    // Peak-preserving: when there are more points than dots, each dot
    // column shows the maximum of its points (spectra) instead of a sample.
    bool maxDecimation;

    // Clicks / drags / wheel on the plot area, with the x value under the
    // pointer (and y value).
    std::function<void(double x, double y, const Event& e)> onMouse;

    // The plot area inside the axes, in widget-local cells.
    Rect plotArea() const;
    // x value at a local column (inverse of the drawing).
    double xAt(int column) const;
    int columnOf(double x) const;

    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);
};

// Scrolling spectrogram: addRow() pushes a new line at the top (newest
// first); values are mapped through a colormap between low and high.
class Waterfall : public Widget {
public:
    Waterfall();
    double low, high;      // value range mapped to the colormap
    bool useThemeMap;      // theme colormap (default) or `map`
    Colormap map;
    std::vector<Plot::Marker> markers;  // vertical lines (x as a fraction 0..1)
    std::function<void(double fraction, const Event& e)> onMouse;  // x as 0..1

    // values: any number of bins, spread over the width (max per pixel when
    // reducing).  Rows are 2 per cell (half blocks).
    void addRow(const std::vector<float>& values);
    void clear();
    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);

private:
    void ensureSize();
    Image history_;
    std::vector<Rgb> table_;
    Colormap tableMap_;
    bool hasTable_;
};

// Large text from the built-in bitmap fonts.  Font3x5 + Quadrants is 2
// cells per character and 3 rows; Font5x7 + HalfBlocks 6 x 4.
class BigText : public Widget {
public:
    explicit BigText(const std::string& text = std::string());
    std::string text;
    Font font;
    PixelMode mode;
    Align align;
    bool useColor;
    Rgb color;
    // Size in cells this text needs.
    void measure(int& width, int& height) const;
    void draw(Painter& p, const Theme& t, bool focused);
};

class ImageView : public Widget {
public:
    ImageView() : mode(PixelMode::HalfBlocks), filter(Filter::Box), keepAspect(true) {}
    Image image;
    PixelMode mode;
    Filter filter;
    bool keepAspect;
    void draw(Painter& p, const Theme& t, bool focused);
};

// ------------------------------------------------------------- controls --

class Button : public Widget {
public:
    explicit Button(const std::string& label = std::string());
    std::string label;
    bool active;     // drawn highlighted (toggle buttons, mode selectors)
    bool brackets;   // "[ label ]" (default) or plain " label "
    std::function<void()> onPress;
    bool focusable() const { return enabled; }
    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);
};

class Toggle : public Widget {
public:
    explicit Toggle(const std::string& label = std::string(), bool checked = false);
    std::string label;
    bool checked;
    std::function<void(bool)> onChange;
    bool focusable() const { return enabled; }
    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);
};

// One choice of several: horizontal segmented buttons (default) or a
// vertical list of (•) options.
class RadioGroup : public Widget {
public:
    RadioGroup();
    std::vector<std::string> options;
    std::vector<bool> disabled;   // per option (optional, same size)
    int selected;
    bool vertical;
    std::function<void(int)> onChange;
    bool focusable() const { return enabled; }
    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);
    // Option under a local column/row (-1 none).
    int optionAt(int x, int y) const;

private:
    void select(int index);
};

class Slider : public Widget {
public:
    Slider(const std::string& label = std::string(), double min = 0, double max = 1, double value = 0);
    std::string label;
    double min, max, value, step;
    int labelWidth;  // columns reserved for the label (0 = its length + 1)
    std::function<std::string(double)> format;  // value text (default %.2g)
    std::function<void(double)> onChange;
    bool focusable() const { return enabled; }
    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);
    void set(double v);

private:
    Rect track() const;  // local
};

class TextInput : public Widget {
public:
    explicit TextInput(const std::string& placeholder = std::string());
    std::string text;
    std::string placeholder;
    std::string label;      // drawn before the field
    size_t maxLength;       // code points (0 = no limit)
    std::function<bool(std::uint32_t)> accept;  // filter typed characters (optional)
    std::function<void(const std::string&)> onSubmit;  // Enter
    std::function<void()> onCancel;                    // Esc
    std::function<void(const std::string&)> onChange;
    bool focusable() const { return enabled; }
    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);
    void setText(const std::string& s);
    size_t cursor() const { return cursor_; }

private:
    size_t cursor_;  // in code points
    size_t scroll_;
};

class ListBox : public Widget {
public:
    ListBox();
    struct Item {
        std::string text;
        std::string right;  // right-aligned extra text (shortcut, value)
        bool useColor;
        Rgb color;
    };
    std::vector<Item> items;
    int selected;          // -1 = none
    bool border;           // frame with title around the list
    std::string title;
    std::function<void(int)> onSelect;    // selection moved
    std::function<void(int)> onActivate;  // Enter / double click / click on selected
    void setItems(const std::vector<std::string>& texts);
    bool focusable() const { return enabled; }
    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);
    void ensureVisible();

private:
    Rect listArea() const;  // local
    int top_;
};

class Table : public Widget {
public:
    Table();
    struct Column {
        std::string title;
        int width;  // 0 = share of the remaining width
        Align align;
    };
    std::vector<Column> columns;
    std::vector<std::vector<std::string> > rows;
    int selected;
    bool border;
    std::string title;
    std::function<void(int)> onSelect;
    std::function<void(int)> onActivate;
    void addColumn(const std::string& title, int width = 0, Align align = Align::Left);
    bool focusable() const { return enabled; }
    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);

private:
    int top_;
};

class Tabs : public Widget {
public:
    Tabs() : selected(0) {}
    std::vector<std::string> tabs;
    int selected;
    std::function<void(int)> onChange;
    bool focusable() const { return enabled; }
    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);

private:
    int tabAt(int x) const;
    int padding() const;  // 1, or 0 when the padded names do not fit
};

// Popup menu: open with App::openPopup after setting bounds via place().
class Menu : public Widget {
public:
    Menu();
    struct Item {
        std::string text;
        std::string shortcut;
        bool separator;
        bool disabled;
        bool checked;  // ✓ mark
    };
    std::vector<Item> items;
    int selected;
    std::string title;
    std::function<void(int)> onPick;  // item index; the menu closes itself
    void addItem(const std::string& text, const std::string& shortcut = std::string(), bool checked = false);
    void addSeparator();
    // Sets bounds to fit the items at (x, y), kept inside `screen`.
    void place(int x, int y, const Rect& screen);
    bool focusable() const { return true; }
    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);

private:
    void move(int direction);
};

// Modal message box with buttons (←/→ or Tab to choose, Enter, Esc = last).
class Dialog : public Widget {
public:
    Dialog();
    std::string title, message;
    std::vector<std::string> buttons;  // default {"OK"}
    int selected;
    std::function<void(int)> onClose;  // button index (-1 = Esc)
    // Centers itself on `screen` with a width that fits the text.
    void place(const Rect& screen, int width = 50);
    bool focusable() const { return true; }
    void draw(Painter& p, const Theme& t, bool focused);
    bool handle(const Event& e);

private:
    int buttonAt(int x, int y) const;
};

}  // namespace ui
}  // namespace gfx

#endif
