//--------------------------------------------------------------------------------------
// SharedDefinitions.h
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
#define BC1_BYTES_PER_BLOCK         8
#define BC3_BYTES_PER_BLOCK         16
#define BC4_BYTES_PER_BLOCK         8
#define BC5_BYTES_PER_BLOCK         16
#define BC7_BYTES_PER_BLOCK         16
#define BC7_MODES_COUNT             9
#define DWORD_BITS                  32
#define DWORD_BYTES                 4
#define BINNED_BUFFER_STRIDE        4

// Descriptor Indices
enum DESCRIPTOR_HANDLE_INDICES
{
    UAV_OUTPUT_BUFFER,
    COUNT
};

struct UnshufflePassIndirectBuffer
{
    D3D12_DISPATCH_ARGUMENTS arg;
};

struct ModeBitLayout
{
    uint32_t colorSizeBytes;
    uint32_t miscSizeBytes;
    uint32_t scrapSizeBits;
};

struct PerModeData
{
    uint32_t modeLUT;
    uint32_t modeRotationByteAddress;
    uint32_t totalModeCounts;

    uint32_t packedData;
    uint32_t modePattern;

    ModeBitLayout modeBitLayout;
};

// Duplicate of struct in SharedDefinitions.hlsl. Needed here to expose size on the API side.
struct CommonHeader
{
    uint32_t shuffledHeaderSizeBytes;

    uint32_t modesUsedCount;

    uint32_t chunkCount;

    uint32_t chunkSize;

    PerModeData perModeData[BC7_MODES_COUNT];
};

struct ModeCountsStruct
{
    uint32_t modeCount[BC7_MODES_COUNT];
};