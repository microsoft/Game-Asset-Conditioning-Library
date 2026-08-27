//--------------------------------------------------------------------------------------
// TestParameters.h
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "gacl.h"
#include "bc7.h"

constexpr std::tuple<uint32_t, uint32_t, bool, bool> g_testResolutions[] = 
{
    { 1, 1, false, false},
    { 1, 3, false, false },
    { 3, 1, false, false },
    { 4, 4, false, false },
    { 32, 1024, false, false },
    { 1024, 32, false, false },
    { 4, 1050, false, false },
    { 1050, 4, false, false },
    { 1011, 953, false, false },
    { 2048, 2048, true, true },         // Space Curve eligible: BC1, BC3, BC4, BC5, BC7
    //{ 16384,16384, true, true },      // extremely large test case, disabled for now
    { 128, 128, false, true },           // Space Curve eligible: BC3, BC5, BC7
    { 128, 384, false, true },           // Space Curve eligible: BC3, BC5, BC7
    { 128, 400, false, false },
    { 355, 2041, false, false },
    { 256, 512, true, true },           // Space Curve eligible: BC1, BC3, BC4, BC5, BC7
    { 512, 256, true, true }            // Space Curve eligible: BC1, BC3, BC4, BC5, BC7
};

constexpr uint32_t g_testChunkSizes[] =
{
    0,                  // 0
    4   * 1024,         // 4KB
    64  * 1024,         // 64KB 
    64  * 1024 * 1024,  // 64MB
};

constexpr char const* gs_chunkSizesName[] =
{
    "_0",
    "_4KB",
    "_64KB", 
    "_64MB",
};
static_assert(std::size(gs_chunkSizesName) == std::size(g_testChunkSizes));

constexpr uint8_t g_endpointOrderStrategies[] =
{
    0,      // Strategy 0
    1,      // Strategy 1
    2       // Strategy 2
};

constexpr uint8_t g_useSpaceCurve[] =
{
    0,      // Do not use space curve 
    1       // Use space curve
};
 
constexpr BC7ModeSplitShufflePattern g_patternSets[][8] =
{    
    // Test matrix for testing every possible pattern, every permutation is not needed since each mode is handled independent of the rest
    { EndpointPair4bit, EndpointPairSignificantBitInderleaved, EndpointPair4bit, EndpointPairSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointPairSignificantBitInderleaved },
    { ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointQuadSignificantBitInderleaved },
    { EndpointPairSignificantBitInderleaved, EndpointQuadSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt },

    // Test matrix that mimics what GACL does when finding best strategy. Used for debugging issues
    /*
    { EndpointPair4bit, EndpointQuadSignificantBitInderleaved, EndpointPair4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt },
    { EndpointPair4bit, EndpointQuadSignificantBitInderleaved, EndpointPair4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt },
    { EndpointPair4bit, EndpointQuadSignificantBitInderleaved, EndpointPair4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt },
    { EndpointPair4bit, EndpointQuadSignificantBitInderleaved, EndpointPair4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt },
    { ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt},
    { ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt},
    { ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt},
    { EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved},
    { EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved},
    { EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved},
    { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointPairSignificantBitInderleaved},
    { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointPairSignificantBitInderleaved},
    { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointPairSignificantBitInderleaved},
    { ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt},
    { ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt},
    { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved},
    { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved},
    { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved},
    //*/
};
