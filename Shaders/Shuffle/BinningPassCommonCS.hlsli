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
// Whether the stream is decoded (see CheckStreamParallel)
groupshared uint gs_streamValid;

// Per wave: mode 8 count, modes 4-7 counts and modes 0-3 counts (a byte per mode), scrap bits
groupshared uint4 gs_waveSums[WAVES_PER_THREADGROUP];

struct ModeSlot
{
    uint lutEntry;
    uint pattern;
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
    slot.pattern = modeSlot1.x;
    slot.totalCount = modeSlot0.z;
    slot.perBlock = uint4(1u, modeSlot1.y, modeSlot1.z, modeSlot1.w + extraScrapBits);

    // Get the accumulated mode count for all groups with ID lower than this one.
    const uint address = (constants.histogramCount * DWORD_BYTES * m) + (groupId - 1) * DWORD_BYTES;
    slot.previousGroupsCount = (groupId > 0) ? scratchBuffer.Load(constants.offsetIntoHistogram + address) : 0u;
    return slot;
}

// The section layout flag: bit 4 of the extension byte (stream byte 1), which start byte bit 7 flags
uint SectionLayoutFlag(uint streamStartBytes)
{
#if GACL_SPLIT_SECTION_LAYOUT
    return ((streamStartBytes & 0x80u) != 0u) ? ((streamStartBytes >> 12u) & 0x1u) : 0u;
#else
    return 0u;
#endif
}

// Whether mode m (or a lane past the modes) may have a section selector with its block count, from the prepass's mask of the modes with
// nonzero selectors (bit m for mode m, bits 8+ 0): only the sections of modes with 2 or more blocks are transformed
bool SelectorMaskValid(uint selMask, uint m, uint totalCount)
{
    return (((selMask >> min(m, 8u)) & 1u) == 0u) || (totalCount >= 2u);
}

// 1 if a section of stride bytes can't use unit code `code` (0 none, 1/2/3 = 1/2/4 byte units): units must divide it, and be smaller
uint SectionUnitBad(uint code, uint stride)
{
    const uint unit = (1u << code) >> 1u;
    return ((code != 0u) && (((stride & (unit - 1u)) != 0u) || (stride == unit))) ? 1u : 0u;
}

// Dword i (0-3) of v, without dynamic indexing
uint SelectDword(uint4 v, uint i)
{
    return (i < 2u) ? ((i == 0u) ? v.x : v.y) : ((i == 2u) ? v.z : v.w);
}

// Whether mode m (or a lane past the modes) has a valid section layout, from the block's selectors and table offsets (zeros without
// one) and the mode's color and misc bytes per block.  A mode with fewer than 2 blocks has selector 0 (SelectorMaskValid), so its
// units and remapped bytes are 0 whatever its slot holds, and its table must be empty
bool SectionLayoutValid(uint m, uint2 selectors, uint4 offsets, uint tableBytes, uint color, uint misc)
{
    const uint selector = (m < 8u) ? ((((m < 4u) ? selectors.x : selectors.y) >> (8u * (m & 3u))) & 0xFFu) : 0u;
    const uint start = (SelectDword(offsets, (m >> 1u) & 3u) >> (16u * (m & 1u))) & 0xFFFFu;
    const uint end = (m < 7u) ? ((SelectDword(offsets, ((m + 1u) >> 1u) & 3u) >> (16u * ((m + 1u) & 1u))) & 0xFFFFu) : tableBytes;
    const uint remapped = ((selector & 0x4u) ? color : 0u) + ((selector & 0x40u) ? misc : 0u);
    const uint span = end - start;
    const uint bad = SectionUnitBad(selector & 0x3u, color) | SectionUnitBad((selector >> 4u) & 0x3u, misc) |
        ((span < 2u * remapped) ? 1u : 0u) | ((span > 257u * remapped) ? 1u : 0u) | ((m == 0u) ? start : 0u);
    return (m >= 8u) || (bad == 0u);
}

// Whether mode m (0-7) has pattern `pattern`
bool ModeHasPattern(uint m, uint pattern)
{
    const uint validPatterns = (m < 4u) ? (0x0C070C07u >> (8u * m)) : (0x1C242424u >> (8u * ((m - 4u) & 3u)));
    return ((validPatterns >> pattern) & 1u) != 0u;
}

// Whether mode m's rotation header byte names all of the mode's channels (RGB, and A in modes 4-7)
bool RotationChannelsValid(uint m, uint header)
{
    return (header & 0xFu) == ((m < 4u) ? 0x7u : 0xFu);
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
    sectionLayout = SectionLayoutFlag(streamStartBytes);
    const uint sectionTableBytes = (sectionLayout != 0u) ? (streamStartBytes >> 16u) : 0u;

    const uint metadataAndModeStreamSizeBytes = (((headerSizeBytes * 8) + streamTotals.x * bitsPerMode + 7) >> 3) + sectionTableBytes;
    const uint miscStreamBaseBytes = metadataAndModeStreamSizeBytes + streamTotals.y;
    return uint4(0u, metadataAndModeStreamSizeBytes, miscStreamBaseBytes, (miscStreamBaseBytes + streamTotals.z) * 8);
}

// The group wide values, from the thread that builds the tables' mode 0 entry.  scWord: the prepass's space curve dword
void StoreGroupValues(uint scrapGroupBaseBits, uint sectionLayout, uint scWord)
{
    gs_scrapGroupBaseBits = scrapGroupBaseBits;
    gs_sectionLayout = sectionLayout;
    gs_bc7SizeInBytes = constants.blockCount * 16;

    // Space curve width recorded in the stream header (0 = linear), only inverted when the dimensions are eligible.  Those need a
    // power of two width, so the pitch is a power of two whenever the height matters
    const uint spaceCurveWidthInPixels = scWord & 0xFFFFu;
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
    const uint bitsPerMode = ModeTransformBCountToModeBits[scratchBuffer.Load(4u) & 0xfu];
    const uint scWord = scratchBuffer.Load(GACL_BC7_SC_WIDTH_OFFSET);
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
        StoreGroupValues(streamBases.w + previousGroupsScrapBits, sectionLayout, scWord);
    }
}

// Wave 1 checks the stream while wave 0 builds the tables, lane m for mode m: writes gs_streamValid, and group 0's indirect args.  The
// prepass checks the reserved fields (its verdict is bit 31 of the space curve dword)
void CheckStreamParallel(uint m, uint groupId)
{
    const uint modesUsed = scratchBuffer.Load(4u);
    const uint scWord = scratchBuffer.Load(GACL_BC7_SC_WIDTH_OFFSET);
    uint4 slot0 = uint4(0u, 0u, 0u, 0u);
    uint4 slot1 = uint4(0u, 0u, 0u, 0u);
    if (m < BC7_MODES_COUNT)
    {
        slot0 = scratchBuffer.Load4(16u + (32 * m)); // LUT entry m's mode, rotation byte address, total count, packed
        slot1 = scratchBuffer.Load4(32u + (32 * m)); // pattern, color bytes, misc bytes, scrap bits
    }
    const uint startBytes = shuffledBuffer.Load(0u);
    const bool sectionLayout = (SectionLayoutFlag(startBytes) != 0u);
    uint2 selectors = uint2(0u, 0u);
    uint4 offsets = uint4(0u, 0u, 0u, 0u);
    [branch]
    if (sectionLayout)
    {
        selectors = shuffledBuffer.Load2(4u);
        offsets = shuffledBuffer.Load4(12u);
    }

    // The prepass counted all of the blocks (it counts no block whose index names no LUT entry) and found no reserved field set, and
    // only modes with 2 or more blocks have section selectors
    const bool totalsValid = (WaveActiveSum(slot0.z) == constants.blockCount) && ((scWord >> 31u) == 0u);
    bool laneValid = SelectorMaskValid(scWord >> 16u, m, slot0.z);
    // Lanes 0-8 hold LUT entries 0-8's modes (modes 9-15 as 8, which the prepass rejects).  Only the LUT's modes' slots are fresh
    const uint lutEntries = min(modesUsed & 0xfu, 9u);
    const uint lutModes = WaveActiveBitOr((m < lutEntries) ? (1u << slot0.x) : 0u);
    const bool inLut = (m < 8u) && (((lutModes >> m) & 1u) != 0u);
    // The LUT's entries name distinct modes, each mode a pattern it has, with a valid section layout
    const bool lutValid = (countbits(lutModes) == lutEntries);
    laneValid = laneValid && (!inLut || ModeHasPattern(m, slot1.x));
    laneValid = laneValid && SectionLayoutValid(m, selectors, offsets, sectionLayout ? (startBytes >> 16u) : 0u, inLut ? slot1.y : 0u,
        inLut ? slot1.z : 0u);
    // Rotation headers name all of their mode's channels, with one region size for all.  A mode without one loads byte 0 instead
    const bool hasRotation = inLut && (slot0.y != 0u);
    const uint rotationAddress = hasRotation ? slot0.y : 0u;
    const uint rotationHeader = (shuffledBuffer.Load(rotationAddress & ~3u) >> (8u * (rotationAddress & 3u))) & 0xFFu;
    laneValid = laneValid && (!hasRotation || RotationChannelsValid(m, rotationHeader));
    const bool regionsValid = (countbits(WaveActiveBitOr(hasRotation ? (1u << (rotationHeader >> 4u)) : 0u)) <= 1u);
    const bool streamValid = WaveActiveAllTrue(laneValid) && lutValid && regionsValid && totalsValid;
    if (m == 0u)
    {
        gs_streamValid = streamValid ? 1u : 0u;
    }

    // Group 0 writes the unshuffle pass's indirect args: no groups for a stream that isn't decoded
    if ((groupId == 0u) && (m < BC7_MODES_COUNT))
    {
        UnshufflePassIndirectBuffer args;
        args.ThreadGroupCountX = streamValid ? ((slot0.z + TG_THREAD_COUNT - 1) / TG_THREAD_COUNT) : 0u;
        args.ThreadGroupCountY = 1u;
        args.ThreadGroupCountZ = 1u;
        UnshufflePassIndirectArgsBuffer[m] = args;
    }
}

#if HW_MIN_SUPPORTED_WAVE_SIZE < 16
// Thread 0 builds the group's per mode tables, for waves of fewer than BC7_MODES_COUNT lanes
void BuildModeTablesSerial(uint groupId)
{
    const uint headerSizeBytes = scratchBuffer.Load(0u);
    const uint modesUsed = scratchBuffer.Load(4u);
    const uint bitsPerMode = ModeTransformBCountToModeBits[modesUsed & 0xfu];
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

    // The stream checks, as in CheckStreamParallel
    const uint scWord = scratchBuffer.Load(GACL_BC7_SC_WIDTH_OFFSET);
    bool streamValid = (streamTotals.x == constants.blockCount);
    streamValid = streamValid && ((scWord >> 31u) == 0u);
    [unroll(BC7_MODES_COUNT)]
    for (uint mb = 0; mb < BC7_MODES_COUNT; ++mb)
    {
        gs_modeLUTEntry[mb] = slots[mb].lutEntry;
        gs_modeBase[mb] = modeStart + slots[mb].previousGroupsCount * slots[mb].perBlock;
        gs_modeSize[mb] = uint4(slots[mb].perBlock.yzw, 0u);
        modeStart += slots[mb].totalCount * slots[mb].perBlock;
        scrapGroupBaseBits += slots[mb].previousGroupsCount * slots[mb].perBlock.w;
        streamValid = streamValid && SelectorMaskValid(scWord >> 16u, mb, slots[mb].totalCount);
    }
    const uint lutEntries = min(modesUsed & 0xfu, 9u);
    uint lutModes = 0u;
    [unroll(BC7_MODES_COUNT)]
    for (uint ml = 0; ml < BC7_MODES_COUNT; ++ml)
    {
        lutModes |= (ml < lutEntries) ? (1u << slots[ml].lutEntry) : 0u;
    }
    streamValid = streamValid && (countbits(lutModes) == lutEntries);
    const uint startBytes = shuffledBuffer.Load(0u);
    uint2 selectors = uint2(0u, 0u);
    uint4 offsets = uint4(0u, 0u, 0u, 0u);
    [branch]
    if (sectionLayout != 0u)
    {
        selectors = shuffledBuffer.Load2(4u);
        offsets = shuffledBuffer.Load4(12u);
    }
    const uint tableBytes = (sectionLayout != 0u) ? (startBytes >> 16u) : 0u;
    uint regions = 0u;
    [unroll(8)]
    for (uint mc = 0; mc < 8u; ++mc)
    {
        const bool inLut = ((lutModes >> mc) & 1u) != 0u;
        streamValid = streamValid && (!inLut || ModeHasPattern(mc, slots[mc].pattern));
        streamValid = streamValid && SectionLayoutValid(mc, selectors, offsets, tableBytes, inLut ? slots[mc].perBlock.y : 0u,
            inLut ? slots[mc].perBlock.z : 0u);
        const uint rotationSlot = scratchBuffer.Load(16u + (32 * mc) + 4u);
        const bool hasRotation = inLut && (rotationSlot != 0u);
        const uint rotationAddress = hasRotation ? rotationSlot : 0u;
        const uint rotationHeader = (shuffledBuffer.Load(rotationAddress & ~3u) >> (8u * (rotationAddress & 3u))) & 0xFFu;
        streamValid = streamValid && (!hasRotation || RotationChannelsValid(mc, rotationHeader));
        regions |= hasRotation ? (1u << (rotationHeader >> 4u)) : 0u;
    }
    streamValid = streamValid && (countbits(regions) <= 1u);
    if (groupId == 0u)
    {
        [unroll(BC7_MODES_COUNT)]
        for (uint ma = 0; ma < BC7_MODES_COUNT; ++ma)
        {
            UnshufflePassIndirectBuffer args;
            args.ThreadGroupCountX = streamValid ? ((slots[ma].totalCount + TG_THREAD_COUNT - 1) / TG_THREAD_COUNT) : 0u;
            args.ThreadGroupCountY = 1u;
            args.ThreadGroupCountZ = 1u;
            UnshufflePassIndirectArgsBuffer[ma] = args;
        }
    }
    StoreGroupValues(scrapGroupBaseBits, sectionLayout, scWord);
    gs_streamValid = streamValid ? 1u : 0u;
}
#endif

[numthreads(TG_THREAD_COUNT, 1, 1)]
[RootSignature(ModeBinningCS_RS)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gindex : SV_GroupIndex)
{
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
    // Wave 1 checks the stream meanwhile
    else if (gindex < 2u * waveLaneCount)
    {
#if HW_MIN_SUPPORTED_WAVE_SIZE < 16
        if (waveLaneCount >= BC7_MODES_COUNT)
#endif
        {
            CheckStreamParallel(gindex - waveLaneCount, gid.x);
        }
    }

    // Only process up to number of blocks (wave might have more lanes)
    if (DTid.x >= constants.blockCount)
    {
        return;
    }

    // Each thread reads its mode before the barrier, overlapping the group's tables
    const uint headerSizeBytes = scratchBuffer.Load(0u);
    const uint bitsPerMode = ModeTransformBCountToModeBits[scratchBuffer.Load(4u) & 0xfu];
    uint parsedMode = ParseBitsFromBuffer((headerSizeBytes * 8) + DTid.x * bitsPerMode, bitsPerMode, shuffledBuffer);
    
    GroupMemoryBarrierWithGroupSync();

    // A stream that isn't decoded leaves the destination untouched (the flag is group uniform, so the whole group returns)
    if (gs_streamValid == 0u)
    {
        return;
    }
    
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

    // The scrap bits (at most 11) span at most two dwords; the second is only read when they cross into it, keeping reads in the stream.
    // A block without scrap bits can sit at the stream end, so it reads dword 0 instead (masked to nothing)
    const uint scrapDwordAddress = (threadScrapSizeBits != 0u) ? ((scrapAddressBits >> 5) << 2) : 0u;
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