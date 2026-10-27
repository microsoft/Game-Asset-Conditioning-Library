//--------------------------------------------------------------------------------------
// UnshuffleCommonCS.hlsli
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#define NUM_THREADS 64

#include "SharedDefinitions.hlsli"

// Section layout (stream extension byte bit 4): the color and misc bytes of each mode (n blocks of `stride` bytes, in same-mode rank order)
// may be rank remapped per byte position and transposed into unit planes (see SplitSectionLayout in bc7modesplit.cpp).  main stages the
// color / misc bytes the helpers of its mode read in registers, from either layout (StagePlain / StageSection), and the helpers address
// them with section tags (bits 30-31 of the byte address, the offsets below): each of their fetches folds to a bit extract, and one
// expansion of the helpers serves both layouts.
static const uint SECTION_LAYOUT_BLOCK_ADDRESS = 2u; // after the start and extension bytes
static const uint SECTION_TAG_COLOR = 1u << 30;
static const uint SECTION_TAG_MISC = 2u << 30;

struct SectionInfo
{
    uint address;   // this block's bytes, as laid out without a section layout
    uint stride;
    uint selector;  // bits 0-1 unit code (1/2/3 = 1/2/4 byte units), bit 2 rank remap
    uint lens;      // per byte position symbol count - 1
    uint symbols;   // per byte position symbols, in rank order
};

// The helpers of each mode read bytes 0 to count - 1 of the block's color / misc bytes (at constant offsets)
static const uint kStageColorBytes[8] = { 9u, 10u, 12u, 12u, 5u, 7u, 7u, 11u };
static const uint kStageMiscBytes[8] = { 6u, 6u, 6u, 4u, 10u, 8u, 8u, 5u };
static uint4 g_stageColor = 0; // 4 bytes per dword, LSB first
static uint4 g_stageMisc = 0;

// Byte k of the staged bytes s.  Selects rather than an indexed array, which would be placed in memory; k is a literal at each helper
// fetch, where they fold away
uint StageGet(uint4 s, uint k)
{
    const uint d = k >> 2;
    const uint w = d == 0u ? s.x : (d == 1u ? s.y : (d == 2u ? s.z : s.w));
    return (w >> ((k & 3u) << 3)) & 0xFFu;
}

void StageSet(inout uint4 s, uint k, uint b)
{
    const uint sh = (k & 3u) << 3;
    const uint m = ~(0xFFu << sh);
    const uint v = (b & 0xFFu) << sh;
    const uint d = k >> 2;
    s.x = d == 0u ? (s.x & m) | v : s.x;
    s.y = d == 1u ? (s.y & m) | v : s.y;
    s.z = d == 2u ? (s.z & m) | v : s.z;
    s.w = d == 3u ? (s.w & m) | v : s.w;
}

// The helpers' byte fetch: a section tagged address reads the staged bytes, any other the buffer
uint StagedFetchByte(uint byteIndex, in ByteAddressBuffer buffer)
{
    if ((byteIndex & ~(SECTION_TAG_COLOR - 1u)) == SECTION_TAG_COLOR)
    {
        return StageGet(g_stageColor, byteIndex - SECTION_TAG_COLOR);
    }
    if ((byteIndex & ~(SECTION_TAG_COLOR - 1u)) == SECTION_TAG_MISC)
    {
        return StageGet(g_stageMisc, byteIndex - SECTION_TAG_MISC);
    }
    return fetchNextByte(byteIndex, buffer);
}

// Bytes 0 to count - 1 (count a literal, at most 12) of a block without a section layout, read as dwords that stop at the dword holding
// the block's last byte (stride bytes; a helper may read past it, an unused byte).  A block with no bytes reads a dword before it
uint4 StagePlain(uint address, uint count, uint stride, in ByteAddressBuffer buffer)
{
    const uint a = address & ~3u;
    const uint last = (address + min(count, stride) - 1u) & ~3u;
    const uint sh = (address & 3u) << 3;
    // byte k sits in dword (address % 4 + k) / 4: dwords 1 / 2 / 3 hold bytes the helpers read from count 2 / 6 / 10
    const uint d0 = buffer.Load(min(a, last));
    const uint d1 = count >= 2u ? buffer.Load(min(a + 4u, last)) : d0;
    const uint d2 = count >= 6u ? buffer.Load(min(a + 8u, last)) : d1;
    const uint d3 = count >= 10u ? buffer.Load(min(a + 12u, last)) : d2;
    // (d << 1) << (31 - sh) is d << (32 - sh), 0 when sh is 0
    return uint4((d0 >> sh) | ((d1 << 1u) << (31u - sh)), (d1 >> sh) | ((d2 << 1u) << (31u - sh)), (d2 >> sh) | ((d3 << 1u) << (31u - sh)), 0u);
}

// Bytes 0 to count - 1 (count a literal) of the block of rank `rank` of n laid out by the section layout: unit transposition (selector
// bits 0-1), then the rank remap (bit 2) in one compact loop
uint4 StageSection(SectionInfo section, uint count, uint rank, uint n, in ByteAddressBuffer buffer)
{
    uint4 s = 0;
    const uint unitCode = section.selector & 0x3u;
    const uint unitLog2 = unitCode - 1u;
    const uint sectionStart = section.address - rank * section.stride;
    [unroll]
    for (uint k = 0u; k < count; ++k)
    {
        // a helper may read past a short section (an unused byte), so read inside the section
        const uint kc = min(k, section.stride - 1u);
        uint address = section.address + kc;
        if (unitCode != 0u)
        {
            // byte k of rank r is at ((k / unit) * n + r) * unit + k % unit from the section start
            address = sectionStart + ((((kc >> unitLog2) * n) + rank) << unitLog2) + (kc & ((1u << unitLog2) - 1u));
        }
        StageSet(s, k, fetchNextByte(address, buffer));
    }
    if ((section.selector & 0x4u) != 0u)
    {
        // the symbols of byte position k follow those of the positions below it (a byte past the section reads the last position's)
        uint symbols = section.symbols;
        [loop]
        for (uint k = 0u; k < count; ++k)
        {
            StageSet(s, k, fetchNextByte(symbols + StageGet(s, k), buffer));
            if (k + 1u < section.stride)
            {
                symbols += fetchNextByte(section.lens + k, buffer) + 1u;
            }
        }
    }
    return s;
}

#define fetchNextByte(byteIndex, buffer) StagedFetchByte(byteIndex, buffer)
#include "UnshuffleHelpers.hlsli"
#include "DerotationHelpers.hlsli"
#undef fetchNextByte
#include "EndpointOrderingHelpers.hlsli"
#include "IndexPredictionHelpers.hlsli"

struct Constants
{
    uint offsetIntoBinning;
};

ConstantBuffer<Constants> constants : register(b0);

ByteAddressBuffer shuffledBuffer : register(t0);

RWByteAddressBuffer scratchBuffer : register(u0);

RWByteAddressBuffer bc7Buffer : register(u1);

// Unshuffles this thread's block of mode BIN_MODE (index prediction, derotation and endpoint reorder included).  main passes the section
// tags as the color / misc addresses, so the helpers read the staged bytes.
void unshuffleBlock(uint colorBaseAddress, uint miscBaseAddress, uint scrapData, uint extraScrap, uint modePattern, uint modeStatics,
    uint lowEntropy, uint endpointOrderBytes, uint indexPrediction, uint modeRotationByteAddress, uint chunkIndex, inout uint4 rawOut)
{
    const uint mode = BIN_MODE;
    
    #if BIN_MODE == 0
    unshuffleMode0(colorBaseAddress, miscBaseAddress, scrapData, modePattern, shuffledBuffer, rawOut);
    if (indexPrediction != 0u)
    {
        inversePredictIndices(mode, rawOut);
    }
    if (modeRotationByteAddress != 0)
    {
        derotateMode0(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields0(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 1
    unshuffleMode1(colorBaseAddress, miscBaseAddress, scrapData, modePattern, shuffledBuffer, rawOut);
    if (indexPrediction != 0u)
    {
        inversePredictIndices(mode, rawOut);
    }
    if (modeRotationByteAddress != 0)
    {
        derotateMode1(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields1(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 2
    unshuffleMode2(colorBaseAddress, miscBaseAddress, scrapData, modePattern, shuffledBuffer, rawOut);
    if (indexPrediction != 0u)
    {
        inversePredictIndices(mode, rawOut);
    }
    if (modeRotationByteAddress != 0)
    {
        derotateMode2(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields2(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 3
    unshuffleMode3(colorBaseAddress, miscBaseAddress, scrapData, modePattern, shuffledBuffer, rawOut);
    if (indexPrediction != 0u)
    {
        inversePredictIndices(mode, rawOut);
    }
    if (modeRotationByteAddress != 0)
    {
        derotateMode3(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields3(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 4
    unshuffleMode4(colorBaseAddress, miscBaseAddress, scrapData, modePattern, modeStatics, lowEntropy, shuffledBuffer, rawOut);
    if (indexPrediction != 0u)
    {
        inversePredictIndices(mode, rawOut);
    }
    if (modeRotationByteAddress != 0)
    {
        derotateMode4(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields4(extraScrap, endpointOrderBytes, rawOut);
    mode4ReorderRGBA(shuffledBuffer, miscBaseAddress, scrapData, rawOut);
    
    #elif BIN_MODE == 5
    unshuffleMode5(colorBaseAddress, miscBaseAddress, scrapData, modePattern, modeStatics, lowEntropy, shuffledBuffer, rawOut);
    if (indexPrediction != 0u)
    {
        inversePredictIndices(mode, rawOut);
    }
    if (modeRotationByteAddress != 0)
    {
        derotateMode5(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields5(extraScrap, endpointOrderBytes, rawOut);
    mode5ReorderRGBA(shuffledBuffer, miscBaseAddress, scrapData, rawOut);
    
    #elif BIN_MODE == 6
    unshuffleMode6(colorBaseAddress, miscBaseAddress, scrapData, modePattern, modeStatics, lowEntropy, shuffledBuffer, rawOut);
    if (indexPrediction != 0u)
    {
        inversePredictIndices(mode, rawOut);
    }
    if (modeRotationByteAddress != 0)
    {
        derotateMode6(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields6(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 7
    unshuffleMode7(colorBaseAddress, miscBaseAddress, scrapData, modePattern, shuffledBuffer, rawOut);
    if (indexPrediction != 0u)
    {
        inversePredictIndices(mode, rawOut);
    }
    if (modeRotationByteAddress != 0)
    {
        derotateMode7(modeRotationByteAddress, chunkIndex, shuffledBuffer, rawOut);
    }
    reorderEndpointFields7(extraScrap, endpointOrderBytes, rawOut);
    
    #elif BIN_MODE == 8
    unshuffleMode8(rawOut);
    #endif
}

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
    uint packedInfo = scratchBuffer.Load(GACL_BC7_PER_MODE_OFFSET + (GACL_BC7_PER_MODE_STRIDE * mode) + GACL_BC7_PACKED_DATA_OFFSET);
    uint modeStatics = packedInfo & 0xFF;
    uint lowEntropy = (packedInfo >> 8u) & 0xFF;
    uint endpointOrderBytes = (packedInfo >> 16u) & 0xFFFF;
    
    uint modePattern = scratchBuffer.Load(16u + (32u * mode) + 16u);
    // Read from the stream rather than scratch, so the host's CommonHeader layout is unchanged.
    // Start byte bit 7 flags the extension byte (stream byte 1), whose bit 3 is index prediction.
    uint streamStartBytes = shuffledBuffer.Load(0u);
    uint indexPrediction = ((streamStartBytes & 0x80u) != 0u) ? ((streamStartBytes >> 11u) & 0x1u) : 0u;
    
    #if BIN_MODE < 8
    // Extension byte bit 4 = section layout: the layout block (stream byte 2) holds the table bytes, mode selectors (+2) and table offsets (+10),
    // the tables sit between the mode stream and the color data.  The bytes as laid out without a section layout are staged ahead of the check
    const uint colorStride = scratchBuffer.Load(16u + (32 * mode) + 20u);
    const uint miscStride = scratchBuffer.Load(16u + (32 * mode) + 24u);
    g_stageColor = StagePlain(colorBaseAddress, kStageColorBytes[mode], colorStride, shuffledBuffer);
    g_stageMisc = StagePlain(miscBaseAddress, kStageMiscBytes[mode], miscStride, shuffledBuffer);
    const uint selector = ((streamStartBytes & 0x80u) != 0u && (streamStartBytes & 0x1000u) != 0u) ? fetchNextByte(SECTION_LAYOUT_BLOCK_ADDRESS + 2u + mode, shuffledBuffer) : 0u;
    if (selector != 0u)
    {
        const uint sectionCount = scratchBuffer.Load(16u + (32 * mode) + 8u);
        SectionInfo colorSection = (SectionInfo)0;
        SectionInfo miscSection = (SectionInfo)0;
        colorSection.address = colorBaseAddress;
        colorSection.stride = colorStride;
        colorSection.selector = selector & 0x7u;
        miscSection.address = miscBaseAddress;
        miscSection.stride = miscStride;
        miscSection.selector = (selector >> 4u) & 0x7u;
        const bool colorRemap = (colorSection.selector & 0x4u) != 0u;
        const bool miscRemap = (miscSection.selector & 0x4u) != 0u;
        if (colorRemap || miscRemap)
        {
            const uint tableOffsetAddress = SECTION_LAYOUT_BLOCK_ADDRESS + 10u + 2u * mode;
            const uint tableOffset = fetchNextByte(tableOffsetAddress, shuffledBuffer) | (fetchNextByte(tableOffsetAddress + 1u, shuffledBuffer) << 8u);
            
            // the color stream starts with the lower modes' color sections
            uint colorStreamAddress = colorBaseAddress - DTid.x * colorStride;
            [unroll(BIN_MODE)]
            for (uint lm = 0; lm < BIN_MODE; ++lm)
            {
                colorStreamAddress -= scratchBuffer.Load(16u + (32 * lm) + 8u) * scratchBuffer.Load(16u + (32 * lm) + 20u);
            }
            const uint tableAddress = colorStreamAddress - (streamStartBytes >> 16u) + tableOffset;
            
            // tables: [color lens][misc lens][color symbols][misc symbols], each present when that section is remapped
            colorSection.lens = tableAddress;
            miscSection.lens = colorSection.lens + (colorRemap ? colorStride : 0u);
            colorSection.symbols = miscSection.lens + (miscRemap ? miscStride : 0u);
            uint colorSymbolCount = 0u;
            if (colorRemap)
            {
                colorSymbolCount = colorStride;
                for (uint k = 0u; k < colorStride; ++k)
                {
                    colorSymbolCount += fetchNextByte(colorSection.lens + k, shuffledBuffer);
                }
            }
            miscSection.symbols = colorSection.symbols + colorSymbolCount;
        }
        g_stageColor = StageSection(colorSection, kStageColorBytes[mode], DTid.x, sectionCount, shuffledBuffer);
        g_stageMisc = StageSection(miscSection, kStageMiscBytes[mode], DTid.x, sectionCount, shuffledBuffer);
    }
    #endif
    unshuffleBlock(SECTION_TAG_COLOR, SECTION_TAG_MISC, scrapData, extraScrap, modePattern, modeStatics, lowEntropy, endpointOrderBytes, indexPrediction,
        modeRotationByteAddress, chunkIndex, rawOut);
        uint writeAddress = finalBlockID * 16;
    bc7Buffer.Store4(writeAddress, rawOut);
}