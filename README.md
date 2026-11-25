# Graphicx

Draw pictures, animations, video **and complete user interfaces** in a text
terminal, in C++11, with no dependencies.

Graphicx has three layers; use only the ones you need:

| Layer | Header | What it gives you |
|-------|--------|-------------------|
| Pictures | `graphicx.h` | any `gfx::Image` as terminal text (half blocks, ASCII, colored ASCII), `gfx::Screen` for 60 fps video with only the changed cells sent |
| Drawing | `canvas.h`, `color.h` | a pixel canvas: lines (anti-aliased, thick, dashed), rectangles, rounded rectangles, circles, ellipses, arcs, pies, polygons, gradients, images, two bitmap fonts; colors, colormaps (Turbo, Inferno, Viridis ...) and themes |
| Interfaces | `ui.h`, `widgets.h` | a cell surface with boxes, text and pictures in half blocks, quadrants or braille; a diff renderer; an `App` with focus, mouse and keys; ~25 widgets: buttons, sliders, radio groups, text inputs, lists, tables, tabs, menus, dialogs, meters, sparklines, bar charts, plots, waterfalls, big text |

```
********************++++++++++++++++++++++++====================
******************++++++++++++++++++++++++======================
*****************++++++++++++++++++++++++=======================
***************++++++++++++++++++++++++=========================
*************+++++++++++++++++++++++++========================--
************++++++++++++++++++++++++========================----
```

(`examples/hello.cpp`, ASCII style.)

Graphicx turns any picture (photo, game screen, video frame, generated
image) into terminal text, in one of three styles:

| Style | Looks like | Use it for |
|-------|------------|------------|
| `Blocks` (default) | `▀` half blocks with a foreground **and** a background color: 2 pixels per character, full color | the best picture |
| `AsciiColor` | ASCII characters (` .:-=+*#%@`) chosen by brightness, each in its pixels' color, no background | a retro look that keeps the colors |
| `Ascii` | the same characters with **no escape codes at all** | plain text: files, e-mail, any terminal, logs |

Colors are sent as the xterm 256-color palette (works almost everywhere) or
as 24-bit truecolor.

Graphicx was extracted from [TBoyEmu](https://github.com/solariun/TBoyEmu), a
Game Boy / GBA emulator that runs in the terminal, which uses it for every
frame.

---

## Quick start

```bash
make            # library + tools + examples
make test       # unit tests
./bin/gfx-convert photo.jpg                 # show a picture
./bin/gfx-convert -s ascii -w 80 -o photo.txt photo.jpg
./bin/gfx-play movie.mp4                    # play a video (needs ffmpeg)
./bin/example-animation                     # a full-screen animation
```

A complete program:

```cpp
#include <graphicx/graphicx.h>
#include <graphicx/image_io.h>
#include <iostream>

int main() {
    gfx::Image photo = gfx::loadImage("cat.jpg");   // JPG, PNG, BMP, GIF...
    gfx::Options options;
    options.columns = 80;                           // 80 characters wide
    std::cout << gfx::render(photo, options);
}
```

```bash
c++ -std=c++11 -Igraphicx/include main.cpp graphicx/build/libgraphicx.a -o show
```

Requirements: a C++11 compiler. The core has no dependencies. Image loading
bundles stb_image. The terminal helpers need a POSIX system (macOS, Linux).
The video player needs `ffmpeg` and `ffprobe` installed.

---

## Using Graphicx in your project

Pick one:

1. **Copy the files.** Add `include/` to your include path and compile the
   `.cpp` files you need:
   - `src/graphicx.cpp`: always (the core).
   - `src/image_io.cpp`: to load picture files. Add `-isystem third_party`.
   - `src/terminal.cpp`: for full-screen mode, terminal size and keys.
2. **Static library.** Run `make lib`, then build with
   `-I<path>/include` and link `<path>/build/libgraphicx.a`.
3. **Git submodule.** This is how TBoyEmu uses it:
   ```bash
   git submodule add https://github.com/solariun/Graphicx.git external/Graphicx
   ```
   Then compile `external/Graphicx/src/graphicx.cpp` with
   `-Iexternal/Graphicx/include`.

---

## API

Everything is in namespace `gfx`. The headers are fully commented; this is
the overview.

### Pictures: `gfx::Image`, `gfx::Rgb`

```cpp
gfx::Image img(320, 200);                        // black 320 x 200
img.set(10, 20, gfx::rgb(255, 128, 0));          // one pixel
gfx::Rgb c = img.get(10, 20);
gfx::Rgb* line = img.row(20);                    // fast access to a row

gfx::Image a = gfx::Image::fromRgb24(bytes, w, h);     // R,G,B bytes (ffmpeg rgb24)
gfx::Image b = gfx::Image::fromRgba32(bytes, w, h);    // R,G,B,A bytes
gfx::Image c = gfx::Image::fromRgb555(words, w, h);    // 15-bit (Game Boy Color, GBA)
gfx::Image d = gfx::loadImage("file.png");             // image_io.h; throws gfx::ImageError
```

### Options: `gfx::Options`

| Field | Default | Meaning |
|-------|---------|---------|
| `style` | `Style::Blocks` | `Blocks`, `AsciiColor` or `Ascii` |
| `colorMode` | `ColorMode::Palette256` | `Palette256` or `TrueColor` |
| `filter` | `Filter::Box` | `Box` averages (photos, video). `Nearest` keeps crisp pixels (pixel art) |
| `columns`, `rows` | `0`, `0` | size in characters. Both 0: 1 column per pixel. One set: the other follows the aspect ratio. Both set: exact size |
| `ramp` | `" .:-=+*#%@"` | ASCII characters, emptiest first |
| `invert` | `false` | ASCII for light terminal backgrounds |
| `contrast` | `0` | contrast change: `0.10` = +10%, `-0.10` = -10%, `-1` = flat gray (every style) |
| `brightness` | `0` | brightness change: `0.10` = +10%, `-0.10` = -10%, `1` = white, `-1` = black (applied after contrast) |

Helpers:
- `gfx::fit(w, h, maxColumns, maxRows, columns, rows)`: the largest size
  that fits, keeping proportions.
- `gfx::gridSize(...)`: the size render() will use.
- `gfx::styleName()` / `gfx::parseStyle()`: convert a style to and from
  `"blocks"`, `"ascii-color"` or `"ascii"`.

A terminal character is about twice as tall as it is wide, so one character
stands for 1×2 pixels. That's why a 160×144 picture becomes 160×72
characters.

### One picture: `gfx::render()`

```cpp
std::string text = gfx::render(image, options);
```

It returns complete lines with no cursor movement. You can print the text,
save it to a file, or `cat` it later. Each line ends with a color reset and
`\n`. `Style::Ascii` output is pure text.

### Animation and video: `gfx::Screen`

```cpp
gfx::Screen screen(options);
screen.setOrigin(1, 1);             // terminal row / column of the top-left corner
for (;;) {
    update(frame);                  // your gfx::Image
    std::string out = screen.draw(frame);  // only the cells that changed
    write(1, out.data(), out.size());
}
```

- **Diffing:** `Screen` remembers what each cell shows and sends only the
  differences, which makes 60 fps practical.
- **Synchronized output:** each frame is wrapped in `CSI ?2026h … CSI ?2026l`,
  so terminals that support it show the frame all at once. Turn this off
  with `setSynchronized(false)` if you add your own text to the frame.
- **`invalidate()`:** forces a full repaint, e.g. after you clear the screen.
- **`invalidateCells(row, col, rows, cols)`:** repaints a rectangle, e.g.
  after drawing a message over the picture.
- **`draw(frame, out)`:** appends to a string you reuse, so there's no
  allocation per frame.

`Screen` never writes to the terminal itself. Send the bytes wherever you
want: stdout, a socket, or a file to replay later (TBoyEmu records "movies"
this way).

### Palette: `paletteColor`, `nearestPaletteIndex`, `brightness`

- `paletteColor(i)`: the RGB value of xterm palette entry 16–255.
- `nearestPaletteIndex(rgb)`: the closest entry. It's exact, and fast
  thanks to a ~2 MB table built once, on first use (about 0.2 s).
- `brightness(rgb)`: perceived brightness, 0–255 (Rec. 601).

Entries 0–15 are never used, because every terminal theme changes them.

### Terminal helpers: `graphicx/terminal.h`

```cpp
gfx::terminal::Size s = gfx::terminal::size();        // columns, rows
gfx::terminal::Session session;     // alternate screen, hidden cursor, keys without Enter
session.write(text);
int key = session.readKey(16);      // waits up to 16 ms; 'q', KeyEscape, KeyUp, ...
if (gfx::terminal::Session::resized()) { /* the terminal changed size */ }
```

The terminal is restored when the `Session` object goes away.

Full input for interactive programs: keys with modifiers, function keys,
Unicode, and the mouse.

```cpp
session.enableMouse(true);                 // clicks, drags, wheel (SGR 1006)
gfx::terminal::Event e;
if (session.readEvent(e, 16)) {
    if (e.type == gfx::terminal::EventType::Key && e.key == gfx::terminal::KeyF5) { ... }
    if (e.type == gfx::terminal::EventType::Mouse && e.button == gfx::terminal::MouseButton::Left)
        click(e.x, e.y);                    // 0-based cell
    if (e.type == gfx::terminal::EventType::Resize) relayout();
}
```

`gfx::terminal::parseEvent(bytes, event)` parses raw input yourself (it is what
`readEvent` uses).

### Drawing: `graphicx/canvas.h` and `graphicx/color.h`

```cpp
gfx::Image img(160, 96);
gfx::Canvas c(img);
c.clear(gfx::hex(0x101018));
c.fillCircle(40, 48, 30, gfx::hex(0xffb000));
c.lineAA(0, 95, 159, 0, gfx::hex(0x40d0ff));          // anti-aliased
c.thickLine(10, 10, 150, 20, 3, gfx::hex(0xffffff));
c.fillRoundRect(gfx::rect(90, 40, 60, 30), 6, gfx::hex(0x3080ff));
c.pie(120, 60, 20, 0, 270, gfx::hex(0xff4060));        // degrees, counter-clockwise
c.fillPolygon(points, gfx::hex(0x40e080));             // even-odd rule
c.gradient(gfx::rect(0, 90, 160, 6), gfx::Colormap::Turbo);
c.text(4, 4, "HELLO", gfx::hex(0xffffff), gfx::Font::Font5x7, 2);   // scale 2
c.scroll(gfx::rect(0, 0, 160, 96), 0, 1, gfx::hex(0));               // waterfalls
std::cout << gfx::render(img);
```

Everything is clipped to the image and to `setClip()`. Colors:
`gfx::hex`, `gfx::hsv`, `gfx::lerp`, `gfx::blend`, `gfx::scale`;
`gfx::colormap(map, t)` for `Classic`, `Turbo`, `Inferno`, `Viridis`, `Ice`,
`Grey`, `Amber`, `Green`, `Hot`; `gfx::theme(name)` for the interface themes
`amber`, `orange`, `lime`, `green`, `cyan`, `vfd`, `blue`, `violet`, `rose`,
`mono`, `light`.

### Interfaces: `graphicx/ui.h`

The cell layer. A `Surface` is what the screen should show; a `Painter`
draws on part of it (clipped, local coordinates); a `Display` sends only the
cells that changed.

```cpp
gfx::ui::Surface surface(80, 24);
gfx::ui::Painter p(surface);
p.fill(gfx::hex(0x101010));
p.box(gfx::rect(0, 0, 80, 24), gfx::hex(0xffb000), gfx::ui::Border::Rounded, " Title ");
p.text(2, 2, "Bold text", gfx::hex(0xffffff), gfx::ui::Bold);
p.bar(gfx::rect(2, 4, 30, 1), 0.65, gfx::hex(0x40d060), gfx::hex(0x202020));  // 1/8-cell steps
p.image(gfx::rect(2, 6, 40, 12), img, gfx::ui::PixelMode::Braille);
gfx::ui::Display display;          // truecolor; Palette256 for older terminals
std::string out;
display.render(surface, out);      // only what changed since the last call
session.write(out);
```

| `PixelMode` | Pixels per cell | Best for |
|-------------|-----------------|----------|
| `HalfBlocks` | 1 x 2, two exact colors | photos, video, waterfalls |
| `Quadrants` | 2 x 2, two colors | sharper shapes, big text |
| `Braille` | 2 x 4, two colors | line art, plots, fine detail |
| `Ascii` | 1 x 2, one color | retro look |

`DotCanvas` plots braille dots directly (2x4 per cell, drawn over the
background that is there): the basis of the charts. Layout helpers split a
rectangle into rows or columns: `splitRows(r, {fixed(3), flex(), percent(25)}, gap)`.

### Widgets: `graphicx/widgets.h`

```cpp
gfx::terminal::Session session;
session.enableMouse(true);
gfx::ui::App app(gfx::theme("amber"));

gfx::ui::Slider volume("Volume", 0, 100, 50);
gfx::ui::Button quit("Quit");
quit.onPress = [&]() { app.quit(); };
app.add(volume);
app.add(quit);
app.bindKey('q', [&]() { app.quit(); });
app.onLayout = [&](const gfx::Rect& screen) {      // called on every resize
    volume.bounds = gfx::rect(2, 2, screen.width - 4, 1);
    quit.bounds = gfx::rect(2, 4, 10, 1);
};
app.run(session, 30, [&]() { /* update values before each frame */ });
```

The `App` keeps the focus (Tab / Shift+Tab, or a click), sends keys to the
focused widget and then to `bindKey` bindings, sends mouse events to the
widget under the pointer (drags and the release go to the widget that got
the press), and draws popups (`openPopup`, for menus and modal dialogs) on
top. Programs with their own loop call `app.dispatch(event)` and
`app.frame(out)`.

| Widget | What it is |
|--------|------------|
| `Label`, `Panel`, `Separator`, `StatusBar` | text, titled frames, lines, a status row with left / right segments |
| `Led`, `Spinner`, `ProgressBar` | on/off light, activity spinner, smooth progress bar |
| `Meter` | level meter with zones, peak hold and a scale (S-meters, VU) |
| `Sparkline`, `BarChart` | small history graph, vertical bars with labels |
| `Plot` | line / area chart in braille with axes, grid, several series, markers and shaded bands; peak-preserving for spectra; `onMouse(x, y, event)` gives the value under the pointer |
| `Waterfall` | scrolling spectrogram with a colormap, markers, `onMouse(fraction, event)` |
| `BigText` | large text from the built-in fonts (e.g. a frequency display) |
| `ImageView` | any `gfx::Image`, aspect kept |
| `Button`, `Toggle`, `RadioGroup` | push / toggle buttons, check boxes, segmented choices |
| `Slider`, `TextInput` | value with keys, wheel and drag; one-line editor with filter and cursor |
| `ListBox`, `Table`, `Tabs` | scrolling lists and tables with selection, tab bar |
| `Menu`, `Dialog` | popup menu with shortcuts and check marks, modal message box |

Every widget takes its colors from the `Theme`, so every theme works
everywhere, and everything clickable also works from the keyboard.

---

## Tools

### gfx-convert: a picture in the terminal, or saved as text

```
gfx-convert [options] IMAGE
  -s, --style S       blocks (default) | ascii-color | ascii
  -w, --width N       width in characters (default: terminal width, 80 if not a terminal)
  -H, --height N      height in lines (default: keeps the proportions)
      --fit           fit width AND height in the terminal
      --truecolor     24-bit colors
      --nearest       crisp resizing for pixel art
      --invert        ASCII styles on light backgrounds
      --contrast P    contrast change, e.g. 10% or -10%
      --brightness P  brightness change, e.g. 10% or -10%
      --ramp CHARS    ASCII characters from empty to dense
  -o, --output FILE   save instead of printing
```

### gfx-play: a demo video player (picture only, no sound)

```
gfx-play [options] VIDEO
  -s, --style S      blocks (default) | ascii-color | ascii
      --truecolor    24-bit colors
      --invert       ASCII styles on light backgrounds
      --contrast P   contrast change, e.g. 10% or -10%
      --brightness P brightness change, e.g. 10% or -10%
      --loop         start again at the end
      --ffmpeg PATH / --ffprobe PATH   programs to use (default: from the PATH)
keys: space = pause / resume, q or Esc = quit
```

- **Install ffmpeg:** `brew install ffmpeg` (macOS) or `apt install ffmpeg`
  (Debian/Ubuntu).
- **Decoding:** ffmpeg decodes the video and scales it to exactly the
  character grid that fits your terminal.
- **Timing:** frames play at the video's real frame rate. When the terminal
  can't keep up, frames are dropped rather than slowing down.
- **Resizing:** resizing the terminal restarts ffmpeg at the same position
  with the new size.
- **Speed:** full-color blocks in a big window can need several MB/s of
  text. If a video drops frames, use a smaller window or an ASCII style.

---

## Examples

| File | Shows |
|------|-------|
| `examples/hello.cpp` | build a picture in memory and print it in all three styles |
| `examples/show-image.cpp` | load a file and fill the terminal with it |
| `examples/animation.cpp` | 30 fps full-screen animation with `gfx::Screen`; keys 1/2/3 switch style, q quits |
| `examples/canvas.cpp` | every canvas primitive, printed in the four pixel modes |
| `examples/dashboard.cpp` | a full interactive interface with every widget: live spectrum and waterfall, meters, controls, lists, a menu and a dialog; `t` changes the theme |

Build them with `make examples`. They become `bin/example-hello`,
`bin/example-show-image`, `bin/example-animation`, `bin/example-canvas` and
`bin/example-dashboard`.

---

## Project layout

```
include/graphicx/graphicx.h   core API (Image, Options, render, Screen, palette)
include/graphicx/image_io.h   loadImage (stb_image)
include/graphicx/terminal.h   size, full-screen Session, keys, mouse, events
include/graphicx/color.h      colors, colormaps, themes
include/graphicx/canvas.h     pixel drawing (shapes, lines, fonts, images)
include/graphicx/ui.h         cell surface, painter, braille dots, display, layout
include/graphicx/widgets.h    App and widgets
src/                          implementations (font_data.h: the bitmap fonts)
tools/                        gfx-convert, gfx-play
examples/                     hello, show-image, animation, canvas, dashboard
tests/test_graphicx.cpp       unit tests of the picture layer (make test)
tests/test_ui.cpp             unit tests of drawing, interface, widgets and input
third_party/stb_image.h       image decoder (public domain / MIT, by Sean Barrett)
```

## Contributing

- **Code:** C++11 only. Keep `graphicx.h` free of platform code. Every file
  starts with a header explaining what it does and how it fits in.
- **Tests:** add a `CHECK` in `tests/test_graphicx.cpp` (pictures) or
  `tests/test_ui.cpp` (drawing, interface) for every behavior you add or fix. `make test` must pass. The tests need no terminal or
  files.
- **Speed:** `Screen::draw()` runs once per video frame. Keep it free of
  allocations.

## Third-party code

`third_party/stb_image.h` (v2.34) is by Sean Barrett,
https://github.com/nothings/stb. It's dual-licensed as public domain or MIT;
see the end of the file.

## License

Graphicx is released under the [MIT License](LICENSE): use it in any
project, open or closed source, as long as the copyright and license notice
stay with it. The bundled stb_image is public domain or MIT (your choice).
