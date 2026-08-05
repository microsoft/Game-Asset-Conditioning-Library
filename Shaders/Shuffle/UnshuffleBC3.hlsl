//--------------------------------------------------------------------------------------
// UnshuffleBC3.hlsl
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "Shared.hlsli"

ByteAddressBuffer srcBuffer : register(t0);

RWStructuredBuffer<uint4> dstBuffer : register(u0);

cbuffer BC3Info : register(b0)
{
    uint bufferSizeInBytes;
    uint bufferOffsetInBytes;
};

#define BC3_BLOCK_SIZE 16
#define BLOCKS_PER_QUAD 4

[numthreads(256, 1, 1)]
[RootSignature(RootSig)]
void main(uint3 dtID : SV_DispatchThreadID)
{
    uint totalBlocks = bufferSizeInBytes / BC3_BLOCK_SIZE;
    uint numQuads = totalBlocks / BLOCKS_PER_QUAD;
    uint numStragglers = totalBlocks % BLOCKS_PER_QUAD;

    uint shuffledBufferSizeInBytes = (numQuads * BLOCKS_PER_QUAD * BC3_BLOCK_SIZE);
    uint bufferOffsetInElements = bufferOffsetInBytes / BC3_BLOCK_SIZE;

    uint threadIndex = dtID.x;

    if(threadIndex < numQuads)
    {
        // Unshuffle four BC3 blocks
        uint a1Offset           = (shuffledBufferSizeInBytes * 1) / 16;
        uint alphaIndicesOffset = (shuffledBufferSizeInBytes * 2) / 16;
        uint e0Offset           = (shuffledBufferSizeInBytes * 8) / 16;
        uint e1Offset           = (shuffledBufferSizeInBytes * 10) / 16;
        uint colorIndicesOffset = (shuffledBufferSizeInBytes * 12) / 16;

        uint firstBlockIndex = (threadIndex * BLOCKS_PER_QUAD);

        uint a0Quad         = srcBuffer.Load(firstBlockIndex);
        uint a1Quad         = srcBuffer.Load(a1Offset + firstBlockIndex);
        uint3 alphaIndicesA = srcBuffer.Load3(alphaIndicesOffset + firstBlockIndex * 6);
        uint3 alphaIndicesB = srcBuffer.Load3(alphaIndicesOffset + firstBlockIndex * 6 + 12);
        uint2 e0Quad        = srcBuffer.Load2(e0Offset + firstBlockIndex * 2);
        uint2 e1Quad        = srcBuffer.Load2(e1Offset + firstBlockIndex * 2);
        uint4 colorIndices  = srcBuffer.Load4(colorIndicesOffset + firstBlockIndex * 4);

        uint4 bc3Blocks[4];

        bc3Blocks[0].x = ((a0Quad >> 0) & 0xFF) | (((a1Quad >> 0) & 0xFF) << 8) | (alphaIndicesA.x << 16);
        bc3Blocks[0].y = (alphaIndicesA.x >> 16) | (alphaIndicesA.y << 16);
        bc3Blocks[0].z = (e0Quad.x & 0xFFFF) | (e1Quad.x << 16);
        bc3Blocks[0].w = colorIndices.x;

        bc3Blocks[1].x = ((a0Quad >> 8) & 0xFF) | (((a1Quad >> 8) & 0xFF) << 8) | (alphaIndicesA.y & 0xFFFF0000);
        bc3Blocks[1].y = alphaIndicesA.z;
        bc3Blocks[1].z = (e0Quad.x >> 16) | (e1Quad.x & 0xFFFF0000);
        bc3Blocks[1].w = colorIndices.y;

        bc3Blocks[2].x = ((a0Quad >> 16) & 0xFF) | (((a1Quad >> 16) & 0xFF) << 8) | (alphaIndicesB.x << 16);
        bc3Blocks[2].y = (alphaIndicesB.x >> 16) | (alphaIndicesB.y << 16);
        bc3Blocks[2].z = (e0Quad.y & 0xFFFF) | (e1Quad.y << 16);
        bc3Blocks[2].w = colorIndices.z;

        bc3Blocks[3].x = ((a0Quad >> 24) & 0xFF) | (((a1Quad >> 24) & 0xFF) << 8) | (alphaIndicesB.y & 0xFFFF0000);
        bc3Blocks[3].y = alphaIndicesB.z;
        bc3Blocks[3].z = (e0Quad.y  >> 16) | (e1Quad.y & 0xFFFF0000);
        bc3Blocks[3].w = colorIndices.w;

        dstBuffer[bufferOffsetInElements + threadIndex * 4 + 0] = bc3Blocks[0];
        dstBuffer[bufferOffsetInElements + threadIndex * 4 + 1] = bc3Blocks[1];
        dstBuffer[bufferOffsetInElements + threadIndex * 4 + 2] = bc3Blocks[2];
        dstBuffer[bufferOffsetInElements + threadIndex * 4 + 3] = bc3Blocks[3];
    }
    else if(threadIndex < (numQuads + numStragglers))
    {
        // Memcpy the straggler BC3 blocks (if any)
        uint stragglerIndex = threadIndex - numQuads;
        uint stragglerByteOffset = shuffledBufferSizeInBytes + (stragglerIndex * BC3_BLOCK_SIZE);

        uint4 oneBC3Block = srcBuffer.Load4(stragglerByteOffset);
        dstBuffer[bufferOffsetInElements + numQuads * BLOCKS_PER_QUAD + stragglerIndex] = oneBC3Block;
    }
    // else: do nothing for threads beyond the number we need
}
