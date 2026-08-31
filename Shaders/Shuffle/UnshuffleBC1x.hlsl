//--------------------------------------------------------------------------------------
// UnshuffleBC1x.hlsl
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "Shared.hlsli"
#include "SharedDefinitions.hlsli"

ByteAddressBuffer srcBuffer : register(t0);

RWStructuredBuffer<uint2> dstBuffer : register(u0);

cbuffer BC1Info : register(b0)
{
    uint bufferSizeInBytes;
    uint bufferOffsetInBytes;
    uint transformId;
    uint widthInPixels;
};

#define BC1_BLOCK_SIZE 8
#define BLOCKS_PER_PAIR 2

[numthreads(256, 1, 1)]
[RootSignature(RootSig4c)]
void main(uint3 dtID : SV_DispatchThreadID, uint3 gID : SV_GroupID)
{
    uint totalBlocks = bufferSizeInBytes / BC1_BLOCK_SIZE;
    uint numPairs = totalBlocks / BLOCKS_PER_PAIR;
    uint numStragglers = totalBlocks % BLOCKS_PER_PAIR;

    uint shuffledBufferSizeInBytes = (numPairs * BLOCKS_PER_PAIR * BC1_BLOCK_SIZE);
    uint bufferOffsetInElements = bufferOffsetInBytes / BC1_BLOCK_SIZE;

    uint threadIndex = dtID.x;

    if(threadIndex < numPairs)
    {
        // Unshuffle two BC1 blocks
        uint4 twoBC1Blocks;
        uint firstBlockIndex = threadIndex * BLOCKS_PER_PAIR;

        if (transformId == 1 || transformId == 17)                                      // micro-pattern 1, and SC variant
        {
            uint e1Offset       = (shuffledBufferSizeInBytes / 4);
            uint indicesOffset  = (shuffledBufferSizeInBytes / 2);

            uint e0Pair         = srcBuffer.Load(firstBlockIndex * 2);
            uint e1Pair         = srcBuffer.Load(e1Offset + firstBlockIndex * 2);
            uint2 indicesPair   = srcBuffer.Load2(indicesOffset + firstBlockIndex * 4);

            twoBC1Blocks.x = (e0Pair & 0x0000FFFF) | (e1Pair << 16);
            twoBC1Blocks.y = indicesPair.x;
            twoBC1Blocks.z = (e0Pair >> 16) | (e1Pair & 0xFFFF0000);
            twoBC1Blocks.w = indicesPair.y;
        } 
        else if (transformId == 8 || transformId == 24)                                // micro-pattern 2, and SC variant
        {
            uint indicesOffset  = (shuffledBufferSizeInBytes / 2);

            uint2 ePairs        = srcBuffer.Load2(firstBlockIndex * 4);
            uint2 indicesPair   = srcBuffer.Load2(indicesOffset + firstBlockIndex * 4);

                // R[15:11] G[10:5] B[4:0]    - 5:6:5
                // R4 R3 R2 R1 R0 G5 G4 G3 G2 G1 G0 B4 B3 B2 B1 B0
                // 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
                //  M  M  M  M  L  M  M  M  M  L  L  M  M  M  M  L

                // src endpoint stream   MSB              LSB
                //
                // byte[0] -           eo.R4:1          eo.G5:2    
                // byte[1] -           eo.B4:1          e1.R4:1    
                // byte[2] -           e1.G5:2          e1.B4:1 
                // byte[3] -    e0.R0  e0.G1:0  e0.B0   e1.R0  e1.G1:0  e1.B0

            twoBC1Blocks.x = (((ePairs.x >> 28) & 0xF) << 12) |          // e0.r4:1      src bit: 28
                             (((ePairs.x >> 24) & 0xF) << 7) |           // e0.g5:2      src bit: 24
                             (((ePairs.x >> 20) & 0xF) << 1) |           // e0.b4:1      src bit: 20
                             (((ePairs.x >> 16) & 0xF) << 28) |          // e1.r4:1      src bit: 16
                             (((ePairs.x >> 12) & 0xF) << 23) |          // e1.g5:2      src bit: 12
                             (((ePairs.x >> 8) & 0xF) << 17) |           // e1.b4:1      src bit: 8


                             (((ePairs.x >> 7) & 0x1) << 11) |           // e0.r0        src bit: 7
                             (((ePairs.x >> 5) & 0x3) << 5) |            // e0.g1:0      src bit: 5
                             (((ePairs.x >> 4) & 0x1) << 0) |            // e0.b0        src bit: 4

                             (((ePairs.x >> 3) & 0x1) << 27) |           // e0.r0        src bit: 3
                             (((ePairs.x >> 1) & 0x3) << 21) |           // e0.g1:0      src bit: 1
                             (((ePairs.x >> 0) & 0x1) << 16);            // e0.b0        src bit: 0

            twoBC1Blocks.y = indicesPair.x;

            twoBC1Blocks.z = (((ePairs.y >> 28) & 0xF) << 12) |          // e0.r4:1      src bit: 28
                             (((ePairs.y >> 24) & 0xF) << 7) |           // e0.g5:2      src bit: 24
                             (((ePairs.y >> 20) & 0xF) << 1) |           // e0.b4:1      src bit: 20
                             (((ePairs.y >> 16) & 0xF) << 28) |          // e1.r4:1      src bit: 16
                             (((ePairs.y >> 12) & 0xF) << 23) |          // e1.g5:2      src bit: 12
                             (((ePairs.y >> 8) & 0xF) << 17) |           // e1.b4:1      src bit: 8


                             (((ePairs.y >> 7) & 0x1) << 11) |           // e0.r0        src bit: 7
                             (((ePairs.y >> 5) & 0x3) << 5) |            // e0.g1:0      src bit: 5
                             (((ePairs.y >> 4) & 0x1) << 0) |            // e0.b0        src bit: 4

                             (((ePairs.y >> 3) & 0x1) << 27) |           // e0.r0        src bit: 3
                             (((ePairs.y >> 1) & 0x3) << 21) |           // e0.g1:0      src bit: 1
                             (((ePairs.y >> 0) & 0x1) << 16);            // e0.b0        src bit: 0
                             
            twoBC1Blocks.w = indicesPair.y;
        }
        else
        {
            // else invalid transform ID...  pass through
            twoBC1Blocks = srcBuffer.Load4(firstBlockIndex * 4);
        }

        if (transformId == 17 || transformId == 33)                                        // curved data requiring reversal
        {
#if USE_SCALAR_REVERSAL
            uint groupBaseBlockID = ReverseSpaceCurveFor8ByteBlock(shuffledBufferSizeInBytes, widthInPixels, gID.x * 256 * BLOCKS_PER_PAIR);
            uint finalBlockRowOffset = (threadIndex & 255) >> 5; // tiles are 32x64 elements.  with 2 blocks per element, that means 32 threads per row
            uint finalBlockID = groupBaseBlockID + finalBlockRowOffset * (widthInPixels >> 2) + (threadIndex & 31) * BLOCKS_PER_PAIR;
#else
            uint finalBlockID = ReverseSpaceCurveFor8ByteBlock(shuffledBufferSizeInBytes, widthInPixels, threadIndex * BLOCKS_PER_PAIR);
#endif
            
            dstBuffer[bufferOffsetInElements + finalBlockID + 0] = twoBC1Blocks.xy;
            dstBuffer[bufferOffsetInElements + finalBlockID + 1] = twoBC1Blocks.zw;
        }
        else  //      transformId == 1 or transformId == 32 or invalid transform           // linear data in, linear data out
        {
            dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_PAIR + 0] = twoBC1Blocks.xy;
            dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_PAIR + 1] = twoBC1Blocks.zw;
        }
    }
    else if(threadIndex < (numPairs + numStragglers))
    {
        // Memcpy the straggler BC1 block (if any)
        uint stragglerIndex = threadIndex - numPairs;
        uint stragglerByteOffset = shuffledBufferSizeInBytes + (stragglerIndex * BC1_BLOCK_SIZE);

        uint2 oneBC1Block = srcBuffer.Load2(stragglerByteOffset);
        dstBuffer[bufferOffsetInElements + numPairs * BLOCKS_PER_PAIR + stragglerIndex] = oneBC1Block;
    }
    // else: do nothing for threads beyond the number we need
}