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
    uint offsetIntoHistogram;
    uint offsetIntoBinning;
};

ConstantBuffer<Constants> constants : register(b0);

ByteAddressBuffer shuffledBuffer : register(t0);

RWByteAddressBuffer scratchBuffer : register(u0);

RWByteAddressBuffer bc7Buffer : register(u1);

RWStructuredBuffer<UnshufflePassIndirectBuffer> UnshufflePassIndirectArgsBuffer : register(u2);


// Per mode: the mode at mode stream (LUT) index m
groupshared uint gs_modeLUTEntry[BC7_MODES_COUNT];
// Per mode: this group's rank 0 binning slot, color byte address, misc byte address and section layout scrap bit address
groupshared uint4 gs_modeBase[BC7_MODES_COUNT];
// Per mode: color bytes, misc bytes and scrap bits (with the endpoint order bits) per block, unused
groupshared uint4 gs_modeSize[BC7_MODES_COUNT];
// Block order scrap layout: this group's first scrap bit address
groupshared uint gs_scrapGroupBaseBits;
groupshared uint gs_bc7SizeInBytes;
groupshared uint gs_spaceCurveWidthInPixels;
groupshared uint gs_sectionLayout;

// Per wave: mode 8 count, modes 4-7 counts and modes 0-3 counts (a byte per mode), scrap bits
groupshared uint4 gs_waveSums[WAVES_PER_THREADGROUP];

struct ModeSlot
{
    uint lutEntry;
    uint totalCount;
    // Per block: 1 (binning slot), color bytes, misc bytes, scrap bits (with the endpoint order bits)
    uint4 perBlock;
    // This mode's blocks in the groups before this one
    uint previousGroupsCount;
};

ModeSlot LoadModeSlot(uint m, uint groupId)
{
    const uint4 modeSlot0 = scratchBuffer.Load4(16u + (32 * m)); // LUT entry, rotation byte address, total count, statics | low entropy | endpoint order
    const uint4 modeSlot1 = scratchBuffer.Load4(32u + (32 * m)); // pattern, color bytes, misc bytes, scrap bits

    // Endpoint reordering adds a scrap bit per endpoint order nibble, up to the highest nonzero one.  Branch free, so that the histogram
    // load below doesn't wait on the loads above (firstbithigh(0) is ~0u, giving 0)
    const uint extraScrapBits = (firstbithigh(modeSlot0.w >> 16u) + 4u) >> 2u;

    ModeSlot slot;
    slot.lutEntry = modeSlot0.x;
    slot.totalCount = modeSlot0.z;
    slot.perBlock = uint4(1u, modeSlot1.y, modeSlot1.z, modeSlot1.w + extraScrapBits);

    // Get the accumulated mode count for all groups with ID lower than this one.
    const uint address = (constants.histogramCount * DWORD_BYTES * m) + (groupId - 1) * DWORD_BYTES;
    slot.previousGroupsCount = (groupId > 0) ? scratchBuffer.Load(constants.offsetIntoHistogram + address) : 0u;
    return slot;
}

// Stream order: header and mode stream, section tables, then the color, misc and scrap streams, each in mode order.  From the stream's
// block, color byte, misc byte and scrap bit totals, returns the first binning slot (0), color byte, misc byte and scrap bit addresses,
// and the section layout flag.  Unused modes may hold stale sizes in their slots; their zero counts cancel them, as all the totals are
// sums of count * size
uint4 StreamBases(uint4 streamTotals, uint headerSizeBytes, uint bitsPerMode, out uint sectionLayout)
{
    // Start byte bit 7 flags the extension byte (stream byte 1), whose bit 4 is the section layout: scrap bits grouped per mode,
    // and remap tables (16 bit size at stream byte 2) between the mode stream and the color data
    const uint streamStartBytes = shuffledBuffer.Load(0u);
    sectionLayout = ((streamStartBytes & 0x80u) != 0u) ? ((streamStartBytes >> 12u) & 0x1u) : 0u;
    const uint sectionTableBytes = (sectionLayout != 0u) ? (streamStartBytes >> 16u) : 0u;

    const uint metadataAndModeStreamSizeBytes = (((headerSizeBytes * 8) + streamTotals.x * bitsPerMode + 7) >> 3) + sectionTableBytes;
    const uint miscStreamBaseBytes = metadataAndModeStreamSizeBytes + streamTotals.y;
    return uint4(0u, metadataAndModeStreamSizeBytes, miscStreamBaseBytes, (miscStreamBaseBytes + streamTotals.z) * 8);
}

// The group wide values, from the thread that builds the tables' mode 0 entry
void StoreGroupValues(uint scrapGroupBaseBits, uint sectionLayout)
{
    gs_scrapGroupBaseBits = scrapGroupBaseBits;
    gs_sectionLayout = sectionLayout;
    gs_bc7SizeInBytes = constants.blockCount * 16;

    // Space curve width recorded in the stream header (0 = linear), only inverted when the dimensions are eligible.  Those need a
    // power of two width, so the pitch is a power of two whenever the height matters
    const uint spaceCurveWidthInPixels = scratchBuffer.Load(GACL_BC7_SC_WIDTH_OFFSET);
    const uint widthInBlocks = (spaceCurveWidthInPixels + 3) / 4;
    const uint pitchBytes = max(16u * widthInBlocks, 1u);
    const uint heightInBlocks = (gs_bc7SizeInBytes + pitchBytes - 1) >> firstbithigh(pitchBytes);
    gs_spaceCurveWidthInPixels =
        (gs_bc7SizeInBytes > 16384u && countbits(widthInBlocks) == 1u &&
         widthInBlocks >= 32u && countbits(heightInBlocks) == 1u && heightInBlocks >= 32u) ? spaceCurveWidthInPixels : 0u;
}

// Wave 0 builds the group's per mode tables, lane m for mode m, so it needs at least BC7_MODES_COUNT lanes
void BuildModeTablesParallel(uint m, uint groupId)
{
    const uint headerSizeBytes = scratchBuffer.Load(0u);
    const uint bitsPerMode = ModeTransformBCountToModeBits[scratchBuffer.Load(4u)];
    ModeSlot slot = (ModeSlot)0;
    if (m < BC7_MODES_COUNT)
    {
        slot = LoadModeSlot(m, groupId);
    }

    const uint4 modeStreamTotals = slot.totalCount * slot.perBlock;
    const uint4 modeStart = WavePrefixSum(modeStreamTotals);
    const uint4 streamTotals = WaveActiveSum(modeStreamTotals);
    const uint previousGroupsScrapBits = WaveActiveSum(slot.previousGroupsCount * slot.perBlock.w);
    uint sectionLayout;
    const uint4 streamBases = StreamBases(streamTotals, headerSizeBytes, bitsPerMode, sectionLayout);
    if (m < BC7_MODES_COUNT)
    {
        gs_modeLUTEntry[m] = slot.lutEntry;
        gs_modeBase[m] = streamBases + modeStart + slot.previousGroupsCount * slot.perBlock;
        gs_modeSize[m] = uint4(slot.perBlock.yzw, 0u);
    }
    if (m == 0)
    {
        StoreGroupValues(streamBases.w + previousGroupsScrapBits, sectionLayout);
    }
}

#if HW_MIN_SUPPORTED_WAVE_SIZE < 16
// Thread 0 builds the group's per mode tables, for waves of fewer than BC7_MODES_COUNT lanes
void BuildModeTablesSerial(uint groupId)
{
    const uint headerSizeBytes = scratchBuffer.Load(0u);
    const uint bitsPerMode = ModeTransformBCountToModeBits[scratchBuffer.Load(4u)];
    ModeSlot slots[BC7_MODES_COUNT];
    uint4 streamTotals = 0u;
    [unroll(BC7_MODES_COUNT)]
    for (uint m = 0; m < BC7_MODES_COUNT; ++m)
    {
        slots[m] = LoadModeSlot(m, groupId);
        streamTotals += slots[m].totalCount * slots[m].perBlock;
    }

    uint sectionLayout;
    uint4 modeStart = StreamBases(streamTotals, headerSizeBytes, bitsPerMode, sectionLayout);
    uint scrapGroupBaseBits = modeStart.w;
    [unroll(BC7_MODES_COUNT)]
    for (uint mb = 0; mb < BC7_MODES_COUNT; ++mb)
    {
        gs_modeLUTEntry[mb] = slots[mb].lutEntry;
        gs_modeBase[mb] = modeStart + slots[mb].previousGroupsCount * slots[mb].perBlock;
        gs_modeSize[mb] = uint4(slots[mb].perBlock.yzw, 0u);
        modeStart += slots[mb].totalCount * slots[mb].perBlock;
        scrapGroupBaseBits += slots[mb].previousGroupsCount * slots[mb].perBlock.w;
    }
    StoreGroupValues(scrapGroupBaseBits, sectionLayout);
}
#endif

[numthreads(TG_THREAD_COUNT, 1, 1)]
[RootSignature(ModeBinningCS_RS)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gindex : SV_GroupIndex)
{
    // Indirect args for unshuffle EI pass, from threads 128-136 of group 0, outside wave 0 so they don't hold up the group's tables
    const uint argsMode = DTid.x - 128u;
    if (argsMode < BC7_MODES_COUNT)
    {
        uint numberOfGroupsNeeded = (scratchBuffer.Load(16u + (32 * argsMode) + 8u) + TG_THREAD_COUNT - 1) / TG_THREAD_COUNT;
        
        UnshufflePassIndirectArgsBuffer[argsMode].ThreadGroupCountX = numberOfGroupsNeeded;
        UnshufflePassIndirectArgsBuffer[argsMode].ThreadGroupCountY = 1;
        UnshufflePassIndirectArgsBuffer[argsMode].ThreadGroupCountZ = 1;
    }

    const uint waveLaneCount = WaveGetLaneCount();

    // Wave 0 builds the group's tables, ahead of the early return so that all of its lanes take part
    if (gindex < waveLaneCount)
    {
#if HW_MIN_SUPPORTED_WAVE_SIZE < 16
        if (waveLaneCount < BC7_MODES_COUNT)
        {
            if (gindex == 0)
            {
                BuildModeTablesSerial(gid.x);
            }
        }
        else
#endif
        {
            BuildModeTablesParallel(gindex, gid.x);
        }
    }

    // Only process up to number of blocks (wave might have more lanes)
    if (DTid.x >= constants.blockCount)
    {
        return;
    }

    // Each thread reads its mode before the barrier, overlapping the group's tables
    const uint headerSizeBytes = scratchBuffer.Load(0u);
    const uint bitsPerMode = ModeTransformBCountToModeBits[scratchBuffer.Load(4u)];
    uint parsedMode = ParseBitsFromBuffer((headerSizeBytes * 8) + DTid.x * bitsPerMode, bitsPerMode, shuffledBuffer);
    
    GroupMemoryBarrierWithGroupSync();
    
    uint mode = gs_modeLUTEntry[parsedMode];
    const uint4 modeSize = gs_modeSize[mode];
    const uint4 modeBase = gs_modeBase[mode];
    uint threadScrapSizeBits = modeSize.z;
    const bool sectionLayout = (gs_sectionLayout != 0u);
    
    // Wave index within the threadgroup
    uint waveIndexForThisThread = gindex / waveLaneCount;
    
    // Per wave mode counts, a byte per mode (waves have at most 128 lanes)
    uint modeMask_0_3 = (mode < 4) ? 1u << (8u * (mode - 0u)) : 0u;
    uint modeMask_4_7 = (mode >= 4 && mode < 8) ? 1u << (8u * (mode - 4u)) : 0u;
    uint modeMask_8 = (mode == 8) ? 1u : 0u;
    
    uint waveMode03Sum = WaveActiveSum(modeMask_0_3);
    uint waveMode47Sum = WaveActiveSum(modeMask_4_7);
    uint waveMode8Sum = WaveActiveSum(modeMask_8);

    // The section layout places scrap bits by same-mode rank, so the scrap bit sums are only needed for the block order layout
    uint waveScrapSizeBits = 0u;
    if (!sectionLayout)
    {
        waveScrapSizeBits = WaveActiveSum(threadScrapSizeBits);
    }
    if (WaveIsFirstLane())
    {
        gs_waveSums[waveIndexForThisThread] = uint4(waveMode8Sum, waveMode47Sum, waveMode03Sum, waveScrapSizeBits);
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // The block's rank among this group's blocks of its mode: those in earlier waves,
    uint offsetIntoModeAccum = (mode < 4) ? 2u : (mode < 8) ? 1u : 0u;
    
    uint currentModeOffsetWithinWave = 0;
    uint scrapThreadOffsetBitsInTG = 0u;
    [unroll(WAVES_PER_THREADGROUP)]
    for (uint w = 0; w < WAVES_PER_THREADGROUP; w++)
    {
        if (w >= waveIndexForThisThread) continue;
        uint waveModeCount = gs_waveSums[w][offsetIntoModeAccum];
        uint a = (mode < 4) ? 0 : 4;
        waveModeCount = (waveModeCount >> (8u * (mode - a))) & 0xFFu;
        currentModeOffsetWithinWave += waveModeCount;
        scrapThreadOffsetBitsInTG += gs_waveSums[w].w;
    }

    // and those in lower lanes of this wave, which match its mode in all four mode bits
    uint4 sameModeLanes = WaveActiveBallot(true);
    [unroll]
    for (uint bit = 0; bit < 4; ++bit)
    {
        const uint4 modeBitLanes = WaveActiveBallot(((mode >> bit) & 1u) != 0u);
        sameModeLanes &= (((mode >> bit) & 1u) != 0u) ? modeBitLanes : ~modeBitLanes;
    }
    const uint laneIndex = WaveGetLaneIndex();
    [unroll]
    for (uint c = 0; c < 4; ++c)
    {
        const uint firstLane = 32u * c;
        const uint lanesBelow = (laneIndex >= firstLane + 32u) ? ~0u : ((laneIndex > firstLane) ? ((1u << (laneIndex - firstLane)) - 1u) : 0u);
        currentModeOffsetWithinWave += countbits(sameModeLanes[c] & lanesBelow);
    }
    if (!sectionLayout)
    {
        scrapThreadOffsetBitsInTG += WavePrefixSum(threadScrapSizeBits);
    }
    
    // The block's rank among the group's blocks of its mode places it in each stream
    uint destBufferWriteAddress = (modeBase.x + currentModeOffsetWithinWave) * BINNED_BUFFER_STRIDE;
    
    // Doing space curve inverse transform.
    uint finalBlockID = DTid.x;
    if (gs_spaceCurveWidthInPixels != 0u)
    {
        finalBlockID = ReverseSpaceCurveFor16ByteBlock(gs_bc7SizeInBytes, gs_spaceCurveWidthInPixels, finalBlockID);
    }
    
    // Write the current block's ID into the address
    scratchBuffer.Store(constants.offsetIntoBinning + destBufferWriteAddress, finalBlockID);
    
    uint colorAddress = modeBase.y + currentModeOffsetWithinWave * modeSize.x;
    uint miscAddress = modeBase.z + currentModeOffsetWithinWave * modeSize.y;
    
    // Section layout: the scrap bits of each mode are grouped, in same-mode rank order; otherwise they're in block order
    uint scrapAddressBits = sectionLayout ? (modeBase.w + currentModeOffsetWithinWave * threadScrapSizeBits) : (gs_scrapGroupBaseBits + scrapThreadOffsetBitsInTG);

    // The scrap bits (at most 11) span at most two dwords; the second is only read when they cross into it, keeping reads in the stream
    const uint scrapDwordAddress = (scrapAddressBits >> 5) << 2;
    const uint scrapShift = scrapAddressBits & 31u;
    uint scrapData = shuffledBuffer.Load(scrapDwordAddress) >> scrapShift;
    if (scrapShift + threadScrapSizeBits > 32u)
    {
        scrapData |= shuffledBuffer.Load(scrapDwordAddress + 4u) << (32u - scrapShift);
    }
    scrapData &= (1u << threadScrapSizeBits) - 1u;
    
    // ENDING 
    
    uint4 result;
    result.x = colorAddress;
    result.y = miscAddress;
    result.z = scrapData;
    result.w = DTid.x; // We need to pass the blockID before the inverse z curve transform, since its used for the chunk calculation in the next pass.
    
    // Write the current block's address into color stream
    bc7Buffer.Store4(16 * finalBlockID, result);
}