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

    // Every thread finds the mode bit count itself, so its table read is off the post-barrier chain
    const uint dword0 = shuffledBuffer.Load(0u);
    const uint bitsPerMode = ModeTransformBCountToModeBits[dword0 & 0xfu];

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

        // ModesUsed goes [0-8], so it uses first 4 bits
        const uint modesUsedCount = Byte0 & 0xfu;

        // GPU_ASSERT(modesUsedCount <= 8u, scratchBuffer);

        const uint staticFieldsFlag = (Byte0 >> 5) & 0x1u;
        const uint lowEntropyFlag = (Byte0 >> 6) & 0x1u;
        const uint extensionByteFlag = (Byte0 >> 7) & 0x1u;

        // Extension byte bits [0-2] are the space curve width in 128 pixel tiles (log2), 0 = linear;
        // bit 3 = index prediction, read directly from the stream by the unshuffle pass;
        // bit 4 = section layout, a 26 byte layout block follows (read by the binning and unshuffle passes), its remap tables follow the mode stream
        const uint extensionByte = (extensionByteFlag) ? ((dword0 >> 8) & 0xFFu) : 0u;
        const uint spaceCurveWidthInTilesLog2 = extensionByte & 0x7u;
        const uint spaceCurveWidthInPixels = (spaceCurveWidthInTilesLog2 != 0u) ? (128u << spaceCurveWidthInTilesLog2) : 0u;
        uint currentByteIndex = 1u + extensionByteFlag + ((extensionByte & 0x10u) ? 26u : 0u);

        if (DTid.x == 0u)
        {
            scratchBuffer.Store(4u, modesUsedCount);
            scratchBuffer.Store(GACL_BC7_SC_WIDTH_OFFSET, spaceCurveWidthInPixels);

            // Mode 8 blocks have no stream data, so the parse below writes no layout for them; zero their slot, as the binning pass
            // multiplies every mode's sizes by its block count and the host's scratch may hold anything.  The parse may then write the
            // slot's +0 (LUT entry 8, with all 9 modes used), and the prefix sum pass writes its count (+8)
            scratchBuffer.Store4(GACL_BC7_PER_MODE_OFFSET + (GACL_BC7_PER_MODE_STRIDE * 8u), uint4(0u, 0u, 0u, 0u));
            scratchBuffer.Store4(GACL_BC7_PER_MODE_OFFSET + (GACL_BC7_PER_MODE_STRIDE * 8u) + 16u, uint4(0u, 0u, 0u, 0u));
        }

        // The rest of the header is read 8 bytes at a time, with 12 byte reads.  Each mode 0-7 block adds at least 15 bytes after
        // the header, so those reads stay in the stream; only a stream of mode 8 blocks alone may end with its header
        bool wideReads = (modesUsedCount >= 2u);
        [branch]
        if (modesUsedCount == 1u)
        {
            wideReads = ((fetchNextByte(currentByteIndex + staticFieldsFlag + lowEntropyFlag, shuffledBuffer) & 0xfu) != 8u);
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
            for (uint i = 0; i < modesUsedCount; ++i)
            {
                // Mode is encoded in first 4 bits
                const uint lutByte = entry.x & 0xFFu;
                const uint mode = lutByte & 0xfu;

                // Storing the used modes, ordered by frequency, in shared memory and in header
                gs_modeLUT[i] = mode;
                if (DTid.x == 0u)
                {
                    scratchBuffer.Store(16u + (32 * i), mode);
                }

                uint entryBytes = 1u;
                if (mode != 8u)
                {
                    // following 3 bits are the mode pattern
                    const uint modePattern = (lutByte >> 4) & 0x7u;
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

                        const uint lowEntropyAt = 2u + modeStatics;
                        const uint endpointOrderAt = lowEntropyAt + modeLowEntropy;
                        entryBytes = endpointOrderAt + ((endpointOrderBytes == 0u) ? 0u : ((endpointOrderBytes == 2u) ? 2u : 1u));

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

                currentByteIndex += entryBytes;
                if (i + 1u < modesUsedCount)
                {
                    entry = PeekStream8(currentByteIndex);
                }
            }
        }
        else
        {
            currentByteIndex += staticFieldsFlag + lowEntropyFlag;
            if (modesUsedCount == 1u)
            {
                // The only entry is a mode 8 LUT byte
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
    
    // Based on how many bits we use to store mode, we get which byte we need to fetch from
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
