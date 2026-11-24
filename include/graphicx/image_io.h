// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  C++11, no dependencies (the optional image loader bundles stb_image).
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   include/graphicx/image_io.h
//  Layer:  Image loading (optional part of the library)
//
//  What this file does
//  -------------------
//  Reads picture files into a gfx::Image: JPEG, PNG, BMP, GIF (first frame),
//  TGA, PSD (composited), HDR, PIC and PNM, via the bundled public-domain
//  stb_image decoder (third_party/stb_image.h).
//
//      gfx::Image photo = gfx::loadImage("photo.jpg");   // throws on error
//
//  How it fits in
//  --------------
//  Separate from graphicx.h so programs that never read files (games,
//  emulators, video players that get frames from elsewhere) don't compile
//  or link the decoder.  Implementation: src/image_io.cpp.
//
//  Contributing
//  ------------
//  Do not edit third_party/stb_image.h; update it as a whole from
//  https://github.com/nothings/stb when needed.  Transparent pixels are
//  blended onto the `background` color (black by default).
// ============================================================================
#ifndef GRAPHICX_IMAGE_IO_H
#define GRAPHICX_IMAGE_IO_H

#include <stdexcept>
#include <string>

#include "graphicx.h"

namespace gfx {

// Thrown by loadImage() with a readable message ("cannot open ...",
// "unsupported format", ...).
class ImageError : public std::runtime_error {
public:
    explicit ImageError(const std::string& message) : std::runtime_error(message) {}
};

// Loads a picture file.  Transparent areas are blended onto `background`.
// Throws gfx::ImageError when the file cannot be read or decoded.
Image loadImage(const std::string& path, Rgb background = rgb(0, 0, 0));

// Same, from a file already in memory (e.g. downloaded or embedded).
Image loadImageFromMemory(const void* data, size_t size, Rgb background = rgb(0, 0, 0));

}  // namespace gfx

#endif
