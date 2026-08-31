//--------------------------------------------------------------------------------------
// PrepassCS.hlsli
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedDefinitions.hlsli"

struct Constants
{
    uint blockCount;
    uint offsetIntoHistogram;
};
ConstantBuffer<Constants> constants : register(b0);

ByteAddressBuffer shuffledBuffer : register(t0);

RWByteAddressBuffer scratchBuffer : register(u0);

// LDS variables
groupshared uint gs_modeCountsInThreadGroup[BC7_MODES_COUNT];
groupshared uint gs_modeLUT[BC7_MODES_COUNT]; 
groupshared uint gs_currentByteIndex;
groupshared uint gs_modesUsedCount;


[numthreads(TG_THREAD_COUNT, 1, 1)]
[RootSignature(PrepassCS_RS)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 gId : SV_GroupID, uint gindex : SV_GroupIndex)
{
    // Only process up to number of blocks (wave might have more lanes)
    if (DTid.x >= constants.blockCount)
    {
        return;
    }

    uint chunkCount = 0;
    
    if (gindex == 0)
    {
        // Zero-out shared memory variables 
        [unroll(BC7_MODES_COUNT)]
        for (uint m = 0; m < BC7_MODES_COUNT; ++m)
        {
            gs_modeCountsInThreadGroup[m] = 0;
            gs_modeLUT[m] = 0;
        }
        
        uint currentByteIndex = 0u;
        uint Byte0 = fetchNextByte(currentByteIndex++, shuffledBuffer);
        
        // ModesUsed goes [0-8], so it uses first 4 bits
        uint modesUsedCount = Byte0 & 0xf;

        // GPU_ASSERT(modesUsedCount <= 8u, scratchBuffer);
    
        // the 4 most significant bits will be m6
        uint modeEncodingFlag = (Byte0 >> 4) & 0x1u;
        uint staticFieldsFlag = (Byte0 >> 5) & 0x1u;
        uint lowEntropyFlag = (Byte0 >> 6) & 0x1u;
        
        uint globalStatics = (staticFieldsFlag) ? fetchNextByte(currentByteIndex++, shuffledBuffer) : 0u;
        uint globalLowEntropy = (lowEntropyFlag) ? fetchNextByte(currentByteIndex++, shuffledBuffer) : 0u;
     
        if (DTid.x == 0u)
        {
            scratchBuffer.Store(4u, modesUsedCount);
            //scratchBuffer.Store(x, header.modeEncoding);
        }
        
        // Populate array with modes used from most to least frequently
        for (int i = 0; i < modesUsedCount; ++i)
        {
            uint lutByte = fetchNextByte(currentByteIndex++, shuffledBuffer);
            
            // Mode is encoded in first 4 bits
            uint mode = lutByte & 0xf;
            
            // Storing the used modes, ordered by frequency, in shared memory and in header
            gs_modeLUT[i] = mode;
            if (DTid.x == 0u)
            {
                scratchBuffer.Store(16u + (32 * i), mode);
            }
            
            if (mode == 8)
            {
                continue;
            }
            
            // following 3 bits are the mode pattern
            uint modePattern = (lutByte >> 4) & 0x7u;
            uint packedInfo = (globalStatics) | (globalLowEntropy << 8);
            
            if (DTid.x == 0u)
            {
                // Set these to defaults, may later be overwritten
                scratchBuffer.Store(16u + (32 * mode) + 12u, packedInfo); // modeStatics and lowEntropy
                scratchBuffer.Store(16u + (32 * mode) + 4u, 0u); // modeRotationByteAddress
                
                scratchBuffer.Store(16u + (32 * mode) + 16u, modePattern);
                
                ModeBitLayout mbl = GetModeSpecificLayout(mode, modePattern);
                scratchBuffer.Store(16u + (32 * mode) + 20u, mbl.colorSizeBytes);
                scratchBuffer.Store(16u + (32 * mode) + 24u, mbl.miscSizeBytes);
                scratchBuffer.Store(16u + (32 * mode) + 28u, mbl.scrapSizeBits);
            }
        
            // Final bit are additionalExtensions.
            uint additionalExtensions = lutByte >> 7u;
            
            if (additionalExtensions)
            {
                uint nextByte = fetchNextByte(currentByteIndex++, shuffledBuffer);
                
                uint modeStatics = nextByte & 0x1u;
                if (modeStatics)
                {
                    uint modeStatic = fetchNextByte(currentByteIndex++, shuffledBuffer);
                    packedInfo = (packedInfo & 0xFFFFFF00u) | modeStatic;
                }
                
                uint modeLowEntropy = (nextByte >> 1u) & 0x1u;
                if (modeLowEntropy)
                {
                    uint lowEntropy = fetchNextByte(currentByteIndex++, shuffledBuffer);
                    packedInfo = (packedInfo & 0xFFFF00FFu) | (lowEntropy << 8u);
                }
                
                uint endpointOrderBytes = (nextByte >> 2u) & 0x3u;
                if (endpointOrderBytes)
                {
                    uint byte1 = fetchNextByte(currentByteIndex++, shuffledBuffer);
                    uint byte2 = (endpointOrderBytes == 2u) ? fetchNextByte(currentByteIndex++, shuffledBuffer) : 0u;
                    WriteBitsToDword((byte2 << 8u | byte1), 16u, packedInfo);
                }
                else
                {
                    WriteBitsToDword(0u, 16u, packedInfo);
                }
                
                // If either modeLowEntropy, modeStatics or endpointOrderBytes are set, overwrite packedInfo
                if (DTid.x == 0u && (modeStatics || modeLowEntropy || endpointOrderBytes))
                {
                    scratchBuffer.Store(16u + (32 * mode) + 12u, packedInfo);
                }
                
                uint rotation = (nextByte >> 4) & 0x1u;
                if (rotation)
                {
                    if (DTid.x == 0u)
                    {
                        scratchBuffer.Store(16u + (32 * mode) + 4u, currentByteIndex);
                    }
                    
                    uint lutRotationHeader = fetchNextByte(currentByteIndex++, shuffledBuffer);
                        
                    uint alpha = (lutRotationHeader >> 3u) & 0x1F;
                    uint frequency = (lutRotationHeader >> 4u) & 0xF;
                    uint modeChannelBits = (alpha == 0u) ? 6u : 8u;
                    
                    uint chunkSize = 1u << (frequency + 12u);
                    uint chunkCounts = ((constants.blockCount * 16u) + chunkSize - 1u) / chunkSize;
                    
                    if (DTid.x == 0u)
                    {
                        scratchBuffer.Store(8u, chunkCounts);
                        scratchBuffer.Store(12u, chunkSize);
                    }
                
                    // This is after we already got one byte out of the array
                    uint arrayCountInBytes = modeChannelBits * chunkCounts;
                    currentByteIndex += arrayCountInBytes;
                }
            }
        }
        
        // Write header to memory - Only one thread writes this
        // header.shuffledHeaderSizeBytes = currentByteIndex;
        if (DTid.x == 0u)
        {
            scratchBuffer.Store(0u, currentByteIndex);
        }
        
        gs_currentByteIndex = currentByteIndex;
        gs_modesUsedCount = modesUsedCount;
    }
    
    GroupMemoryBarrierWithGroupSync(); // ----------------------
    
    // Based on how many bits we use to store mode, we get which byte we need to fetch from
    uint bitsPerMode = ModeTransformBCountToModeBits[gs_modesUsedCount];
    uint bitStartPos = (gs_currentByteIndex * 8u) + DTid.x * bitsPerMode;
    
    uint parsedMode = ParseBitsFromBuffer(bitStartPos, bitsPerMode, shuffledBuffer);
    uint threadMode = gs_modeLUT[parsedMode];

    // GPU_ASSERT(gs_modesUsedCount <= 8u && parsedMode <= gs_modesUsedCount, scratchBuffer);

    // keeps track of number of blocks per wave
    uint previousModeCount;
    InterlockedAdd(gs_modeCountsInThreadGroup[threadMode], 1, previousModeCount);
    
    GroupMemoryBarrierWithGroupSync(); // ----------------------
    
    uint histogramCount = (constants.blockCount + TG_THREAD_COUNT - 1) / TG_THREAD_COUNT;
    uint perModeMemoryRegionSizeBytes = histogramCount * DWORD_BYTES;
    
    // Write into the histogram. We write each mode total count (for this group) into 
    // a separate memory offset in the histogram buffer.
    // We got to offset per mode, but also offset to the right index within that mode.
    if (gindex == 0)
    {
        // This needs to be a loop, since the wave might have less than 9 active threads
        [unroll(BC7_MODES_COUNT)]
        for (uint m = 0; m < BC7_MODES_COUNT; ++m)
        {
            // Get number of times mode m appeared in this wave.
            uint modeCount = gs_modeCountsInThreadGroup[m];
            
            uint address = (m * perModeMemoryRegionSizeBytes) + gId.x * DWORD_BYTES;
            
            // Write into the final address.
            scratchBuffer.Store(constants.offsetIntoHistogram + address, modeCount);
        }
    }
}