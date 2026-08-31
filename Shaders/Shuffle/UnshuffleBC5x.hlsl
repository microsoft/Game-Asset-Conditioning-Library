//--------------------------------------------------------------------------------------
// UnshuffleBC5x.hlsl
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "Shared.hlsli"
#include "SharedDefinitions.hlsli"

ByteAddressBuffer srcBuffer : register(t0);

RWStructuredBuffer<uint4> dstBuffer : register(u0);

cbuffer BC5Info : register(b0)
{
    uint bufferSizeInBytes;
    uint bufferOffsetInBytes;
    uint transformId;
    uint widthInPixels;
};

#define BC5_BLOCK_SIZE 16
#define BLOCKS_PER_QUAD 4

[numthreads(256, 1, 1)]
[RootSignature(RootSig4c)]
void main(uint3 dtID : SV_DispatchThreadID)
{
    uint totalBlocks = bufferSizeInBytes / BC5_BLOCK_SIZE;
    uint numQuads = totalBlocks / BLOCKS_PER_QUAD;
    uint numStragglers = totalBlocks % BLOCKS_PER_QUAD;

    uint shuffledBufferSizeInBytes = (numQuads * BLOCKS_PER_QUAD * BC5_BLOCK_SIZE);
    uint bufferOffsetInElements = bufferOffsetInBytes / BC5_BLOCK_SIZE;

    uint threadIndex = dtID.x;

    if(threadIndex < numQuads)
    {
        // Unshuffle four BC5 blocks
        uint firstBlockIndex = (threadIndex * BLOCKS_PER_QUAD);
        uint4 bc5Blocks[4];

        if (transformId == 4 || transformId == 20)
        {
            uint r1Offset           = (shuffledBufferSizeInBytes * 1) / 16;
            uint redIndicesOffset   = (shuffledBufferSizeInBytes * 2) / 16;
            uint g0Offset           = (shuffledBufferSizeInBytes * 8) / 16;
            uint g1Offset           = (shuffledBufferSizeInBytes * 9) / 16;
            uint greenIndicesOffset = (shuffledBufferSizeInBytes * 10) / 16;

            uint r0Quad         = srcBuffer.Load(firstBlockIndex);
            uint r1Quad         = srcBuffer.Load(r1Offset + firstBlockIndex);
            uint3 redIndicesA   = srcBuffer.Load3(redIndicesOffset + firstBlockIndex * 6);
            uint3 redIndicesB   = srcBuffer.Load3(redIndicesOffset + firstBlockIndex * 6 + 12);
            uint g0Quad         = srcBuffer.Load(g0Offset + firstBlockIndex);
            uint g1Quad         = srcBuffer.Load(g1Offset + firstBlockIndex);
            uint3 greenIndicesA = srcBuffer.Load3(greenIndicesOffset + firstBlockIndex * 6);
            uint3 greenIndicesB = srcBuffer.Load3(greenIndicesOffset + firstBlockIndex * 6 + 12);



            bc5Blocks[0].x = ((r0Quad >> 0) & 0xFF) | (((r1Quad >> 0) & 0xFF) << 8) | (redIndicesA.x << 16);
            bc5Blocks[0].y = (redIndicesA.x >> 16) | (redIndicesA.y << 16);
            bc5Blocks[0].z = ((g0Quad >> 0) & 0xFF) | (((g1Quad >> 0) & 0xFF) << 8) | (greenIndicesA.x << 16);
            bc5Blocks[0].w = (greenIndicesA.x >> 16) | (greenIndicesA.y << 16);

            bc5Blocks[1].x = ((r0Quad >> 8) & 0xFF) | (((r1Quad >> 8) & 0xFF) << 8) | (redIndicesA.y & 0xFFFF0000);
            bc5Blocks[1].y = redIndicesA.z;
            bc5Blocks[1].z = ((g0Quad >> 8) & 0xFF) | (((g1Quad >> 8) & 0xFF) << 8) | (greenIndicesA.y & 0xFFFF0000);
            bc5Blocks[1].w = greenIndicesA.z;

            bc5Blocks[2].x = ((r0Quad >> 16) & 0xFF) | (((r1Quad >> 16) & 0xFF) << 8) | (redIndicesB.x << 16);
            bc5Blocks[2].y = (redIndicesB.x >> 16) | (redIndicesB.y << 16);
            bc5Blocks[2].z = ((g0Quad >> 16) & 0xFF) | (((g1Quad >> 16) & 0xFF) << 8) | (greenIndicesB.x << 16);
            bc5Blocks[2].w = (greenIndicesB.x >> 16) | (greenIndicesB.y << 16);

            bc5Blocks[3].x = ((r0Quad >> 24) & 0xFF) | (((r1Quad >> 24) & 0xFF) << 8) | (redIndicesB.y & 0xFFFF0000);
            bc5Blocks[3].y = redIndicesB.z;
            bc5Blocks[3].z = ((g0Quad >> 24) & 0xFF) | (((g1Quad >> 24) & 0xFF) << 8) | (greenIndicesB.y & 0xFFFF0000);
            bc5Blocks[3].w = greenIndicesB.z;
        }
        else
        {
            bc5Blocks[0] = srcBuffer.Load4(firstBlockIndex * 16);
            bc5Blocks[1] = srcBuffer.Load4(firstBlockIndex * 16 + 16);
            bc5Blocks[2] = srcBuffer.Load4(firstBlockIndex * 16 + 32);
            bc5Blocks[3] = srcBuffer.Load4(firstBlockIndex * 16 + 48);
        }

        if (transformId == 20)
        {
            uint finalBlockID = ReverseSpaceCurveFor16ByteBlock(shuffledBufferSizeInBytes, widthInPixels, threadIndex * BLOCKS_PER_QUAD);
            dstBuffer[bufferOffsetInElements + finalBlockID + 0] = bc5Blocks[0];
            dstBuffer[bufferOffsetInElements + finalBlockID + 1] = bc5Blocks[1];
            dstBuffer[bufferOffsetInElements + finalBlockID + 2] = bc5Blocks[2];
            dstBuffer[bufferOffsetInElements + finalBlockID + 3] = bc5Blocks[3];
        }
        else
        {
            dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_QUAD + 0] = bc5Blocks[0];
            dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_QUAD + 1] = bc5Blocks[1];
            dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_QUAD + 2] = bc5Blocks[2];
            dstBuffer[bufferOffsetInElements + threadIndex * BLOCKS_PER_QUAD + 3] = bc5Blocks[3];
        }
    }
    else if(threadIndex < (numQuads + numStragglers))
    {
        // Memcpy the straggler BC5 blocks (if any)
        uint stragglerIndex = threadIndex - numQuads;
        uint stragglerByteOffset = shuffledBufferSizeInBytes + (stragglerIndex * BC5_BLOCK_SIZE);

        uint4 oneBC5Block = srcBuffer.Load4(stragglerByteOffset);
        dstBuffer[bufferOffsetInElements + numQuads * BLOCKS_PER_QUAD + stragglerIndex] = oneBC5Block;
    }
    // else: do nothing for threads beyond the number we need
}
