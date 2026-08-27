//--------------------------------------------------------------------------------------
// UnshuffleBC3x.hlsl
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "Shared.hlsli"
#include "SharedDefinitions.hlsli"

ByteAddressBuffer srcBuffer : register(t0);

RWStructuredBuffer<uint4> dstBuffer : register(u0);

cbuffer BC3Info : register(b0)
{
    uint bufferSizeInBytes;
    uint bufferOffsetInBytes;
    uint transformId;
    uint widthInPixels;
};

#define BC3_BLOCK_SIZE 16
#define BLOCKS_PER_QUAD 4

[numthreads(256, 1, 1)]
[RootSignature(RootSig4c)]
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
        uint4 bc3Blocks[4];
        uint firstBlockIndex = (threadIndex * BLOCKS_PER_QUAD);

        if (transformId == 2 || transformId == 18)                                      // micro-pattern 1, and SC variant
        {
            uint a1Offset           = (shuffledBufferSizeInBytes * 1) / 16;
            uint alphaIndicesOffset = (shuffledBufferSizeInBytes * 2) / 16;
            uint e0Offset           = (shuffledBufferSizeInBytes * 8) / 16;
            uint e1Offset           = (shuffledBufferSizeInBytes * 10) / 16;
            uint colorIndicesOffset = (shuffledBufferSizeInBytes * 12) / 16;


            uint a0Quad         = srcBuffer.Load(firstBlockIndex);
            uint a1Quad         = srcBuffer.Load(a1Offset + firstBlockIndex);
            uint3 alphaIndicesA = srcBuffer.Load3(alphaIndicesOffset + firstBlockIndex * 6);
            uint3 alphaIndicesB = srcBuffer.Load3(alphaIndicesOffset + firstBlockIndex * 6 + 12);
            uint2 e0Quad        = srcBuffer.Load2(e0Offset + firstBlockIndex * 2);
            uint2 e1Quad        = srcBuffer.Load2(e1Offset + firstBlockIndex * 2);
            uint4 colorIndices  = srcBuffer.Load4(colorIndicesOffset + firstBlockIndex * 4);

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
        }
        else if (transformId == 34 || transformId == 35)                                // micro-pattern 2, and SC variant
        {
            // v2 has only three streams in a 6:6:4 ratio:
            // stream0[0] - a0.3:0 & a1.3:0               - high entropy alpha bits
            // stream0[1] - a0.7:4 & a1.7:4               - low entropy alpha bits
            // stream0[2] - e0.r4:1 & e0.g5:2
            // stream0[3] - e0.b4:1 & e1.r4:1
            // stream0[4] - e1.g5:2 & e1.b4:1
            // stream0[5] -                               - remaining high precision\entropy color bits
            // stream1                                    - alpha index bytes
            // stream2                                    - color index bits
            //
            //  stream 0 bytes for BC3 quad: 
            //
            //   aH\aL - alpha High\Low bits
            //   cH\cL - color High\Low bits
            //
            //   |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  | 10  | 11  | 12  | 13  | 14  | 15  | 16  | 17  | 18  | 19  | 20  | 21  | 22  | 23  |
            //   |            BC3 block 0            |              BC3 block 1          |            BC3 block 2            |            BC3 block 3            |
            //   | aL    aH    cH    cH    cH    cL  | aL    aH    cH    cH    cH    cL  |  aL   aH    cH    cH    cH    cL  | aL    aH    cH    cH    cH    cL  |  
            //   |        uint3A.x       |        uint3A.y       |        uint3A.z       |        uint3B.x       |        uint3B.y       |        uint3B.z       |


            uint alphaIndicesOffset = (shuffledBufferSizeInBytes * 6) / 16;
            uint colorIndicesOffset = (shuffledBufferSizeInBytes * 12) / 16;

            uint3 alphaColorA   = srcBuffer.Load3(firstBlockIndex * 6);
            uint3 alphaColorB   = srcBuffer.Load3(firstBlockIndex * 6 + 12);

            uint3 alphaIndicesA = srcBuffer.Load3(alphaIndicesOffset + firstBlockIndex * 6);
            uint3 alphaIndicesB = srcBuffer.Load3(alphaIndicesOffset + firstBlockIndex * 6 + 12);
            uint4 colorIndices  = srcBuffer.Load4(colorIndicesOffset + firstBlockIndex * 4);

            bc3Blocks[0].x = 
                (((alphaColorA.x >> 4) & 0xF) << 0) |       // a0.3:0
                (((alphaColorA.x >> 0) & 0xF) << 8) |       // a1.3:0
                (((alphaColorA.x >> 12) & 0xF) << 4) |      // a0.7:4
                (((alphaColorA.x >> 8) & 0xF) << 12) |      // a1.7:4
                (alphaIndicesA.x << 16);
            bc3Blocks[0].y = (alphaIndicesA.x >> 16) | (alphaIndicesA.y << 16);
            bc3Blocks[0].z = 
                (((alphaColorA.x >> 20) & 0xF) << 12) |      // e0.r4:1
                (((alphaColorA.x >> 16) & 0xF) << 7) |       // e0.g5:2
                (((alphaColorA.x >> 28) & 0xF) << 1) |       // e0.b4:1
                (((alphaColorA.x >> 24) & 0xF) << 28) |      // e1.r4:1
                (((alphaColorA.y >> 4) & 0xF) << 23) |       // e1.g5:2
                (((alphaColorA.y >> 0) & 0xF) << 17) |       // e1.b4:1
                (((alphaColorA.y >> 15) & 0x1) << 11) |      // e0.r0
                (((alphaColorA.y >> 13) & 0x3) << 5) |       // e0.g1:0
                (((alphaColorA.y >> 12) & 0x1) << 0) |       // e0.b0
                (((alphaColorA.y >> 11) & 0x1) << 27) |      // e1.r0
                (((alphaColorA.y >> 9) & 0x3) << 21) |       // e1.g1:0
                (((alphaColorA.y >> 8) & 0x1) << 16);        // e1.b0
            bc3Blocks[0].w = colorIndices.x;

            bc3Blocks[1].x = 
                (((alphaColorA.y >> 20) & 0xF) << 0) |       // a0.3:0
                (((alphaColorA.y >> 16) & 0xF) << 8) |       // a1.3:0
                (((alphaColorA.y >> 28) & 0xF) << 4) |       // a0.7:4
                (((alphaColorA.y >> 24) & 0xF) << 12) |      // a1.7:4
                (alphaIndicesA.y & 0xFFFF0000);
            bc3Blocks[1].y = alphaIndicesA.z;
            bc3Blocks[1].z = 
                (((alphaColorA.z >> 4) & 0xF) << 12) |       // e0.r4:1
                (((alphaColorA.z >> 0) & 0xF) << 7) |        // e0.g5:2
                (((alphaColorA.z >> 12) & 0xF) << 1) |       // e0.b4:1
                (((alphaColorA.z >> 8) & 0xF) << 28) |       // e1.r4:1
                (((alphaColorA.z >> 20) & 0xF) << 23) |      // e1.g5:2
                (((alphaColorA.z >> 16) & 0xF) << 17) |      // e1.b4:1
                (((alphaColorA.z >> 31) & 0x1) << 11) |      // e0.r0
                (((alphaColorA.z >> 29) & 0x3) << 5) |       // e0.g1:0
                (((alphaColorA.z >> 28) & 0x1) << 0) |       // e0.b0
                (((alphaColorA.z >> 27) & 0x1) << 27) |      // e1.r0
                (((alphaColorA.z >> 25) & 0x3) << 21) |      // e1.g1:0
                (((alphaColorA.z >> 24) & 0x1) << 16);       // e1.b0
            bc3Blocks[1].w = colorIndices.y;

            bc3Blocks[2].x = 
                (((alphaColorB.x >> 4) & 0xF) << 0) |       // a0.3:0
                (((alphaColorB.x >> 0) & 0xF) << 8) |       // a1.3:0
                (((alphaColorB.x >> 12) & 0xF) << 4) |      // a0.7:4
                (((alphaColorB.x >> 8) & 0xF) << 12) |      // a1.7:4
                (alphaIndicesB.x << 16);
            bc3Blocks[2].y = (alphaIndicesB.x >> 16) | (alphaIndicesB.y << 16);
            bc3Blocks[2].z = 
                (((alphaColorB.x >> 20) & 0xF) << 12) |      // e0.r4:1
                (((alphaColorB.x >> 16) & 0xF) << 7) |       // e0.g5:2
                (((alphaColorB.x >> 28) & 0xF) << 1) |       // e0.b4:1
                (((alphaColorB.x >> 24) & 0xF) << 28) |      // e1.r4:1
                (((alphaColorB.y >> 4) & 0xF) << 23) |       // e1.g5:2
                (((alphaColorB.y >> 0) & 0xF) << 17) |       // e1.b4:1
                (((alphaColorB.y >> 15) & 0x1) << 11) |      // e0.r0
                (((alphaColorB.y >> 13) & 0x3) << 5) |       // e0.g1:0
                (((alphaColorB.y >> 12) & 0x1) << 0) |       // e0.b0
                (((alphaColorB.y >> 11) & 0x1) << 27) |      // e1.r0
                (((alphaColorB.y >> 9) & 0x3) << 21) |       // e1.g1:0
                (((alphaColorB.y >> 8) & 0x1) << 16);        // e1.b0
            bc3Blocks[2].w = colorIndices.z;

            bc3Blocks[3].x = 
                (((alphaColorB.y >> 20) & 0xF) << 0) |       // a0.3:0
                (((alphaColorB.y >> 16) & 0xF) << 8) |       // a1.3:0
                (((alphaColorB.y >> 28) & 0xF) << 4) |       // a0.7:4
                (((alphaColorB.y >> 24) & 0xF) << 12) |      // a1.7:4
                (alphaIndicesB.y & 0xFFFF0000);
            bc3Blocks[3].y = alphaIndicesB.z;
            bc3Blocks[3].z = 
                (((alphaColorB.z >> 4) & 0xF) << 12) |       // e0.r4:1
                (((alphaColorB.z >> 0) & 0xF) << 7) |        // e0.g5:2
                (((alphaColorB.z >> 12) & 0xF) << 1) |       // e0.b4:1
                (((alphaColorB.z >> 8) & 0xF) << 28) |       // e1.r4:1
                (((alphaColorB.z >> 20) & 0xF) << 23) |      // e1.g5:2
                (((alphaColorB.z >> 16) & 0xF) << 17) |      // e1.b4:1
                (((alphaColorB.z >> 31) & 0x1) << 11) |      // e0.r0
                (((alphaColorB.z >> 29) & 0x3) << 5) |       // e0.g1:0
                (((alphaColorB.z >> 28) & 0x1) << 0) |       // e0.b0
                (((alphaColorB.z >> 27) & 0x1) << 27) |      // e1.r0
                (((alphaColorB.z >> 25) & 0x3) << 21) |      // e1.g1:0
                (((alphaColorB.z >> 24) & 0x1) << 16);       // e1.b0
            bc3Blocks[3].w = colorIndices.w;
        }
        else
        {
            // else invalid transform ID...  pass through
            bc3Blocks[0] = srcBuffer.Load4(firstBlockIndex * 16);
            bc3Blocks[1] = srcBuffer.Load4(firstBlockIndex * 16 + 16);
            bc3Blocks[2] = srcBuffer.Load4(firstBlockIndex * 16 + 32);
            bc3Blocks[3] = srcBuffer.Load4(firstBlockIndex * 16 + 48);
        }

        if (transformId == 18 || transformId == 35)                                        // curved data requiring reversal
        {
            uint finalBlockID = ReverseSpaceCurveFor16ByteBlock(shuffledBufferSizeInBytes, widthInPixels, threadIndex * BLOCKS_PER_QUAD);
            dstBuffer[bufferOffsetInElements + finalBlockID + 0] = bc3Blocks[0];
            dstBuffer[bufferOffsetInElements + finalBlockID + 1] = bc3Blocks[1];
            dstBuffer[bufferOffsetInElements + finalBlockID + 2] = bc3Blocks[2];
            dstBuffer[bufferOffsetInElements + finalBlockID + 3] = bc3Blocks[3];
        }
        else  //      transformId == 2 or transformId == 18 or invalid transform           // linear data in, linear data out
        {
            dstBuffer[bufferOffsetInElements + threadIndex * 4 + 0] = bc3Blocks[0];
            dstBuffer[bufferOffsetInElements + threadIndex * 4 + 1] = bc3Blocks[1];
            dstBuffer[bufferOffsetInElements + threadIndex * 4 + 2] = bc3Blocks[2];
            dstBuffer[bufferOffsetInElements + threadIndex * 4 + 3] = bc3Blocks[3];
        }
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
