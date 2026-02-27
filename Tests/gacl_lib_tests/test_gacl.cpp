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

#include <DirectXTex.h>

#include <gacl.h>
#include "../../ThirdParty/zstd/lib/zstd.h"

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
        vector<uint8_t> uncurved;

        size_t scBytes;
        sc.resize(srcImgSize);

        uint8_t* dataForCurved = includeCurve ? img->pixels : nullptr;

        SHUFFLE_COMPRESS_PARAMETERS params = { srcImgSize, siMeta.format, img->pixels, {19}, {img->width, dataForCurved} };

        hr = GACL_ShuffleCompress_BCn(sc.data(), &tid, &scBytes, params);
        EXPECT_EQ(hr, S_OK);

        void BC7_ModeSplit_Reverse(const uint8_t * src, size_t srcSize, std::vector<uint8_t>&dest, size_t destSize, const uint8_t * ref);
        if (hr == S_OK)
        {
            uncurved.resize(srcImgSize);
            uncompressed.resize(srcImgSize);
            unshuffled.resize(srcImgSize);
            sc.resize(scBytes);

            bool compareUncurved = false;

            size_t ucBytes = ZSTD_decompress(uncompressed.data(), uncompressed.size(), sc.data(), scBytes);

            switch (tid)
            {
            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:
                UnshuffleBC1v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224_SC:
                UnshuffleBC1v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                GACL_Shuffle_ApplySpaceCurve(uncurved.data(), unshuffled.data(), srcImgSize, 8, img->width, false);
                compareUncurved = true;
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:
                UnshuffleBC3v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224_SC:
                UnshuffleBC3v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                GACL_Shuffle_ApplySpaceCurve(uncurved.data(), unshuffled.data(), srcImgSize, 16, img->width, false);
                compareUncurved = true;
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:
                UnshuffleBC4v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116_SC:
                UnshuffleBC4v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                GACL_Shuffle_ApplySpaceCurve(uncurved.data(), unshuffled.data(), srcImgSize, 8, img->width, false);
                compareUncurved = true;
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:
                UnshuffleBC5v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116_SC:
                UnshuffleBC5v1(unshuffled.data(), uncompressed.data(), srcImgSize);
                GACL_Shuffle_ApplySpaceCurve(uncurved.data(), unshuffled.data(), srcImgSize, 16, img->width, false);
                compareUncurved = true;
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT:
                BC7_ModeSplit_Reverse(uncompressed.data(), ucBytes, unshuffled, srcImgSize, nullptr/*img->pixels*/);
                break;

            case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT_SC:
                BC7_ModeSplit_Reverse(uncompressed.data(), ucBytes, unshuffled, srcImgSize, nullptr /*img->pixels*/);
                GACL_Shuffle_ApplySpaceCurve(uncurved.data(), unshuffled.data(), srcImgSize, 16, img->width, false);
                compareUncurved = true;
                break;

            default:
                ADD_FAILURE();
            }

            // Previous: EXPECT_EQ(0, memcmp(compareUncurved ? uncurved.data() : unshuffled.data(), img->pixels, srcImgSize));
            // Below is effectively the same, but will export the corrupt offset rather than just a bool pass\fail
            const uint8_t* cmpData = compareUncurved ? uncurved.data() : unshuffled.data();
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
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224_SC);
    EXPECT_EQ(sc.size(), 618536);    // known size for this asset shuffle compressed in this way
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

TEST(ShuffleCompress, LargeBC4_ShuffleCompress_curved) {
    vector<uint8_t> sc;
    GACL_SHUFFLE_TRANSFORM tid = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;
    HRESULT hr = ValidateRoundTripShuffleCompress(Ladybug_fresnel_2k_2k_BC4, tid, true, sc);

    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116_SC);
    EXPECT_EQ(sc.size(), 66388);    // known size for this asset shuffle compressed in this way
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
    EXPECT_EQ(tid, GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116_SC);
    EXPECT_EQ(sc.size(), 3876264);    // known size for this asset shuffle compressed in this way
}

struct SC_BC7TestParam
{
    wstring file;
    size_t size;

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
    EXPECT_TRUE(tid == GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT || tid == GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT_SC);
    EXPECT_EQ(sc.size(), p.size);    // known size for this asset shuffle compressed in this way
}

// These files were selected in order to yield coverage of the general pattners and BC7 features

INSTANTIATE_TEST_CASE_P(
    SC,
    SC_BC7_Test,
    ::testing::Values(
        SC_BC7TestParam{ L"Ladybug_Body_Spec_mask.DDS",  22219 },
        SC_BC7TestParam{ L"Ladybug_Body_albedo.DDS",  3740646 },
        SC_BC7TestParam{ L"gerberaSingleLeaf_sss.DDS", 897656 },
        SC_BC7TestParam{ L"Ladybug_DetailSkin_normal.DDS", 421368 },
        SC_BC7TestParam{ L"BlackboardEraser_albedo.DDS", 507255 },
        SC_BC7TestParam{ L"GerberaLeafHigh_normal.DDS", 815678 },
        SC_BC7TestParam{ L"LetterBlock_LetterA_albedo.DDS", 2170355 },
        SC_BC7TestParam{ L"Ladybug_Shell_Roughness_mask.DDS", 7046 }
    )
);

