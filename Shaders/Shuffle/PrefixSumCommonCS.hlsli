//--------------------------------------------------------------------------------------
// PrefixSumCommonCS.hlsli
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedDefinitions.hlsli"

#define NUM_THREADS                 1024 
#define NUM_WAVES                   (NUM_THREADS / HW_MIN_SUPPORTED_WAVE_SIZE)

// Bindings 

struct Constants
{
    uint histogramCount;
    uint offsetIntoHistogram;
};
ConstantBuffer<Constants> constants : register(b0);

RWByteAddressBuffer scratchBuffer : register(u0);

// Groupshared memory variables
groupshared uint gs_runningTotals[NUM_WAVES];
groupshared uint gs_waveIndex;

// Call once at entry
uint GroupWaveIndex()
{
    gs_waveIndex = 0; // Initialise to 0
    GroupMemoryBarrierWithGroupSync();

    uint waveIndex;

    // First lane in every wave increments a counter
    // That's the 'wave index'
    if (WaveIsFirstLane())
    {
        InterlockedAdd(gs_waveIndex, 1, waveIndex);
    }

    GroupMemoryBarrierWithGroupSync();

    // Every other thread in that wave now has that wave index too
    return WaveReadLaneFirst(waveIndex);
}

[RootSignature(PrefixSumCS_RS)]
[numthreads(NUM_THREADS, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    // Zero out LDS. gs_runningTotals has NUM_WAVES elements
    if (groupIndex < NUM_WAVES)
    {
        gs_runningTotals[groupIndex] = 0;
    }
    
    uint waveSize = WaveGetLaneCount();
    
    uint groupWaveIndex = GroupWaveIndex();
    groupIndex = groupWaveIndex * waveSize + WaveGetLaneIndex();

    uint perModeMemoryRegionSizeBytes = constants.histogramCount * DWORD_BYTES;
    uint offset = groupID.y * perModeMemoryRegionSizeBytes;
    uint runningTotal = 0;
    
    // hardcoded 16 comes from reading 4 dwords at a time
    for (uint address = groupIndex * 16; address < perModeMemoryRegionSizeBytes; address += (NUM_THREADS * 16))
    {
        uint4 values = scratchBuffer.Load4(constants.offsetIntoHistogram + offset + address);
        uint threadSum = values.x + values.y + values.z + values.w;
        
        uint waveSum = WaveActiveSum(threadSum);
        uint prefixSum = WavePrefixSum(threadSum);
        
        if (WaveIsFirstLane())
        {
            gs_runningTotals[groupWaveIndex] = waveSum;
        }

        GroupMemoryBarrierWithGroupSync();
        
        // TODO this can be optimized further in the future.
        if (groupIndex == 0)
        {
            uint waveSum_i = gs_runningTotals[0];
            gs_runningTotals[0] = runningTotal;
            runningTotal += waveSum_i;
            
            uint realWaveNum = NUM_THREADS / waveSize;
            for (uint i = 1; i < realWaveNum; ++i)
            {
                waveSum_i = gs_runningTotals[i];
                gs_runningTotals[i] = runningTotal;
                runningTotal += waveSum_i;
            }
        }
        
        GroupMemoryBarrierWithGroupSync();

        uint4 result;
        uint accum = gs_runningTotals[groupWaveIndex] + prefixSum;
        result.x = accum + values.x;
        result.y = result.x + values.y;
        result.z = result.y + values.z;
        result.w = result.z + values.w;
        
        scratchBuffer.Store(constants.offsetIntoHistogram + address + offset, result.x);
        if ((address + 4) < perModeMemoryRegionSizeBytes)
            scratchBuffer.Store(constants.offsetIntoHistogram + address + offset + 4, result.y);
        if ((address + 8) < perModeMemoryRegionSizeBytes)
            scratchBuffer.Store(constants.offsetIntoHistogram + address + offset + 8, result.z);
        if ((address + 12) < perModeMemoryRegionSizeBytes)
            scratchBuffer.Store(constants.offsetIntoHistogram + address + offset + 12, result.w);
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // Once a group is done adding up, save the total for the mode in the header
    if (groupIndex == 0)
    {
        uint address = (groupID.y * perModeMemoryRegionSizeBytes) + (constants.histogramCount - 1) * DWORD_BYTES;
        
        // TODO - make sure this does not cause issues
        uint totalModeCount = scratchBuffer.Load(constants.offsetIntoHistogram + address);
        scratchBuffer.Store(16u + (32 * groupID.y) + 8u, totalModeCount);
    }
}