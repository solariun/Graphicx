# Graphicx

Draw pictures, animations and video in a text terminal, in C++11, with no
dependencies.

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

Build them with `make examples`. They become `bin/example-hello`,
`bin/example-show-image` and `bin/example-animation`.

---

## Project layout

```
include/graphicx/graphicx.h   core API (Image, Options, render, Screen, palette)
include/graphicx/image_io.h   loadImage (stb_image)
include/graphicx/terminal.h   size, full-screen Session, keys
src/                          implementations
tools/                        gfx-convert, gfx-play
examples/                     hello, show-image, animation
tests/test_graphicx.cpp       unit tests (make test)
third_party/stb_image.h       image decoder (public domain / MIT, by Sean Barrett)
```

## Contributing

- **Code:** C++11 only. Keep `graphicx.h` free of platform code. Every file
  starts with a header explaining what it does and how it fits in.
- **Tests:** add a `CHECK` in `tests/test_graphicx.cpp` for every behavior
  you add or fix. `make test` must pass. The tests need no terminal or
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
