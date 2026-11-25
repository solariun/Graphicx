// ============================================================================
//  Graphicx - draw pictures, animations, video and user interfaces in a
//  text terminal.  C++11.  https://github.com/solariun/Graphicx
//  SPDX-License-Identifier: MIT
// ----------------------------------------------------------------------------
//  File:   src/png.cpp
//
//  What this file does
//  -------------------
//  Implementation of png.h.
//
//  Deflate (RFC 1951) as written here:
//    * one block, type 1 (fixed Huffman codes), final;
//    * LZ77: a 32 KB window, a hash of the next 3 bytes points at the most
//      recent position with that hash, plus a chain of up to 32 earlier ones;
//      the longest match (3..258) is taken greedily.
//  PNG (ISO/IEC 15948): signature, IHDR (8-bit, colour type 2 RGB or 0
//  grey), one IDAT, IEND.  Each row is filtered with the filter (None, Sub,
//  Up, Average, Paeth) that gives the smallest sum of absolute values.
// ============================================================================
#include "graphicx/png.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace gfx {

namespace {

// ---- bit writer (deflate writes bits least significant first)
struct BitWriter {
    std::string out;
    unsigned acc;
    int n;
    BitWriter() : acc(0), n(0) {}
    void bits(unsigned value, int count) {
        acc |= value << n;
        n += count;
        while (n >= 8) {
            out.push_back(static_cast<char>(acc & 0xFF));
            acc >>= 8;
            n -= 8;
        }
    }
    // Huffman codes are sent most significant bit first.
    void code(unsigned code, int length) {
        unsigned r = 0;
        for (int i = 0; i < length; ++i) r |= ((code >> i) & 1u) << (length - 1 - i);
        bits(r, length);
    }
    void flush() {
        if (n > 0) out.push_back(static_cast<char>(acc & 0xFF));
        acc = 0;
        n = 0;
    }
};

// Fixed Huffman code of a literal/length symbol (0..287).
void literal(BitWriter& w, int sym) {
    if (sym <= 143) w.code(0x30 + sym, 8);
    else if (sym <= 255) w.code(0x190 + (sym - 144), 9);
    else if (sym <= 279) w.code(sym - 256, 7);
    else w.code(0xC0 + (sym - 280), 8);
}

const int kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                           1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void match(BitWriter& w, int length, int distance) {
    int li = 28;
    while (kLenBase[li] > length) --li;
    literal(w, 257 + li);
    if (kLenExtra[li]) w.bits(static_cast<unsigned>(length - kLenBase[li]), kLenExtra[li]);
    int di = 29;
    while (kDistBase[di] > distance) --di;
    w.code(static_cast<unsigned>(di), 5);  // fixed distance codes: 5 bits
    if (kDistExtra[di]) w.bits(static_cast<unsigned>(distance - kDistBase[di]), kDistExtra[di]);
}

unsigned adler32(const unsigned char* d, size_t n) {
    unsigned a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) {
        a = (a + d[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

unsigned crc32(const unsigned char* d, size_t n, unsigned crc = 0) {
    static unsigned table[256];
    static bool ready = false;
    if (!ready) {
        for (unsigned i = 0; i < 256; ++i) {
            unsigned c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::string& s, unsigned v) {
    s.push_back(static_cast<char>((v >> 24) & 0xFF));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>(v & 0xFF));
}

void chunk(std::string& png, const char* type, const std::string& data) {
    put32(png, static_cast<unsigned>(data.size()));
    std::string body(type, 4);
    body += data;
    png += body;
    put32(png, crc32(reinterpret_cast<const unsigned char*>(body.data()), body.size()));
}

int paeth(int a, int b, int c) {
    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

}  // namespace

std::string zlibCompress(const unsigned char* data, size_t size) {
    BitWriter w;
    w.out.push_back(static_cast<char>(0x78));  // deflate, 32 KB window
    w.out.push_back(static_cast<char>(0x01));  // check bits (0x7801 % 31 == 0), fastest
    w.bits(1, 1);                              // final block
    w.bits(1, 2);                              // fixed Huffman
    const int kHash = 1 << 15, kWindow = 32768, kChain = 32;
    std::vector<int> head(kHash, -1), prev(size > 0 ? size : 1, -1);
    size_t i = 0;
    while (i < size) {
        int bestLen = 0, bestDist = 0;
        if (i + 3 <= size) {
            unsigned h = ((data[i] << 10) ^ (data[i + 1] << 5) ^ data[i + 2]) & (kHash - 1);
            int cand = head[h];
            for (int c = 0; c < kChain && cand >= 0 && static_cast<int>(i) - cand <= kWindow; ++c) {
                int len = 0;
                const size_t maxLen = std::min<size_t>(258, size - i);
                while (static_cast<size_t>(len) < maxLen && data[cand + len] == data[i + len]) ++len;
                if (len > bestLen) {
                    bestLen = len;
                    bestDist = static_cast<int>(i) - cand;
                    if (len == 258) break;
                }
                cand = prev[static_cast<size_t>(cand)];
            }
        }
        size_t step = bestLen >= 3 ? static_cast<size_t>(bestLen) : 1;
        if (bestLen >= 3) match(w, bestLen, bestDist);
        else literal(w, data[i]);
        // Index every position passed over.
        for (size_t k = 0; k < step && i + k + 3 <= size; ++k) {
            unsigned h = ((data[i + k] << 10) ^ (data[i + k + 1] << 5) ^ data[i + k + 2]) & (kHash - 1);
            prev[i + k] = head[h];
            head[h] = static_cast<int>(i + k);
        }
        i += step;
    }
    literal(w, 256);  // end of block
    w.flush();
    put32(w.out, adler32(data, size));
    return w.out;
}

std::string encodePng(const Image& image) {
    if (image.empty()) return std::string();
    const int width = image.width(), height = image.height();
    bool grey = true;
    for (int y = 0; y < height && grey; ++y) {
        const Rgb* row = image.row(y);
        for (int x = 0; x < width; ++x)
            if (row[x].r != row[x].g || row[x].g != row[x].b) {
                grey = false;
                break;
            }
    }
    const int bpp = grey ? 1 : 3;
    const size_t stride = static_cast<size_t>(width) * bpp;
    std::vector<unsigned char> raw(static_cast<size_t>(height) * (stride + 1));
    std::vector<unsigned char> cur(stride), prior(stride, 0), cand(stride);
    for (int y = 0; y < height; ++y) {
        const Rgb* row = image.row(y);
        for (int x = 0; x < width; ++x) {
            if (grey) {
                cur[static_cast<size_t>(x)] = row[x].r;
            } else {
                cur[static_cast<size_t>(x) * 3] = row[x].r;
                cur[static_cast<size_t>(x) * 3 + 1] = row[x].g;
                cur[static_cast<size_t>(x) * 3 + 2] = row[x].b;
            }
        }
        // Pick the filter with the smallest sum of |value| (as signed bytes).
        unsigned char* out = &raw[static_cast<size_t>(y) * (stride + 1)];
        long bestSum = -1;
        for (int f = 0; f < 5; ++f) {
            long sum = 0;
            for (size_t i = 0; i < stride; ++i) {
                int a = i >= static_cast<size_t>(bpp) ? cur[i - bpp] : 0;
                int b = prior[i];
                int c = i >= static_cast<size_t>(bpp) ? prior[i - bpp] : 0;
                int pred = f == 0 ? 0 : f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : paeth(a, b, c);
                unsigned char v = static_cast<unsigned char>(cur[i] - pred);
                cand[i] = v;
                sum += v < 128 ? v : 256 - v;
            }
            if (bestSum < 0 || sum < bestSum) {
                bestSum = sum;
                out[0] = static_cast<unsigned char>(f);
                std::copy(cand.begin(), cand.end(), out + 1);
            }
        }
        prior.swap(cur);
    }
    std::string png("\x89PNG\r\n\x1a\n", 8);
    std::string ihdr;
    put32(ihdr, static_cast<unsigned>(width));
    put32(ihdr, static_cast<unsigned>(height));
    ihdr.push_back(8);                    // bit depth
    ihdr.push_back(grey ? 0 : 2);         // colour type
    ihdr.push_back(0);                    // compression
    ihdr.push_back(0);                    // filter method
    ihdr.push_back(0);                    // no interlace
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", zlibCompress(&raw[0], raw.size()));
    chunk(png, "IEND", std::string());
    return png;
}

bool savePng(const std::string& path, const Image& image, std::string& error) {
    std::string bytes = encodePng(image);
    if (bytes.empty()) {
        error = "empty image";
        return false;
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        error = "cannot create " + path;
        return false;
    }
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok) error = "cannot write " + path;
    return ok;
}

}  // namespace gfx
