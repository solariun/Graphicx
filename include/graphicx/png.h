// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   include/graphicx/png.h
//
//  What this file does
//  -------------------
//  Writes a gfx::Image as a PNG file, with no library (no zlib): its own
//  deflate compressor (LZ77 with a hash table + the fixed Huffman codes),
//  the PNG row filters (the best of the five per row), CRC-32 and Adler-32.
//  Grey images (every pixel r = g = b) are written as 8-bit greyscale, a
//  third of the size.
//
//      gfx::Image chart(1809, 1200);
//      ...
//      std::string error;
//      if (!gfx::savePng("chart.png", chart, error)) std::cerr << error;
//
//  How it fits in
//  --------------
//  Independent of the rest (only needs graphicx.h's Image).  Reading PNG
//  (and JPEG, GIF ...) is image_io.h's loadImage().
//
//  Contributing
//  ------------
//  Compression favours speed and simplicity over the last few percent:
//  fixed Huffman codes only.  Files open in every viewer (checked against
//  stb_image in tests/test_ui.cpp).
// ============================================================================
#ifndef GRAPHICX_PNG_H
#define GRAPHICX_PNG_H

#include <string>

#include "graphicx.h"

namespace gfx {

// The PNG file's bytes for `image` (empty for an empty image).
std::string encodePng(const Image& image);
// Writes `image` to `path`.  false + error on failure.
bool savePng(const std::string& path, const Image& image, std::string& error);

// zlib-format compression (RFC 1950/1951) used by encodePng, for anyone who
// needs it (e.g. other file formats).
std::string zlibCompress(const unsigned char* data, size_t size);

}  // namespace gfx

#endif
