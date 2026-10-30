//--------------------------------------------------------------------------------------
// Main.cpp
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "Game.h"

#include <gtest/gtest.h>

using namespace DirectX;

#ifdef __clang__
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

#pragma warning(disable : 4061)

namespace
{
    std::unique_ptr<Game> g_game;
}

bool g_HDRMode = false;

void ExitGame() noexcept;

// ---------------------------------------------------------------------------
// Parameterized test suite
// ---------------------------------------------------------------------------

#include "TestParameters.h"

std::string PrintToStringParamName(testing::TestParamInfo<GTestParameters> params)
{
    return params.param.testName;
}

class BCnUnshuffleTest : public ::testing::TestWithParam<GTestParameters> {};

TEST_P(BCnUnshuffleTest, UnshuffleAndVerify)
{
    const GTestParameters params = GetParam();

    ASSERT_TRUE(g_game->RunTestWithParams(params)) << L"Unshuffle validation failed for test " << params.testName;
}

std::vector<GTestParameters> GetUnshufflingTestParameters()
{
    std::vector<GTestParameters> testParams;

    // Permutations will be - For every resolution, go through every config option.
    for (uint32_t resIndex = 0; resIndex < std::size(g_testResolutions); ++resIndex)
    {
#ifdef NEWBC1345
        const size_t resFirst = testParams.size();
#endif

        // BC1, BC3, BC4, BC5
        for (GBaseFormat format : {GBaseFormat::BC1, GBaseFormat::BC3, GBaseFormat::BC4, GBaseFormat::BC5})
        {
            GTestParameters p1 = { format };

            const char* bcNames[] = { "BC1", "BC3", "BC4", "BC5" };

            p1.width = std::get<0>(g_testResolutions[resIndex]);
            p1.height = std::get<1>(g_testResolutions[resIndex]);
            p1.useSpaceCurve = false;
            p1.BC1345.microShuffleId = 1;
            p1.BC1345.useHeader = false;
            {
                std::stringstream testName;
                testName << bcNames[format] << "_Res" << p1.width << "x" << p1.height << "_SCFalse_Pattern1";
                p1.testName = testName.str();
                testParams.push_back(p1);
            }

#ifdef NEWBC1345
            // BC1\3\5 have two micro-shuffle variants
            const uint32_t patterns = format == GBaseFormat::BC4 ? 1 : 2;
            if (patterns == 2)
            {
                GTestParameters p2 = p1;
                p2.BC1345.microShuffleId = 2;
                std::stringstream testName;
                testName << bcNames[format] << "_Res" << p2.width << "x" << p2.height << "_SCFalse_Pattern2";
                p2.testName = testName.str();
                testParams.push_back(p2);
            }

            const size_t elementSize = format == GBaseFormat::BC1 || format == GBaseFormat::BC4 ? 8 :
                format == GBaseFormat::BC3 || format == GBaseFormat::BC5 ? 16 : 0;

            // EX streams need room for the header (at least 8 blocks, so the test can add duplicates), and are
            // curved when the curve applies to the stream: the encoder and decoders reject a curve parameter that
            // doesn't fit (e.g. 128x128 at 16 bytes per block is not over 16KB)
            const uint32_t blocks = ((p1.width + 3) / 4) * ((p1.height + 3) / 4);
            const bool curveEligible = GACL_Shuffle_ApplySpaceCurve(nullptr, nullptr, size_t(blocks) * elementSize, elementSize, p1.width, true);
            if (blocks >= 8)
            {
                for (uint32_t sc = 0; sc < (curveEligible ? 2u : 1u); ++sc)
                {
                    for (uint32_t pattern = 1; pattern <= patterns; ++pattern)
                    {
                        GTestParameters px = p1;
                        px.useSpaceCurve = sc != 0;
                        px.BC1345.microShuffleId = pattern;
                        px.BC1345.useHeader = true;
                        std::stringstream testName;
                        testName << bcNames[format] << "_Res" << px.width << "x" << px.height <<
                            "_SC" << (px.useSpaceCurve ? "True" : "False") << "_EX_Pattern" << pattern;
                        px.testName = testName.str();
                        testParams.push_back(px);
                    }
                }

                // index prediction masks other than the format's default (header parameter 2): none for every format,
                // and for BC3 also color only and alpha only. Curved when eligible, so both parameters are present.
                const uint8_t predictionOffs[] = { 1, 2, 3 };   // BCNEX_PREDICT_HALVES = 1, BCNEX_PREDICT_COLOUR = 2
                const uint8_t choices = format == GBaseFormat::BC1 ? 2 : format == GBaseFormat::BC3 ? 3 : 1;
                for (uint8_t off : predictionOffs)
                {
                    if ((off & ~choices) != 0)
                    {
                        continue;
                    }
                    GTestParameters px = p1;
                    px.useSpaceCurve = curveEligible;
                    px.BC1345.useHeader = true;
                    px.BC1345.predictionOff = off;
                    std::stringstream testName;
                    testName << bcNames[format] << "_Res" << px.width << "x" << px.height <<
                        "_SC" << (px.useSpaceCurve ? "True" : "False") << "_EX_Pattern1_PredictOff" << uint32_t(off);
                    px.testName = testName.str();
                    testParams.push_back(px);
                }

                // the kept blocks unshuffled (BCNEX_UNSHUFFLED in parameter 2), which the encoder only writes for
                // curve-only streams but the decoders take with any prediction, curve and pattern (the second patterns
                // decode the same): with the default index prediction for each pattern, curved when eligible, and
                // without prediction (parameter 2 exactly BCNEX_UNSHUFFLED, which BC7_EX takes only for BC5_116116),
                // linear
                for (uint32_t pattern = 1; pattern <= patterns; ++pattern)
                {
                    GTestParameters px = p1;
                    px.useSpaceCurve = curveEligible;
                    px.BC1345.microShuffleId = pattern;
                    px.BC1345.useHeader = true;
                    px.BC1345.unshuffled = true;
                    std::stringstream testName;
                    testName << bcNames[format] << "_Res" << px.width << "x" << px.height <<
                        "_SC" << (px.useSpaceCurve ? "True" : "False") << "_EX_Pattern" << pattern << "_Unshuffled";
                    px.testName = testName.str();
                    testParams.push_back(px);
                }
                for (uint32_t pattern = 1; pattern <= patterns; ++pattern)
                {
                    GTestParameters px = p1;
                    px.BC1345.microShuffleId = pattern;
                    px.BC1345.useHeader = true;
                    px.BC1345.predictionOff = choices;
                    px.BC1345.unshuffled = true;
                    std::stringstream testName;
                    testName << bcNames[format] << "_Res" << px.width << "x" << px.height <<
                        "_SCFalse_EX_Pattern" << pattern << "_PredictOff" << uint32_t(choices) << "_Unshuffled";
                    px.testName = testName.str();
                    testParams.push_back(px);
                }

                // without duplicate blocks: the stream header codes blocks to make room for itself
                for (uint32_t sc = 0; sc < (curveEligible ? 2u : 1u); ++sc)
                {
                    for (uint32_t pattern = 1; pattern <= patterns; ++pattern)
                    {
                        GTestParameters px = p1;
                        px.useSpaceCurve = sc != 0;
                        px.BC1345.microShuffleId = pattern;
                        px.BC1345.useHeader = true;
                        px.BC1345.coded = true;
                        std::stringstream testName;
                        testName << bcNames[format] << "_Res" << px.width << "x" << px.height <<
                            "_SC" << (px.useSpaceCurve ? "True" : "False") << "_EX_Coded_Pattern" << pattern;
                        px.testName = testName.str();
                        testParams.push_back(px);
                    }
                }

                // curve-only streams (BCNEX_UNSHUFFLED: the curved blocks, unshuffled), whose header makes room for itself
                // by removing duplicates or, without them, by coding blocks; curved, so only when eligible
                for (uint32_t c = 0; curveEligible && c < 2; ++c)
                {
                    GTestParameters px = p1;
                    px.useSpaceCurve = true;
                    px.BC1345.useHeader = true;
                    px.BC1345.curveOnly = true;
                    px.BC1345.coded = c != 0;
                    std::stringstream testName;
                    testName << bcNames[format] << "_Res" << px.width << "x" << px.height << "_SCTrue_EX_CurveOnly" << (c != 0 ? "_Coded" : "");
                    px.testName = testName.str();
                    testParams.push_back(px);
                }

                // two duplicate pairs far apart (blocks 10,000 to 22,000): long codes, filling a two-block header with
                // 8-byte blocks
                if (blocks > 22000u)
                {
                    GTestParameters px = p1;
                    px.BC1345.useHeader = true;
                    px.BC1345.farDuplicates = true;
                    std::stringstream testName;
                    testName << bcNames[format] << "_Res" << px.width << "x" << px.height << "_SCFalse_EX_FarDuplicates";
                    px.testName = testName.str();
                    testParams.push_back(px);
                }

                // a header written by the test, coding the costliest block the format's code allows (no encoder codes
                // it: it doesn't save room), for the decoders' furthest reads of a body
                for (uint32_t pattern = 1; pattern <= patterns; ++pattern)
                {
                    GTestParameters px = p1;
                    px.BC1345.microShuffleId = pattern;
                    px.BC1345.useHeader = true;
                    px.BC1345.longCoded = true;
                    std::stringstream testName;
                    testName << bcNames[format] << "_Res" << px.width << "x" << px.height << "_SCFalse_EX_LongCoded_Pattern" << pattern;
                    px.testName = testName.str();
                    testParams.push_back(px);
                }

                // the same with two removed duplicates sourced above coded blocks, and at least 40 coded blocks (more
                // positions than a 32-lane wave reads at once)
                if (blocks >= 256u)
                {
                    for (uint32_t pattern = 1; pattern <= patterns; ++pattern)
                    {
                        GTestParameters px = p1;
                        px.BC1345.microShuffleId = pattern;
                        px.BC1345.useHeader = true;
                        px.BC1345.longCoded = true;
                        px.BC1345.manyCoded = true;
                        std::stringstream testName;
                        testName << bcNames[format] << "_Res" << px.width << "x" << px.height << "_SCFalse_EX_ManyCoded_Pattern" << pattern;
                        px.testName = testName.str();
                        testParams.push_back(px);
                    }
                }

                // the same, with a header one bit too long for its blocks, in a stream of one decoder thread group: every
                // group that decodes the last body rejects the stream. Textures of up to 1024 blocks, cropped to a group
                // of the per-format shaders' EX dispatch (Game.cpp gShaderInfo: 128 threads of 2 BC1 blocks, 256 of 4
                // BC3, 256 of 2 BC4, 128 of 4 BC5; the unified shader's groups hold as many blocks or more)
                if (blocks <= 1024u)
                {
                    const uint32_t groupBlocks = format == GBaseFormat::BC1 ? 256u : format == GBaseFormat::BC3 ? 1024u : 512u;
                    const uint32_t blockColumns = (p1.width + 3) / 4;
                    GTestParameters pc = p1;
                    if (blocks > groupBlocks && blockColumns <= groupBlocks)
                    {
                        pc.height = groupBlocks / blockColumns * 4;
                    }
                    else if (blocks > groupBlocks)
                    {
                        pc.width = groupBlocks * 4;
                        pc.height = 4;
                    }
                    for (uint32_t pattern = 1; pattern <= patterns; ++pattern)
                    {
                        GTestParameters px = pc;
                        px.BC1345.microShuffleId = pattern;
                        px.BC1345.useHeader = true;
                        px.BC1345.longCoded = true;
                        px.BC1345.overrun = true;
                        std::stringstream testName;
                        testName << bcNames[format] << "_Res" << px.width << "x" << px.height << "_SCFalse_EX_LongCodedOverrun_Pattern" << pattern;
                        px.testName = testName.str();
                        testParams.push_back(px);
                    }
                }
            }
#endif
        }
        

#ifdef NEWBC1345    
        // Curve-only BC7 streams (BC7_EX, the blocks carried as BC5_116116 blocks), for resolutions the space curve applies to
        const uint32_t bc7Width = std::get<0>(g_testResolutions[resIndex]), bc7Height = std::get<1>(g_testResolutions[resIndex]);
        const size_t bc7Bytes = size_t((bc7Width + 3) / 4) * ((bc7Height + 3) / 4) * 16;
        if (GACL_Shuffle_ApplySpaceCurve(nullptr, nullptr, bc7Bytes, 16, bc7Width, true))
        {
            for (uint32_t c = 0; c < 2; ++c)
            {
                GTestParameters cp = { GBaseFormat::BC7Join };
                cp.width = bc7Width;
                cp.height = bc7Height;
                cp.useSpaceCurve = true;
                cp.BC7Join = {};
                cp.BC7Join.curveOnly = true;
                cp.BC7Join.coded = c != 0;
                std::stringstream testName;
                testName << "BC7CurveOnly_Res" << cp.width << "x" << cp.height << "_SCTrue" << (c != 0 ? "_Coded" : "");
                cp.testName = testName.str();
                testParams.push_back(cp);
            }
        }

        // BC7 Join, unified shader. A stream header needs room, made by removing duplicate blocks or coding blocks in
        // the header: the test texture's Mode 8 blocks are all zero, so a texture of 18 blocks or more has duplicates.
        {
            struct JoinVariant
            {
                const char* name;
                uint8_t masks;
                bool coded;
                uint8_t truncation;
                bool useSpaceCurve;
                uint32_t minBlocks;
                bool remap;
                bool mode5;
                bool spareHeaderBlock;
                bool farDuplicate;
            };
            const JoinVariant variants[] =
            {
                { "NoStatics",              0, false, 0, false, 1,  false, false, false, false },    // headerless
                { "Statics",                1, false, 0, false, 18, false, false, false, false },
                { "ModeStatics",            2, false, 0, false, 18, false, false, false, false },
                { "Statics_Coded",          1, true,  0, false, 18, false, false, false, false },
                { "NoStatics_TruncBlock",   0, false, 1, false, 18, false, false, false, false },
                { "ModeStatics_TruncBlock", 2, false, 1, false, 18, false, false, false, false },
                { "ModeStatics_TruncByte",  2, false, 2, false, 18, false, false, false, false },
                { "NoStatics",              0, false, 0, true,  18, false, false, false, false },
                { "ModeStatics",            2, false, 0, true,  18, false, false, false, false },
                { "ModeStatics_Coded",      2, true,  0, true,  18, false, false, false, false },
                { "NoStatics_Remap",        0, false, 0, false, 18, true,  false, false, false },    // a header for the remap alone
                { "ModeStatics_Remap",      2, false, 0, false, 18, true,  false, false, false },
                { "Statics_Coded_Remap",    1, true,  0, false, 18, true,  false, false, false },
                { "NoStatics_Remap_TruncBlock", 0, false, 1, false, 18, true, false, false, false },
                { "ModeStatics_Coded_Remap", 2, true, 0, true,  18, true,  false, false, false },
                // mostly Mode 5 blocks: the decoder's path for waves of Mode 5 blocks alone
                { "Mode5_NoStatics",        0, false, 0, false, 1,  false, true,  false, false },    // headerless
                { "Mode5_ModeStatics",      2, false, 0, false, 18, false, true,  false, false },
                { "Mode5_ModeStatics_Remap", 2, false, 0, false, 18, true, true,  false, false },
                { "Mode5_ModeStatics_Coded_Remap", 2, true, 0, true, 18, true, true, false, false },
                { "Mode5_AlphaStatics",     3, false, 0, false, 18, false, true,  false, false },
                // the layouts of the masks of opaque content (Modes 4-7 with only their alpha flags) and of every flag
                { "OpaqueStatics",          4, false, 0, false, 18, false, false, false, false },
                { "AllStatics",             5, false, 0, false, 18, false, false, false, false },
                // a two-block header that fits in one (three copies of the zero Mode 8 block from 27 blocks): the
                // decoder's general header parse, not its one-block BC7 Join parse
                { "Statics_SpareHeaderBlock", 1, false, 0, false, 27, false, false, true, false },
                { "NoStatics_Remap_SpareHeaderBlock", 0, false, 0, false, 27, true, false, true, false },
                { "Statics_SpareHeaderBlock", 1, false, 0, true,  27, false, false, true, false },
                // one removed duplicate far from block 0 and from its source (block n / 2, 12,000 at most, copied 12,000
                // blocks on): long codes in a one-block header, which the decoder's one-block BC7 Join parse reads across
                // its 32-bit words
                { "Statics_FarDuplicate",   1, false, 0, false, 18, false, false, false, true },
                { "NoStatics_Remap_FarDuplicate", 0, false, 0, false, 18, true, false, false, true },
                { "Statics_Remap_FarDuplicate", 1, false, 0, true, 18, true, false, false, true },
            };

            const uint32_t width = std::get<0>(g_testResolutions[resIndex]);
            const uint32_t height = std::get<1>(g_testResolutions[resIndex]);
            const uint32_t blocks = ((width + 3) / 4) * ((height + 3) / 4);
            const bool curveEligible = GACL_Shuffle_ApplySpaceCurve(nullptr, nullptr, size_t(blocks) * 16, 16, width, true);
            for (const JoinVariant& v : variants)
            {
                if (blocks < v.minBlocks || (v.useSpaceCurve && !curveEligible))
                {
                    continue;
                }

                GTestParameters jp = { GBaseFormat::BC7Join };
                jp.width = width;
                jp.height = height;
                jp.useSpaceCurve = v.useSpaceCurve;
                jp.BC7Join.masks = v.masks;
                jp.BC7Join.coded = v.coded;
                jp.BC7Join.truncation = v.truncation;
                jp.BC7Join.remap = v.remap;
                jp.BC7Join.mode5 = v.mode5;
                jp.BC7Join.spareHeaderBlock = v.spareHeaderBlock;
                jp.BC7Join.farDuplicate = v.farDuplicate;
                jp.BC7Join.curveOnly = false;
                jp.BC7Join.transformId = 0;
                jp.BC7Join.streamSize = 0;

                std::stringstream testName;
                testName << "BC7Join_Res" << width << "x" << height << "_SC" << (v.useSpaceCurve ? "True" : "False") << "_" << v.name;
                jp.testName = testName.str();
                testParams.push_back(jp);
            }
        }

        // Every EX stream of 256x512 (curved for every format) under each other format's EX id, dispatched for that id as
        // DirectStorage would: the decoders reject a header naming another format (the output stays zero), but BC5_EX and
        // BC7_EX both take BC5_116116 headers of parameter 2 exactly BCNEX_UNSHUFFLED (curve-only BC7 streams among them)
        if (std::get<0>(g_testResolutions[resIndex]) == 256 && std::get<1>(g_testResolutions[resIndex]) == 512)
        {
            // by GBaseFormat (BC1, BC3, BC4, BC5, BC7Join)
            const uint32_t exIds[] = { GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX, GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_EX,
                GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_EX, GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_EX, GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX };
            const char* exNames[] = { "BC1EX", "BC3EX", "BC4EX", "BC5EX", "BC7EX" };
            const size_t resLast = testParams.size();
            for (size_t i = resFirst; i < resLast; ++i)
            {
                const GTestParameters t = testParams[i];
                const bool header = t.baseFormat == GBaseFormat::BC7Join ?
                    t.BC7Join.curveOnly || t.BC7Join.masks != 0 || t.useSpaceCurve || t.BC7Join.remap :
                    t.baseFormat < GBaseFormat::BC7Join && t.BC1345.useHeader;
                for (uint32_t x = 0; header && x < std::size(exIds); ++x)
                {
                    if (x != uint32_t(t.baseFormat))
                    {
                        GTestParameters o = t;
                        o.transformOverride = exIds[x];
                        o.testName = t.testName + "_As" + exNames[x];
                        testParams.push_back(o);
                    }
                }
                // and with the header's escape bit set: every decoder path rejects it (not for a stream cut short of a
                // whole block, which they reject regardless)
                if (header && !(t.baseFormat == GBaseFormat::BC7Join && t.BC7Join.truncation == 2))
                {
                    GTestParameters e = t;
                    e.escape = true;
                    e.testName = t.testName + "_Escape";
                    testParams.push_back(e);
                }
            }
        }
#endif


        // BC7 
        for (uint32_t chunkIndex = 0; chunkIndex < std::size(g_testChunkSizes); ++chunkIndex)
        {
            for (uint32_t stratIndex = 0; stratIndex < std::size(g_endpointOrderStrategies); ++stratIndex)
            {
                for (uint32_t scIndex = 0; scIndex < std::size(g_useSpaceCurve); ++scIndex)
                {
                    // only run curved variants for resolutions that are space-curve eligible (16 byte elements)
                    if (g_useSpaceCurve[scIndex] && !std::get<3>(g_testResolutions[resIndex]))
                    {
                        continue;
                    }

                    for (uint32_t patternSetIndex = 0; patternSetIndex < std::size(g_patternSets); ++patternSetIndex)
                    {
                        for (uint32_t ipIndex = 0; ipIndex < std::size(g_useIndexPrediction); ++ipIndex)
                        for (uint32_t slIndex = 0; slIndex < std::size(g_sectionLayouts); ++slIndex)
                        {
                            // section layouts are only exercised with index prediction, to bound the test count
                            if (g_sectionLayouts[slIndex] && !g_useIndexPrediction[ipIndex])
                            {
                                continue;
                            }

                            GTestParameters params = { GBaseFormat::BC7 };

                            params.width = std::get<0>(g_testResolutions[resIndex]);
                            params.height = std::get<1>(g_testResolutions[resIndex]);
                            params.useSpaceCurve = static_cast<bool>(g_useSpaceCurve[scIndex]);
                            params.BC7.endpointOrderStrategy = g_endpointOrderStrategies[stratIndex];
                            params.BC7.ChunkSize = g_testChunkSizes[chunkIndex];
                            params.BC7.patternIndex = patternSetIndex;
                            params.BC7.useIndexPrediction = static_cast<bool>(g_useIndexPrediction[ipIndex]);
                            params.BC7.sectionLayout = g_sectionLayouts[slIndex];

                            std::stringstream testName;
                            testName << "BC7_Res" << params.width << "x" << params.height << 
                                "_ChunkSize" << params.BC7.ChunkSize <<
                                "_Strat" << std::to_string(params.BC7.endpointOrderStrategy) <<
                                "_SC" << (params.useSpaceCurve ? "True" : "False") << 
                                "_PatternSet" << params.BC7.patternIndex <<
                                "_IP" << (params.BC7.useIndexPrediction ? "True" : "False") <<
                                "_SL" << std::to_string(params.BC7.sectionLayout);

                            params.testName = testName.str();

                            testParams.push_back(params);
                        }
                    }
                }
            }
        }
    }

    return testParams;
}

INSTANTIATE_TEST_SUITE_P(
    GT, BCnUnshuffleTest,
    ::testing::ValuesIn(GetUnshufflingTestParameters()), 
    PrintToStringParamName);

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    g_game = std::make_unique<Game>();

    // Parse args and initialise the D3D12 device. No tests run here.
    g_game->Initialize(GetCommandLineW());

    #if RUN_TEST_FROM_TEXTURE_IN_DISK == 0
    return RUN_ALL_TESTS();
    #else
    g_game->RunAllPermutationsForLoadedTexture();
    #endif
}

// Exit helper
void ExitGame() noexcept {}

