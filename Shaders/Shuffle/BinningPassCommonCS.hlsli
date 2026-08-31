//--------------------------------------------------------------------------------------
// BinningPassCommonCS.hlsli
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedDefinitions.hlsli"

// This is used to allocate according to the minimum supported (worst case) wave size
#define WAVES_PER_THREADGROUP   TG_THREAD_COUNT / HW_MIN_SUPPORTED_WAVE_SIZE

struct Constants
{
    uint histogramCount;
    uint blockCount;
    uint bcTextureWidthPixels;
    bool applySpaceCurveInverse;
    uint offsetIntoHistogram;
    uint offsetIntoBinning;
};

ConstantBuffer<Constants> constants : register(b0);

ByteAddressBuffer shuffledBuffer : register(t0);

RWByteAddressBuffer scratchBuffer : register(u0);

RWByteAddressBuffer bc7Buffer : register(u1);

RWStructuredBuffer<UnshufflePassIndirectBuffer> UnshufflePassIndirectArgsBuffer : register(u2);


// Auxiliary structure. For every mode, returns accumulated mode count (excluding current)
groupshared uint gsPerModeDensityFn[BC7_MODES_COUNT];

groupshared uint gs_perModeExtraScrapBits[BC7_MODES_COUNT];
groupshared uint gs_totalModeCounts[BC7_MODES_COUNT];
groupshared uint gs_colorSizeBytesPerMode[BC7_MODES_COUNT];
groupshared uint gs_miscSizeBytesPerMode[BC7_MODES_COUNT];
groupshared uint gs_scrapSizeBitsPerMode[BC7_MODES_COUNT];
groupshared uint gs_headerSizeBytes;
groupshared uint gs_bitsPerMode;
groupshared uint gs_bc7SizeInBytes;
groupshared uint gs_checkIfSpaceCurveInversePossible;

groupshared ModeCountsStruct gs_modePrefixSumUpToPreviousGroup;

groupshared uint gs_WaveScrapActiveSum[WAVES_PER_THREADGROUP];

groupshared uint gs_modeCountActiveSum[WAVES_PER_THREADGROUP][3];


[numthreads(TG_THREAD_COUNT, 1, 1)]
[RootSignature(ModeBinningCS_RS)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gindex : SV_GroupIndex)
{
    // Indirect args for unshuffle EI pass
    if (DTid.x < BC7_MODES_COUNT)
    {
        uint numberOfGroupsNeeded = (scratchBuffer.Load(16u + (32 * DTid.x) + 8u) + TG_THREAD_COUNT - 1) / TG_THREAD_COUNT;
        
        UnshufflePassIndirectArgsBuffer[DTid.x].ThreadGroupCountX = numberOfGroupsNeeded;
        UnshufflePassIndirectArgsBuffer[DTid.x].ThreadGroupCountY = 1;
        UnshufflePassIndirectArgsBuffer[DTid.x].ThreadGroupCountZ = 1;
    }
    
    // Only process up to number of blocks (wave might have more lanes)
    if (DTid.x >= constants.blockCount)
    {
        return;
    }

    uint modeOffsetBytes = constants.histogramCount * DWORD_BYTES;
    
    if (gindex == 0)
    {
        gs_headerSizeBytes = scratchBuffer.Load(0u);
        uint modesUsedCount = scratchBuffer.Load(4u);
        gs_bitsPerMode = ModeTransformBCountToModeBits[modesUsedCount];
        gs_bc7SizeInBytes = constants.blockCount * 16;

        const uint widthInBlocks = (constants.bcTextureWidthPixels + 3) / 4;
        const uint pitchBytes = 16u * widthInBlocks;
        const uint heightInBlocks = (gs_bc7SizeInBytes + pitchBytes - 1) / pitchBytes;
        gs_checkIfSpaceCurveInversePossible =
            (gs_bc7SizeInBytes > 16384u && countbits(widthInBlocks) == 1u &&
             widthInBlocks >= 32u && countbits(heightInBlocks) == 1u && heightInBlocks >= 32u) ? 1u : 0u;

        // This needs to be a loop, since the wave might have less than 9 active threads.
        [unroll(BC7_MODES_COUNT)]
        for (uint m = 0; m < BC7_MODES_COUNT; ++m)
        {
            uint endpointOrderBytes = (scratchBuffer.Load(16u + (32 * m) + 12u) >> 16u);
            uint totalModeCount = scratchBuffer.Load(16u + (32 * m) + 8u);
            
            // Here we add the number of extra bits due to endpoint reorder   
            uint quad0 = (endpointOrderBytes & 0x000F);
            uint quad1 = (endpointOrderBytes & 0x00F0) >> 4;
            uint quad2 = (endpointOrderBytes & 0x0F00) >> 8;
            uint quad3 = (endpointOrderBytes & 0xF000) >> 12;
            gs_perModeExtraScrapBits[m] = quad3 != 0 ? 4 : (quad2 != 0 ? 3 : (quad1 != 0 ? 2 : (quad0 != 0 ? 1 : 0)));
            
            gs_totalModeCounts[m] = totalModeCount;
            gs_colorSizeBytesPerMode[m] = scratchBuffer.Load(16u + (32 * m) + 20u);
            gs_miscSizeBytesPerMode[m] = scratchBuffer.Load(16u + (32 * m) + 24u);
            gs_scrapSizeBitsPerMode[m] = scratchBuffer.Load(16u + (32 * m) + 28u);
        }
        
        // This needs to be a loop, since the wave might have less than 9 active threads.
        // Get the accumulated mode count (for each mode) for all groups with ID lower than this one.
        gs_modePrefixSumUpToPreviousGroup = (ModeCountsStruct) 0;
        if (gid.x > 0)
        {
            [unroll(BC7_MODES_COUNT)]
            for (uint m = 0; m < BC7_MODES_COUNT; ++m)
            {
                uint address = (modeOffsetBytes * m) + (gid.x - 1) * DWORD_BYTES;
                gs_modePrefixSumUpToPreviousGroup.modeCount[m] = scratchBuffer.Load(constants.offsetIntoHistogram + address);
            }
        }
    }
    
    // This needs to be a loop, since the wave might have less than 9 active threads.
    if (gindex == 0)
    {
        uint offset = 0u;
        [unroll(BC7_MODES_COUNT)]
        for (uint m = 0; m < BC7_MODES_COUNT; ++m)
        {
            gsPerModeDensityFn[m] = offset;
            offset += gs_totalModeCounts[m];
        }
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // Based on how many bits we use to store mode, we get which byte we need to fetch from
    uint bitStartPos = (gs_headerSizeBytes * 8) + DTid.x * gs_bitsPerMode;
    
    uint parsedMode = ParseBitsFromBuffer(bitStartPos, gs_bitsPerMode, shuffledBuffer);
    
    uint mode = scratchBuffer.Load(16u + (32 * parsedMode));
    
    uint waveLaneCount = WaveGetLaneCount();
    
    // Wave index within the threadgroup
    uint waveIndexForThisThread = gindex / waveLaneCount;
    
    uint modeMask_0_3 = (mode < 4) ? 1u << (8u * (mode - 0u)) : 0u;
    uint modeMask_4_7 = (mode >= 4 && mode < 8) ? 1u << (8u * (mode - 4u)) : 0u;
    uint modeMask_8 = (mode == 8) ? 1u : 0u;
    
    uint waveMode03Sum = WaveActiveSum(modeMask_0_3);
    uint waveMode47Sum = WaveActiveSum(modeMask_4_7);
    uint waveMode8Sum = WaveActiveSum(modeMask_8);
    if (WaveIsFirstLane())
    {
        gs_modeCountActiveSum[waveIndexForThisThread][2] = waveMode03Sum;
        gs_modeCountActiveSum[waveIndexForThisThread][1] = waveMode47Sum;
        gs_modeCountActiveSum[waveIndexForThisThread][0] = waveMode8Sum;
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    uint offsetIntoModeAccum = (mode < 4) ? 2u : (mode < 8) ? 1u : 0u;
    
    uint currentModeOffsetWithinWave = 0;
    [unroll(WAVES_PER_THREADGROUP)]
    for (uint w = 0; w < WAVES_PER_THREADGROUP; w++)
    {
        if (w >= waveIndexForThisThread) continue;
        uint waveModeCount = gs_modeCountActiveSum[w][offsetIntoModeAccum];
        uint a = (mode < 4) ? 0 : 4;
        waveModeCount = (waveModeCount >> (8u * (mode - a))) & 0xFFu;
        currentModeOffsetWithinWave += waveModeCount;
    }
    
    if (mode < 4)
    {
        modeMask_0_3 = WavePrefixSum(modeMask_0_3);
        currentModeOffsetWithinWave += (modeMask_0_3 >> (8u * mode)) & 0xFFu;
    }
    else if (mode < 8)
    {
        modeMask_4_7 = WavePrefixSum(modeMask_4_7);
        currentModeOffsetWithinWave += (modeMask_4_7 >> (8u * (mode - 4))) & 0xFFu;
    }
    else
    {
        currentModeOffsetWithinWave += WavePrefixSum(modeMask_8);
    }
        
    // Total count of all modes smaller than this thread's mode.
    // Example: if this thread's mode equals 3, then offsetIntoBinningBuffer is the sum of all blocks 
    // with modes 1 and 2 (in the entire texture).
    uint sumOfSmallerModes = gsPerModeDensityFn[mode];
    
    // We also need the offset, for our mode, regarding all previous waves/TG
    uint modeOffsetPreviousGroups = gs_modePrefixSumUpToPreviousGroup.modeCount[mode];
    
    // Within this wave, how many threads (with lower gtid) share the same mode.
    uint modeOffsetWithinWave = currentModeOffsetWithinWave;
    
    // The final address we want to write to. Each mode uses a 16 byte entry (BINNED_BUFFER_STRIDE) 
    // so we multiply by that amount.
    uint destBufferWriteAddress = (sumOfSmallerModes + modeOffsetPreviousGroups + modeOffsetWithinWave) * BINNED_BUFFER_STRIDE;
    
    // Doing space curve inverse transform.
    uint finalBlockID = DTid.x;
    if (constants.applySpaceCurveInverse && gs_checkIfSpaceCurveInversePossible != 0u)
    {
        finalBlockID = ReverseSpaceCurveFor16ByteBlock(gs_bc7SizeInBytes, constants.bcTextureWidthPixels, finalBlockID);
    }
    
    // Write the current block's ID into the address
    scratchBuffer.Store(constants.offsetIntoBinning + destBufferWriteAddress, finalBlockID);
    
    // COLOR AND MISC
    
    // This section of the shader fetches info from the header and prefix sum buffer to determine the 
    // size of each memory stream (color and misc)
    uint colorStreamSizeBytes = 0;
    uint miscStreamSizeBytes = 0;
    uint modePlusHeaderStreamSizeBits = gs_headerSizeBytes * 8;
        
    [unroll(BC7_MODES_COUNT)]
    for (uint i = 0; i < BC7_MODES_COUNT; ++i)
    {
        uint totalModeCount = gs_totalModeCounts[i];
        uint colorSizeBytes = gs_colorSizeBytesPerMode[i];
        uint miscSizeBytes = gs_miscSizeBytesPerMode[i];
        
        colorStreamSizeBytes += totalModeCount * colorSizeBytes;
        miscStreamSizeBytes += totalModeCount * miscSizeBytes;
        modePlusHeaderStreamSizeBits += totalModeCount * gs_bitsPerMode;
    }
        
    uint MetadataAndModeStreamSizeBytes = (modePlusHeaderStreamSizeBits + 7) >> 3;
    
    // Base address for the entire color and misc memory streams.
    uint colorAddress = MetadataAndModeStreamSizeBytes;
    uint miscAddress = MetadataAndModeStreamSizeBytes + colorStreamSizeBytes;

    // Advance the baseAddress pointers to point at the start of the current mode memory region within the stream.
    // Goes through the totalModeCounts until the previous mode
    [unroll(BC7_MODES_COUNT)]
    for (uint m = 0; m < BC7_MODES_COUNT; ++m)
    {
        if (m >= mode) continue;
        uint modeCount = gs_totalModeCounts[m];
        uint colorSizeBytes = gs_colorSizeBytesPerMode[m];
        uint miscSizeBytes = gs_miscSizeBytesPerMode[m];
        
        colorAddress += modeCount * colorSizeBytes;
        miscAddress += modeCount * miscSizeBytes;
    }
    
    uint colorSizeBytes = gs_colorSizeBytesPerMode[mode];
    uint miscSizeBytes = gs_miscSizeBytesPerMode[mode];
    
    // Advance the baseAddress pointers to point to the ofset for the 
    // curent wave (further inside the memory region for the mode)
    colorAddress += modeOffsetPreviousGroups * colorSizeBytes;
    miscAddress += modeOffsetPreviousGroups * miscSizeBytes;

    // Advance the baseAddress pointers to account for the mode-offset inside the wave
    colorAddress += currentModeOffsetWithinWave * colorSizeBytes;
    miscAddress += currentModeOffsetWithinWave * miscSizeBytes;
    
    // ScrapStream base address will be the sum of every previous stream size.
    uint scrapStreamBase = MetadataAndModeStreamSizeBytes + colorStreamSizeBytes + miscStreamSizeBytes;
    
    // Scrap offset (in bits) for the start of this group (offset w.r.t start of stream)
    uint scrapWaveOffsetBits = 0;
    [unroll(BC7_MODES_COUNT)]
    for (uint i = 0; i < BC7_MODES_COUNT; ++i)
    {
        uint scrapSizeBits = gs_scrapSizeBitsPerMode[i];
        uint modeScrapBits = scrapSizeBits + gs_perModeExtraScrapBits[i];
        scrapWaveOffsetBits += gs_modePrefixSumUpToPreviousGroup.modeCount[i] * modeScrapBits;
    }
    
    // PrefixSum so we know our offset within the wave 
    uint scrapSizeBits = gs_scrapSizeBitsPerMode[mode];
    uint threadScrapSizeBits = scrapSizeBits + gs_perModeExtraScrapBits[mode];

    // Only first thread of each wave needs to do this
    uint waveScrapSizeBits = WaveActiveSum(threadScrapSizeBits);
    if (WaveIsFirstLane())
    {
        gs_WaveScrapActiveSum[waveIndexForThisThread] = waveScrapSizeBits;
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    uint scrapThreadOffsetBitsInTG = 0u;
    [unroll(WAVES_PER_THREADGROUP)]
    for (uint w = 0; w < WAVES_PER_THREADGROUP; w++)
    {
        scrapThreadOffsetBitsInTG += (w < waveIndexForThisThread) ? gs_WaveScrapActiveSum[w] : 0u;
    }
    scrapThreadOffsetBitsInTG += WavePrefixSum(threadScrapSizeBits);
    
    uint scrapAddressBits = (scrapStreamBase * 8) + scrapWaveOffsetBits + scrapThreadOffsetBitsInTG;
    uint scrapAddressBytes = scrapAddressBits >> 3;
    uint bitOffset = scrapAddressBits & 0x7u;
    
    uint bitsInFirstByte = min(8 - bitOffset, threadScrapSizeBits);
    threadScrapSizeBits -= bitsInFirstByte;
    uint bitsInSecondByte = min(8, threadScrapSizeBits);
    uint bitsInThirdByte = threadScrapSizeBits - bitsInSecondByte;
    
    uint scrapData = 0;
 
    if (bitsInThirdByte) // Load 3 bytes
    {
        uint scrapData1 = fetchNextByte(scrapAddressBytes++, shuffledBuffer);
        uint scrapData2 = fetchNextByte(scrapAddressBytes++, shuffledBuffer);
        uint scrapData3 = fetchNextByte(scrapAddressBytes, shuffledBuffer);
     
        uint mask3 = (~0u) >> (32 - bitsInThirdByte);
        scrapData = (scrapData1 >> bitOffset) | (scrapData2 << bitsInFirstByte) | ((scrapData3 & mask3) << (bitsInFirstByte + bitsInSecondByte));
    }
    else if (bitsInSecondByte) // Load 2 bytes
    {
        uint scrapData1 = fetchNextByte(scrapAddressBytes++, shuffledBuffer);
        uint scrapData2 = fetchNextByte(scrapAddressBytes, shuffledBuffer);
     
        uint mask2 = (~0u) >> (32 - bitsInSecondByte);
        scrapData = (scrapData1 >> bitOffset) | ((scrapData2 & mask2) << bitsInFirstByte);
    }
    else // Load 1 bytes
    {
        scrapData = fetchNextByte(scrapAddressBytes, shuffledBuffer);
        scrapData = (scrapData >> bitOffset) & (0xFFu >> (8u - bitsInFirstByte));
    }
    
    // ENDING 
    
    uint4 result;
    result.x = colorAddress;
    result.y = miscAddress;
    result.z = scrapData;
    result.w = DTid.x; // We need to pass the blockID before the inverse z curve transform, since its used for the chunk calculation in the next pass.
    
    // Write the current block's address into color stream
    bc7Buffer.Store4(16 * finalBlockID, result);
}
