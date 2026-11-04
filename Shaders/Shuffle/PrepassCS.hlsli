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

// The 8 stream bytes from byte pos on, from one 12 byte read at pos & ~3
uint2 PeekStream8(uint pos)
{
    const uint3 w = shuffledBuffer.Load3(pos & ~3u);
    const uint s = (pos & 3u) * 8u;

    // Funnel shifts, split in two so that s = 0 doesn't shift by 32
    return uint2((w.x >> s) | ((w.y << 1u) << (31u - s)), (w.y >> s) | ((w.z << 1u) << (31u - s)));
}

// Byte k (0-7) of PeekStream8's 8 bytes
uint PeekByte(uint2 q, uint k)
{
    return ((k < 4u ? q.x : q.y) >> ((k & 3u) * 8u)) & 0xFFu;
}

// Stream checks.  Extension byte bits these shaders don't decode: the reserved bits 5-7, and the features compiled out of them (see
// SharedDefinitions.hlsli)
static const uint kExtensionRejectBits = 0xE0u | (GACL_SPLIT_INDEX_PREDICTION ? 0u : 0x08u) | (GACL_SPLIT_SECTION_LAYOUT ? 0u : 0x10u);

// Section selector bits these shaders don't decode, in each of 4 selector bytes: the reserved bits 3 and 7, and the section transforms
// compiled out of them
static const uint kSelectorRejectBits =
    (0x88u | (GACL_SPLIT_SECTION_REMAP ? 0u : 0x44u) | (GACL_SPLIT_SECTION_TRANSPOSE ? 0u : 0x33u)) * 0x01010101u;

// The LUT fields the reserved field checks need, gathered as the LUT is read
struct LutSummary
{
    uint patterns;      // bits 3m-3m+2: mode m's pattern (modes 0-7)
    uint otherBytes;    // the LUT bytes of modes 8-15, ORed
    uint extensions;    // the additional extensions bytes, ORed, with bit 3 set by an endpoint order of 3 (bits 2 and 3 set)
};

// The section selectors of modes 0-7 (stream bytes 4-11, a byte per mode) of a stream with the section layout, else 0
uint2 LoadSectionSelectors(uint dword0)
{
    uint2 selectors = uint2(0u, 0u);
#if GACL_SPLIT_SECTION_LAYOUT
    [branch]
    if ((dword0 & 0x1080u) == 0x1080u)
    {
        selectors = shuffledBuffer.Load2(4u);
    }
#endif
    return selectors;
}

// The reserved fields, nonzero if one is set: the mode count (1-9), mode encoding B (start byte bit 4), extension byte bits these
// shaders don't decode, the LUT bytes of modes 8-15 other than 0x08 (mode 8 clean, with no additional extensions), additional
// extensions reserved bits (5-7) and endpoint order 3, patterns 6 and 7, and section selector bits these shaders don't decode.  The
// binning pass checks the rest of the stream (see CheckStreamParallel)
uint ReservedFieldsInvalid(uint dword0, LutSummary lut, uint2 selectors)
{
    const uint Byte0 = dword0 & 0xFFu;
    const uint modesUsedCount = Byte0 & 0xfu;
    const uint extensionByte = (Byte0 & 0x80u) ? ((dword0 >> 8) & 0xFFu) : 0u;
    return (((modesUsedCount - 1u) >= 9u) ? 1u : 0u) | (~Byte0 & 0x10u) | (extensionByte & kExtensionRejectBits) |
        (lut.otherBytes & ~0x08u) | (lut.extensions & 0xE8u) | ((lut.patterns >> 1u) & (lut.patterns >> 2u) & 0x249249u) |
        ((selectors.x | selectors.y) & kSelectorRejectBits);
}

// Bit k set if byte k of v is nonzero
uint NonzeroBytes(uint v)
{
    uint t = v | (v >> 1u);
    t |= t >> 2u;
    t |= t >> 4u;
    return ((t & 0x01010101u) * 0x01020408u) >> 24u;
}

// The space curve width in pixels (0 = linear)
uint ScWidthPixels(uint dword0)
{
    const uint extensionByte = (dword0 & 0x80u) ? ((dword0 >> 8) & 0xFFu) : 0u;
    const uint widthLog2 = extensionByte & 0x7u;
    return (widthLog2 != 0u) ? (128u << widthLog2) : 0u;
}

// The space curve dword: the space curve width in pixels (0 = linear), bit 16 + m set if mode m has a nonzero section selector, and
// bit 31 set if a reserved field is set
uint VerdictWord(uint dword0, uint2 selectors, bool bad)
{
    return ScWidthPixels(dword0) | (NonzeroBytes(selectors.x) << 16u) | (NonzeroBytes(selectors.y) << 20u) | (bad ? 0x80000000u : 0u);
}

[numthreads(TG_THREAD_COUNT, 1, 1)]
[RootSignature(PrepassCS_RS)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 gId : SV_GroupID, uint gindex : SV_GroupIndex)
{
    // A group past the blocks returns whole.  In the last group, the threads past the blocks stay for the barriers, which every thread
    // of the group must reach, but read no mode
    if (DTid.x - gindex >= constants.blockCount)
    {
        return;
    }
    const bool inRange = DTid.x < constants.blockCount;

    uint chunkCount = 0;

    // Every thread finds the mode bit count itself, so its table read is off the post-barrier chain
    const uint dword0 = shuffledBuffer.Load(0u);
    const uint bitsPerMode = ModeTransformBCountToModeBits[dword0 & 0xfu];

    // The stream is checked after the histogram stores below, off the path to them; until then, this pass only keeps its own accesses
    // in place (at most 9 LUT entries, LUT modes of at most 8, and header slots for modes 0-7 only)
    LutSummary lut = (LutSummary)0;
    uint2 selectors = uint2(0u, 0u);

    if (gindex == 0)
    {
        // Zero-out shared memory variables 
        [unroll(BC7_MODES_COUNT)]
        for (uint m = 0; m < BC7_MODES_COUNT; ++m)
        {
            gs_modeCountsInThreadGroup[m] = 0;
            gs_modeLUT[m] = 0;
        }

        const uint Byte0 = dword0 & 0xFFu;

        // ModesUsed goes [1-9], so it uses first 4 bits
        const uint modesUsedCount = Byte0 & 0xfu;

        const uint staticFieldsFlag = (Byte0 >> 5) & 0x1u;
        const uint lowEntropyFlag = (Byte0 >> 6) & 0x1u;
        const uint extensionByteFlag = (Byte0 >> 7) & 0x1u;

        // Extension byte bits [0-2] are the space curve width in 128 pixel tiles (log2), 0 = linear;
        // bit 3 = index prediction, read directly from the stream by the unshuffle pass;
        // bit 4 = section layout, a 26 byte layout block follows (read by the binning and unshuffle passes), its remap tables follow the mode stream
        const uint extensionByte = (extensionByteFlag) ? ((dword0 >> 8) & 0xFFu) : 0u;
        uint currentByteIndex = 1u + extensionByteFlag + ((extensionByte & 0x10u) ? 26u : 0u);

        // The section selectors, for the reserved field checks: read now, alongside the LUT reads
        if (DTid.x == 0u)
        {
            selectors = LoadSectionSelectors(dword0);
        }

        if (DTid.x == 0u)
        {
            scratchBuffer.Store(4u, modesUsedCount);

            // Mode 8 blocks have no stream data, so the parse below writes no layout for them; zero their slot, as the binning pass
            // multiplies every mode's sizes by its block count and the host's scratch may hold anything.  The parse may then write the
            // slot's +0 (LUT entry 8, with all 9 modes used), and the prefix sum pass writes its count (+8)
            scratchBuffer.Store4(GACL_BC7_PER_MODE_OFFSET + (GACL_BC7_PER_MODE_STRIDE * 8u), uint4(0u, 0u, 0u, 0u));
            scratchBuffer.Store4(GACL_BC7_PER_MODE_OFFSET + (GACL_BC7_PER_MODE_STRIDE * 8u) + 16u, uint4(0u, 0u, 0u, 0u));
        }

        // The rest of the header is read 8 bytes at a time, with 12 byte reads.  Each mode 0-7 block adds at least 15 bytes after
        // the header, so those reads stay in the stream; only a stream of mode 8 blocks alone may end with its header
        const uint lutEntries = min(modesUsedCount, 9u);
        uint firstLutByte = 0u;
        bool wideReads = (lutEntries >= 2u);
        [branch]
        if (lutEntries == 1u)
        {
            firstLutByte = fetchNextByte(currentByteIndex + staticFieldsFlag + lowEntropyFlag, shuffledBuffer);
            wideReads = ((firstLutByte & 0xfu) != 8u);
        }

        [branch]
        if (wideReads)
        {
            const uint2 globals = PeekStream8(currentByteIndex);
            const uint globalStatics = (staticFieldsFlag) ? (globals.x & 0xFFu) : 0u;
            const uint globalLowEntropy = (lowEntropyFlag) ? PeekByte(globals, staticFieldsFlag) : 0u;
            currentByteIndex += staticFieldsFlag + lowEntropyFlag;

            // Populate array with modes used from most to least frequently; one read per entry, as an entry's bytes up to
            // its rotation header are at most 7
            uint2 entry = PeekStream8(currentByteIndex);
            for (uint i = 0; i < lutEntries; ++i)
            {
                // Mode is encoded in first 4 bits
                const uint lutByte = entry.x & 0xFFu;
                const uint mode = lutByte & 0xfu;

                // Storing the used modes, ordered by frequency, in shared memory and in header (modes 9-15 as mode 8)
                const uint lutMode = min(mode, 8u);
                gs_modeLUT[i] = lutMode;
                if (DTid.x == 0u)
                {
                    scratchBuffer.Store(16u + (32 * i), lutMode);
                }

                uint entryBytes = 1u;
                if (mode < 8u)
                {
                    // following 3 bits are the mode pattern
                    const uint modePattern = (lutByte >> 4) & 0x7u;
                    lut.patterns |= modePattern << (3u * mode);
                    uint packedInfo = (globalStatics) | (globalLowEntropy << 8);

                    if (DTid.x == 0u)
                    {
                        // Set these to defaults, may later be overwritten
                        scratchBuffer.Store(GACL_BC7_PER_MODE_OFFSET + (GACL_BC7_PER_MODE_STRIDE * mode) + GACL_BC7_PACKED_DATA_OFFSET, packedInfo); // modeStatics and lowEntropy
                        scratchBuffer.Store(16u + (32 * mode) + 4u, 0u); // modeRotationByteAddress

                        scratchBuffer.Store(16u + (32 * mode) + 16u, modePattern);

                        ModeBitLayout mbl = GetModeSpecificLayout(mode, modePattern);
                        scratchBuffer.Store(16u + (32 * mode) + 20u, mbl.colorSizeBytes);
                        scratchBuffer.Store(16u + (32 * mode) + 24u, mbl.miscSizeBytes);
                        scratchBuffer.Store(16u + (32 * mode) + 28u, mbl.scrapSizeBits);
                    }

                    // Final bit are additionalExtensions: a flags byte, then the statics, low entropy, endpoint order (1 or 2)
                    // and rotation header bytes it flags
                    if (lutByte >> 7u)
                    {
                        const uint nextByte = (entry.x >> 8) & 0xFFu;
                        const uint modeStatics = nextByte & 0x1u;
                        const uint modeLowEntropy = (nextByte >> 1u) & 0x1u;
                        const uint endpointOrderBytes = (nextByte >> 2u) & 0x3u;
                        const uint rotation = (nextByte >> 4) & 0x1u;
                        lut.extensions |= nextByte & ((nextByte << 1u) | 0xF7u);

                        const uint lowEntropyAt = 2u + modeStatics;
                        const uint endpointOrderAt = lowEntropyAt + modeLowEntropy;
                        entryBytes = endpointOrderAt + endpointOrderBytes;

                        if (modeStatics)
                        {
                            packedInfo = (packedInfo & 0xFFFFFF00u) | ((entry.x >> 16) & 0xFFu);
                        }
                        if (modeLowEntropy)
                        {
                            packedInfo = (packedInfo & 0xFFFF00FFu) | (PeekByte(entry, lowEntropyAt) << 8u);
                        }
                        if (endpointOrderBytes)
                        {
                            const uint byte1 = PeekByte(entry, endpointOrderAt);
                            const uint byte2 = (endpointOrderBytes == 2u) ? PeekByte(entry, endpointOrderAt + 1u) : 0u;
                            WriteBitsToDword((byte2 << 8u | byte1), 16u, packedInfo);
                        }

                        // If either modeLowEntropy, modeStatics or endpointOrderBytes are set, overwrite packedInfo
                        if (DTid.x == 0u && (modeStatics || modeLowEntropy || endpointOrderBytes))
                        {
                            scratchBuffer.Store(GACL_BC7_PER_MODE_OFFSET + (GACL_BC7_PER_MODE_STRIDE * mode) + GACL_BC7_PACKED_DATA_OFFSET, packedInfo);
                        }

                        if (rotation)
                        {
                            if (DTid.x == 0u)
                            {
                                scratchBuffer.Store(16u + (32 * mode) + 4u, currentByteIndex + entryBytes);
                            }

                            const uint lutRotationHeader = PeekByte(entry, entryBytes);

                            // bits [0-3] are the rotated channels (2 bytes per region each), bits [4-7] the region size
                            const uint frequency = (lutRotationHeader >> 4u) & 0xF;
                            const uint modeChannelBits = 2u * countbits(lutRotationHeader & 0xFu);

                            const uint chunkSize = 1u << (frequency + 12u);
                            const uint chunkCounts = ((constants.blockCount * 16u) + chunkSize - 1u) >> (frequency + 12u);

                            if (DTid.x == 0u)
                            {
                                scratchBuffer.Store(8u, chunkCounts);
                                scratchBuffer.Store(12u, chunkSize);
                            }

                            // The rotation header byte and the rotation array that follows it
                            entryBytes += 1u + modeChannelBits * chunkCounts;
                        }
                    }
                }
                else
                {
                    lut.otherBytes |= lutByte;
                }

                currentByteIndex += entryBytes;
                if (i + 1u < lutEntries)
                {
                    entry = PeekStream8(currentByteIndex);
                }
            }
        }
        else
        {
            currentByteIndex += staticFieldsFlag + lowEntropyFlag;
            if (lutEntries == 1u)
            {
                // The only entry is a mode 8 LUT byte
                lut.otherBytes = firstLutByte;
                gs_modeLUT[0] = 8u;
                if (DTid.x == 0u)
                {
                    scratchBuffer.Store(16u, 8u);
                }
                currentByteIndex += 1u;
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
    
    if (inRange)
    {
        // Based on how many bits we use to store mode, we get which byte we need to fetch from
        uint bitStartPos = (gs_currentByteIndex * 8u) + DTid.x * bitsPerMode;

        uint parsedMode = ParseBitsFromBuffer(bitStartPos, bitsPerMode, shuffledBuffer);
        uint threadMode = gs_modeLUT[min(parsedMode, 8u)];

        // keeps track of number of blocks per wave.  Only a block whose index names a LUT entry counts, so for a stream with a block
        // that names no entry, the counts fall short of the block count, and the binning pass dispatches no unshuffle groups
        uint previousModeCount;
        InterlockedAdd(gs_modeCountsInThreadGroup[threadMode], (parsedMode < gs_modesUsedCount) ? 1u : 0u, previousModeCount);
    }
    
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

        // Thread 0 checks the reserved fields and stores the verdict word, for the binning pass
        [branch]
        if (DTid.x == 0u)
        {
            const uint bad = ReservedFieldsInvalid(dword0, lut, selectors);
            scratchBuffer.Store(GACL_BC7_SC_WIDTH_OFFSET, VerdictWord(dword0, selectors, bad != 0u));
        }
    }
}
