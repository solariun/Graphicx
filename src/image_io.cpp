// ============================================================================
//  Graphicx - draw pictures, animations and video in a text terminal.
//  C++11, no dependencies (the optional image loader bundles stb_image).
//  https://github.com/solariun/Graphicx
// ----------------------------------------------------------------------------
//  File:   src/image_io.cpp
//  Layer:  Image loading (optional part of the library)
//
//  What this file does
//  -------------------
//  Implements gfx::loadImage() / loadImageFromMemory() on top of stb_image:
//  decodes to 4 channels (RGBA), blends alpha onto the background color and
//  copies the result into a gfx::Image.
//
//  How it fits in
//  --------------
//  This is the only file that compiles third_party/stb_image.h (with
//  STB_IMAGE_IMPLEMENTATION).  Build it with -isystem third_party so the
//  decoder's own code does not trigger your project's warnings.
//
//  Contributing
//  ------------
//  stb_image returns 8 bits per channel for every format it supports
//  (16-bit PNGs and HDR are converted), so nothing else needs to change when
//  stb_image is updated.
// ============================================================================
#include "graphicx/image_io.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG  // readable error messages
#include "stb_image.h"

namespace gfx {

namespace {

// Wraps stb_image's result: RGBA pixels -> Image, alpha blended.
Image fromStb(unsigned char* pixels, int width, int height, Rgb background, const std::string& what) {
    if (!pixels) {
        const char* reason = stbi_failure_reason();
        throw ImageError(what + ": " + (reason ? reason : "cannot decode"));
    }
    Image image(width, height);
    const unsigned char* p = pixels;
    for (int y = 0; y < height; ++y) {
        Rgb* out = image.row(y);
        for (int x = 0; x < width; ++x, p += 4) {
            unsigned a = p[3];
            out[x] = rgb(static_cast<std::uint8_t>((p[0] * a + background.r * (255 - a) + 127) / 255),
                         static_cast<std::uint8_t>((p[1] * a + background.g * (255 - a) + 127) / 255),
                         static_cast<std::uint8_t>((p[2] * a + background.b * (255 - a) + 127) / 255));
        }
    }
    stbi_image_free(pixels);
    return image;
}

}  // namespace

Image loadImage(const std::string& path, Rgb background) {
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    return fromStb(pixels, width, height, background, path);
}

Image loadImageFromMemory(const void* data, size_t size, Rgb background) {
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &width,
                                                  &height, &channels, 4);
    return fromStb(pixels, width, height, background, "image in memory");
}

}  // namespace gfx
