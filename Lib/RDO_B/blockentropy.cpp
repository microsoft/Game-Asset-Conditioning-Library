//-------------------------------------------------------------------------------------
// blockentropy.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#define NOMINMAX

#include "gacl.h"

#include "ClusterBlocks_BruteForce.h"
#include "../helpers/Utility.h"


#define USE_BRUTE_FORCE 1
#define USE_SCALED_DIST 1

/* Block-Level Entropy Reduction algorithm.  Finds and merges similar blocks based on comparisons of decoded pixel data. */

void GACL_RDO_BlockLevelEntropyReduce(
    uint32_t numBlocks,
    _Inout_updates_all_(bcElementSizeBytes* numBlocks)  void* encodedData,
    uint32_t bcElementSizeBytes,
    _Inout_updates_all_(64 * numBlocks) void* decodedR8G8B8A8,
    float uniqueBlockReduce,
    float maxDistSq,
    float avgDistSq
)
{
    // lossless compression window size in bytes.
    uint32_t window = 256 * 1024;

    uint32_t targetDuplicateBlocks = uint32_t(uniqueBlockReduce * numBlocks);

#if USE_BRUTE_FORCE
#define CLUSTER_BLOCKS_METHOD ClusterBlocks_BruteForce
#define CLUSTER_BLOCKS_METHOD_NAME L"ClusterBlocks_BruteForce"
#else
#define CLUSTER_BLOCKS_METHOD ClusterBlocks_SpatialTree
#define CLUSTER_BLOCKS_METHOD_NAME L"ClusterBlocks_SpatialTree"
#endif

    // Set error quality limits (on max error and avg error)
    //  and create instance of ClusterBlocks.
#if USE_SCALED_DIST
    // Use sum of squares divided by min standard deviation error metric
    // Produces better quality
    // Note comparable limits will be different for different metrics.
    //float maxDistSq = 64.0f * 4.0f;
    //float avgDistSq = 64.0f * 0.5f;
    CLUSTER_BLOCKS_METHOD</*UseScaledDist*/true> clusterBlocks;
#else
    // Use sum of squares distance error metric
    //float maxDistSq = 64.0f * 1024.0f;
    //float avgDistSq = 64.0f * 16.0f;
    CLUSTER_BLOCKS_METHOD</*UseScaledDist*/false> clusterBlocks;
#endif

    // Perform block clustering
    uint32_t final_duplicates;
    float final_largestDist;
    float final_avgDist;
    clusterBlocks.ClusterBlocks(
        numBlocks,
        (uint64_t*)encodedData, 
        bcElementSizeBytes / 8,
        reinterpret_cast<Vec64u8*>(decodedR8G8B8A8),
        targetDuplicateBlocks,
        maxDistSq,
        avgDistSq,
        window / bcElementSizeBytes, // window is in blocks here
        final_duplicates, final_largestDist, final_avgDist
    );
    
    Utility::Printf(GACL_Logging_Priority_Medium, CLUSTER_BLOCKS_METHOD_NAME L" %ull: ~maxErr = %g, err = %g, maxAvgErr = %g, avgErr = %g, targetDuplicates = %d, duplicates = %d\n", numBlocks* bcElementSizeBytes, maxDistSq, final_largestDist, avgDistSq, final_avgDist, targetDuplicateBlocks, final_duplicates);
}

/* Helper function to translate linear decoded R8G8B8A8 data into the 4x4 block grouping order expected for block entropy reduction algorithm above */

GACL_API void GACL_RDO_R8G8B8A8LinearToBlockGrouped(
    _Out_writes_all_(64 * ((width + 3) >> 2) * ((height + 3) >> 2)) uint8_t* blockGroupedData,
    _In_reads_(rowPitch* ((height + 3) >> 2)) const uint8_t* linearR8G8B8A8Data,
    size_t rowPitch,
    size_t width,
    size_t height
)
{
    struct RGBA { uint8_t r, g, b, a; };
    RGBA* decodedBlocks = reinterpret_cast<RGBA*>(blockGroupedData);
    size_t blocksX = (width + 3) / 4;
    size_t blocksY = (height + 3) / 4;

    for (size_t by = 0; by < blocksY; ++by) {
        for (size_t bx = 0; bx < blocksX; ++bx) {
            size_t blockIndex = by * blocksX + bx;
            for (size_t y = 0; y < 4; ++y) {
                for (size_t x = 0; x < 4; ++x) {
                    size_t px = bx * 4 + x;
                    size_t py = by * 4 + y;
                    size_t pixelIndex = y * 4 + x;

                    RGBA& dstPixel = decodedBlocks[blockIndex * 16 + pixelIndex];
                    if (px < width && py < height) {
                        const uint8_t* srcPixel = linearR8G8B8A8Data + py * rowPitch + px * 4;
                        dstPixel = { srcPixel[0], srcPixel[1], srcPixel[2], srcPixel[3] };
                    }
                    else {
                        dstPixel = { 0, 0, 0, 0 };
                    }
                }
            }
        }
    }
}