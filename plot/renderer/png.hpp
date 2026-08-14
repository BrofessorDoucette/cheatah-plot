#pragma once

/**
 * @file png.hpp
 * @brief A dependency-free PNG writer for the renderer's RGBA8 framebuffers, plus a plain P6
 *        PPM fallback — the file-output end of the render pipeline.
 *
 * The encoder emits a fully standard PNG (signature, IHDR, IDAT, IEND) whose IDAT carries a
 * zlib stream built from STORED (uncompressed) deflate blocks, so no compression library is
 * ever linked and encoding is one pass over the pixels. Stored blocks trade file size for zero
 * dependencies and total determinism — a saved plot is bytes-stable across platforms and
 * toolchains, which is what the golden-image tests lean on. CRC-32 and Adler-32 are
 * implemented here from their public specifications (the PNG spec and RFC 1950/1951).
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cheatah::plot::renderer {

/**
 * An RGBA8 raster image — the CPU-side pixel form the rasterizer produces and both writers
 * below serialize. Pixels are row-major from the top-left, 4 bytes per pixel in R, G, B, A
 * order, so a valid image has `rgba.size() == width * height * 4`.
 */
struct Image {
    /// Width in pixels.
    std::uint32_t width = 0;
    /// Height in pixels.
    std::uint32_t height = 0;
    /// Row-major RGBA bytes, exactly `width * height * 4` of them.
    std::vector<std::uint8_t> rgba;
};

namespace detail {

/// Unpack a raster framebuffer (one packed RGBA8 uint per pixel — the kernels' byte order,
/// r | g<<8 | b<<16 | a<<24) into an @ref Image's byte planes. The one fb→Image conversion,
/// shared by the CPU and GPU render paths so their outputs are byte-comparable.
/// @param fb The packed framebuffer, width*height pixels (row-major).
/// @param width The framebuffer width in pixels.
/// @param height The framebuffer height in pixels.
/// @return The image (rgba sized width*height*4; fb pixels beyond that are ignored).
/// @complexity O(pixels). @alloc the returned image.
inline Image pack_image(const std::vector<std::uint32_t>& fb, std::uint32_t width,
                        std::uint32_t height) {
    Image img;
    img.width = width;
    img.height = height;
    img.rgba.resize(static_cast<std::size_t>(width) * height * 4u);
    for (std::size_t i = 0; i < fb.size(); ++i) {
        img.rgba[i * 4 + 0] = static_cast<std::uint8_t>(fb[i] & 0xFFu);
        img.rgba[i * 4 + 1] = static_cast<std::uint8_t>((fb[i] >> 8) & 0xFFu);
        img.rgba[i * 4 + 2] = static_cast<std::uint8_t>((fb[i] >> 16) & 0xFFu);
        img.rgba[i * 4 + 3] = static_cast<std::uint8_t>((fb[i] >> 24) & 0xFFu);
    }
    return img;
}

/// The 256-entry CRC-32 table (reflected polynomial 0xEDB88320), built once on first use.
/// @return The shared table. @complexity O(1) amortized (one 256×8 build per process).
/// @alloc none after the first call (function-local static).
inline const std::array<std::uint32_t, 256>& crc32_table() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) != 0 ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[n] = c;
        }
        return t;
    }();
    return table;
}

/// Feed @p len bytes at @p data into a running CRC-32 (PNG convention: seed 0xFFFFFFFF, the
/// caller finalizes with `~crc`). @return The updated running value.
/// @complexity O(len). @alloc none.
inline std::uint32_t crc32_update(std::uint32_t crc, const std::uint8_t* data, std::size_t len) {
    const std::array<std::uint32_t, 256>& table = crc32_table();
    for (std::size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

/// Adler-32 (RFC 1950) of @p len bytes at @p data — the checksum a zlib stream ends with.
/// @return The Adler-32 value. @complexity O(len). @alloc none.
inline std::uint32_t adler32(const std::uint8_t* data, std::size_t len) {
    std::uint32_t s1 = 1;
    std::uint32_t s2 = 0;
    for (std::size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16) | s1;
}

/// Append @p v to @p out as 4 big-endian bytes — the PNG integer encoding.
/// @complexity O(1). @alloc amortized growth of @p out.
inline void append_u32_be(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

/// Append one complete PNG chunk to @p out: big-endian data length, the 4-byte @p type, the
/// @p len data bytes, then the CRC-32 over type + data.
/// @complexity O(len). @alloc grows @p out by len + 12 bytes.
inline void append_chunk(std::vector<std::uint8_t>& out, const char (&type)[5],
                         const std::uint8_t* data, std::size_t len) {
    append_u32_be(out, static_cast<std::uint32_t>(len));
    const std::uint8_t* type_bytes = reinterpret_cast<const std::uint8_t*>(type);
    out.insert(out.end(), type_bytes, type_bytes + 4);
    std::uint32_t crc = crc32_update(0xFFFFFFFFu, type_bytes, 4);
    if (len != 0) {
        out.insert(out.end(), data, data + len);
        crc = crc32_update(crc, data, len);
    }
    append_u32_be(out, ~crc);
}

}  // namespace detail

/**
 * Encode @p img as a complete PNG byte stream: the 8-byte signature; IHDR declaring 8-bit
 * RGBA (colour type 6, no interlace); one IDAT whose zlib stream (header 0x78 0x01) carries
 * the filtered pixel data as STORED deflate blocks of at most 65535 bytes each, followed by
 * the Adler-32 of that raw data; then IEND. Every scanline is prefixed with filter byte 0
 * (None), so the raw stream is exactly `height * (1 + width * 4)` bytes and any conforming
 * PNG reader reproduces the input pixels bit for bit.
 *
 * @param img The image to encode; `img.rgba.size()` must equal `width * height * 4`.
 * @return The PNG file bytes, ready to write to disk or stream to a viewer.
 * @throws std::runtime_error when the pixel buffer size does not match the dimensions.
 * @complexity O(width × height).
 * @alloc the returned vector plus one transient filtered-scanline buffer of the same order.
 * @test plot:png
 */
inline std::vector<std::uint8_t> encode_png(const Image& img) {
    const std::uint64_t expected =
        static_cast<std::uint64_t>(img.width) * static_cast<std::uint64_t>(img.height) * 4u;
    if (img.rgba.size() != expected) {
        throw std::runtime_error("encode_png: rgba size does not match width * height * 4");
    }

    // Filter stage: each scanline becomes [0x00 | row bytes] — filter None keeps pixels verbatim.
    const std::size_t stride = static_cast<std::size_t>(img.width) * 4u;
    std::vector<std::uint8_t> filtered;
    filtered.reserve(static_cast<std::size_t>(img.height) * (stride + 1));
    for (std::uint32_t y = 0; y < img.height; ++y) {
        filtered.push_back(0);  // filter type 0: None
        const std::uint8_t* row = img.rgba.data() + static_cast<std::size_t>(y) * stride;
        filtered.insert(filtered.end(), row, row + stride);
    }

    // zlib stage: 0x78 0x01 header, STORED blocks (BTYPE 00, <= 65535 bytes, LEN/NLEN little-
    // endian, BFINAL on the last), then the Adler-32 of the raw filtered stream, big-endian.
    std::vector<std::uint8_t> zlib;
    zlib.reserve(filtered.size() + filtered.size() / 65535u * 5u + 16u);
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(filtered.size() - pos, 65535u);
        const bool last = (pos + n == filtered.size());
        zlib.push_back(last ? 0x01 : 0x00);
        const std::uint32_t len16 = static_cast<std::uint32_t>(n);
        zlib.push_back(static_cast<std::uint8_t>(len16 & 0xFFu));
        zlib.push_back(static_cast<std::uint8_t>((len16 >> 8) & 0xFFu));
        zlib.push_back(static_cast<std::uint8_t>(~len16 & 0xFFu));
        zlib.push_back(static_cast<std::uint8_t>((~len16 >> 8) & 0xFFu));
        zlib.insert(zlib.end(), filtered.data() + pos, filtered.data() + pos + n);
        pos += n;
    } while (pos < filtered.size());
    detail::append_u32_be(zlib, detail::adler32(filtered.data(), filtered.size()));

    // Chunk stage: signature, IHDR (13 data bytes), the one IDAT, IEND.
    std::vector<std::uint8_t> ihdr;
    ihdr.reserve(13);
    detail::append_u32_be(ihdr, img.width);
    detail::append_u32_be(ihdr, img.height);
    ihdr.push_back(8);   // bit depth: 8 bits per channel
    ihdr.push_back(6);   // colour type: RGBA
    ihdr.push_back(0);   // compression: deflate (the only defined method)
    ihdr.push_back(0);   // filter method: adaptive (per-scanline filter bytes)
    ihdr.push_back(0);   // interlace: none

    std::vector<std::uint8_t> png;
    png.reserve(zlib.size() + 64);
    const std::uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), signature, signature + 8);
    detail::append_chunk(png, "IHDR", ihdr.data(), ihdr.size());
    detail::append_chunk(png, "IDAT", zlib.data(), zlib.size());
    detail::append_chunk(png, "IEND", nullptr, 0);
    return png;
}

/**
 * Encode @p img and write the PNG to @p path in binary mode — the renderer's standard save.
 *
 * @param img The image to write; `img.rgba.size()` must equal `width * height * 4`.
 * @param path The destination file path; its parent directory must already exist.
 * @throws std::runtime_error when the pixel buffer size is wrong, the file cannot be opened,
 *         or a write fails.
 * @complexity O(width × height).
 * @alloc the transient encoded byte stream.
 * @test plot:png
 */
inline void save_png(const Image& img, const std::string& path) {
    const std::vector<std::uint8_t> bytes = encode_png(img);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("save_png: cannot open " + path);
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
        throw std::runtime_error("save_png: write failed for " + path);
    }
}

/**
 * Write @p img to @p path as a binary P6 PPM (24-bit RGB; the alpha channel is dropped) — the
 * fallback format for tooling that predates PNG support, and a handy raw-bytes debug tap.
 *
 * @param img The image to write; `img.rgba.size()` must equal `width * height * 4`.
 * @param path The destination file path; its parent directory must already exist.
 * @throws std::runtime_error when the pixel buffer size is wrong, the file cannot be opened,
 *         or a write fails.
 * @complexity O(width × height).
 * @alloc one transient RGB row buffer.
 * @test plot:png
 */
inline void save_ppm(const Image& img, const std::string& path) {
    const std::uint64_t expected =
        static_cast<std::uint64_t>(img.width) * static_cast<std::uint64_t>(img.height) * 4u;
    if (img.rgba.size() != expected) {
        throw std::runtime_error("save_ppm: rgba size does not match width * height * 4");
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("save_ppm: cannot open " + path);
    }
    out << "P6\n" << img.width << ' ' << img.height << "\n255\n";
    const std::size_t stride = static_cast<std::size_t>(img.width) * 4u;
    std::vector<std::uint8_t> row;
    row.reserve(static_cast<std::size_t>(img.width) * 3u);
    for (std::uint32_t y = 0; y < img.height; ++y) {
        row.clear();
        const std::uint8_t* src = img.rgba.data() + static_cast<std::size_t>(y) * stride;
        for (std::uint32_t x = 0; x < img.width; ++x) {
            row.push_back(src[static_cast<std::size_t>(x) * 4u + 0u]);
            row.push_back(src[static_cast<std::size_t>(x) * 4u + 1u]);
            row.push_back(src[static_cast<std::size_t>(x) * 4u + 2u]);
        }
        out.write(reinterpret_cast<const char*>(row.data()),
                  static_cast<std::streamsize>(row.size()));
    }
    out.flush();
    if (!out) {
        throw std::runtime_error("save_ppm: write failed for " + path);
    }
}

}  // namespace cheatah::plot::renderer
