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

class BC7UnshuffleTest : public ::testing::TestWithParam<GTestParameters> {};

TEST_P(BC7UnshuffleTest, UnshuffleAndVerify)
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
        // BC1, BC3, BC4, BC5
        for (GBaseFormat format : {GBaseFormat::BC1, GBaseFormat::BC3, GBaseFormat::BC4, GBaseFormat::BC5})
        {
            GTestParameters p1 = { format };

            const char* bcNames[] = { "BC1", "BC3", "BC4", "BC5" };

            p1.width = std::get<0>(g_testResolutions[resIndex]);
            p1.height = std::get<1>(g_testResolutions[resIndex]);
            p1.useSpaceCurve = false;
            p1.BC1345.microShuffleId = 1;
            {
                std::stringstream testName;
                testName << bcNames[format] << "_Res" << p1.width << "x" << p1.height << "_SCFalse_Pattern1";
                p1.testName = testName.str();
                testParams.push_back(p1);
            }

#ifdef NEWBC1345
            GTestParameters p2 = p1;
            p2.BC1345.microShuffleId = 2;

            // BC 1\3 have two micro-shuffle variants
            if (format == GBaseFormat::BC1 || format == GBaseFormat::BC3)
            {
                std::stringstream testName;
                testName << bcNames[format] << "_Res" << p2.width << "x" << p2.height << "_SCFalse_Pattern2";
                p2.testName = testName.str();
                testParams.push_back(p2);
            }

            const size_t elementSize = format == GBaseFormat::BC1 || format == GBaseFormat::BC4 ? 8 :
                format == GBaseFormat::BC3 || format == GBaseFormat::BC5 ? 16 : 0;

            // if test resolution permits, curved variants also exist
            if ((std::get<2>(g_testResolutions[resIndex]) && elementSize == 8) ||
                (std::get<3>(g_testResolutions[resIndex]) && elementSize == 16))
            {
                p1.useSpaceCurve = true;
                {
                    std::stringstream testName;
                    testName << bcNames[format] << "_Res" << p1.width << "x" << p1.height << "_SCTrue_Pattern1";
                    p1.testName = testName.str();
                    testParams.push_back(p1);
                }

                p2.useSpaceCurve = true;
                if (format == GBaseFormat::BC1 || format == GBaseFormat::BC3)
                {
                    std::stringstream testName;
                    testName << bcNames[format] << "_Res" << p2.width << "x" << p2.height << "_SCTrue_Pattern2";
                    p2.testName = testName.str();
                    testParams.push_back(p2);
                }
            }
#endif
        }
        

#ifdef NEWBC1345    
        if (std::get<3>(g_testResolutions[resIndex]))  // Only run CurveOnly tests for resolutions that are space-curve eligible
        {
            GTestParameters scp = { GBaseFormat::CurveOnly };

            scp.width = std::get<0>(g_testResolutions[resIndex]);
            scp.height = std::get<1>(g_testResolutions[resIndex]);
            scp.useSpaceCurve = true;
            {
                std::stringstream testName;
                testName << "CurveOnly_Res" << scp.width << "x" << scp.height << "_SCTrue";
                scp.testName = testName.str();
                testParams.push_back(scp);
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
                    for (uint32_t patternSetIndex = 0; patternSetIndex < std::size(g_patternSets); ++patternSetIndex)
                    {
                        GTestParameters params = { GBaseFormat::BC7 };

                        params.width = std::get<0>(g_testResolutions[resIndex]);
                        params.height = std::get<1>(g_testResolutions[resIndex]);
                        params.useSpaceCurve = static_cast<bool>(g_useSpaceCurve[scIndex]);
                        params.BC7.endpointOrderStrategy = g_endpointOrderStrategies[stratIndex];
                        params.BC7.ChunkSize = g_testChunkSizes[chunkIndex];
                        params.BC7.patternIndex = patternSetIndex;

                        std::stringstream testName;
                        testName << "BC7_Res" << params.width << "x" << params.height << 
                            "_ChunkSize" << params.BC7.ChunkSize <<
                            "_Strat" << std::to_string(params.BC7.endpointOrderStrategy) <<
                            "_SC" << (params.useSpaceCurve ? "True" : "False") << 
                            "_PatternSet" << params.BC7.patternIndex;

                        params.testName = testName.str();

                        testParams.push_back(params);
                    }
                }
            }
        }
    }

    return testParams;
}

INSTANTIATE_TEST_CASE_P(
    GT, BC7UnshuffleTest,
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

