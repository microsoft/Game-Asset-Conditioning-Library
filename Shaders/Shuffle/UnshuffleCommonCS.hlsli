//--------------------------------------------------------------------------------------
// UnshuffleCommonCS.hlsli
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#define NUM_THREADS 64

#include "SharedDefinitions.hlsli"
#include "UnshuffleHelpers.hlsli"
#include "DerotationHelpers.hlsli"
#include "EndpointOrderingHelpers.hlsli"

struct Constants
{
    uint offsetIntoBinning;
};

ConstantBuffer<Constants> constants : register(b0);

ByteAddressBuffer shuffledBuffer : register(t0);

RWByteAddressBuffer scratchBuffer : register(u0);

RWByteAddressBuffer bc7Buffer : register(u1);


[numthreads(TG_THREAD_COUNT, 1, 1)]
[RootSignature(UnshuffleCS_RS)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{   
    // In the bin model, threadID won't directly tell us where in the final buffer 
    // we are, so we cannot ust fetch that information.
    
    // Depending on which mode dispatch this is, set the mode variable

    const uint mode = BIN_MODE;
    
    // We return if this thread's ID is greater-equal than the number of modes in this bin.
    if (DTid.x >= scratchBuffer.Load(16u + (32 * mode) + 8u))
    {
        return;
    }

    uint headerSizeBytes = scratchBuffer.Load(0u);
    
    // Get accumulated value of all previous modes
    uint modeDensity = 0u;
    [unroll(BIN_MODE)]
    for (uint m = 0; m < BIN_MODE; ++m)
    {
        modeDensity += scratchBuffer.Load(16u + (32 * m) + 8u);
    }
    
    // Loads from the texture that has the binning related info
    uint address = (modeDensity + DTid.x) * BINNED_BUFFER_STRIDE;
    uint finalBlockID = scratchBuffer.Load(constants.offsetIntoBinning + address);
    
    uint4 fetchData = bc7Buffer.Load4(finalBlockID * 16);
    uint colorBaseAddress = fetchData.x;
    uint miscBaseAddress = fetchData.y;
    uint scrapData = fetchData.z;
    uint unswizzledBlockID = fetchData.w; // Block Id before inverse z curve
    
    /// Add the number of extra bits due to endpoint reorder
    const uint scrapSizeBits = scratchBuffer.Load(16u + (32 * mode) + 28u);
    uint extraScrap = scrapData >> scrapSizeBits;
    scrapData = scrapData & (~0u >> (32u - scrapSizeBits));
    
    // Block index divided by bc7 texture size is the chunk index, used to get rotation info
    uint chunkIndex = (unswizzledBlockID * BC7_BYTES_PER_BLOCK) / scratchBuffer.Load(12u);
    uint modeRotationByteAddress = scratchBuffer.Load(16u + (32u * mode) + 4u);
    
    uint4 rawOut = 0;
    
    /// HEADER INFO - this is mode uniform, so also uniform across this wave
    uint packedInfo = scratchBuffer.Load(16u + (32 * mode) + 12u);
    uint modeStatics = packedInfo & 0xFF;
    uint lowEntropy = (packedInfo >> 8u) & 0xFF;
    uint endpointOrderBytes = (packedInfo >> 16u) & 0xFFFF;
    
    uint modePattern = scratchBuffer.Load(16u + (32u * mode) + 16u);
    
    #if BIN_MODE == 0
    unshuffleMode0(colorBaseAddress, miscBaseAddress, scrapData, modePattern, shuffledBuffer, rawOut);
    if (modeRotationByteAddress != 0)
    {
        derotateMode0(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields0(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 1
    unshuffleMode1(colorBaseAddress, miscBaseAddress, scrapData, modePattern, shuffledBuffer, rawOut);
    if (modeRotationByteAddress != 0)
    {
        derotateMode1(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields1(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 2
    unshuffleMode2(colorBaseAddress, miscBaseAddress, scrapData, modePattern, shuffledBuffer, rawOut);
    if (modeRotationByteAddress != 0)
    {
        derotateMode2(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields2(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 3
    unshuffleMode3(colorBaseAddress, miscBaseAddress, scrapData, modePattern, shuffledBuffer, rawOut);
    if (modeRotationByteAddress != 0)
    {
        derotateMode3(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields3(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 4
    unshuffleMode4(colorBaseAddress, miscBaseAddress, scrapData, modePattern, modeStatics, lowEntropy, shuffledBuffer, rawOut);
    if (modeRotationByteAddress != 0)
    {
        derotateMode4(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields4(extraScrap, endpointOrderBytes, rawOut);
    mode4ReorderRGBA(shuffledBuffer, miscBaseAddress, scrapData, rawOut);
    
    #elif BIN_MODE == 5
    unshuffleMode5(colorBaseAddress, miscBaseAddress, scrapData, modePattern, modeStatics, lowEntropy, shuffledBuffer, rawOut);
    if (modeRotationByteAddress != 0)
    {
        derotateMode5(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields5(extraScrap, endpointOrderBytes, rawOut);
    mode5ReorderRGBA(shuffledBuffer, miscBaseAddress, scrapData, rawOut);
    
    #elif BIN_MODE == 6
    unshuffleMode6(colorBaseAddress, miscBaseAddress, scrapData, modePattern, modeStatics, lowEntropy, shuffledBuffer, rawOut);
    if (modeRotationByteAddress != 0)
    {
        derotateMode6(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields6(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 7
    unshuffleMode7(colorBaseAddress, miscBaseAddress, scrapData, modePattern, shuffledBuffer, rawOut);
    if (modeRotationByteAddress != 0)
    {
        derotateMode7(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields7(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 8
    unshuffleMode8(rawOut);
    #endif
    
    uint writeAddress = finalBlockID * 16;
    bc7Buffer.Store4(writeAddress, rawOut);
}