//--------------------------------------------------------------------------------------
// SharedDefinitions.hlsli
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

// This value will determine the size of the histograms (how many blocks are in each histogram)
// and also the threadgroup count for every pass except prefix sum.
// Temporary workaround - This is fixed to 256 to make sure the dispatch limit for d3d12 won't be an issue for 16k textures
#define TG_THREAD_COUNT             256u

// Constant values
#define BC7_MODES_COUNT             9
#define BC7_BYTES_PER_BLOCK         16
#define DWORD_BITS                  32
#define DWORD_BYTES                 4
#define BINNED_BUFFER_STRIDE        4

/// Root Signatures
#define PrepassCS_RS " \
    RootConstants(num32BitConstants = 2, b0),\
    SRV(t0),\
    UAV(u0)"

#define PrefixSumCS_RS " \
    RootConstants(num32BitConstants = 2, b0),\
    UAV(u0)"

#define ModeBinningCS_RS "\
    RootConstants(num32BitConstants = 6, b0),\
    SRV(t0),\
    UAV(u0),\
    UAV(u1),\
    UAV(u2)"

#define UnshuffleCS_RS "\
    RootConstants(num32BitConstants = 1, b0),\
    SRV(t0),\
    UAV(u0),\
    UAV(u1)"

struct UnshufflePassIndirectBuffer
{
    uint ThreadGroupCountX;
    uint ThreadGroupCountY;
    uint ThreadGroupCountZ;
};

// Packed data corresponds to:
//  uint modeStatics: 8
//  uint lowEntropy: 8
//  uint endpointOrderBytes: 16
struct ModeBitLayout
{
    uint colorSizeBytes;
    uint miscSizeBytes;
    uint scrapSizeBits;
};

struct PerModeData
{
    uint modeLUT;
    uint modeRotationByteAddress;
    uint totalModeCounts;
    
    uint packedData;
    uint modePattern;
    
    ModeBitLayout modeBitLayout;
};

struct CommonHeader 
{
    uint shuffledHeaderSizeBytes;

    uint modesUsedCount;
    
    uint chunkCount;
    
    uint chunkSize;
    
    PerModeData perModeData[BC7_MODES_COUNT];
};

struct ModeCountsStruct
{
    uint modeCount[BC7_MODES_COUNT];
};

static uint ModeTransformBCountToModeBits[] =
{
    0,
    0, //  if there's only one mode, we know which one by the LUT byte
    1, //  2 modes
    2, //  3 modes
    2, //  4 modes
    3, //  5
    3, //  6
    3, //  7
    3, //  8
    4, //  9 - only exists in pre-swizzled textures, which fill blank space with mode 8
};

#define EndpointPair4bit                            0
#define ColorPlane4bit                              1
#define EndpointPairSignificantBitInderleaved       2
#define EndpointQuadSignificantBitInderleaved       3
#define EndpointQuadSignificantBitInderleavedAlt    4
#define StableIsland                                5

uint2 parallelExtract(uint tileId, uint2 mask)
{
    uint2 res = 0.xx;
    for (uint bb = 1; mask.x != 0 || mask.y != 0; bb += bb)
    {
        if (tileId & mask.x & -mask.x)
        {
            res.x |= bb;
        }
        
        if (tileId & mask.y & -mask.y)
        {
            res.y |= bb;
        }
        
        mask &= (mask - uint2(1, 1));
    }
    return res;
}

ModeBitLayout GetModeSpecificLayout(uint mode, uint pattern) 
{
    ModeBitLayout result = (ModeBitLayout)0;
    switch (mode) 
    {
    case 0:
        if (pattern == ColorPlane4bit || pattern == EndpointPair4bit) 
        {
            result.colorSizeBytes = 9;
            result.miscSizeBytes = 6;
            result.scrapSizeBits = 7;
        }
        else if (pattern == EndpointPairSignificantBitInderleaved)
        {
            result.colorSizeBytes = 9;
            result.miscSizeBytes = 6;
            result.scrapSizeBits = 7;
        }
        break;
    case 1:
        if (pattern == EndpointQuadSignificantBitInderleaved)
        {
            result.colorSizeBytes = 9;
            result.miscSizeBytes = 6;
            result.scrapSizeBits = 6;
        }
        else if (pattern == EndpointPairSignificantBitInderleaved)
        {
            result.colorSizeBytes = 10;
            result.miscSizeBytes = 5;
            result.scrapSizeBits = 6;
        }
        break;
    case 2:
        if (pattern == ColorPlane4bit || pattern == EndpointPair4bit)
        {
            result.colorSizeBytes = 9;
            result.miscSizeBytes = 6;
            result.scrapSizeBits = 5;
        }
        else if (pattern == EndpointPairSignificantBitInderleaved)
        {
            result.colorSizeBytes = 12;
            result.miscSizeBytes = 3;
            result.scrapSizeBits = 5;
        }
        break;
    case 3:
        if (pattern == EndpointQuadSignificantBitInderleaved)
        {
            result.colorSizeBytes = 11;
            result.miscSizeBytes = 4;
            result.scrapSizeBits = 4;
        }
        else if (pattern == EndpointPairSignificantBitInderleaved)
        {
            result.colorSizeBytes = 12;
            result.miscSizeBytes = 3;
            result.scrapSizeBits = 4;
        }
        break;
    case 4:
        if (pattern == StableIsland)
        {
            result.colorSizeBytes = 5;
            result.miscSizeBytes = 10;
            result.scrapSizeBits = 3;
        }
        else if (pattern == EndpointPairSignificantBitInderleaved)
        {
            result.colorSizeBytes = 5;
            result.miscSizeBytes = 10;
            result.scrapSizeBits = 3;
        }
        break;
    case 5:
        if (pattern == StableIsland)
        {
            result.colorSizeBytes = 7;
            result.miscSizeBytes = 8;
            result.scrapSizeBits = 2;
        }
        else if (pattern == EndpointPairSignificantBitInderleaved)
        {
            result.colorSizeBytes = 7;
            result.miscSizeBytes = 8;
            result.scrapSizeBits = 2;
        }
        break;
    case 6:
        if (pattern == StableIsland)
        {
            result.colorSizeBytes = 7;
            result.miscSizeBytes = 8;
            result.scrapSizeBits = 1;
        }
        else if (pattern == EndpointPairSignificantBitInderleaved)
        {
            result.colorSizeBytes = 7;
            result.miscSizeBytes = 8;
            result.scrapSizeBits = 1;
        }
        break;
    case 7:
        if (pattern == EndpointQuadSignificantBitInderleaved)
        {
            result.colorSizeBytes = 11;
            result.miscSizeBytes = 4;
            result.scrapSizeBits = 0;
        }
        else if (pattern == EndpointQuadSignificantBitInderleavedAlt)
        {
            result.colorSizeBytes = 10;
            result.miscSizeBytes = 5;
            result.scrapSizeBits = 0;
        }
        else if (pattern == EndpointPairSignificantBitInderleaved)
        {
            result.colorSizeBytes = 11;
            result.miscSizeBytes = 4;
            result.scrapSizeBits = 0;
        }
        break;
    case 8: 
        {
            result.colorSizeBytes = 0;
            result.miscSizeBytes = 15;
            result.scrapSizeBits = 0;
        }
        break;
    default:
        break;
    };

    return result;
}

struct ByteCache 
{
    uint start;
    uint end;
    uint data;
};

uint fetchNextByte(uint byteIndex, ByteAddressBuffer buffer)
{
    uint Dword = buffer.Load(byteIndex & ~0x3u);
    uint shiftAmount = (byteIndex & 0x3u) << 3u;
    return (Dword >> shiftAmount) & 0xFFu;
}

uint fetchNextByteCached(uint byteAddress, inout ByteCache byteCache, ByteAddressBuffer buffer)
{
    if (byteCache.start == byteCache.end || 
        byteAddress < byteCache.start || 
        byteAddress > byteCache.end)
    {
        uint dwordAddress = byteAddress & ~0x3u;
        byteCache.data = buffer.Load(dwordAddress);
        byteCache.start = dwordAddress;
        byteCache.end = byteCache.start + 3;
    }

    uint shiftAmount = (byteAddress & 0x3u) << 3u;
    return (byteCache.data >> shiftAmount) & 0xFFu;
}

#define GPU_ASSERT(condition, rwBuffer) \
    do { \
        if (!(condition)) { \
            rwBuffer.Store(0xFFFFFFF0u, 0xDEADBEEF); \
        } \
    } while(0)

#define GPU_ASSERT_ID(condition, rwBuffer, id) \
    do { \
        if (!(condition)) { \
            rwBuffer.Store(0xFFFFFFF0u, 0xA55E0000u | id); \
        } \
    } while(0)

uint ParseBitsFromBuffer(uint bitGlobalPos, uint bitCount, ByteAddressBuffer buffer)
{
    uint byteIndex = bitGlobalPos >> 3u;
    uint positionWithinByte = bitGlobalPos & 0x7u;
    uint combinedData = 0;
    
    uint data1 = fetchNextByte(byteIndex, buffer);
    if (positionWithinByte + bitCount <= 8u)
    {
        uint mask = (0xFFu >> (8 - bitCount)) << positionWithinByte;
        combinedData = (data1 & mask) >> positionWithinByte;
    }
    else
    {
        uint mask1 = (0xffu << positionWithinByte) & 0xFFu;
        data1 = (data1 & mask1) >> positionWithinByte;

        uint bitsInSecondByte = bitCount - (8u - positionWithinByte);
        uint data2 = fetchNextByte(byteIndex + 1, buffer);
        uint mask2 = ~(~0u << bitsInSecondByte);
        data2 &= mask2;

        uint bitsInFirstByte = bitCount - bitsInSecondByte;
        combinedData = (data2 << bitsInFirstByte) | data1;
    }

    return combinedData;
}

void WriteBitsToDword(uint value, uint bitPosition, inout uint destDword)
{
    destDword |= (value << bitPosition);
}

uint ReverseSpaceCurveFor16ByteBlock(uint srcSizeInBytes, uint widthInPixels, uint blockId)
{
    // 32 element * 32 element micro tile  
    const uint tileSizeBytes = (16u * 1024);
    const uint tileCount = srcSizeInBytes >> 14; // tileSizeBytes;

    const uint textureWidthInBlocks = (widthInPixels + 3) / 4;
    const uint tileWidthInBlocks = 32; // 512 (tile width bytes) div by 16 (block width bytes)
    const uint tileHeightInBlocks = 32; // Tiles defined to always have 32 rows

    const uint widthInTiles = textureWidthInBlocks / tileWidthInBlocks;
    const uint heightInTiles = tileCount / widthInTiles;

    uint tileID = blockId >> 10; //(tileWidthInBlocks * tileHeightInBlocks)

    // default mask 
    uint maskBase = tileCount - 1;
    uint2 mask = uint2(0xAAAAAAAA, 0x55555555) & maskBase;
    if (widthInTiles > heightInTiles)
    {
        uint smallDimMask = (heightInTiles * heightInTiles) - 1;
        mask.y &= smallDimMask;
        mask.x |= ~smallDimMask;
    }
    else if (widthInTiles < heightInTiles)
    {
        uint smallDimMask = (widthInTiles * widthInTiles) - 1;
        mask.y |= ~smallDimMask;
        mask.x &= smallDimMask;
    }

    // This transform gives us the Z curve traversal values.
    uint2 dt = parallelExtract(tileID, mask);

    uint blockIdWithinTile = blockId & 1023;
    uint rowWithinTile = blockIdWithinTile >> 5;
    uint colWithinTile = blockIdWithinTile & 31u;

    // Find the ID of the block we need to write to
    uint tileOffsetInBlocks = dt.y * (tileHeightInBlocks * textureWidthInBlocks) + dt.x * (tileWidthInBlocks);
    uint newBlockID = tileOffsetInBlocks + ((rowWithinTile)*textureWidthInBlocks) + (colWithinTile);
    return newBlockID;
}


uint ReverseSpaceCurveFor8ByteBlock(uint srcSizeInBytes, uint widthInPixels, uint blockId)
{
    // 32 element * 64 element micro tile  
    const uint tileSizeBytes = (16u * 1024);
    const uint tileCount = srcSizeInBytes >> 14; // tileSizeBytes;

    const uint textureWidthInBlocks = (widthInPixels + 3) / 4;
    const uint tileWidthInBlocks = 64; // 512 (tile width bytes) div by 8 (block width bytes)
    const uint tileHeightInBlocks = 32; // Tiles defined to always have 32 rows

    const uint widthInTiles = textureWidthInBlocks / tileWidthInBlocks;
    const uint heightInTiles = tileCount / widthInTiles;

    uint tileID = blockId >> 11; //(tileWidthInBlocks * tileHeightInBlocks)

    // default mask 
    uint maskBase = tileCount - 1;
    uint2 mask = uint2(0xAAAAAAAA, 0x55555555) & maskBase;
    if (widthInTiles > heightInTiles)
    {
        uint smallDimMask = (heightInTiles * heightInTiles) - 1;
        mask.y &= smallDimMask;
        mask.x |= ~smallDimMask;
    }
    else if (widthInTiles < heightInTiles)
    {
        uint smallDimMask = (widthInTiles * widthInTiles) - 1;
        mask.y |= ~smallDimMask;
        mask.x &= smallDimMask;
    }

    // This transform gives us the Z curve traversal values.
    uint2 dt = parallelExtract(tileID, mask);

    uint blockIdWithinTile = blockId & 2047;
    uint rowWithinTile = blockIdWithinTile >> 6;
    uint colWithinTile = blockIdWithinTile & 63u;

    // Find the ID of the block we need to write to
    uint tileOffsetInBlocks = dt.y * (tileHeightInBlocks * textureWidthInBlocks) + dt.x * (tileWidthInBlocks);
    uint newBlockID = tileOffsetInBlocks + ((rowWithinTile)*textureWidthInBlocks) + (colWithinTile);
    return newBlockID;
}

/* included as an experiment, for perf measure on different hardware */
uint ReverseSpaceCurveScalar(uint srcSizeInBytes, uint widthInPixels, uint blockId, uint elementSize)
{
    // ---- Uniform setup (depends only on the constant inputs) ----
    const uint tileSizeBytes = (16u * 1024u);
    const uint tileCount = srcSizeInBytes / tileSizeBytes;

    const uint textureWidthInBlocks = (widthInPixels + 3u) / 4u;
    const uint tileWidthInBlocks = (elementSize == 16u) ? 32u : 64u;
    const uint tileHeightInBlocks = 32u; // Tiles defined to always have 32 rows
    const uint tileSizeInBlocks = tileWidthInBlocks * tileHeightInBlocks; // power of two

    const uint widthInTiles = textureWidthInBlocks / tileWidthInBlocks;
    const uint heightInTiles = tileCount / widthInTiles;

    // Integer power-of-two mask base (equivalent to exp2(firstbithigh(tileCount)) - 1).
    uint maskBase = (1u << firstbithigh(tileCount)) - 1u;
    uint2 mask = uint2(0xAAAAAAAAu, 0x55555555u) & maskBase;
    if (widthInTiles > heightInTiles)
    {
        uint smallDimMask = (heightInTiles * heightInTiles) - 1u;
        mask.y &= smallDimMask;
        mask.x |= ~smallDimMask;
    }
    else if (widthInTiles < heightInTiles)
    {
        uint smallDimMask = (widthInTiles * widthInTiles) - 1u;
        mask.y |= ~smallDimMask;
        mask.x &= smallDimMask;
    }

    // Pin the uniform mask into scalar registers (safe no-ops: identical on all lanes).
    // This makes parallelExtract()'s mask isolate/clear and loop control scalar.
    mask.x = WaveReadLaneFirst(mask.x);
    mask.y = WaveReadLaneFirst(mask.y);

    // ---- Per-thread apply (only blockId is divergent) ----
    uint tileID = blockId / tileSizeInBlocks; // power of two -> shift

    // Z curve traversal values (same 5-ish iteration loop as the original).
    uint2 dt = parallelExtract(tileID, mask);

    uint blockIdWithinTile = blockId & (tileSizeInBlocks - 1u); // == blockId % tileSizeInBlocks
    uint rowWithinTile = blockIdWithinTile / tileWidthInBlocks;  // power of two -> shift
    uint colWithinTile = blockIdWithinTile & (tileWidthInBlocks - 1u);

    uint tileOffsetInBlocks = dt.y * (tileHeightInBlocks * textureWidthInBlocks) + dt.x * (tileWidthInBlocks);
    uint newBlockID = tileOffsetInBlocks + (rowWithinTile * textureWidthInBlocks) + colWithinTile;
    return newBlockID;
}