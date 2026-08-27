//--------------------------------------------------------------------------------------
// UnshuffleBC1.hlsl
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "Shared.hlsli"

ByteAddressBuffer srcBuffer : register(t0);

RWStructuredBuffer<uint2> dstBuffer : register(u0);

cbuffer BC1Info : register(b0)
{
    uint bufferSizeInBytes;
    uint bufferOffsetInBytes;
};

#define BC1_BLOCK_SIZE 8
#define BLOCKS_PER_PAIR 2

[numthreads(256, 1, 1)]
[RootSignature(RootSig)]
void main(uint3 dtID : SV_DispatchThreadID)
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
        uint e1Offset       = (shuffledBufferSizeInBytes / 4);
        uint indicesOffset  = (shuffledBufferSizeInBytes / 2);
        uint firstBlockIndex = threadIndex * BLOCKS_PER_PAIR;

        uint e0Pair         = srcBuffer.Load(firstBlockIndex * 2);
        uint e1Pair         = srcBuffer.Load(e1Offset + firstBlockIndex * 2);
        uint2 indicesPair   = srcBuffer.Load2(indicesOffset + firstBlockIndex * 4);

        uint4 twoBC1Blocks;
        twoBC1Blocks.x = (e0Pair & 0x0000FFFF) | (e1Pair << 16);
        twoBC1Blocks.y = indicesPair.x;
        twoBC1Blocks.z = (e0Pair >> 16) | (e1Pair & 0xFFFF0000);
        twoBC1Blocks.w = indicesPair.y;
     
        dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_PAIR + 0] = twoBC1Blocks.xy;
        dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_PAIR + 1] = twoBC1Blocks.zw;
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