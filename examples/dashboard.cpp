// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   examples/dashboard.cpp
//
//  What this file does
//  -------------------
//  A tour of the interface toolkit (widgets.h) in one full-screen program:
//  a live spectrum Plot and Waterfall from a simulated signal, a Meter,
//  Sparkline, BarChart, BigText clock, Leds, Tabs, RadioGroup, Slider,
//  Toggle, Buttons, TextInput, ListBox, Table, a popup Menu and a Dialog.
//  Mouse and keyboard both work everywhere.
//
//      ./bin/example-dashboard [theme]      e.g. amber, cyan, vfd, light
//
//  Keys: Tab / Shift+Tab move the focus, arrows / Enter / Space operate the
//  focused widget, F2 opens the menu, t cycles themes, q (or Esc) quits.
// ============================================================================
#include <cmath>
#include <cstdio>
#include <ctime>
#include <random>
#include <string>

#include "graphicx/widgets.h"

using namespace gfx;
using namespace gfx::ui;

int main(int argc, char** argv) {
    std::vector<std::string> themes = themeNames();
    size_t themeIndex = 0;
    if (argc > 1)
        for (size_t i = 0; i < themes.size(); ++i)
            if (themes[i] == argv[1]) themeIndex = i;

    terminal::Session session;
    session.enableMouse(true);
    App app(theme(themes[themeIndex]));

    // ---- widgets
    Panel top("Graphicx dashboard");
    BigText clock;
    Led ledRun("RUN"), ledRec("REC"), ledWarn("WARN");
    ledRun.on = true;
    Tabs tabs;
    tabs.tabs.push_back("Spectrum");
    tabs.tabs.push_back("Lists");
    tabs.tabs.push_back("About");

    Plot spectrum;
    spectrum.xMin = -1000;
    spectrum.xMax = 1000;
    spectrum.yMin = -100;
    spectrum.yMax = 0;
    spectrum.formatX = [](double v) {
        char b[16];
        std::snprintf(b, sizeof(b), "%+.0fk", v);
        return std::string(b);
    };
    spectrum.formatY = [](double v) {
        char b[16];
        std::snprintf(b, sizeof(b), "%.0f", v);
        return std::string(b);
    };
    Plot::Series line;
    line.fill = true;
    line.useColor = false;
    line.color = rgb(0, 0, 0);
    spectrum.series.push_back(line);
    double tuned = 0;
    Waterfall waterfall;
    waterfall.low = -95;
    waterfall.high = -20;

    Meter meter;
    meter.label = "S";
    meter.min = -100;
    meter.max = 0;
    meter.warnAt = -40;
    meter.badAt = -15;
    meter.scale.push_back(std::make_pair(-100.0, std::string("1")));
    meter.scale.push_back(std::make_pair(-73.0, std::string("5")));
    meter.scale.push_back(std::make_pair(-50.0, std::string("9")));
    meter.scale.push_back(std::make_pair(-30.0, std::string("+20")));
    meter.scale.push_back(std::make_pair(-10.0, std::string("+40")));
    Sparkline spark;
    BarChart bars;
    for (int i = 0; i < 8; ++i) {
        BarChart::Bar b = {std::string(1, static_cast<char>('A' + i)), 0};
        bars.bars.push_back(b);
    }

    Panel controls("Controls");
    RadioGroup mode;
    mode.options.push_back("AM");
    mode.options.push_back("FM");
    mode.options.push_back("USB");
    mode.options.push_back("LSB");
    mode.options.push_back("CW");
    mode.selected = 1;
    Slider volume("Volume", 0, 100, 60);
    volume.step = 1;
    volume.format = [](double v) {
        char b[16];
        std::snprintf(b, sizeof(b), "%.0f%%", v);
        return std::string(b);
    };
    Slider noise("Noise", 0, 20, 6);
    noise.step = 0.5;
    Toggle peak("Peak hold", true);
    TextInput freq("type a frequency, Enter");
    freq.label = "Freq";
    Button menuButton("Menu F2"), dialogButton("Dialog"), quitButton("Quit");
    ProgressBar progress;
    progress.label = "Load";
    Spinner spinner;
    spinner.label = "working";

    ListBox list;
    list.border = true;
    list.title = "Stations";
    const char* stations[] = {"BBC World Service", "Radio Nacional", "Deutsche Welle", "Voice of America",
                              "Radio France Int.", "NHK World", "Radio Australia", "CHU Canada", "WWV Fort Collins",
                              "RTTY Weather", "NAVTEX Niton", "The Buzzer"};
    for (int i = 0; i < 12; ++i) {
        ListBox::Item it = {stations[i], std::to_string(5000 + i * 735) + " kHz", false, rgb(0, 0, 0)};
        list.items.push_back(it);
    }
    list.selected = 0;
    Table table;
    table.title = "Log";
    table.addColumn("Time", 8);
    table.addColumn("Freq", 10, Align::Right);
    table.addColumn("Mode", 5);
    table.addColumn("Notes");
    Label about("", Align::Left);
    about.wrap = true;
    about.text = "Graphicx draws pictures, video and complete user interfaces in a text terminal with "
                 "nothing but C++11. Everything here is a widget from widgets.h: try the mouse (click, drag, "
                 "wheel) and the keyboard (Tab, arrows, Enter, Space, F2). Press t to change the theme.";
    StatusBar status;

    Menu menu;
    menu.title = "Menu";
    menu.addItem("Next theme", "t");
    menu.addItem("Clear waterfall");
    menu.addItem("Show dialog");
    menu.addSeparator();
    menu.addItem("Quit", "q");
    Dialog dialog;
    dialog.title = "Hello";
    dialog.message = "This is a modal dialog. Use the arrows or the mouse, then Enter.";
    dialog.buttons.clear();
    dialog.buttons.push_back("OK");
    dialog.buttons.push_back("Cancel");
    std::string lastDialog = "-";

    // ---- wiring
    app.add(top);
    app.add(clock);
    app.add(ledRun);
    app.add(ledRec);
    app.add(ledWarn);
    app.add(tabs);
    app.add(spectrum);
    app.add(waterfall);
    app.add(list);
    app.add(table);
    app.add(about);
    app.add(meter);
    app.add(spark);
    app.add(bars);
    app.add(controls);
    app.add(mode);
    app.add(volume);
    app.add(noise);
    app.add(peak);
    app.add(freq);
    app.add(menuButton);
    app.add(dialogButton);
    app.add(quitButton);
    app.add(progress);
    app.add(spinner);
    app.add(status);
    app.focus(mode);

    auto nextTheme = [&]() {
        themeIndex = (themeIndex + 1) % themes.size();
        app.setTheme(theme(themes[themeIndex]));
        waterfall.clear();
    };
    auto openMenu = [&]() {
        menu.place(menuButton.bounds.x, menuButton.bounds.y + 1, app.surface().width() > 0 ? rect(0, 0, app.surface().width(), app.surface().height()) : rect(0, 0, 80, 24));
        app.openPopup(menu);
    };
    auto openDialog = [&]() {
        dialog.place(rect(0, 0, app.surface().width(), app.surface().height()), 46);
        dialog.selected = 0;
        app.openPopup(dialog, true);
    };
    menu.onPick = [&](int i) {
        if (i == 0) nextTheme();
        else if (i == 1) waterfall.clear();
        else if (i == 2) openDialog();
        else if (i == 4) app.quit();
    };
    dialog.onClose = [&](int i) { lastDialog = i < 0 ? "Esc" : dialog.buttons[static_cast<size_t>(i)]; };
    menuButton.onPress = openMenu;
    dialogButton.onPress = openDialog;
    quitButton.onPress = [&]() { app.quit(); };
    freq.accept = [](std::uint32_t c) { return (c >= '0' && c <= '9') || c == '.' || c == '-'; };
    freq.onSubmit = [&](const std::string& s) {
        tuned = std::atof(s.c_str());
        tuned = std::max(-1000.0, std::min(1000.0, tuned));
    };
    spectrum.onMouse = [&](double x, double, const Event& e) {
        if (e.button == terminal::MouseButton::Left && e.action != terminal::MouseAction::Release) tuned = x;
        if (e.button == terminal::MouseButton::WheelUp) tuned = std::min(1000.0, tuned + 10);
        if (e.button == terminal::MouseButton::WheelDown) tuned = std::max(-1000.0, tuned - 10);
    };
    waterfall.onMouse = [&](double f, const Event& e) {
        if (e.button == terminal::MouseButton::Left) tuned = -1000 + 2000 * f;
    };
    list.onActivate = [&](int i) {
        std::time_t now = std::time(0);
        char hhmm[16];
        std::strftime(hhmm, sizeof(hhmm), "%H:%M:%S", std::localtime(&now));
        std::vector<std::string> row;
        row.push_back(hhmm);
        row.push_back(list.items[static_cast<size_t>(i)].right);
        row.push_back(mode.options[static_cast<size_t>(mode.selected)]);
        row.push_back(list.items[static_cast<size_t>(i)].text);
        table.rows.insert(table.rows.begin(), row);
    };
    app.bindKey('q', [&]() { app.quit(); });
    app.bindKey(terminal::KeyEscape, [&]() { app.quit(); });
    app.bindKey('t', nextTheme);
    app.bindKey(terminal::KeyF2, openMenu);

    app.onLayout = [&](const Rect& screen) {
        std::vector<Rect> rows = splitRows(screen, std::vector<Size>{fixed(5), flex(), fixed(1)});
        top.bounds = rows[0];
        Rect head = inset(rows[0], 1);
        clock.bounds = rect(head.x + 1, head.y, 30, 3);
        ledRun.bounds = rect(head.right() - 30, head.y, 8, 1);
        ledRec.bounds = rect(head.right() - 20, head.y, 8, 1);
        ledWarn.bounds = rect(head.right() - 10, head.y, 9, 1);
        tabs.bounds = rect(head.right() - 36, head.y + 2, 36, 1);
        std::vector<Rect> cols = splitColumns(rows[1], std::vector<Size>{flex(), fixed(40)}, 1);
        Rect main = cols[0];
        std::vector<Rect> mainRows = splitRows(main, std::vector<Size>{flex(), flex(), fixed(2), fixed(4)});
        spectrum.bounds = mainRows[0];
        waterfall.bounds = rect(mainRows[1].x + 7, mainRows[1].y, std::max(0, mainRows[1].width - 7), mainRows[1].height);
        list.bounds = rect(main.x, main.y, main.width / 2, main.height - 6);
        table.bounds = rect(main.x + main.width / 2 + 1, main.y, main.width - main.width / 2 - 1, main.height - 6);
        about.bounds = inset(rect(main.x, main.y, main.width, main.height - 6), 2);
        meter.bounds = mainRows[2];
        std::vector<Rect> low = splitColumns(mainRows[3], std::vector<Size>{flex(), fixed(30)}, 2);
        spark.bounds = low[0];
        bars.bounds = low[1];
        controls.bounds = cols[1];
        Rect in = inset(cols[1], 2);
        mode.bounds = rect(in.x, in.y, in.width, 1);
        volume.bounds = rect(in.x, in.y + 2, in.width, 1);
        noise.bounds = rect(in.x, in.y + 4, in.width, 1);
        peak.bounds = rect(in.x, in.y + 6, in.width, 1);
        freq.bounds = rect(in.x, in.y + 8, in.width, 1);
        menuButton.bounds = rect(in.x, in.y + 10, 11, 1);
        dialogButton.bounds = rect(in.x + 12, in.y + 10, 10, 1);
        quitButton.bounds = rect(in.x + 23, in.y + 10, 8, 1);
        progress.bounds = rect(in.x, in.y + 12, in.width, 1);
        spinner.bounds = rect(in.x, in.y + 14, in.width, 1);
        status.bounds = rows[2];
    };

    // ---- simulation
    std::mt19937 rng(7);
    std::normal_distribution<double> gauss(0, 1);
    double phase = 0;
    const int bins = 512;
    std::vector<float> spec(bins);
    app.run(session, 30, [&]() {
        phase += 0.03;
        // A simulated band: noise floor + a few carriers + a drifting one,
        // and the tuned signal at `tuned`.
        double floorDb = -90 + noise.value;
        for (int i = 0; i < bins; ++i) {
            double f = -1000 + 2000.0 * i / bins;
            double p = std::pow(10, (floorDb + 3 * gauss(rng)) / 10);
            const double carriers[4] = {-620, -300, 250 + 80 * std::sin(phase), 700};
            for (int k = 0; k < 4; ++k) p += std::pow(10, (-35 - 8 * k) / 10.0) * std::exp(-std::pow((f - carriers[k]) / 6, 2));
            p += std::pow(10, -25 / 10.0) * std::exp(-std::pow((f - tuned) / 4, 2)) * (0.6 + 0.4 * std::sin(phase * 3));
            spec[static_cast<size_t>(i)] = static_cast<float>(10 * std::log10(p));
        }
        std::vector<double>& y = spectrum.series[0].y;
        y.assign(spec.begin(), spec.end());
        spectrum.bands.clear();
        Plot::BandArea band = {tuned - 30, tuned + 30, lerp(app.theme().background, app.theme().accent, 0.25)};
        spectrum.bands.push_back(band);
        spectrum.markers.clear();
        Plot::Marker mk = {tuned, app.theme().accent, std::string()};
        spectrum.markers.push_back(mk);
        waterfall.markers.clear();
        Plot::Marker wm = {(tuned + 1000) / 2000, app.theme().accent, std::string()};
        waterfall.markers.push_back(wm);
        if (tabs.selected == 0) waterfall.addRow(spec);

        int at = static_cast<int>((tuned + 1000) / 2000 * bins);
        double level = spec[static_cast<size_t>(std::max(0, std::min(bins - 1, at)))];
        if (peak.checked) meter.update(level, 0.7);
        else {
            meter.value = level;
            meter.peak = std::nan("");
        }
        char vt[32];
        std::snprintf(vt, sizeof(vt), "%5.1f dB", level);
        meter.valueText = vt;
        spark.push(level);
        for (size_t i = 0; i < bars.bars.size(); ++i) bars.bars[i].value = 50 + 40 * std::sin(phase * (1 + i * 0.3));
        ledRec.on = static_cast<int>(phase * 2) % 2 == 0;
        ledWarn.on = level > meter.warnAt;
        ledWarn.useColor = true;
        ledWarn.color = app.theme().warn;
        progress.value = std::fmod(phase / 3, 1.0);

        std::time_t now = std::time(0);
        char hms[16];
        std::strftime(hms, sizeof(hms), "%H:%M:%S", std::localtime(&now));
        clock.text = hms;

        bool spectrumTab = tabs.selected == 0;
        spectrum.visible = waterfall.visible = meter.visible = spark.visible = bars.visible = spectrumTab;
        list.visible = table.visible = tabs.selected == 1;
        about.visible = tabs.selected == 2;

        status.clear();
        status.addLeft("theme " + app.theme().name, app.theme().accentText, app.theme().accent);
        char tb[48];
        std::snprintf(tb, sizeof(tb), "tuned %+.0f  vol %.0f%%", tuned, volume.value);
        status.addLeft(tb);
        status.addLeft("dialog: " + lastDialog);
        status.addRight("Tab focus  F2 menu  t theme  q quit");
    });
    return 0;
}
