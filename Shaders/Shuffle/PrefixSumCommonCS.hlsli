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

[RootSignature(PrefixSumCS_RS)]
[numthreads(NUM_THREADS, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    // Waves hold consecutive group indices in lane order (as the binning pass assumes)
    const uint waveSize = WaveGetLaneCount();
    const uint groupWaveIndex = groupIndex / waveSize;
    const uint realWaveNum = NUM_THREADS / waveSize;

    const uint perModeMemoryRegionSizeBytes = constants.histogramCount * DWORD_BYTES;
    const uint regionAddress = constants.offsetIntoHistogram + groupID.y * perModeMemoryRegionSizeBytes;
    uint runningTotal = 0;

    // hardcoded 16 comes from reading 4 dwords at a time.  Every thread runs every pass, so the barriers are uniform; entries past
    // the region count as zero
    for (uint passBase = 0; passBase < perModeMemoryRegionSizeBytes; passBase += (NUM_THREADS * 16))
    {
        const uint address = passBase + groupIndex * 16;
        uint4 values = 0;
        if (address < perModeMemoryRegionSizeBytes)
        {
            values = scratchBuffer.Load4(regionAddress + address);
            values.y = ((address + 4) < perModeMemoryRegionSizeBytes) ? values.y : 0u;
            values.z = ((address + 8) < perModeMemoryRegionSizeBytes) ? values.z : 0u;
            values.w = ((address + 12) < perModeMemoryRegionSizeBytes) ? values.w : 0u;
        }
        const uint threadSum = values.x + values.y + values.z + values.w;

        const uint waveSum = WaveActiveSum(threadSum);
        const uint prefixSum = WavePrefixSum(threadSum);

        if (WaveIsFirstLane())
        {
            gs_runningTotals[groupWaveIndex] = waveSum;
        }

        GroupMemoryBarrierWithGroupSync();

#if HW_MIN_SUPPORTED_WAVE_SIZE >= 32
        // Wave 0 scans the wave totals (there are at most 1024 / 32 = 32 of them, one per lane)
        if (groupWaveIndex == 0)
        {
            const uint lane = WaveGetLaneIndex();
            const uint waveSum_i = (lane < realWaveNum) ? gs_runningTotals[lane] : 0u;
            const uint wavePrefix = WavePrefixSum(waveSum_i);
            if (lane < realWaveNum)
            {
                gs_runningTotals[lane] = runningTotal + wavePrefix;
            }
            runningTotal += WaveActiveSum(waveSum_i);
        }
#else
        if (groupIndex == 0)
        {
            for (uint i = 0; i < realWaveNum; ++i)
            {
                const uint waveSum_i = gs_runningTotals[i];
                gs_runningTotals[i] = runningTotal;
                runningTotal += waveSum_i;
            }
        }
#endif

        GroupMemoryBarrierWithGroupSync();

        uint4 result;
        const uint accum = gs_runningTotals[groupWaveIndex] + prefixSum;
        result.x = accum + values.x;
        result.y = result.x + values.y;
        result.z = result.y + values.z;
        result.w = result.z + values.w;

        if (address < perModeMemoryRegionSizeBytes)
            scratchBuffer.Store(regionAddress + address, result.x);
        if ((address + 4) < perModeMemoryRegionSizeBytes)
            scratchBuffer.Store(regionAddress + address + 4, result.y);
        if ((address + 8) < perModeMemoryRegionSizeBytes)
            scratchBuffer.Store(regionAddress + address + 8, result.z);
        if ((address + 12) < perModeMemoryRegionSizeBytes)
            scratchBuffer.Store(regionAddress + address + 12, result.w);
    }

    // Once a group is done adding up, save the total for the mode in the header: the running total, which is the inclusive sum at the
    // last entry
    if (groupIndex == 0)
    {
        scratchBuffer.Store(16u + (32 * groupID.y) + 8u, runningTotal);
    }
}
