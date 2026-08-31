//--------------------------------------------------------------------------------------
// UnshuffleCurveOnly.hlsl
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "Shared.hlsli"
#include "SharedDefinitions.hlsli"

ByteAddressBuffer srcBuffer : register(t0);

RWStructuredBuffer<uint4> dstBuffer : register(u0);

cbuffer TexInfo : register(b0)
{
    uint bufferSizeInBytes;
    uint bufferOffsetInBytes;
    uint transformId;
    uint widthInPixels;
};

[numthreads(32, 1, 1)]
[RootSignature(RootSig4c)]
void main(uint3 dtID : SV_DispatchThreadID)
{
    // 16KB tile lines are always composed of 32 lines of 512 bytes
    uint totalTileLines = bufferSizeInBytes / 512;
    uint total16ByteBlocks = bufferSizeInBytes / 16;   
    uint bufferOffsetInBlocks = bufferOffsetInBytes / 16;
    
    uint threadIndex = dtID.x;
    
    uint4 block16B[32];
    [unroll(32)]
    for (int i = 0; i < 32; ++i)
    {
        block16B[i] = srcBuffer.Load4((threadIndex * 512) + (i * 16));
    }
    
    if (transformId == 23)
    {
        uint finalBlockID = ReverseSpaceCurveFor16ByteBlock(bufferSizeInBytes, widthInPixels, threadIndex * 32);

        [unroll(32)]
        for (int i = 0; i < 32; ++i)
        {
            dstBuffer[bufferOffsetInBlocks + finalBlockID + i] = block16B[i];
        }      
    }
    else // invalid transformId, just copy the data to the destination buffer
    {
        [unroll(32)]
        for (int i = 0; i < 32; ++i)
        {
            dstBuffer[bufferOffsetInBlocks + (threadIndex * 32) + i] = block16B[i];
        }
    }
}