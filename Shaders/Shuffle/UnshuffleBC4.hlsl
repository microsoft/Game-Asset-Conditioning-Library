//--------------------------------------------------------------------------------------
// UnshuffleBC4.hlsl
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "Shared.hlsli"

ByteAddressBuffer srcBuffer : register(t0);

RWStructuredBuffer<uint2> dstBuffer : register(u0);

cbuffer BC4Info : register(b0)
{
    uint bufferSizeInBytes;
    uint bufferOffsetInBytes;
};

#define BC4_BLOCK_SIZE 8
#define BLOCKS_PER_QUAD 4

[numthreads(256, 1, 1)]
[RootSignature(RootSig)]
void main(uint3 dtID : SV_DispatchThreadID)
{
    uint totalBlocks = bufferSizeInBytes / BC4_BLOCK_SIZE;
    uint numQuads = totalBlocks / BLOCKS_PER_QUAD;
    uint numStragglers = totalBlocks % BLOCKS_PER_QUAD;

    uint shuffledBufferSizeInBytes = (numQuads * BLOCKS_PER_QUAD * BC4_BLOCK_SIZE);
    uint bufferOffsetInElements = bufferOffsetInBytes / BC4_BLOCK_SIZE;

    uint threadIndex = dtID.x;

    if(threadIndex < numQuads)
    {
        // Unshuffle four BC4 blocks
        uint e1Offset       = (shuffledBufferSizeInBytes / 8);
        uint indicesOffset  = (shuffledBufferSizeInBytes / 4);

        uint firstBlockIndex    = (threadIndex * BLOCKS_PER_QUAD);

        uint e0Quad     = srcBuffer.Load(firstBlockIndex);
        uint e1Quad     = srcBuffer.Load(e1Offset + firstBlockIndex);
        uint3 indicesA  = srcBuffer.Load3(indicesOffset + firstBlockIndex * 6);
        uint3 indicesB  = srcBuffer.Load3(indicesOffset + firstBlockIndex * 6 + 12);

        uint4 bc4Blocks[2];

        bc4Blocks[0].x = ((e0Quad >> 0) & 0xFF) | (((e1Quad >> 0) & 0xFF) << 8) | (indicesA.x << 16);
        bc4Blocks[0].y = (indicesA.x >> 16) | (indicesA.y << 16);

        bc4Blocks[0].z = ((e0Quad >> 8) & 0xFF) | (((e1Quad >> 8) & 0xFF) << 8) | (indicesA.y & 0xFFFF0000);
        bc4Blocks[0].w = indicesA.z;

        bc4Blocks[1].x = ((e0Quad >> 16) & 0xFF) | (((e1Quad >> 16) & 0xFF) << 8) | (indicesB.x << 16);
        bc4Blocks[1].y = (indicesB.x >> 16) | (indicesB.y << 16);

        bc4Blocks[1].z = ((e0Quad >> 24) & 0xFF) | (((e1Quad >> 24) & 0xFF) << 8) | (indicesB.y & 0xFFFF0000);
        bc4Blocks[1].w = indicesB.z;

        dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_QUAD + 0] = bc4Blocks[0].xy;
        dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_QUAD + 1] = bc4Blocks[0].zw;
        dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_QUAD + 2] = bc4Blocks[1].xy;
        dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_QUAD + 3] = bc4Blocks[1].zw;
    }
    else if(threadIndex < (numQuads + numStragglers))
    {
        // Memcpy the straggler BC4 block (if any)
        uint stragglerIndex = threadIndex - numQuads;
        uint stragglerByteOffset = shuffledBufferSizeInBytes + (stragglerIndex * BC4_BLOCK_SIZE);

        uint2 oneBC4Block = srcBuffer.Load2(stragglerByteOffset);
        dstBuffer[bufferOffsetInElements + numQuads * BLOCKS_PER_QUAD + stragglerIndex] = oneBC4Block;
    }
    // else: do nothing for threads beyond the number we need
}
