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
    //{ 16384, 128, true, true },       // widest space curve, disabled as it slows testing too much
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

constexpr uint8_t g_useIndexPrediction[] =
{
    0,      // Raw BC7 index fields
    1       // Index fields stored as prediction residuals (BC7 split)
};

constexpr uint8_t g_sectionLayouts[] =
{
    0,      // Block order color, misc and scrap sections
    1,      // Section layout chosen by encoder trials (BC7 split, with index prediction only)
    2       // Section layout forced to g_sectionSelectors (BC7 split, with index prediction only)
};

// per mode: bits 0-1 color unit code, bit 2 color remap, bits 4-5 misc unit code, bit 6 misc remap (codes a section's stride can't use are dropped)
constexpr uint8_t g_sectionSelectors[8] = { 0x25, 0x52, 0x37, 0x64, 0x71, 0x46, 0x13, 0x65 };

// The patterns a BC7 split LUT entry may name for each mode, bit p for pattern p: those GetModeSpecificLayout (SharedDefinitions.hlsli)
// lays out.  Mode 8: clean (0) only, as the shaders write mode 8 blocks as zeros
constexpr uint8_t g_splitValidPatterns[9] = { 0x07, 0x0C, 0x07, 0x0C, 0x24, 0x24, 0x24, 0x1C, 0x01 };

// BC7 split streams made invalid by the test (Game.cpp CorruptSplitStream): a field the shaders check, set out of its range, with the
// case's argument.  The shaders reject the stream, leaving the destination untouched
enum SplitReject : uint8_t
{
    SplitRejectNone,
    SplitRejectModesUsed,           // start byte: arg modes (0, or more than 9)
    SplitRejectModeEncodingA,       // start byte: mode encoding A (bit 4 clear), which the shaders don't decode
    SplitRejectExtensionReserved,   // extension byte: reserved bit arg (5-7)
    SplitRejectLutMode,             // LUT entry 0: mode arg (9-15)
    SplitRejectLutDuplicate,        // LUT entry arg (1, or 8: the mode 8 entry, whose blocks have no section selector): entry 0's mode
                                    // and pattern
    SplitRejectLutPattern,          // the LUT entry of mode arg >> 3: pattern arg & 7, which g_splitValidPatterns doesn't have
    SplitRejectMode8Extensions,     // the mode 8 LUT entry: the additional extensions bit, which mode 8 doesn't have
    SplitRejectExtensionsReserved,  // the first additional extensions byte: reserved bit arg (5-7)
    SplitRejectEndpointOrder3,      // the first additional extensions byte: 3 endpoint order bytes
    SplitRejectRotationChannels,    // a rotation header's channels: arg 0, a mode 0-3 header with RG; 1, one with RGBA; 2, a mode 4-7
                                    // header with RGB; 3, a mode 0-3 header with RGA (as many channels, so the header still parses)
    SplitRejectRotationFrequency,   // the second rotation header: another region size than the first's
    SplitRejectSelectorReserved,    // mode 0's section selector: reserved bit arg (3 or 7)
    SplitRejectSelectorUnit,        // a unit its section can't use: arg 0, 2 byte units in mode 0's color section (9 bytes); 1, 4 byte
                                    // units in mode 7's misc section (4 bytes)
    SplitRejectTableOffset,         // mode 0's remap table offset: 1
    SplitRejectTableShort,          // mode arg's remap tables: 1 byte short of a length byte and a symbol per position remapped
    SplitRejectTableLong,           // mode 7's remap tables: 1 byte past a length byte and 256 symbols per position remapped
    SplitRejectTableUnremapped,     // mode 6, which remaps no section: a remap table byte
    SplitRejectSelectorFewBlocks,   // mode 1's section selector, with 1 mode 1 block (40x4): 1 byte color units
    SplitRejectSelectorUnusedMode,  // mode 7's section selector, with no mode 7 blocks: 1 byte color units
    SplitRejectBlockIndex,          // block 0's mode index: arg (9-15), which names no LUT entry (of 9)
};

// BC7 split test content (GTestParameters BC7.content)
enum SplitContent : uint8_t
{
    SplitContentModes,          // blocks cycling through modes 0-8 (Game::CreateBC7RawFromMetadata)
    SplitContentMode8Only,      // every block mode 8 (zero): a one-entry LUT, which the prepass reads apart
    SplitContentNoMode7,        // as SplitContentModes, with the mode 7 blocks made mode 6 blocks
    SplitContentFewModes,       // as SplitContentModes for the first 36 blocks (4 of each mode), the rest mode 8: a stream small enough
                                // that the encoder grants mode 0's color remap, and shorter than its symbol reads could reach
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
