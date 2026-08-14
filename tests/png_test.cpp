// Unit tests for plot.renderer's PNG/PPM writer (plot/renderer/png.hpp) — the plot:png suite
// the header's @test tags point at. Container-level checks (signature, chunk order, IHDR
// fields, the well-known IEND CRC) plus a stored-block reassembler written HERE, without any
// zlib dependency, so an encoder bug cannot be masked by a decoder's tolerance. The two-block
// case (200x90 > 65535 raw bytes) pins the stored-block splitting byte for byte.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "plot/renderer/png.hpp"

namespace r = cheatah::plot::renderer;

namespace {

// Big-endian 32-bit read at byte offset — the PNG integer encoding.
std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes, std::size_t off) {
    return (static_cast<std::uint32_t>(bytes[off]) << 24) |
           (static_cast<std::uint32_t>(bytes[off + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[off + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[off + 3]);
}

// First byte offset of the 4-char sequence @p type in @p bytes; bytes.size() when absent.
std::size_t find_marker(const std::vector<std::uint8_t>& bytes, const std::string& type) {
    auto it = std::search(bytes.begin(), bytes.end(), type.begin(), type.end());
    return static_cast<std::size_t>(it - bytes.begin());
}

// Walks the chunk list from offset 8 and returns the offset of the DATA of the first chunk of
// @p type (its length in *len_out); bytes.size() when absent.
std::size_t chunk_data(const std::vector<std::uint8_t>& bytes, const std::string& type,
                       std::uint32_t* len_out) {
    std::size_t off = 8;
    while (off + 8 <= bytes.size()) {
        const std::uint32_t len = read_be32(bytes, off);
        const std::string t(bytes.begin() + static_cast<std::ptrdiff_t>(off + 4),
                            bytes.begin() + static_cast<std::ptrdiff_t>(off + 8));
        if (t == type) {
            *len_out = len;
            return off + 8;
        }
        off += 12 + len;  // length + type + data + crc
    }
    *len_out = 0;
    return bytes.size();
}

// Parses the STORED-block zlib stream in png[data_off, data_off+data_len) and reassembles the
// raw (filtered) bytes; counts the deflate blocks into *blocks_out. Validates the zlib header,
// each block's BTYPE/LEN/NLEN, and that nothing but the Adler-32 trails the final block.
std::vector<std::uint8_t> inflate_stored(const std::vector<std::uint8_t>& png,
                                         std::size_t data_off, std::uint32_t data_len,
                                         int* blocks_out) {
    std::vector<std::uint8_t> raw;
    *blocks_out = 0;
    if (data_len < 11) {  // header(2) + one empty stored block(5) + adler(4)
        ADD_FAILURE() << "IDAT too small to be a stored-block zlib stream: " << data_len;
        return raw;
    }
    EXPECT_EQ(png[data_off], 0x78u) << "zlib CMF byte";
    EXPECT_EQ(png[data_off + 1], 0x01u) << "zlib FLG byte";
    std::size_t pos = data_off + 2;
    const std::size_t end = data_off + data_len - 4;  // the trailing Adler-32
    bool saw_final = false;
    while (!saw_final) {
        if (pos + 5 > end) {
            ADD_FAILURE() << "truncated stored-block header at " << pos;
            return raw;
        }
        const std::uint8_t head = png[pos];
        EXPECT_EQ(head & 0x06u, 0u) << "block " << *blocks_out << " is not BTYPE 00 (stored)";
        saw_final = (head & 0x01u) != 0;
        const std::size_t n = static_cast<std::size_t>(png[pos + 1]) |
                              (static_cast<std::size_t>(png[pos + 2]) << 8);
        const std::size_t nlen = static_cast<std::size_t>(png[pos + 3]) |
                                 (static_cast<std::size_t>(png[pos + 4]) << 8);
        EXPECT_EQ(nlen, ~n & 0xFFFFu) << "NLEN must be the ones' complement of LEN";
        pos += 5;
        if (pos + n > end) {
            ADD_FAILURE() << "stored block overruns the stream: " << n << " bytes at " << pos;
            return raw;
        }
        raw.insert(raw.end(), png.begin() + static_cast<std::ptrdiff_t>(pos),
                   png.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
        ++*blocks_out;
    }
    EXPECT_EQ(pos, end) << "unexpected bytes between the final block and the Adler-32";
    return raw;
}

// A deterministic image whose byte values vary with position (i*31+7 mod 256 makes all 24
// bytes of the 3x2 case pairwise distinct, and gives the big case boundary-sensitive data).
r::Image make_test_image(std::uint32_t w, std::uint32_t h) {
    r::Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * h * 4u);
    for (std::size_t i = 0; i < img.rgba.size(); ++i) {
        img.rgba[i] = static_cast<std::uint8_t>((i * 31u + 7u) & 0xFFu);
    }
    return img;
}

}  // namespace

TEST(Png, SignatureIhdrFieldsAndChunkOrder) {
    const r::Image img = make_test_image(3, 2);
    const std::vector<std::uint8_t> png = r::encode_png(img);

    ASSERT_GE(png.size(), 8u + 25u + 12u + 12u);  // signature + IHDR + empty IDAT + IEND floor
    const std::uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(png[i], signature[i]) << "signature byte " << i;
    }

    EXPECT_EQ(read_be32(png, 16), 3u) << "IHDR width (big-endian at 16..19)";
    EXPECT_EQ(read_be32(png, 20), 2u) << "IHDR height (big-endian at 20..23)";
    EXPECT_EQ(png[24], 8u) << "bit depth";
    EXPECT_EQ(png[25], 6u) << "colour type (RGBA)";
    EXPECT_EQ(png[28], 0u) << "interlace (none)";

    const std::size_t at_ihdr = find_marker(png, "IHDR");
    const std::size_t at_idat = find_marker(png, "IDAT");
    const std::size_t at_iend = find_marker(png, "IEND");
    ASSERT_LT(at_ihdr, png.size());
    ASSERT_LT(at_idat, png.size());
    ASSERT_LT(at_iend, png.size());
    EXPECT_LT(at_ihdr, at_idat);
    EXPECT_LT(at_idat, at_iend);
}

TEST(Png, StoredZlibRoundTripsTheFilteredStream) {
    const r::Image img = make_test_image(3, 2);
    const std::vector<std::uint8_t> png = r::encode_png(img);

    std::uint32_t idat_len = 0;
    const std::size_t idat_off = chunk_data(png, "IDAT", &idat_len);
    ASSERT_LT(idat_off, png.size());

    int blocks = 0;
    const std::vector<std::uint8_t> raw = inflate_stored(png, idat_off, idat_len, &blocks);
    const std::size_t stride = 3u * 4u;
    ASSERT_EQ(raw.size(), 2u * (1u + stride)) << "raw length must be height*(1+width*4)";
    EXPECT_EQ(blocks, 1);

    for (std::size_t y = 0; y < 2; ++y) {
        const std::size_t line = y * (1u + stride);
        EXPECT_EQ(raw[line], 0u) << "scanline " << y << " filter byte";
        for (std::size_t i = 0; i < stride; ++i) {
            ASSERT_EQ(raw[line + 1u + i], img.rgba[y * stride + i])
                << "pixel byte " << i << " of scanline " << y;
        }
    }

    // The Adler-32 the stream ends with is the checksum of exactly the raw filtered bytes.
    const std::size_t adler_off = idat_off + idat_len - 4u;
    EXPECT_EQ(read_be32(png, adler_off),
              cheatah::plot::renderer::detail::adler32(raw.data(), raw.size()));
}

TEST(Png, TwoStoredBlocksStitchByteForByte) {
    // 200x90 -> raw stream 90*(1+800) = 72,090 bytes > 65,535: the encoder MUST split.
    const r::Image img = make_test_image(200, 90);
    const std::vector<std::uint8_t> png = r::encode_png(img);

    std::uint32_t idat_len = 0;
    const std::size_t idat_off = chunk_data(png, "IDAT", &idat_len);
    ASSERT_LT(idat_off, png.size());

    int blocks = 0;
    const std::vector<std::uint8_t> raw = inflate_stored(png, idat_off, idat_len, &blocks);
    EXPECT_EQ(blocks, 2) << "72,090 raw bytes need exactly two stored blocks";

    // Rebuild the expected filtered stream and demand byte-for-byte equality — this is the
    // stitch check: the 65,535-byte boundary falls mid-scanline and must be seamless.
    const std::size_t stride = 200u * 4u;
    std::vector<std::uint8_t> expected;
    expected.reserve(90u * (1u + stride));
    for (std::size_t y = 0; y < 90; ++y) {
        expected.push_back(0);
        const std::uint8_t* row = img.rgba.data() + y * stride;
        expected.insert(expected.end(), row, row + stride);
    }
    ASSERT_EQ(raw.size(), expected.size());
    EXPECT_TRUE(raw == expected) << "reassembled stream diverges from the filtered source";

    for (std::size_t y = 0; y < 90; ++y) {  // and every scanline still starts with filter 0
        ASSERT_EQ(raw[y * (1u + stride)], 0u) << "scanline " << y;
    }
}

TEST(Png, IendCrcMatchesTheKnownConstant) {
    const r::Image img = make_test_image(3, 2);
    const std::vector<std::uint8_t> png = r::encode_png(img);

    // The IEND chunk is always the last 12 bytes: length 0, "IEND", then its CRC — a constant
    // every PNG in existence shares.
    ASSERT_GE(png.size(), 12u);
    const std::size_t iend = png.size() - 12u;
    EXPECT_EQ(read_be32(png, iend), 0u);
    EXPECT_EQ(png[iend + 4], 'I');
    EXPECT_EQ(png[iend + 7], 'D');
    EXPECT_EQ(read_be32(png, png.size() - 4u), 0xAE426082u);

    // Cross-check the table-driven CRC-32 itself against the same well-known value.
    const std::uint8_t type[4] = {'I', 'E', 'N', 'D'};
    const std::uint32_t crc =
        ~cheatah::plot::renderer::detail::crc32_update(0xFFFFFFFFu, type, 4);
    EXPECT_EQ(crc, 0xAE426082u);
}

TEST(Png, SavePngAndSavePpmWriteTheRightMagic) {
    const r::Image img = make_test_image(3, 2);
    const std::string dir = ::testing::TempDir();

    const std::string png_path = dir + "cheatah_plot_png_test.png";
    r::save_png(img, png_path);
    std::ifstream png_in(png_path, std::ios::binary);
    ASSERT_TRUE(png_in.good());
    std::uint8_t magic[8] = {};
    png_in.read(reinterpret_cast<char*>(magic), 8);
    ASSERT_TRUE(png_in.good());
    const std::uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(magic[i], signature[i]) << "file signature byte " << i;
    }

    const std::string ppm_path = dir + "cheatah_plot_png_test.ppm";
    r::save_ppm(img, ppm_path);
    std::ifstream ppm_in(ppm_path, std::ios::binary);
    ASSERT_TRUE(ppm_in.good());
    char p6[2] = {};
    ppm_in.read(p6, 2);
    ASSERT_TRUE(ppm_in.good());
    EXPECT_EQ(p6[0], 'P');
    EXPECT_EQ(p6[1], '6');
}

TEST(Png, MismatchedBufferAndUnwritablePathThrow) {
    r::Image bad;
    bad.width = 3;
    bad.height = 2;
    bad.rgba.resize(3u * 2u * 4u - 1u);  // one byte short
    EXPECT_THROW(r::encode_png(bad), std::runtime_error);
    EXPECT_THROW(r::save_ppm(bad, ::testing::TempDir() + "never_written.ppm"),
                 std::runtime_error);

    const r::Image good = make_test_image(3, 2);
    const std::string missing_dir = ::testing::TempDir() + "no-such-dir/out";
    EXPECT_THROW(r::save_png(good, missing_dir + ".png"), std::runtime_error);
    EXPECT_THROW(r::save_ppm(good, missing_dir + ".ppm"), std::runtime_error);
}
