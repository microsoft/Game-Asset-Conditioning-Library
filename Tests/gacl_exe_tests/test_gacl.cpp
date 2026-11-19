// gacl_exe_tests\test_gacl.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include <windows.h>
#include <cstring>
#include <mutex>
#include <utility>

#include <gacl.h>
#include <zlib.h>

#include "../../Api/gaclexe/Deflate.h"

// Basic sanity test
TEST(HelloWorld, BasicEquality) {
    EXPECT_EQ(42, 40 + 2);
}

// -deflate (Api\gaclexe\Deflate.cpp): each stream GACL compresses becomes one zlib stream, its DEFLATE blocks ended where the
// format's rule splits the stream's layout.  These routines wrap -deflate's compress routine, inflating every stream it writes
// block by block to record where its blocks end.
namespace DeflateRoutines
{
    struct Record
    {
        GACL_SHUFFLE_TRANSFORM Transform;
        bool Curved;
        size_t HeaderBytes;                 // the layout's HEADER section
        size_t Bytes;
        std::vector<size_t> BlockEnds;      // where each DEFLATE block but the last ends, in the stream
        bool Inflated;                      // the zlib stream inflates to the stream
    };

    std::mutex s_lock;                      // candidates compress on threads of their own
    std::vector<Record> s_records;
    PGACL_COMPRESSION_COMPRESSROUTINE s_deflateCompress = nullptr;

    // Inflates a zlib stream that should hold the n bytes at expected, recording where each block but the last ends.  With Z_BLOCK,
    // inflate() returns after the zlib header and after each block, setting data_type bit 7 there, and bit 6 in the last block.
    bool InflateBlockEnds(const uint8_t* z, size_t zBytes, const uint8_t* expected, size_t n, std::vector<size_t>& ends)
    {
        ends.clear();
        z_stream s = {};
        if (inflateInit(&s) != Z_OK)
        {
            return false;
        }
        std::vector<uint8_t> out(n + 1);
        s.next_in = const_cast<Bytef*>(z);
        s.avail_in = uInt(zBytes);
        s.next_out = out.data();
        s.avail_out = uInt(out.size());
        int result = Z_OK;
        while ((result = inflate(&s, Z_BLOCK)) == Z_OK)
        {
            if ((s.data_type & (128 | 64)) == 128 && s.total_out > 0 && (ends.empty() || ends.back() != s.total_out))
            {
                ends.push_back(size_t(s.total_out));
            }
        }
        const bool inflated = result == Z_STREAM_END && s.avail_in == 0 && s.total_out == n && memcmp(out.data(), expected, n) == 0;
        inflateEnd(&s);
        return inflated;
    }

    HRESULT Compress(void* cc, void* dest, size_t* destBytes, const void* src, size_t srcBytes)
    {
        const HRESULT hr = s_deflateCompress(cc, dest, destBytes, src, srcBytes);
        if (hr == S_OK)
        {
            const GACL_STREAM_LAYOUT* layout = GACL_Compression_GetStreamLayout();
            Record r = { GACL_SHUFFLE_TRANSFORM_NONE, false, 0, srcBytes, {}, false };
            if (layout != nullptr)
            {
                r.Transform = layout->Transform;
                r.Curved = layout->Curved;
                for (size_t i = 0; i < layout->SectionCount; i++)
                {
                    r.HeaderBytes += layout->Sections[i].Kind == GACL_STREAM_SECTION_KIND_HEADER ? layout->Sections[i].Size : 0;
                }
            }
            r.Inflated = InflateBlockEnds(static_cast<const uint8_t*>(dest), *destBytes, static_cast<const uint8_t*>(src), srcBytes, r.BlockEnds);
            std::lock_guard<std::mutex> lock(s_lock);
            s_records.push_back(std::move(r));
        }
        return hr;
    }

    // installs -deflate's routines, with no records, until destroyed
    struct Installed
    {
        Installed()
        {
            s_records.clear();
            gacl::Deflate_InstallRoutines();
            s_deflateCompress = GACL_Compression_CompressRoutine;
            GACL_Compression_CompressRoutine = &Compress;
        }
        ~Installed()
        {
            GACL_Compression_InitRoutine = &GACL_Compression_DefaultInitRoutine;
            GACL_Compression_CompressRoutine = &GACL_Compression_DefaultCompressRoutine;
            GACL_Compression_CleanupRoutine = &GACL_Compression_DefaultCleanupRoutine;
        }
    };

    // repeating runs of bytes, which DEFLATE codes in far fewer symbols than bytes, so zlib (which ends a block itself every 32767
    // symbols) only ends blocks where it is told to
    std::vector<uint8_t> Patterned(size_t bytes)
    {
        std::vector<uint8_t> data(bytes);
        for (size_t i = 0; i < bytes; i++)
        {
            data[i] = uint8_t((i / 64) % 17);
        }
        return data;
    }
}

// Linear streams: a shuffled stream's blocks end at each section but the blocks the pattern leaves over, and BC3 / BC5 endpoint
// sections are split in 2 equal runs, BC4's in 4; an unshuffled one is one block
TEST(Deflate, LinearBlockEnds) {
    struct FormatCase
    {
        DXGI_FORMAT format;
        size_t blockBytes;
        GACL_SHUFFLE_TRANSFORM tid;
        std::vector<size_t> blockEnds;
    };
    const FormatCase cases[] = {
        // BC1_224: color_0 | color_1 | indices
        { DXGI_FORMAT_BC1_UNORM, 8, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224, { 16384, 32768 } },
        // BC3_116224: alpha_0 2 | alpha_1 2 | alpha indices | color_0 2 | color_1 2 | color indices
        { DXGI_FORMAT_BC3_UNORM, 16, GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224, { 2048, 4096, 6144, 8192, 32768, 36864, 40960, 45056, 49152 } },
        // BC4_116: red_0 4 | red_1 4 | indices
        { DXGI_FORMAT_BC4_UNORM, 8, GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116, { 2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384 } },
        // BC5_116116: red_0 2 | red_1 2 | red indices | green_0 2 | green_1 2 | green indices
        { DXGI_FORMAT_BC5_UNORM, 16, GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116, { 2048, 4096, 6144, 8192, 32768, 34816, 36864, 38912, 40960 } },
    };
    for (const FormatCase& c : cases)
    {
        // 64KB of blocks, and one the pattern leaves over
        const std::vector<uint8_t> tex = DeflateRoutines::Patterned(64 * 1024 + c.blockBytes);
        SHUFFLE_COMPRESS_PARAMETERS params = {};
        params.SizeInBytes = tex.size();
        params.Format = c.format;
        params.TextureData = tex.data();
        GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED;
        std::vector<uint8_t> sc(tex.size());
        size_t scBytes = 0;
        {
            const DeflateRoutines::Installed installed;
            ASSERT_EQ(GACL_ShuffleCompress_BCn(sc.data(), &tid, &scBytes, params), S_OK) << "format " << c.format;
        }

        size_t shuffled = 0, unshuffled = 0;
        for (const DeflateRoutines::Record& r : DeflateRoutines::s_records)
        {
            const unsigned transform = unsigned(r.Transform);
            EXPECT_TRUE(r.Inflated) << "format " << c.format << ", transform " << transform;
            EXPECT_FALSE(r.Curved) << "format " << c.format << ", transform " << transform;
            if (r.Transform == c.tid)
            {
                shuffled++;
                EXPECT_EQ(r.BlockEnds, c.blockEnds) << "format " << c.format;
            }
            else
            {
                unshuffled++;
                EXPECT_EQ(transform, unsigned(GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY)) << "format " << c.format;
                EXPECT_TRUE(r.BlockEnds.empty()) << "format " << c.format << ", " << r.BlockEnds.size() << " block ends";
            }
        }
        EXPECT_EQ(shuffled, 1u) << "format " << c.format;
        EXPECT_EQ(unshuffled, 1u) << "format " << c.format;
    }
}

// Curved streams (BC1 EX streams in space curve order): each section is split in runs of at most 32KB, so after the EX header
// no block codes more than 32KB (with any blocks a pattern leaves over).  The texture is repetitive enough that zlib would
// otherwise code each stream in one block.
TEST(Deflate, CurvedBlockEnds) {
    // 512x512 BC1, 2 16KB micro-tiles wide: 8x8 block squares of 16 blocks
    const size_t widthInBlocks = 512 / 4;
    std::vector<uint8_t> tex(widthInBlocks * widthInBlocks * 8);
    for (size_t b = 0; b < widthInBlocks * widthInBlocks; b++)
    {
        const uint64_t square = (b % widthInBlocks) / 8 + (b / widthInBlocks) / 8;
        const uint64_t block = 0x9E3779B97F4A7C15ull * (square % 16 + 1);
        memcpy(&tex[b * 8], &block, 8);
    }
    SHUFFLE_COMPRESS_PARAMETERS params = {};
    params.SizeInBytes = tex.size();
    params.Format = DXGI_FORMAT_BC1_UNORM;
    params.TextureData = tex.data();
    params.CurvedTransforms.WidthInPixels = 512;
    params.CurvedTransforms.TextureData = tex.data();
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;
    std::vector<uint8_t> sc(tex.size());
    size_t scBytes = 0;
    {
        const DeflateRoutines::Installed installed;
        ASSERT_EQ(GACL_ShuffleCompress_BCn(sc.data(), &tid, &scBytes, params), S_OK);
    }

    size_t curved = 0;
    for (const DeflateRoutines::Record& r : DeflateRoutines::s_records)
    {
        const unsigned transform = unsigned(r.Transform);
        EXPECT_TRUE(r.Inflated) << "transform " << transform;
        if (!r.Curved)
        {
            continue;
        }
        curved++;
        std::vector<size_t> ends = { r.HeaderBytes };
        ends.insert(ends.end(), r.BlockEnds.begin(), r.BlockEnds.end());
        ends.push_back(r.Bytes);
        for (size_t i = 1; i < ends.size(); i++)
        {
            EXPECT_LE(ends[i] - ends[i - 1], 32u * 1024 + 16) << "transform " << transform << ", block " << i - 1 << " of " << ends.size() - 1;
        }
    }
    EXPECT_GT(curved, 0u);
}

// GACL_Compression_CompressBuffer() data has no format, so no rule: one block (zlib ends none itself in data this repetitive),
// and the buffer is the zlib stream
TEST(Deflate, CompressBuffer) {
    const std::vector<uint8_t> src = DeflateRoutines::Patterned(64 * 1024);
    std::vector<uint8_t> dest(src.size());
    size_t destBytes = 0;
    {
        const DeflateRoutines::Installed installed;
        ASSERT_EQ(GACL_Compression_CompressBuffer(dest.data(), src.size(), &destBytes, src.data(), nullptr), S_OK);
    }
    ASSERT_EQ(DeflateRoutines::s_records.size(), 1u);
    const DeflateRoutines::Record& r = DeflateRoutines::s_records[0];
    EXPECT_TRUE(r.Inflated);
    EXPECT_EQ(unsigned(r.Transform), unsigned(GACL_SHUFFLE_TRANSFORM_NONE));
    EXPECT_TRUE(r.BlockEnds.empty()) << r.BlockEnds.size() << " block ends";

    std::vector<size_t> ends;
    EXPECT_TRUE(DeflateRoutines::InflateBlockEnds(dest.data(), destBytes, src.data(), src.size(), ends));
}
