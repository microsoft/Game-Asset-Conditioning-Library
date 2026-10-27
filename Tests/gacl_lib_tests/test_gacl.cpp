// gacl_lib_tests\test_gacl.cpp
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
#include <filesystem>
#include <iostream>
#include <fstream>
#include <random>

#include <DirectXTex.h>

#include <gacl.h>
#include <zstd.h>

using namespace DirectX;
using namespace std;

// Find the root dynamically, so CI builds with remapped output folders work
static std::filesystem::path GetRoot()
{
    static struct init {
        filesystem::path root = "";

        init() {
            wchar_t modulePath[MAX_PATH];
            GetModuleFileNameW(0, modulePath, _countof(modulePath));

            root = filesystem::path(modulePath).parent_path();
        }
    } i;
    return i.root;
}

static std::filesystem::path GetAssetsDirectory()
{
    return GetRoot() / "TestImages" / "InsectsDemo";
}

static std::filesystem::path GetTestOutputDirectory()
{
    auto outputDir = std::filesystem::current_path() / "test_output";
    if (!std::filesystem::exists(outputDir))
    {
        std::filesystem::create_directories(outputDir);
    }
    return outputDir;
}

static std::filesystem::path GetBinResultDir()
{
    static struct init {
        filesystem::path outFilePath;

        init() {
            filesystem::path outFileDir = std::filesystem::current_path() / L"TestResults" / L"Bins";
            if (!std::filesystem::exists(outFileDir))
            {
                std::filesystem::create_directories(outFileDir);
            }
            outFilePath = outFileDir;
            return;
        }
    } i;
    return i.outFilePath;
}

/* Should we add these to the public APIs after preview? */

static void UnshuffleBC1v1(uint8_t* dest, uint8_t* src, size_t size)
{
    uint8_t* s1 = src;                           // e0       1/4
    uint8_t* s2 = src + size / 4;                // e1       1/4
    uint8_t* s3 = src + size / 2;                // ind      1/2

    for (size_t b = 0; b < size / 8; b++)
    {
        *dest++ = *s1++; *dest++ = *s1++;        // e0 (2B)
        *dest++ = *s2++; *dest++ = *s2++;        // e1 (2B)
        *dest++ = *s3++; *dest++ = *s3++;        // aind (4B)
        *dest++ = *s3++; *dest++ = *s3++; 
    }
}

static void UnshuffleBC3v1(uint8_t* dest, uint8_t* src, size_t size)
{
    uint8_t* s1 = src;                           // a0       1/16
    uint8_t* s2 = src + size / 16;               // a1       1/16
    uint8_t* s3 = src + size / 8;                // aind     6/16
    uint8_t* s4 = src + size / 2;                // e0       2/16
    uint8_t* s5 = src + size / 2 + size / 8;     // e1       2/16
    uint8_t* s6 = src + size / 2 + size / 4;     // eind     4/16

    for (size_t b = 0; b < size / 16; b++)
    {
        *dest++ = *s1++;                                   // a0 (1B)
        *dest++ = *s2++;                                   // a1 (1B)
        *dest++ = *s3++; *dest++ = *s3++; *dest++ = *s3++; // aind (6B)
        *dest++ = *s3++; *dest++ = *s3++; *dest++ = *s3++;
        *dest++ = *s4++; *dest++ = *s4++;                  // e0 (2B)
        *dest++ = *s5++; *dest++ = *s5++;                  // e1 (2B)
        *dest++ = *s6++; *dest++ = *s6++;                  // eind (4B)
        *dest++ = *s6++; *dest++ = *s6++;
    }
}

static void UnshuffleBC4v1(uint8_t* dest, uint8_t* src, size_t size)
{
    uint8_t* s1 = src;                                     // r0       1/8
    uint8_t* s2 = src + size / 8;                          // r1       1/8
    uint8_t* s3 = src + size / 4;                          // ind      3/4

    for (size_t b = 0; b < size / 8; b++)
    {
        *dest++ = *s1++;                                   // r0 (1B)
        *dest++ = *s2++;                                   // r1 (1B)
        *dest++ = *s3++; *dest++ = *s3++; *dest++ = *s3++; // ind (6B)
        *dest++ = *s3++; *dest++ = *s3++; *dest++ = *s3++;
    }
}

static void UnshuffleBC5v1(uint8_t* dest, uint8_t* src, size_t size)
{
    uint8_t* s1 = src;                                     // r0       1/16
    uint8_t* s2 = src + (size * 1) / 16;                   // r1       1/16
    uint8_t* s3 = src + (size * 2) / 16;                   // rind     6/16
    uint8_t* s4 = src + (size * 8) / 16;                   // g0       1/16
    uint8_t* s5 = src + (size * 9) / 16;                   // g1       1/16
    uint8_t* s6 = src + (size * 10) / 16;                  // gind     6/16

    for (size_t b = 0; b < size / 16; b++)
    {
        *dest++ = *s1++;                                   // r0 (1B)
        *dest++ = *s2++;                                   // r1 (1B)
        *dest++ = *s3++; *dest++ = *s3++; *dest++ = *s3++; // rind (6B)
        *dest++ = *s3++; *dest++ = *s3++; *dest++ = *s3++;
        *dest++ = *s4++;                                   // g0 (1B)
        *dest++ = *s5++;                                   // g1 (1B)
        *dest++ = *s6++; *dest++ = *s6++; *dest++ = *s6++; // gind (6B)
        *dest++ = *s6++; *dest++ = *s6++; *dest++ = *s6++;
    }
}


HRESULT ShuffleCompressMip0(filesystem::path file, GACL_SHUFFLE_TRANSFORM& tid, bool includeCurve, vector<uint8_t> &dest)
{
    ScratchImage si;
    TexMetadata siMeta;

    HRESULT hr = LoadFromDDSFile(file.c_str(), DDS_FLAGS::DDS_FLAGS_NONE, &siMeta, si);
    if (SUCCEEDED(hr))
    {
        const Image* img = si.GetImage(0, 0, 0);
        size_t srcImgSize = si.GetPixelsSize();
        dest.resize(srcImgSize);
        size_t destBytes;
        const uint8_t* dataForCurved = includeCurve ? img->pixels : nullptr;

        SHUFFLE_COMPRESS_PARAMETERS params = { srcImgSize, siMeta.format, img->pixels, {19, 1u<<17}, {img->width, dataForCurved} };

        hr = GACL_ShuffleCompress_BCn(dest.data(), &tid, &destBytes, params);
        dest.resize(destBytes);
    }
    return hr;
}


HRESULT ValidateRoundTripShuffleCompress(filesystem::path file, GACL_SHUFFLE_TRANSFORM& tid, bool includeCurve, vector<uint8_t>& sc)
{
    ScratchImage si;
    TexMetadata siMeta;
    HRESULT hr = LoadFromDDSFile(file.c_str(), DDS_FLAGS::DDS_FLAGS_NONE, &siMeta, si);
    EXPECT_EQ(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        const Image* img = si.GetImage(0, 0, 0);
        size_t srcImgSize = si.GetPixelsSize();

        vector<uint8_t> uncompressed;
        vector<uint8_t> unshuffled;

        size_t scBytes;
        sc.resize(srcImgSize);

        uint8_t* dataForCurved = includeCurve ? img->pixels : nullptr;

        SHUFFLE_COMPRESS_PARAMETERS params = { srcImgSize, siMeta.format, img->pixels, {19}, {img->width, dataForCurved} };

        hr = GACL_ShuffleCompress_BCn(sc.data(), &tid, &scBytes, params);
        EXPECT_EQ(hr, S_OK);

        void BC7_ModeSplit_Reverse(const uint8_t * src, size_t srcSize, std::vector<uint8_t>&dest, size_t destSize, const uint8_t * ref);
        bool BCn_Unshuffle(const uint8_t * src, size_t size, uint8_t transformId, std::vector<uint8_t>&dest);
        bool BCnEx_Decode(const uint8_t * src, size_t size, uint8_t transformId, std::vector<uint8_t>&dest, bool uncurve, uint8_t * transformOut, uint8_t * curveWidthTilesOut);
        bool BC7_JoinStream_Decode(const uint8_t * src, size_t srcSize, uint8_t transformId, std::vector<uint8_t>&dest, bool uncurve, uint8_t * curveWidthTilesOut);
        if (hr == S_OK)
        {
            uncompressed.resize(srcImgSize);
            unshuffled.resize(srcImgSize);
            sc.resize(scBytes);

            size_t ucBytes = ZSTD_decompress(uncompressed.data(), uncompressed.size(), sc.data(), scBytes);

            switch (tid)
            {
            case GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY:
                memcpy(unshuffled.data(), uncompressed.data(), srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:
                UnshuffleBC1v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:
                UnshuffleBC3v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:
                UnshuffleBC4v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:
                UnshuffleBC5v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44:
            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664:
            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266:
                EXPECT_TRUE(BCn_Unshuffle(uncompressed.data(), ucBytes, uint8_t(tid), unshuffled));
                unshuffled.resize(srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX:        // decode also undoes any space curve recorded in the stream header
            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_EX:
            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_EX:
            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_EX:
            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX:
                EXPECT_TRUE(BCnEx_Decode(uncompressed.data(), ucBytes, uint8_t(tid), unshuffled, true, nullptr, nullptr));
                unshuffled.resize(srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN:      // headerless Join is never curved
                EXPECT_TRUE(BC7_JoinStream_Decode(uncompressed.data(), ucBytes, uint8_t(tid), unshuffled, true, nullptr));
                unshuffled.resize(srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT:     // reverse also undoes any space curve recorded in the stream header
                BC7_ModeSplit_Reverse(uncompressed.data(), ucBytes, unshuffled, srcImgSize, nullptr/*img->pixels*/);
                break;

            default:
                ADD_FAILURE();
            }

            // Previous: EXPECT_EQ(0, memcmp(unshuffled.data(), img->pixels, srcImgSize));
            // Below is effectively the same, but will export the corrupt offset rather than just a bool pass\fail
            const uint8_t* cmpData = unshuffled.data();
            size_t i = 0;
            while (i < srcImgSize)
            {
                if (cmpData[i] != img->pixels[i])
                    break;
                i++;
            }
            if (i != srcImgSize)    // save the shuffle+compressed stream for further follow up
            {
                filesystem::path outFileName = GetBinResultDir() / file.filename();
                outFileName.replace_extension(GACL_ShuffleCompress_GetFileExtensionForTransform(tid));

                ofstream outfile(outFileName.c_str());
                if (outfile.is_open()) {
                    outfile.write(reinterpret_cast<const char*>(sc.data()), scBytes);
                    outfile.close();
                }
            }
            EXPECT_EQ(i, srcImgSize);
        }
        else
        {
            sc.resize(0);
        }
    }
    return hr;
}

auto GerberaSingleLeaf_albedo_2k_2k_BC1 = GetAssetsDirectory() / "BC1mip0/gerberaSingleLeaf_albedo.DDS";

TEST(ShuffleCompress, LargeBC1_ShuffleCompress1) {
    vector<uint8_t> sc;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED;
    HRESULT hr = ValidateRoundTripShuffleCompress(GerberaSingleLeaf_albedo_2k_2k_BC1, tid, false, sc);

    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224);
    EXPECT_EQ(sc.size(), 627863);   // known size for this asset shuffle compressed in this way
}

TEST(ShuffleCompress, LargeBC1_ShuffleCompress_curved) {
    vector<uint8_t> sc;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;
    HRESULT hr = ValidateRoundTripShuffleCompress(GerberaSingleLeaf_albedo_2k_2k_BC1, tid, true, sc);

    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX);
    EXPECT_EQ(sc.size(), 613083);    // known size for this asset shuffle compressed in this way
}

auto BlackboardEraser_albedo_1k_1k_BC1 = GetAssetsDirectory() / "BC1mip0/BlackboardEraser_albedo.DDS";

// Color index prediction is chosen per stream: this asset's curved BC1_EX stream is smaller without it, so the stream
// carries header parameter 2 (bcnex.h), the prediction mask
TEST(ShuffleCompress, LargeBC1_ShuffleCompress_curved_NoColourPrediction) {
    vector<uint8_t> sc;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;
    HRESULT hr = ValidateRoundTripShuffleCompress(BlackboardEraser_albedo_1k_1k_BC1, tid, true, sc);

    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX);
    EXPECT_EQ(sc.size(), 221134);    // known size for this asset shuffle compressed in this way

    // header byte 0 has the presence bits of parameters 1 (curve width, 4 tiles) and 2 (mask 0, no prediction), whose
    // values follow in id order
    vector<uint8_t> stream(1024 * 1024 / 2);
    const size_t size = ZSTD_decompress(stream.data(), stream.size(), sc.data(), sc.size());
    ASSERT_EQ(size, stream.size());
    EXPECT_EQ(stream[0] & 0xF8, 0x18);
    EXPECT_EQ(stream[1], 4);
    EXPECT_EQ(stream[2], 0);
}


auto LadybugColors_1x2_BC3 = GetAssetsDirectory() / "BC3mip0/UI/LadybugColors.DDS";

// check a texture smaller than an element
TEST(ShuffleCompress, TinyBC3) {
    vector<uint8_t> dest;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED;

    HRESULT hr = ShuffleCompressMip0(LadybugColors_1x2_BC3, tid, false, dest);

    EXPECT_EQ(hr, S_FALSE);                         // 1x2 texture (single BC element) will not be compressible
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_NONE);    
}

TEST(ShuffleCompress, TinyBC3_EX) {
    vector<uint8_t> dest;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;

    HRESULT hr = ShuffleCompressMip0(LadybugColors_1x2_BC3, tid, false, dest);

    EXPECT_EQ(hr, S_FALSE);                         // 1x2 texture (single BC element) will not be compressible
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_NONE);
}

TEST(ShuffleCompress, TinyBC3_curve) {
    vector<uint8_t> dest;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;

    HRESULT hr = ShuffleCompressMip0(LadybugColors_1x2_BC3, tid, true, dest);

    EXPECT_EQ(hr, E_INVALIDARG);                         // 1x2 texture is invalid for space curves
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_NONE);
}


auto Animation_sheet_1080x1080_BC3 = GetAssetsDirectory() / "BC3mip0/UI/Animation_sheet.DDS";

TEST(ShuffleCompress, LargeBC3_ShuffleCompress1) {
    vector<uint8_t> sc;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED;
    HRESULT hr = ValidateRoundTripShuffleCompress(Animation_sheet_1080x1080_BC3, tid, false, sc);

    EXPECT_EQ(hr, S_OK);                         
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224);
    EXPECT_EQ(sc.size(), 66317);
}



auto Ladybug_fresnel_2k_2k_BC4 = GetAssetsDirectory() / "BC4mip0/Ladybug_fresnel.DDS";

TEST(ShuffleCompress, LargeBC4_ShuffleCompress) {
    vector<uint8_t> sc;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED;
    HRESULT hr = ValidateRoundTripShuffleCompress(Ladybug_fresnel_2k_2k_BC4, tid, false, sc);

    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116);
    EXPECT_EQ(sc.size(), 69768);    // known size for this asset shuffle compressed in this way
}

// BC4_116 wins this asset under ANY_EXPERIMENTAL (always-on EX index prediction costs ~8% here), so cover the predicted,
// curved BC4 round trip by requesting BC4_EX explicitly
TEST(ShuffleCompress, LargeBC4_ShuffleCompress_curved) {
    vector<uint8_t> sc;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_EX;
    HRESULT hr = ValidateRoundTripShuffleCompress(Ladybug_fresnel_2k_2k_BC4, tid, true, sc);

    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_EX);
    EXPECT_EQ(sc.size(), 71643);    // known size for this asset shuffle compressed in this way
}

// An EX id of another format names no candidate, like a headerless id of another format
TEST(ShuffleCompress, LargeBC4_ShuffleCompress_OtherFormatEx) {
    for (GACL_SHUFFLE_TRANSFORM other : { GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX, GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_EX,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_EX, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX })
    {
        vector<uint8_t> dest;
        GACL_SHUFFLE_TRANSFORM tid = other;
        EXPECT_EQ(ShuffleCompressMip0(Ladybug_fresnel_2k_2k_BC4, tid, true, dest), E_INVALIDARG) << unsigned(other);
    }
}


auto GerberaSingleLeaf_normal_2k_2k_BC5 = GetAssetsDirectory() / "BC5mip0/gerberaSingleLeaf_normal.DDS";

TEST(ShuffleCompress, LargeBC5_ShuffleCompress) {
    vector<uint8_t> sc;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED;
    HRESULT hr = ValidateRoundTripShuffleCompress(GerberaSingleLeaf_normal_2k_2k_BC5, tid, false, sc);

    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116);
    EXPECT_EQ(sc.size(), 3903772);    // known size for this asset shuffle compressed in this way
}

TEST(ShuffleCompress, LargeBC5_ShuffleCompress_curved) {
    vector<uint8_t> sc;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;
    HRESULT hr = ValidateRoundTripShuffleCompress(GerberaSingleLeaf_normal_2k_2k_BC5, tid, true, sc);

    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_EX);
    EXPECT_EQ(sc.size(), 3876142);    // known size for this asset shuffle compressed in this way
}

// EX index prediction masks (header parameter 2): every subset of a format's BCNEX_PREDICT_* choices round trips,
// linear and curved, with or without BCNEX_UNSHUFFLED (the kept blocks then follow the header as they are). The
// transform implies the default (every choice, shuffled), which is never written; the encoder rejects other bits, and
// the decoder rejects a parameter naming the default or holding other bits.
#include "../../Lib/shuffle/bcnex.h"
#include "../../Lib/shuffle/streamheader.h"

TEST(ShuffleBCnEx, PredictionMask)
{
    const GACL_SHUFFLE_TRANSFORM transforms[] = { GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224, GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664, GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116, GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266 };
    for (GACL_SHUFFLE_TRANSFORM transform : transforms)
    {
        const uint8_t tid = uint8_t(transform);
        const size_t bs = BCn_BlockSize(tid);
        const uint8_t choices = BCnEx_PredictionChoices(tid);
        ASSERT_NE(bs, 0u);
        ASSERT_EQ(BCnEx_DefaultPrediction(tid), choices);

        // 512x256 texels of random blocks; every 32nd block repeats its predecessor, making room for the header
        const size_t n = 128 * 64;
        vector<uint8_t> src(n * bs);
        uint32_t x = tid;
        for (uint8_t& b : src)
        {
            x = x * 1664525u + 1013904223u;
            b = uint8_t(x >> 24);
        }
        for (size_t i = 32; i < n; i += 32)
        {
            memcpy(&src[i * bs], &src[(i - 1) * bs], bs);
        }

        // curve width: 512 texels in 256 (8-byte blocks) or 128 (16-byte blocks) texel wide micro-tiles
        for (uint8_t curve : { uint8_t(0), uint8_t(bs == 8 ? 2 : 4) })
        {
            const size_t at = GACL_STREAMHEADER_FIXED_BYTES + (curve ? 1 : 0);    // parameter 2 follows parameter 1
            const unsigned allowed = choices | BCNEX_UNSHUFFLED;
            for (int prediction : { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0x80, 0x100, 0x100 | choices })
            {
                const bool valid = prediction < 0 || (prediction <= 0xFF && (prediction & ~allowed) == 0);
                const uint8_t mask = prediction < 0 ? choices : uint8_t(prediction);
                vector<uint8_t> enc, dec;
                ASSERT_EQ(BCnEx_Encode(src.data(), src.size(), tid, curve, enc, nullptr, prediction), valid) <<
                    "transform " << unsigned(tid) << ", curve " << unsigned(curve) << ", prediction " << prediction;
                if (!valid)
                {
                    EXPECT_TRUE(enc.empty());
                    continue;
                }
                ASSERT_EQ(enc.size(), src.size());
                EXPECT_EQ(enc[0] >> 3, (curve ? 1 : 0) | (mask != choices ? 2 : 0));
                uint8_t outTid = 0, outCurve = 0;
                EXPECT_TRUE(BCnEx_Decode(enc.data(), enc.size(), BCnEx_TransformId(tid), dec, false, &outTid, &outCurve));
                EXPECT_EQ(outTid, tid);
                EXPECT_EQ(outCurve, curve);
                EXPECT_TRUE(dec == src) << "transform " << unsigned(tid) << ", curve " << unsigned(curve) << ", prediction " << prediction;

                // only the format's EX id decodes the stream, and BC7_EX one of BC5_116116 blocks with parameter 2
                // exactly BCNEX_UNSHUFFLED (a curve-only BC7 stream)
                for (unsigned id = 0; id <= 0x11; id++)
                {
                    const bool decodes = id == BCnEx_TransformId(tid) || (id == GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX &&
                        transform == GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116 && mask == BCNEX_UNSHUFFLED);
                    EXPECT_EQ(BCnEx_Decode(enc.data(), enc.size(), uint8_t(id), dec, false), decodes) << "transform " <<
                        unsigned(tid) << ", curve " << unsigned(curve) << ", prediction " << prediction << ", stream id " << id;
                    EXPECT_TRUE(!decodes || dec == src);
                }

                if (mask == BCNEX_UNSHUFFLED)
                {
                    // neither predicted nor shuffled: every stored block is the source block as it is
                    StreamHeaderInfo info;
                    ASSERT_TRUE(StreamHeader_Parse(enc.data(), enc.size(), bs, info));
                    for (size_t i = 0; i < n; i++)
                    {
                        const size_t s = StreamHeader_MapBlock(info, i);
                        ASSERT_TRUE(s != SIZE_MAX && memcmp(&enc[s * bs], &src[i * bs], bs) == 0) <<
                            "transform " << unsigned(tid) << ", curve " << unsigned(curve) << ", block " << i;
                    }
                }

                if (mask != choices)
                {
                    EXPECT_EQ(enc[at], mask);
                    for (unsigned v = 0; v < 256; v++)
                    {
                        enc[at] = uint8_t(v);
                        EXPECT_EQ(BCnEx_Decode(enc.data(), enc.size(), BCnEx_TransformId(tid), dec, false), v != choices && (v & ~allowed) == 0) <<
                            "transform " << unsigned(tid) << ", curve " << unsigned(curve) << ", parameter value " << v;
                        if (transform == GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116)
                        {
                            EXPECT_EQ(BCnEx_Decode(enc.data(), enc.size(), uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX), dec, false), v == BCNEX_UNSHUFFLED) <<
                                "BC7_EX, curve " << unsigned(curve) << ", parameter value " << v;
                        }
                    }
                }
            }
        }
    }
}

// Streams with too few duplicate blocks store their cheapest blocks in the stream header (coded blocks), with a static
// code for BC1 and one for BC3/4/5 (bc5blockcode.h; BC4 uses the BC3/4/5 code's first 8 byte positions).
#include "../../Lib/shuffle/bc5blockcode.h"

TEST(ShuffleBCnEx, CodedBlocks)
{
    const GACL_SHUFFLE_TRANSFORM transforms[] = { GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224, GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664, GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116, GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266 };
    for (GACL_SHUFFLE_TRANSFORM transform : transforms)
    {
        const uint8_t tid = uint8_t(transform);
        const size_t bs = BCn_BlockSize(tid);
        ASSERT_NE(bs, 0u);
        const bool bc1 = transform == GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224 || transform == GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44;
        const StreamHeaderBlockCode blockCode = { bc1 ? kBC1BlockCode : kBC5BlockCode, false };

        // 512x256 texels of distinct random blocks, eight of them flat (zero indices, bytes 0 and 1 equal and the other
        // endpoint bytes zero, so equal endpoints in BC4-style halves), the cheapest to code
        const size_t n = 128 * 64;
        vector<uint8_t> src(n * bs);
        uint32_t x = tid;
        for (uint8_t& b : src)
        {
            x = x * 1664525u + 1013904223u;
            b = uint8_t(x >> 24);
        }
        vector<size_t> flat;
        for (size_t j = 0; j < 8; j++)
        {
            flat.push_back(j * 1000 + 500);
            uint8_t* b = &src[flat.back() * bs];
            memset(b, 0, bs);
            b[0] = b[1] = uint8_t(120 + j);
            if (bs == 16)
            {
                b[8] = b[9] = uint8_t(140 - j);
            }
        }

        for (uint8_t curve : { uint8_t(0), uint8_t(bs == 8 ? 2 : 4) })
        {
            vector<uint8_t> enc, dec;
            size_t k = 0, coded = 0;
            ASSERT_TRUE(BCnEx_Encode(src.data(), src.size(), tid, curve, enc, &k, -1, &coded)) << "transform " << unsigned(tid) << ", curve " << unsigned(curve);

            // every header block is a coded flat block
            StreamHeaderInfo info;
            ASSERT_TRUE(StreamHeader_Parse(enc.data(), enc.size(), bs, info, &blockCode));
            EXPECT_GE(k, 1u);
            EXPECT_EQ(info.HeaderBlocks, k);
            EXPECT_TRUE(info.Removed.empty());
            ASSERT_EQ(info.Coded.size(), k);
            EXPECT_EQ(coded, k);
            for (size_t c : info.Coded)
            {
                EXPECT_TRUE(std::find(flat.begin(), flat.end(), c) != flat.end()) << "coded block " << c;
            }
            EXPECT_FALSE(StreamHeader_Parse(enc.data(), enc.size(), bs, info));

            uint8_t outTid = 0, outCurve = 0;
            EXPECT_TRUE(BCnEx_Decode(enc.data(), enc.size(), BCnEx_TransformId(tid), dec, false, &outTid, &outCurve));
            EXPECT_EQ(outTid, tid);
            EXPECT_EQ(outCurve, curve);
            EXPECT_TRUE(dec == src) << "transform " << unsigned(tid) << ", curve " << unsigned(curve);
        }
    }
}

// Curve-only EX streams (the replacement for the former GACL_SHUFFLE_TRANSFORM_ZSTD_SC) store the curved blocks after
// the header, neither shuffled nor predicted: the header carries the format's first transform code (BC7: BC5_116116,
// the same block size, with kBC5BlockCode), the curve width and parameter 2 BCNEX_UNSHUFFLED, and like other EX
// streams needs duplicate or coded blocks to make room for itself. They decode under the format's EX id, and BC5_EX
// and BC7_EX both take those of BC5_116116 blocks.
TEST(ShuffleBCnEx, CurveOnly)
{
    struct Case
    {
        GACL_SHUFFLE_TRANSFORM transform;
        uint8_t code;       // header transform code written
        size_t bs;
    };
    const Case cases[] = {
        { GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224, 1, 8 }, { GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44, 1, 8 },
        { GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224, 3, 16 }, { GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664, 3, 16 },
        { GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116, 5, 8 }, { GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116, 6, 16 },
        { GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266, 6, 16 }, { GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN, 6, 16 } };
    for (const Case& t : cases)
    {
        const uint8_t tid = uint8_t(t.transform);
        const uint8_t ex = BCnEx_TransformId(tid);
        const size_t bs = t.bs, n = 128 * 64;                   // 512x256 texels
        const uint8_t curve = uint8_t(bs == 8 ? 2 : 4);         // 512 texels in 256 or 128 texel wide micro-tiles
        const size_t width = size_t(curve) * (bs == 8 ? 256 : 128);
        const StreamHeaderBlockCode blockCode = { t.code == 1 ? kBC1BlockCode : kBC5BlockCode, false };

        // encoder rejections: no curve, a curve the stream doesn't fit, other transform ids, and no room for the header
        // (distinct blocks of 0xFF bytes, each costing more to code than the room it frees)
        vector<uint8_t> enc, dec, full(n * bs, 0xFF);
        for (size_t j = 0; j < n; j++)
        {
            full[j * bs] = uint8_t(j);
            full[j * bs + 1] = uint8_t(0x80 | (j >> 8));
        }
        for (uint8_t c : { uint8_t(0), uint8_t(3) })
        {
            EXPECT_FALSE(BCnEx_EncodeCurveOnly(full.data(), full.size(), tid, c, enc)) << "transform " << unsigned(tid) << ", curve " << unsigned(c);
            EXPECT_TRUE(enc.empty());
        }
        for (GACL_SHUFFLE_TRANSFORM other : { GACL_SHUFFLE_TRANSFORM_NONE, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT,
            GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX, GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_EX,
            GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_EX, GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_EX, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX })
        {
            EXPECT_FALSE(BCnEx_EncodeCurveOnly(full.data(), full.size(), uint8_t(other), curve, enc)) << "transform " << unsigned(other);
        }
        EXPECT_FALSE(BCnEx_EncodeCurveOnly(full.data(), full.size(), tid, curve, enc)) << "transform " << unsigned(tid) << ", no room";
        EXPECT_TRUE(enc.empty());

        // random blocks (in curved order) where every 32nd repeats its predecessor, or distinct with eight flat blocks to
        // code (as in CodedBlocks)
        for (bool coded : { false, true })
        {
            const string at = "transform " + to_string(tid) + (coded ? ", coded" : ", duplicates");
            vector<uint8_t> curved(n * bs), linear(n * bs);
            uint32_t x = tid * 2u + coded;
            for (uint8_t& b : curved)
            {
                x = x * 1664525u + 1013904223u;
                b = uint8_t(x >> 24);
            }
            vector<size_t> flat;
            for (size_t j = 0; coded && j < 8; j++)
            {
                flat.push_back(j * 1000 + 500);
                uint8_t* b = &curved[flat.back() * bs];
                memset(b, 0, bs);
                b[0] = b[1] = uint8_t(120 + j);
                if (bs == 16)
                {
                    b[8] = b[9] = uint8_t(140 - j);
                }
            }
            for (size_t i = 32; !coded && i < n; i += 32)
            {
                memcpy(&curved[i * bs], &curved[(i - 1) * bs], bs);
            }
            ASSERT_TRUE(GACL_Shuffle_ApplySpaceCurve(linear.data(), curved.data(), curved.size(), bs, width, false)) << at;

            size_t k = 0, codedBlocks = SIZE_MAX;
            ASSERT_TRUE(BCnEx_EncodeCurveOnly(curved.data(), curved.size(), tid, curve, enc, &k, &codedBlocks)) << at;
            ASSERT_EQ(enc.size(), curved.size()) << at;
            EXPECT_EQ(enc[0] & 7, t.code) << at;
            EXPECT_EQ(enc[0] >> 3, 3) << at;
            EXPECT_EQ(enc[1], curve) << at;
            EXPECT_EQ(enc[2], BCNEX_UNSHUFFLED) << at;

            StreamHeaderInfo info;
            ASSERT_TRUE(StreamHeader_Parse(enc.data(), enc.size(), bs, info, &blockCode)) << at;
            EXPECT_EQ(info.TransformId, kStreamHeaderTransforms[t.code]) << at;
            EXPECT_GE(k, 1u) << at;
            EXPECT_EQ(info.HeaderBlocks, k) << at;
            EXPECT_EQ(info.Removed.size() + info.Coded.size(), k) << at;
            ASSERT_EQ(info.Coded.size(), codedBlocks) << at;
            EXPECT_EQ(codedBlocks, coded ? k : 0u) << at;
            for (size_t c : info.Coded)
            {
                EXPECT_TRUE(std::find(flat.begin(), flat.end(), c) != flat.end()) << at << ", coded block " << c;
            }

            // the kept blocks follow the header as they are
            for (size_t i = 0; i < n; i++)
            {
                const size_t s = StreamHeader_MapBlock(info, i);
                if (s != SIZE_MAX)
                {
                    ASSERT_TRUE(s >= k && s < n && memcmp(&enc[s * bs], &curved[i * bs], bs) == 0) << at << ", block " << i;
                }
            }

            uint8_t outTid = 0, outCurve = 0;
            EXPECT_TRUE(BCnEx_Decode(enc.data(), enc.size(), ex, dec, false, &outTid, &outCurve)) << at;
            EXPECT_EQ(outTid, kStreamHeaderTransforms[t.code]) << at;
            EXPECT_EQ(outCurve, curve) << at;
            EXPECT_TRUE(dec == curved) << at;
            EXPECT_TRUE(BCnEx_Decode(enc.data(), enc.size(), ex, dec)) << at;
            EXPECT_TRUE(dec == linear) << at;
            for (unsigned id = GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX; id <= GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX; id++)
            {
                const bool decodes = id == ex || (t.code == 6 && (id == GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_EX || id == GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX));
                EXPECT_EQ(BCnEx_Decode(enc.data(), enc.size(), uint8_t(id), dec), decodes) << at << ", stream id " << id;
                EXPECT_TRUE(!decodes || dec == linear) << at << ", stream id " << id;
            }

            // the formats' second patterns (codes 2, 4 and 7) decode the same: the kept blocks aren't shuffled. BC7_EX
            // takes only code 6.
            const vector<uint8_t> good = enc;
            if (t.code != 5)
            {
                enc[0] = uint8_t(good[0] + 1);
                const bool decodes = ex != GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX;
                EXPECT_EQ(BCnEx_Decode(enc.data(), enc.size(), ex, dec), decodes) << at << ", code " << unsigned(t.code + 1);
                EXPECT_TRUE(!decodes || dec == linear) << at << ", code " << unsigned(t.code + 1);
            }

            // decoder rejections: the escape bit, a curve width of 0 or one the stream doesn't fit, a parameter 2 with
            // another bit or naming the default prediction, and parameters 3 and 4
            auto rejects = [&](size_t offset, uint8_t value)
            {
                enc = good;
                enc[offset] = value;
                return !BCnEx_Decode(enc.data(), enc.size(), ex, dec);
            };
            EXPECT_TRUE(rejects(0, uint8_t(good[0] | GACL_STREAMHEADER_ESCAPE))) << at;
            EXPECT_TRUE(rejects(1, 0)) << at;
            EXPECT_TRUE(rejects(1, 3)) << at;
            EXPECT_TRUE(rejects(2, uint8_t(BCNEX_UNSHUFFLED | 8))) << at;
            EXPECT_TRUE(rejects(2, BCnEx_DefaultPrediction(kStreamHeaderTransforms[t.code]))) << at;
            EXPECT_TRUE(rejects(0, uint8_t(good[0] | (1 << 5)))) << at;
            EXPECT_TRUE(rejects(0, uint8_t(good[0] | (1 << 6)))) << at;
            enc = good;
        }
    }
}

// At or below the default zstd level the encoder only uses BC1 coded blocks, which slow the GPU decode, when they make
// the stream at least 1% smaller than the best stream without them; above it the smallest stream wins
TEST(ShuffleBCnEx, BC1CodedBlockGate)
{
    struct Result
    {
        GACL_SHUFFLE_TRANSFORM tid;
        size_t size;
    };
    // GACL_SHUFFLE_TRANSFORM_NONE and the texture's size when it is stored uncompressed (S_FALSE)
    auto compress = [](const vector<uint8_t>& src, GACL_SHUFFLE_TRANSFORM request, int level, vector<uint8_t>* stream = nullptr)
    {
        SHUFFLE_COMPRESS_PARAMETERS params = { src.size(), DXGI_FORMAT_BC1_UNORM, src.data(), { level, 0 }, { 512, src.data() } };
        vector<uint8_t> dest(src.size());
        Result r = { request, 0 };
        const HRESULT hr = GACL_ShuffleCompress_BCn(dest.data(), &r.tid, &r.size, params);
        EXPECT_TRUE(hr == S_OK || hr == S_FALSE) << "request " << unsigned(request) << ", level " << level;
        if (hr != S_OK)
        {
            r = { GACL_SHUFFLE_TRANSFORM_NONE, src.size() };
        }
        else if (stream != nullptr)
        {
            stream->assign(dest.begin(), dest.begin() + r.size);
        }
        return r;
    };
    auto bestHeaderless = [&](const vector<uint8_t>& src, int level)
    {
        // in the encoder's candidate order, which breaks ties
        const GACL_SHUFFLE_TRANSFORM headerless[] = { GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224,
            GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44 };
        Result best = { GACL_SHUFFLE_TRANSFORM_NONE, src.size() };
        for (GACL_SHUFFLE_TRANSFORM t : headerless)
        {
            const Result r = compress(src, t, level);
            if (r.size < best.size)
            {
                best = r;
            }
        }
        return best;
    };
    const StreamHeaderBlockCode blockCode = { kBC1BlockCode, false };
    const int defaultLevel = 14;                            // of 64KB sources
    static const uint8_t kRampIndex[4] = { 0, 2, 3, 1 };    // four-color ramp position -> index
    const size_t n = 128 * 64;                              // 512x256 texels, 64KB

    uint32_t palette[64];
    uint32_t seed = 1;
    for (uint32_t& c : palette)
    {
        seed = seed * 1664525u + 1013904223u;
        c = 0x8000u | (seed >> 17);
    }
    // Distinct four-color blocks (endpoint 0 above 0x7FFF from a palette of 64, endpoint 1 unique below it), m of them
    // (scattered) smooth and the rest noise (random indices), and sixteen flat blocks, the cheapest to code. A smooth
    // block's interior ramp positions are the MED prediction from its random first row and column, leaving zero index
    // prediction residuals. With no duplicate blocks every BC1_EX stream codes blocks in its header, and the more smooth
    // blocks the more it gains over the headerless transforms.
    auto make = [&](size_t m)
    {
        vector<uint8_t> src(n * 8);
        for (size_t j = 0; j < n; j++)
        {
            uint32_t x = uint32_t(j) * 2654435761u;     // the block's own, whatever m
            auto rand8 = [&x] { x = x * 1664525u + 1013904223u; return x >> 24; };
            const uint32_t c0 = palette[rand8() % 64], c1 = uint32_t(j * 0x2F1Bu) & 0x7FFFu;
            uint32_t indices = 0;
            if (((j * 37) & (n - 1)) < m)
            {
                int v[16];
                for (int i = 0; i < 16; i++)
                {
                    if (i < 4 || (i & 3) == 0)
                    {
                        v[i] = int(rand8() & 3);
                    }
                    else
                    {
                        const int l = v[i - 1], t = v[i - 4], tl = v[i - 5], mx = (std::max)(l, t), mn = (std::min)(l, t);
                        v[i] = tl >= mx ? mn : (tl <= mn ? mx : l + t - tl);
                    }
                    indices |= uint32_t(kRampIndex[v[i]]) << (2 * i);
                }
            }
            else
            {
                for (int k = 0; k < 4; k++)
                {
                    indices |= rand8() << (8 * k);
                }
            }
            const uint32_t block[2] = { c0 | c1 << 16, indices };
            memcpy(&src[j * 8], block, 8);
        }
        for (size_t i = 0; i < 16; i++)
        {
            uint8_t* b = &src[(i * 500 + 300) * 8];
            memset(b, 0, 8);
            b[0] = b[1] = uint8_t(120 + i);
        }
        return src;
    };
    struct Probe
    {
        size_t m;
        vector<uint8_t> src, stream;    // stream: ex's
        Result plain, ex;
        bool keep;                      // ex saves 1% over plain
    };
    auto probe = [&](size_t m)
    {
        Probe p = { m, make(m) };
        p.plain = bestHeaderless(p.src, 0);
        p.ex = compress(p.src, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX, 0, &p.stream);
        p.keep = p.ex.tid == GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX && p.ex.size * 100 <= p.plain.size * 99;
        return p;
    };

    // bisect the number of smooth blocks for the 1% line, at the default level (0)
    Probe lo = probe(0), hi = probe(n);
    ASSERT_FALSE(lo.keep) << "no smooth blocks: BC1_EX " << lo.ex.size << ", headerless " << lo.plain.size;
    ASSERT_TRUE(hi.keep) << "all smooth: BC1_EX " << hi.ex.size << ", headerless " << hi.plain.size;
    while (hi.m - lo.m > 1)
    {
        Probe p = probe((lo.m + hi.m) / 2);
        Probe& side = p.keep ? hi : lo;
        side = std::move(p);
    }
    // so lo's BC1_EX stream is smaller, just not by 1%
    EXPECT_LT(lo.ex.size, lo.plain.size) << lo.m << " smooth blocks";

    for (const Probe* p : { &lo, &hi })
    {
        const string at = to_string(p->m) + " smooth blocks";
        vector<uint64_t> blocks(n);
        memcpy(blocks.data(), p->src.data(), p->src.size());
        sort(blocks.begin(), blocks.end());
        ASSERT_TRUE(adjacent_find(blocks.begin(), blocks.end()) == blocks.end()) << at;

        // with no duplicate blocks, the BC1_EX stream codes blocks in its header
        ASSERT_EQ(p->ex.tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX) << at;
        vector<uint8_t> unzipped(p->src.size());
        ASSERT_EQ(ZSTD_decompress(unzipped.data(), unzipped.size(), p->stream.data(), p->stream.size()), unzipped.size()) << at;
        StreamHeaderInfo info;
        ASSERT_TRUE(StreamHeader_Parse(unzipped.data(), unzipped.size(), 8, info, &blockCode)) << at;
        EXPECT_TRUE(info.Removed.empty()) << at;
        EXPECT_GE(info.Coded.size(), 1u) << at;

        // at the default level (0, or 14 requested) it must save 1% over the best headerless stream
        for (int level : { 0, defaultLevel })
        {
            const Result any = compress(p->src, GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL, level);
            EXPECT_EQ(any.tid, p->keep ? p->ex.tid : p->plain.tid) << at << ", level " << level << ", BC1_EX " << p->ex.size << ", headerless " << p->plain.size;
            EXPECT_EQ(any.size, p->keep ? p->ex.size : p->plain.size) << at << ", level " << level;
        }

        // above it the smallest stream wins: BC1_EX for both
        const Result ex1 = compress(p->src, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX, defaultLevel + 1);
        const Result plain1 = bestHeaderless(p->src, defaultLevel + 1);
        const Result any1 = compress(p->src, GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL, defaultLevel + 1);
        EXPECT_LT(ex1.size, plain1.size) << at;
        EXPECT_EQ(any1.tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX) << at << ", BC1_EX " << ex1.size << ", headerless " << plain1.size;
        EXPECT_EQ(any1.size, ex1.size) << at;
    }
}

struct SC_BC7TestParam
{
    wstring file;
    size_t size;        // best of all experimental BC7 transforms
    size_t joinSize;    // BC7 Join, requested explicitly (GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN)
    GACL_SHUFFLE_TRANSFORM joinTid;    // BC7_EX, or plain zstd where Join does not beat it

    friend std::ostream& operator<<(std::ostream& os, const SC_BC7TestParam& param)
    {
        return os << param.file.c_str();
    }
};

//helper functions for testing api
class SC_BC7_Test : public ::testing::TestWithParam<SC_BC7TestParam>
{
};

TEST_P(SC_BC7_Test, LargeBC7_ShuffleCompress) {
    const auto& p = GetParam();
    filesystem::path filepath = GetAssetsDirectory() / "BC7mip0" / p.file;

    vector<uint8_t> sc;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;
    HRESULT hr = ValidateRoundTripShuffleCompress(filepath, tid, true, sc);

    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT);
    EXPECT_EQ(sc.size(), p.size);    // known size for this asset shuffle compressed in this way
}

// Split wins every file above, so cover the BC7 Join round trip by requesting Join explicitly (a BC7_EX request also
// tries the curve-only stream, which wins on two of them: LargeBC7_ShuffleCompress_CurveOnly)
TEST_P(SC_BC7_Test, LargeBC7_ShuffleCompress_Join) {
    const auto& p = GetParam();
    filesystem::path filepath = GetAssetsDirectory() / "BC7mip0" / p.file;

    vector<uint8_t> sc;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN;
    HRESULT hr = ValidateRoundTripShuffleCompress(filepath, tid, true, sc);

    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(tid, p.joinTid);
    EXPECT_EQ(sc.size(), p.joinSize);    // known size for this asset shuffle compressed in this way
}

// These files were selected in order to yield coverage of the general pattners and BC7 features

INSTANTIATE_TEST_SUITE_P(
    SC,
    SC_BC7_Test,
    ::testing::Values(
        SC_BC7TestParam{ L"Ladybug_Body_Spec_mask.DDS",  18764, 30511, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX },
        SC_BC7TestParam{ L"Ladybug_Body_albedo.DDS",  3551109, 3782929, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX },
        SC_BC7TestParam{ L"gerberaSingleLeaf_sss.DDS", 865691, 1016269, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX },
        SC_BC7TestParam{ L"Ladybug_DetailSkin_normal.DDS", 433627, 442505, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX },
        SC_BC7TestParam{ L"BlackboardEraser_albedo.DDS", 501855, 540978, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX },
        SC_BC7TestParam{ L"GerberaLeafHigh_normal.DDS", 795504, 822761, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX },
        SC_BC7TestParam{ L"LetterBlock_LetterA_albedo.DDS", 2159634, 2371701, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX },
        SC_BC7TestParam{ L"Ladybug_Shell_Roughness_mask.DDS", 6829, 9413, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX }
    )
);


/*  GACL_Compression_CompressBuffer - non-texture data, no shuffle transform  */

// Repetitive enough to compress well, without being so uniform that the result
// is dominated by RLE of a single byte.
static vector<uint8_t> MakePatternedBuffer(size_t sizeInBytes)
{
    vector<uint8_t> buffer(sizeInBytes);
    for (size_t i = 0; i < sizeInBytes; i++)
    {
        buffer[i] = static_cast<uint8_t>((i / 64) % 17);
    }
    return buffer;
}

// Fixed seed, so an incompressible case cannot vary between runs.
static vector<uint8_t> MakeRandomBuffer(size_t sizeInBytes)
{
    vector<uint8_t> buffer(sizeInBytes);
    mt19937 rng(12345);
    for (size_t i = 0; i < sizeInBytes; i++)
    {
        buffer[i] = static_cast<uint8_t>(rng());
    }
    return buffer;
}

static void ExpectRoundTrip(const vector<uint8_t>& src, const GACL_COMPRESS_BUFFER_PARAMETERS* params)
{
    vector<uint8_t> dest(src.size());
    size_t destBytesWritten = SIZE_MAX;

    HRESULT hr = GACL_Compression_CompressBuffer(dest.data(), src.size(), &destBytesWritten, src.data(), params);

    ASSERT_EQ(hr, S_OK);
    EXPECT_GT(destBytesWritten, 0u);
    EXPECT_LT(destBytesWritten, src.size());       // S_OK is only returned when the result is smaller

    vector<uint8_t> decompressed(src.size());
    size_t decompressedBytes = ZSTD_decompress(decompressed.data(), decompressed.size(), dest.data(), destBytesWritten);

    ASSERT_FALSE(ZSTD_isError(decompressedBytes));
    ASSERT_EQ(decompressedBytes, src.size());
    EXPECT_EQ(0, memcmp(decompressed.data(), src.data(), src.size()));
}

static void ExpectNotSmaller(const vector<uint8_t>& src)
{
    vector<uint8_t> dest(src.size(), 0xCD);
    size_t destBytesWritten = SIZE_MAX;

    HRESULT hr = GACL_Compression_CompressBuffer(dest.data(), src.size(), &destBytesWritten, src.data(), nullptr);

    EXPECT_EQ(hr, S_FALSE);
    EXPECT_EQ(destBytesWritten, 0u);
    EXPECT_EQ(dest, vector<uint8_t>(src.size(), 0xCD));  // destination left untouched
}

TEST(CompressBuffer, RejectsInvalidArguments) {
    vector<uint8_t> src = MakePatternedBuffer(4096);
    vector<uint8_t> dest(src.size());
    size_t destBytesWritten = SIZE_MAX;

    EXPECT_EQ(GACL_Compression_CompressBuffer(nullptr, src.size(), &destBytesWritten, src.data(), nullptr), E_INVALIDARG);
    EXPECT_EQ(destBytesWritten, 0u);               // zeroed even on the failure paths

    destBytesWritten = SIZE_MAX;
    EXPECT_EQ(GACL_Compression_CompressBuffer(dest.data(), src.size(), &destBytesWritten, nullptr, nullptr), E_INVALIDARG);
    EXPECT_EQ(destBytesWritten, 0u);

    destBytesWritten = SIZE_MAX;
    EXPECT_EQ(GACL_Compression_CompressBuffer(dest.data(), 0, &destBytesWritten, src.data(), nullptr), E_INVALIDARG);
    EXPECT_EQ(destBytesWritten, 0u);

    EXPECT_EQ(GACL_Compression_CompressBuffer(dest.data(), src.size(), nullptr, src.data(), nullptr), E_INVALIDARG);
}

TEST(CompressBuffer, RoundTripsCompressibleData) {
    ExpectRoundTrip(MakePatternedBuffer(256 * 1024), nullptr);
}

// Exercises the size buckets GACL_SelectCompressionParameters() switches on.
TEST(CompressBuffer, RoundTripsAcrossSizeBuckets) {
    for (size_t sizeInBytes : { size_t(1024), size_t(16 * 1024), size_t(64 * 1024), size_t(1024 * 1024) })
    {
        SCOPED_TRACE(sizeInBytes);
        ExpectRoundTrip(MakePatternedBuffer(sizeInBytes), nullptr);
    }
}

TEST(CompressBuffer, RoundTripsWithExplicitParameters) {
    GACL_COMPRESS_BUFFER_PARAMETERS params = { 19, 1 << 14 };
    ExpectRoundTrip(MakePatternedBuffer(256 * 1024), &params);
}

// Not smaller by construction: the zstd frame header alone exceeds the input,
// so no encoding of these bytes can come in under sizeInBytes.
TEST(CompressBuffer, ReportsTinyInputAsSFalse) {
    ExpectNotSmaller(MakePatternedBuffer(4));
}

// Not smaller because the content carries no redundancy to exploit.
TEST(CompressBuffer, ReportsRandomDataAsSFalse) {
    ExpectNotSmaller(MakeRandomBuffer(256 * 1024));
}

// A BC7_EX request also tries the curve-only stream (the curved blocks unshuffled, parameter 2 BCNEX_UNSHUFFLED), which
// beats Join on these two masks
TEST(ShuffleCompress, LargeBC7_ShuffleCompress_CurveOnly) {
    const pair<const wchar_t*, size_t> files[] = { { L"Ladybug_Body_Spec_mask.DDS", 29016 }, { L"Ladybug_Shell_Roughness_mask.DDS", 9004 } };
    for (const auto& [file, size] : files)
    {
        vector<uint8_t> sc;
        GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX;
        HRESULT hr = ValidateRoundTripShuffleCompress(GetAssetsDirectory() / "BC7mip0" / file, tid, true, sc);

        EXPECT_EQ(hr, S_OK);
        EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX);
        EXPECT_EQ(sc.size(), size);    // known size for this asset compressed in this way

        // header: transform code 6 (BC5_116116, as BC7 blocks are carried), the curve width and BCNEX_UNSHUFFLED
        const unsigned long long streamSize = ZSTD_getFrameContentSize(sc.data(), sc.size());
        ASSERT_TRUE(streamSize >= GACL_STREAMHEADER_FIXED_BYTES + 2 && streamSize < (1ull << 32));
        vector<uint8_t> stream(static_cast<size_t>(streamSize));
        ASSERT_EQ(ZSTD_decompress(stream.data(), stream.size(), sc.data(), sc.size()), stream.size());
        EXPECT_EQ(stream[0], 0x1E);
        EXPECT_NE(stream[1], 0);
        EXPECT_EQ(stream[2], BCNEX_UNSHUFFLED);
    }
}

// The BC7 Join GPU decoder (Shaders/Shuffle/BC7JoinDecode.hlsli) embeds tables derived from the library's Join transform as
// BC7JoinTables.hlsli, which this test checks and generates: when they differ it fails and writes the regenerated file to test_output.
#include <random>
#include "../../Lib/shuffle/bc7.h"
#include "../../Lib/shuffle/bc7joinremap.h"
#include "../../Lib/shuffle/bc7joinblockcode.h"
namespace BC7JoinShader
{
    using uint = uint32_t;
#include "../../Shaders/Shuffle/BC7JoinTables.hlsli"
}

static void WriteShaderTable(std::ostream& os, const char* comment, const char* name, const uint32_t* t, size_t n)
{
    os << comment << "static const uint " << name << "[" << n << "] =\n{";
    for (size_t i = 0; i < n; i++)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s0x%08x,", (i % 8) ? " " : "\n    ", t[i]);
        os << buf;
    }
    os << "\n};\n\n";
}

namespace
{
    // T rows of BC7JoinDecode.hlsli: the first row of each mode (Modes 1/3/7 have one per anchor class) and the mode of each row
    const unsigned kJoinTestRowBase[8] = { 0, 1, 5, 6, 10, 11, 12, 13 };
    const unsigned kJoinTestRowMode[17] = { 0, 1, 1, 1, 1, 2, 3, 3, 3, 3, 4, 5, 6, 7, 7, 7, 7 };

    // Second anchor texel of the BC7 2-subset partitions (the shader takes it from kIndexPredictionAnchor2)
    const uint8_t kJoinTestAnchor2[64] =
    {
        15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
        15,  2,  8,  2,  2,  8,  8, 15,  2,  8,  2,  2,  8,  8,  2,  2,
        15, 15,  6,  8,  2,  8, 15, 15,  2,  8,  2,  2,  2, 15, 15,  6,
         6,  2,  6,  8, 15, 15,  2,  2, 15, 15, 15, 15, 15,  2,  2, 15,
    };

    bool JoinTestBit(const uint64_t* x, unsigned b)
    {
        return ((x[b >> 6] >> (b & 63)) & 1) != 0;
    }

    unsigned JoinTestByte(const uint32_t* t, unsigned i)
    {
        return (t[i >> 2] >> ((i & 3) * 8)) & 0xFF;
    }

    void JoinTestSetByte(uint32_t* t, unsigned i, unsigned v)
    {
        t[i >> 2] = (t[i >> 2] & ~(0xFFu << ((i & 3) * 8))) | (v << ((i & 3) * 8));
    }

    // Anchor class of a Mode 1/3/7 block, as JoinDecodeBlock finds it from the partition bits of the stored block (122-127 /
    // 0-5, where the 24-bit rotation puts them; the index transforms don't move them): second anchor texel 15 / 2 / 8 / 6
    unsigned JoinTestClass(unsigned m, const uint64_t* s)
    {
        if (m != 1 && m != 3 && m != 7)
        {
            return 0;
        }
        const unsigned a = kJoinTestAnchor2[m == 1 ? unsigned(s[1] >> 58) : unsigned(s[0] & 63)];
        return a == 15 ? 0 : (a == 2 ? 1 : (a == 8 ? 2 : 3));
    }

    // BC7_ModeJoin_Transform after the layout and the 24-bit rotation, up to the byte remap: the index bit reversal (JBO, of
    // the bit ranges BC7_ModeJoin_Reverse restores), then the index field permutation (JFP)
    void JoinTestStore(uint64_t* x, unsigned m, uint64_t jfp)
    {
        static const uint8_t kIdxLo[8] = { 0, 0, 1, 6, 0, 0, 1, 6 }, kIdxHi[8] = { 48, 48, 30, 40, 78, 62, 64, 40 };
        if ((BC7_ModeJoin_IndexReverseModes() >> m) & 1)
        {
            const uint64_t s[2] = { x[0], x[1] };
            for (unsigned k = kIdxLo[m]; k < kIdxHi[m]; k++)
            {
                const unsigned j = kIdxLo[m] + kIdxHi[m] - 1 - k;
                x[j >> 6] = (x[j >> 6] & ~(1ull << (j & 63))) | (uint64_t(JoinTestBit(s, k)) << (j & 63));
            }
        }
        if (jfp)
        {
            BC7_ModeJoin_IndexFieldPermute(x, m, jfp, false);
        }
    }

    // Output bits of the map f that input bit b flips, relative to base
    template <class F>
    void JoinTestFlip(F f, const uint64_t* base, unsigned b, uint64_t* d)
    {
        uint64_t a[2] = { base[0], base[1] }, t[2] = { base[0], base[1] };
        t[b >> 6] ^= 1ull << (b & 63);
        f(a);
        f(t);
        d[0] = a[0] ^ t[0];
        d[1] = a[1] ^ t[1];
    }

    // The bit of a single-bit d, else 128
    unsigned JoinTestSingle(const uint64_t* d)
    {
        unsigned n = 0, b = 128;
        for (unsigned i = 0; i < 128; i++)
        {
            if (JoinTestBit(d, i))
            {
                n++;
                b = i;
            }
        }
        return n == 1 ? b : 128;
    }

    // J: the stored bit of bit q of a rotated mode m block of anchor class cls, Modes 0 and 2 only reversing their index bits
    // (BC7JoinDecode.hlsli has the rows of mask 0 composed in kBC7JoinGather0, and JoinMaskedJ for the bits a mask moves)
    unsigned JoinTestJ(const uint32_t* perm, unsigned m, unsigned cls, unsigned q)
    {
        if (m == 0)
        {
            return q < 48 ? 47 - q : q;
        }
        if (m == 2)
        {
            return (q - 1) < 29 ? 30 - q : q;
        }
        if (m >= 4 && m <= 6)
        {
            return JoinTestByte(perm, 128 * (m - 4) + q);
        }
        if (q >= 48)
        {
            return q;
        }
        return JoinTestByte(perm, 4 * (96 + 12 * ((m == 1 ? 0 : (m == 3 ? 4 : 8)) + cls)) + q);
    }
}

TEST(ShuffleTables, BC7JoinShaderTables)
{
    // the shader implements the production Join field transforms, with the index bit reversal of Modes 0-3 and 7, and
    // has one table set: one byte remap set and one set of JFP settings; Modes 0 and 2 only reverse their index bits
    EXPECT_EQ(BC7_ModeJoin_FieldConfig(), 1);
    EXPECT_EQ(BC7_ModeJoin_IndexReverseModes(), 0x8Fu);
    EXPECT_EQ(BC7_ModeJoin_RemapSets(), 1u);
    uint64_t jfp[8];
    for (unsigned m = 0; m < 8; m++)
    {
        jfp[m] = BC7_ModeJoin_IndexFieldPerm(m);
    }
    EXPECT_EQ(jfp[0], 0u);
    EXPECT_EQ(jfp[2], 0u);

    // inverse byte remap, built as BC7_ModeJoin_ByteRemap builds it, T[m][p][remap[m][p][v]] = v: the stored values below 64
    // first (16 words per (m, p)), then the others (48 words per (m, p))
    auto remapByte = [](unsigned m, unsigned p, unsigned w)
    {
        const unsigned mp = m * 16 + p;
        return 4 * (w < 64 ? (mp << 4) | (w >> 2) : 2048 + mp * 48 + ((w - 64) >> 2)) + (w & 3);
    };
    vector<uint32_t> invRemap(8192, 0);
    for (unsigned m = 0; m < 8; m++)
        for (unsigned p = 0; p < 16; p++)
            for (unsigned v = 0; v < 256; v++)
            {
                JoinTestSetByte(invRemap.data(), remapByte(m, p, kBC7JoinRemap[m][p][v]), v);
            }
    // the remap is a bijection, keeps the mode bits of byte 15 (the shader finds the mode before undoing it), doesn't
    // depend on the masks, and is off for JoinRemap 0
    size_t remapErrors = 0;
    for (unsigned m = 0; m < 8; m++)
        for (unsigned p = 0; p < 16; p++)
            for (unsigned v = 0; v < 256; v++)
            {
                const unsigned w = kBC7JoinRemap[m][p][v], modeBits = (2u << m) - 1;
                remapErrors += JoinTestByte(invRemap.data(), remapByte(m, p, w)) != v;
                remapErrors += p == 15 && (v & modeBits) == (1u << m) && (w & modeBits) != (1u << m);
            }
    EXPECT_EQ(remapErrors, 0u);
    std::mt19937_64 rng(1);
    {
        BC7TextureMetrics off = {}, none = {}, all = {};
        none.JoinRemap = 1;
        all.JoinRemap = 1;
        all.Statics = 0xFF;
        all.LowEntropy = 0xFF;
        size_t maskErrors = 0, offErrors = 0, remapped = 0;
        for (unsigned i = 0; i < 4096; i++)
        {
            const uint64_t x[2] = { rng(), rng() };
            uint64_t a[2] = { x[0], x[1] }, b[2] = { x[0], x[1] }, c[2] = { x[0], x[1] };
            BC7_ModeJoin_ByteRemap(reinterpret_cast<uint8_t*>(a), false, none);
            BC7_ModeJoin_ByteRemap(reinterpret_cast<uint8_t*>(b), false, all);
            BC7_ModeJoin_ByteRemap(reinterpret_cast<uint8_t*>(c), false, off);
            maskErrors += a[0] != b[0] || a[1] != b[1];
            offErrors += c[0] != x[0] || c[1] != x[1];
            remapped += a[0] != x[0] || a[1] != x[1];
        }
        EXPECT_EQ(maskErrors, 0u);
        EXPECT_EQ(offErrors, 0u);
        EXPECT_GT(remapped, 4000u);     // every block but the mode 8 ones (byte 15 zero, 1 in 256)
    }

    // J rows: the stored bit (before the byte remap) of each bit q of a rotated block (after the 24-bit rotation), traced
    // through the JBO / JFP. Modes 4-6 at word 32 * (m - 4); Modes 1/3/7 (k = 0 / 1 / 2) of anchor class c at word
    // 96 + 12 * (4 * k + c), bits q < 48, traced on the first partition of the class: the partition bits (Modes 3/7: 0-5)
    // select the class and don't move
    uint32_t perm[240] = {};
    for (unsigned lane = 0; lane < 15; lane++)
    {
        static const unsigned kModes[3] = { 1, 3, 7 }, kClassPartition[4] = { 0, 17, 18, 34 };
        const unsigned m = lane < 3 ? 4 + lane : kModes[(lane - 3) >> 2], cls = lane < 3 ? 0 : (lane - 3) & 3;
        const unsigned row = lane < 3 ? 32 * lane : 96 + 12 * (lane - 3), bits = lane < 3 ? 128 : 48;
        uint64_t base[2] = { 0, 1ull << (56 + m) };     // the mode bit, at 120 + m after the rotation
        const unsigned pLo = m == 1 ? 122 : 0, pHi = lane < 3 ? 0 : pLo + 6;
        if (pHi)
        {
            base[pLo >> 6] |= uint64_t(kClassPartition[cls]) << (pLo & 63);
            EXPECT_EQ(JoinTestClass(m, base), cls);
        }
        for (unsigned q = 0; q < bits; q++)
        {
            unsigned j = q;
            if (q < pLo || q >= pHi)
            {
                uint64_t d[2];
                JoinTestFlip([&](uint64_t* x) { JoinTestStore(x, m, jfp[m]); }, base, q, d);
                j = JoinTestSingle(d);
                EXPECT_LT(j, 128u) << "mode " << m << ", class " << cls << ", bit " << q;
            }
            JoinTestSetByte(perm, 4 * row + q, j & 0x7F);
        }
    }

    // L: the layout bit of each raw bit of every mode without statics, traced through BC7_ModeJoin_LayoutShuffle as
    // BuildLayoutInverse (bc7modejoinreverse.cpp) traces it: the mode bits r <= m at 96 + r, raw bit r > m relative to the
    // block holding only the mode bit. Mode 4 stores rot0 ^ rot1: raw bit 5 also flips the bit raw bit 6 flips, and L(5) is
    // its other bit (the shader then flips bit 6 by bit 5).
    const BC7TextureMetrics noStatics = {};
    auto layout = [](unsigned m, uint64_t* x, const BC7TextureMetrics& metrics)
    {
        const uint64_t in[2] = { x[0], x[1] };
        x[0] = x[1] = 0;    // the fast paths may OR into the output
        BC7_ModeJoin_LayoutShuffle(m, in, x, metrics);
    };
    unsigned L[8][128];
    for (unsigned m = 0; m < 8; m++)
    {
        const uint64_t base[2] = { 1ull << m, 0 };
        uint64_t o[2] = { base[0], base[1] };
        layout(m, o, noStatics);
        EXPECT_TRUE(o[0] == 0 && o[1] == (1ull << (32 + m))) << "mode " << m;
        for (unsigned r = 0; r <= m; r++)
        {
            L[m][r] = 96 + r;
        }
        for (unsigned r = 127; r > m; r--)
        {
            uint64_t d[2];
            JoinTestFlip([&](uint64_t* x) { layout(m, x, noStatics); }, base, r, d);
            L[m][r] = JoinTestSingle(d);
            if (m == 4 && r == 5 && L[m][r] == 128 && L[m][6] < 128 && JoinTestBit(d, L[m][6]))
            {
                d[L[m][6] >> 6] ^= 1ull << (L[m][6] & 63);
                L[m][r] = JoinTestSingle(d);
            }
            EXPECT_LT(L[m][r], 128u) << "mode " << m << ", raw bit " << r;
        }
    }

    // T rows of every mode without statics (Gather0): T(r) = J((L(r) + 24) mod 128), each a permutation of the 128 bits
    uint32_t gather0[544] = {};
    for (unsigned R = 0; R < 17; R++)
    {
        const unsigned m = kJoinTestRowMode[R], cls = R - kJoinTestRowBase[m];
        bool seen[128] = {};
        for (unsigned r = 0; r < 128; r++)
        {
            const unsigned t = JoinTestJ(perm, m, cls, (L[m][r] + 24) & 127);
            EXPECT_FALSE(seen[t]) << "row " << R << ", raw bit " << r;
            seen[t] = true;
            JoinTestSetByte(gather0, 128 * R + r, t);
        }
    }

    // every mode and anchor class: random raw blocks without statics through the library's layout, the 24-bit rotation and
    // the JBO / JFP, and back through the shader's gather (class from the stored partition bits) and the Mode 4 fix-up.
    // Modes 0-3 ignore mask bits 6-7 (the shader copies their Gather0 rows when the low 6 bits are clear).
    BC7TextureMetrics high = {};
    high.Statics = 0xC0;
    for (unsigned m = 0; m < 8; m++)
    {
        size_t gatherErrors = 0, highErrors = 0;
        for (unsigned i = 0; i < 2048; i++)
        {
            const uint64_t x[2] = { (rng() << (m + 1)) | (1ull << m), rng() };
            uint64_t o[2] = { x[0], x[1] };
            layout(m, o, noStatics);
            if (m < 4)
            {
                uint64_t h[2] = { x[0], x[1] };
                layout(m, h, high);
                highErrors += h[0] != o[0] || h[1] != o[1];
            }
            uint64_t s[2] = { (o[0] << 24) | (o[1] >> 40), (o[1] << 24) | (o[0] >> 40) };
            JoinTestStore(s, m, jfp[m]);
            const unsigned R = kJoinTestRowBase[m] + JoinTestClass(m, s);
            uint64_t g[2] = {};
            for (unsigned r = 0; r < 128; r++)
            {
                g[r >> 6] |= uint64_t(JoinTestBit(s, JoinTestByte(gather0, 128 * R + r))) << (r & 63);
            }
            if (m == 4)
            {
                g[0] ^= (g[0] & 0x20) << 1;
            }
            gatherErrors += g[0] != x[0] || g[1] != x[1];
        }
        EXPECT_EQ(gatherErrors, 0u) << "mode " << m;
        EXPECT_EQ(highErrors, 0u) << "mode " << m;
    }

    // JoinBuildMode (BC7JoinDecode.hlsli) builds the T rows of a mode with a mask from its Gather0 rows: the raw bits outside
    // [kJoinMaskedLo, kJoinMaskedHi) keep their positions under every mask, and those inside get J of their layout position
    // from JoinMaskedJ, for every anchor class. Mode 4's raw bit 5 is traced as in L.
    {
        static const unsigned kMaskedLo[8] = { 5, 8, 9, 10, 5, 8, 7, 14 }, kMaskedHi[8] = { 77, 80, 99, 94, 50, 66, 65, 94 };
        auto maskedJ = [](unsigned m, unsigned q) -> unsigned
        {
            if (m == 4)
            {
                return q < 108 ? q + 2 : (q < 110 ? q - 108 : q);
            }
            if (m == 5)
            {
                return q < 118 ? q + 2 : (q < 120 ? q - 56 : q);
            }
            return m == 6 && q == 0 ? 63 : q;
        };
        for (unsigned m = 0; m < 8; m++)
        {
            const uint64_t base[2] = { 1ull << m, 0 };
            const unsigned classes = (m == 1 || m == 3 || m == 7) ? 4 : 1;
            size_t fixedErrors = 0, maskedErrors = 0;
            for (unsigned mask = 1; mask < 256; mask++)
            {
                if ((mask & (m < 4 ? 0x3F : 0xFF)) == 0)
                {
                    continue;
                }
                BC7TextureMetrics metrics = {};
                metrics.Statics = uint8_t(mask);
                unsigned Lm[128];
                for (unsigned r = 127; r > m; r--)
                {
                    uint64_t d[2];
                    JoinTestFlip([&](uint64_t* x) { layout(m, x, metrics); }, base, r, d);
                    Lm[r] = JoinTestSingle(d);
                    if (m == 4 && r == 5 && Lm[r] == 128 && Lm[6] < 128 && JoinTestBit(d, Lm[6]))
                    {
                        d[Lm[6] >> 6] ^= 1ull << (Lm[6] & 63);
                        Lm[r] = JoinTestSingle(d);
                    }
                    if (r < kMaskedLo[m] || r >= kMaskedHi[m])
                    {
                        fixedErrors += Lm[r] != L[m][r];
                        continue;
                    }
                    const unsigned q = (Lm[r] + 24) & 127;
                    for (unsigned cls = 0; cls < classes; cls++)
                    {
                        maskedErrors += Lm[r] >= 128 || JoinTestJ(perm, m, cls, q) != maskedJ(m, q);
                    }
                }
            }
            EXPECT_EQ(fixedErrors, 0u) << "mode " << m;
            EXPECT_EQ(maskedErrors, 0u) << "mode " << m;
        }
    }

    // JoinBuildMode reads only the words of the anchor class rows of Modes 1/3/7 that differ from their class 0 row (cw1..cw3)
    {
        static const unsigned kClassModes[3] = { 1, 3, 7 };
        static const uint32_t kClassWords[3][3] =
        {
            { 0xFFC00000, 0xFC000000, 0xFEC00000 },
            { 0x74000000, 0x7F000000, 0x70000000 },
            { 0x77000000, 0x7C000000, 0x70000000 },
        };
        for (unsigned k = 0; k < 3; k++)
        {
            const unsigned R0 = kJoinTestRowBase[kClassModes[k]];
            for (unsigned c = 1; c < 4; c++)
            {
                uint32_t words = 0;
                for (unsigned u = 0; u < 32; u++)
                {
                    words |= uint32_t(gather0[32 * (R0 + c) + u] != gather0[32 * R0 + u]) << u;
                }
                EXPECT_EQ(words, kClassWords[k][c - 1]) << "mode " << kClassModes[k] << ", class " << c;
            }
        }
    }

    using namespace BC7JoinShader;
    const bool same = sizeof(kBC7JoinInvRemap) == invRemap.size() * 4 && !memcmp(kBC7JoinInvRemap, invRemap.data(), sizeof(kBC7JoinInvRemap)) &&
        sizeof(kBC7JoinPerm) == sizeof(perm) && !memcmp(kBC7JoinPerm, perm, sizeof(perm)) &&
        sizeof(kBC7JoinGather0) == sizeof(gather0) && !memcmp(kBC7JoinGather0, gather0, sizeof(gather0));
    if (!same)
    {
        const filesystem::path path = GetTestOutputDirectory() / "BC7JoinTables.hlsli";
        ofstream os(path);
        os << "//-------------------------------------------------------------------------------------\n"
              "// BC7JoinTables.hlsli\n"
              "//\n"
              "// Game Asset Conditioning Library - Microsoft toolkit for game asset compression\n"
              "//\n"
              "// Copyright (c) Microsoft Corporation.\n"
              "// Licensed under the MIT License.\n"
              "//\n"
              "//-------------------------------------------------------------------------------------\n"
              "// GENERATED by gacl_lib_tests (ShuffleTables.BC7JoinShaderTables) from the library's BC7 Join transform (Lib/shuffle/bc7joinremap.h,\n"
              "// BC7_ModeJoin_LayoutShuffle, BC7_ModeJoin_IndexFieldPermute). The test fails when this file differs from them and writes a\n"
              "// regenerated copy to its test_output folder; do not edit. Valid HLSL and C++ (uint = uint32_t). 35,904 bytes of tables.\n"
              "\n"
              "#ifndef BC7_JOIN_TABLES_HLSLI\n"
              "#define BC7_JOIN_TABLES_HLSLI\n"
              "\n";
        WriteShaderTable(os, "// Inverse Join byte remap (32 KB): byte p = w of a stored Mode m block was (kBC7JoinInvRemap[i] >> ((w & 3) * 8)) & 0xFF,\n"
                             "// i = ((m * 16 + p) << 4) | (w >> 2) for the common values w < 64 (the first 8 KB), else 2048 + (m * 16 + p) * 48 + ((w - 64) >> 2)\n",
                         "kBC7JoinInvRemap", invRemap.data(), invRemap.size());
        WriteShaderTable(os, "// J rows: bit q of a block after the 24-bit rotation is stored (before the byte remap) at bit (kBC7JoinPerm[r + (q >> 2)] >>\n"
                             "// ((q & 3) * 8)) & 0xFF, through the index field permutation (JFP) and the Mode 1/3/7 index bit reversal (JBO). r = 32 * (m - 4)\n"
                             "// for Modes 4-6; Modes 1 / 3 / 7 of anchor class c (second anchor texel 15 / 2 / 8 / 6): r = 96 + 12 * (4 * {0, 1, 2} + c), q < 48\n",
                         "kBC7JoinPerm", perm, 240);
        WriteShaderTable(os, "// T rows of every mode without statics (mask 0), 17 x 128 bytes: raw bit r of a row R block is stored bit\n"
                             "// (kBC7JoinGather0[32 * R + (r >> 2)] >> ((r & 3) * 8)) & 0xFF; Mode 4 stores rot0 ^ rot1 at T(6). Rows as kJoinRowBase /\n"
                             "// kJoinRowMode (BC7JoinDecode.hlsli).\n",
                         "kBC7JoinGather0", gather0, 544);
        os << "#endif\n";
        EXPECT_TRUE(same) << "Shaders/Shuffle/BC7JoinTables.hlsli is out of date; regenerated: " << path.string();
    }
}

// BC7 Join coded blocks are always coded remapped with set 1, the domain the coded-block code was trained on
// (BC7_JoinBlockCode): a stream without the byte remap (no parameter 4) codes the remapped forms of its blocks and
// decodes them back, so its header matches the remapped stream's.
TEST(ShuffleBCnEx, JoinCodedBlocks)
{
    BC7TextureMetrics set1 = {};
    set1.JoinRemap = 1;
    const size_t n = 4096;
    std::mt19937_64 rng(5);
    vector<uint8_t> plain(n * 16), remapped(n * 16);
    for (size_t i = 0; i < n; i++)
    {
        uint64_t b[2] = { rng(), rng() };
        uint8_t* r = reinterpret_cast<uint8_t*>(b);
        if (i % 512 == 100)
        {
            // distinct blocks of Modes 1-7, cheap to code once remapped: zero but a small byte 0 and the mode bit
            memset(r, 0, 16);
            r[0] = uint8_t(i / 512 + 1);
            r[15] = uint8_t(2u << (i / 512 % 7));
            BC7_ModeJoin_ByteRemap(r, true, set1);
        }
        memcpy(&plain[i * 16], r, 16);
        BC7_ModeJoin_ByteRemap(r, false, set1);
        memcpy(&remapped[i * 16], r, 16);
    }

    // parameter 1 stands in for parameter 4 (the same size), so both headers have the same room; parameter 3 makes the
    // header need more than one coded block
    const vector<uint8_t> modeMasks(8, 0);
    vector<uint8_t> encPlain, encRemapped, dec;
    StreamHeaderInfo infoPlain, infoRemapped, info;
    ASSERT_TRUE(StreamHeader_Encode(plain.data(), plain.size(), 16, uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN),
        { { GACL_STREAMHEADER_PARAM_CURVE_WIDTH_TILES, { 1 } }, { GACL_STREAMHEADER_PARAM_BC7_JOIN_MODE_STATICS, modeMasks } },
        encPlain, &infoPlain, BC7_JoinBlockCode()));
    ASSERT_TRUE(StreamHeader_Encode(remapped.data(), remapped.size(), 16, uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN),
        { { GACL_STREAMHEADER_PARAM_BC7_JOIN_MODE_STATICS, modeMasks }, { GACL_STREAMHEADER_PARAM_BC7_JOIN_REMAP, { 1 } } },
        encRemapped, &infoRemapped, BC7_JoinBlockCode()));
    EXPECT_TRUE(infoPlain.Removed.empty());
    EXPECT_GE(infoPlain.Coded.size(), 2u);
    EXPECT_EQ(infoPlain.Coded, infoRemapped.Coded);
    for (size_t c : infoPlain.Coded)
    {
        EXPECT_EQ(c % 512, 100u) << "coded block " << c;
    }
    const size_t head = GACL_STREAMHEADER_FIXED_BYTES + 8 + 1;
    ASSERT_EQ(infoPlain.HeaderBytes, infoRemapped.HeaderBytes);
    ASSERT_GT(infoPlain.HeaderBytes, head);
    EXPECT_TRUE(std::equal(encPlain.begin() + head, encPlain.begin() + infoPlain.HeaderBytes, encRemapped.begin() + head));

    // both restore their blocks, and the bodies hold the remapped blocks
    ASSERT_TRUE(StreamHeader_Decode(encPlain.data(), encPlain.size(), 16, info, dec, BC7_JoinBlockCode()));
    EXPECT_TRUE(dec == plain);
    ASSERT_TRUE(StreamHeader_Decode(encRemapped.data(), encRemapped.size(), 16, info, dec, BC7_JoinBlockCode()));
    EXPECT_TRUE(dec == remapped);
    const StreamHeaderBlockCode bodies = { &kBC7JoinBlockCode[0][0] };
    ASSERT_TRUE(StreamHeader_Parse(encPlain.data(), encPlain.size(), 16, info, &bodies));
    ASSERT_EQ(info.Coded, infoPlain.Coded);
    for (size_t j = 0; j < info.Coded.size(); j++)
    {
        EXPECT_EQ(memcmp(&info.CodedBlocks[j * 16], &remapped[info.Coded[j] * 16], 16), 0) << "coded block " << info.Coded[j];
    }
}

// The EX GPU decoder (Shaders/Shuffle/UnshuffleBCnEx.hlsl) embeds a decode table of the codes of the blocks coded in the
// stream header (Lib/shuffle/bc7joinblockcode.h, bc5blockcode.h) as BCnExTables.hlsli, which this test checks and generates:
// when they differ it fails and writes the regenerated file to test_output.
namespace BCnExShader
{
    using uint = uint32_t;
#include "../../Shaders/Shuffle/BCnExTables.hlsli"
}

namespace
{
    // ReadCodedBlock's decode of a byte (UnshuffleBCnEx.hlsl) from the 6 table words of its context: the value and the bits used
    void BCnExTestDecode(const uint32_t* t, uint32_t bits, uint32_t& v, uint32_t& used)
    {
        uint32_t next = 0;
        for (unsigned i = 0; i < 8; i++)
        {
            next |= ((bits >> i) & 1) << (7 - i);
        }
        const uint32_t r = next * 0x00100401u;
        unsigned sidx = 0;
        for (unsigned k = 0; k < 3; k++)
        {
            const uint32_t lanes = (r + t[k]) & 0x10040100u;
            sidx += ((lanes >> 8) & 1) + ((lanes >> 18) & 1) + ((lanes >> 28) & 1);
        }
        const uint32_t e = ((sidx < 4 ? t[3] : (sidx < 8 ? t[4] : t[5])) >> ((sidx & 3) * 8)) & 0xFF;
        const uint32_t sym = e & 15;
        used = e >> 4;
        v = sym;
        if (sym >= 2)
        {
            const uint32_t x = sym - 1;
            v = ((bits >> (used - x)) & ((1u << x) - 1)) | (1u << x);
        }
    }
}

TEST(ShuffleTables, BCnExShaderTables)
{
    // Canonical decode of each context's code (streamheader.h: codes in (length, bucket) order, MSB first), BC7 Join (m, p) at
    // context m * 16 + p, BC3/4/5 byte p at 128 + p and BC1 byte p at 144 + p, 6 words each. With the 9 buckets in code order and
    // lo[i] the next 8 bits at which the i-th code starts: words 0-2 hold 256 - lo[i], i = 1..8, in 10-bit lanes; words 3-5 byte i
    // the bucket | the code length plus the bucket's bits below the leading one << 4.
    vector<uint32_t> decode(152 * 6, 0);
    size_t decodeErrors = 0;
    for (unsigned c = 0; c < 152; c++)
    {
        const uint32_t lens = c < 128 ? kBC7JoinBlockCode[c >> 4][c & 15] : c < 144 ? ::kBC5BlockCode[c - 128] : ::kBC1BlockCode[c - 144];
        unsigned len[9], order[9], count[9] = {}, n = 0;
        for (unsigned s = 0; s < 9; s++)
        {
            len[s] = ((lens >> (3 * s)) & 7) + 1;
            count[len[s]]++;
        }
        for (unsigned l = 1; l <= 8; l++)
            for (unsigned s = 0; s < 9; s++)
                if (len[s] == l)
                    order[n++] = s;
        unsigned lo[9], first = 0;
        for (unsigned l = 1, i = 0; l <= 8; l++)
        {
            for (unsigned k = 0; k < count[l]; k++)
            {
                lo[i++] = (first + k) << (8 - l);
            }
            first = (first + count[l]) << 1;
        }
        // the shader relies on complete codes: any next 8 bits decode
        EXPECT_EQ(lo[8] + (1u << (8 - len[order[8]])), 256u) << "context " << c;
        uint32_t* t = &decode[c * 6];
        for (unsigned i = 1; i <= 8; i++)
        {
            t[(i - 1) / 3] |= (256 - lo[i]) << (10 * ((i - 1) % 3));
        }
        for (unsigned i = 0; i < 9; i++)
        {
            const unsigned s = order[i];
            t[3 + i / 4] |= (s | ((len[s] + (s >= 2 ? s - 1 : 0)) << 4)) << (8 * (i % 4));
        }

        // against a bit-serial canonical decode of every 15-bit pattern (a code and its bucket's low bits)
        for (uint32_t bits = 0; bits < 0x8000; bits++)
        {
            unsigned code = 0, start = 0, index = 0, sym = 9, used = 0;
            for (unsigned l = 1; l <= 8 && sym == 9; l++)
            {
                code = (code << 1) | ((bits >> (l - 1)) & 1);
                if (code < start + count[l])
                {
                    sym = order[index + code - start];
                    used = l;
                }
                index += count[l];
                start = (start + count[l]) << 1;
            }
            uint32_t v = sym;
            if (sym >= 2 && sym < 9)
            {
                v = ((bits >> used) & ((1u << (sym - 1)) - 1)) | (1u << (sym - 1));
                used += sym - 1;
            }
            uint32_t fv, fused;
            BCnExTestDecode(t, bits, fv, fused);
            decodeErrors += sym == 9 || fv != v || fused != used;
        }
    }
    EXPECT_EQ(decodeErrors, 0u);

    const bool same = sizeof(BCnExShader::kBCnExBlockCodeDecode) == decode.size() * 4 &&
        !memcmp(BCnExShader::kBCnExBlockCodeDecode, decode.data(), decode.size() * 4);
    if (!same)
    {
        const filesystem::path path = GetTestOutputDirectory() / "BCnExTables.hlsli";
        ofstream os(path);
        os << "//-------------------------------------------------------------------------------------\n"
              "// BCnExTables.hlsli\n"
              "//\n"
              "// Game Asset Conditioning Library - Microsoft toolkit for game asset compression\n"
              "//\n"
              "// Copyright (c) Microsoft Corporation.\n"
              "// Licensed under the MIT License.\n"
              "//\n"
              "//-------------------------------------------------------------------------------------\n"
              "// GENERATED by gacl_lib_tests (ShuffleTables.BCnExShaderTables) from the codes of the blocks coded in the stream header\n"
              "// (Lib/shuffle/bc7joinblockcode.h, bc5blockcode.h). The test fails when this file differs from them and writes a regenerated\n"
              "// copy to its test_output folder; do not edit. Valid HLSL and C++ (uint = uint32_t). 3,648 bytes of tables.\n"
              "\n"
              "#ifndef BCNEX_TABLES_HLSLI\n"
              "#define BCNEX_TABLES_HLSLI\n"
              "\n";
        WriteShaderTable(os, "// Canonical decode of the coded-block codes (streamheader.h; every code is complete), 6 words per context: byte p of BC7\n"
                             "// Join Mode m at [(m * 16 + p) * 6], byte p of BC3/4/5 at [(128 + p) * 6], of BC1 at [(144 + p) * 6]. With the 9 log2\n"
                             "// buckets in code order (by length, then bucket) and lo[i] the next 8 bits (MSB first) at which the i-th code starts:\n"
                             "// words 0-2 hold 256 - lo[i], i = 1..8, in 10-bit lanes (bits 0, 10, 20); word 3 + i / 4 byte i % 4 holds the bucket |\n"
                             "// (its code length + the bits below its leading one) << 4.\n",
                         "kBCnExBlockCodeDecode", decode.data(), decode.size());
        os << "#endif\n";
        EXPECT_TRUE(same) << "Shaders/Shuffle/BCnExTables.hlsli is out of date; regenerated: " << path.string();
    }
}
