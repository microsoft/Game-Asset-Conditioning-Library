//-------------------------------------------------------------------------------------
// bc5.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "gacl.h"
#include <vector>



static void WriteShuffledBC5Block(uint8_t*& d1, uint8_t*& d2, uint8_t*& d3,
    uint8_t*& d4, uint8_t*& d5, uint8_t*& d6, const uint8_t*& src)
{
    *d1++ = *src++;                                 // r0 (1B)
    *d2++ = *src++;                                 // r1 (1B)
    *d3++ = *src++; *d3++ = *src++; *d3++ = *src++; // rind (6B)
    *d3++ = *src++; *d3++ = *src++; *d3++ = *src++;
    *d4++ = *src++;                                 // g0 (1B)
    *d5++ = *src++;                                 // g1 (1B)
    *d6++ = *src++; *d6++ = *src++; *d6++ = *src++; // gind (6B)
    *d6++ = *src++; *d6++ = *src++; *d6++ = *src++;
}


HRESULT Shuffle_BC5(
    _Out_writes_all_(size) uint8_t* dest,
    _In_reads_(size) const uint8_t* src,
    size_t size,
    size_t version
)
{
    if (nullptr == src || nullptr == dest || (size % 8) != 0 || version != 1)
    {
        return E_INVALIDARG;
    }

    static const size_t BC5_BLOCK_SIZE = 16;
    static const size_t BLOCKS_PER_QUAD = 4;

    size_t totalBlocks = size / BC5_BLOCK_SIZE;
    size_t numQuads = totalBlocks / BLOCKS_PER_QUAD;
    size_t numStragglers = totalBlocks % BLOCKS_PER_QUAD;
    size_t shuffleSize = (numQuads * BC5_BLOCK_SIZE * BLOCKS_PER_QUAD);

    uint8_t* d1 = dest + (shuffleSize * 0) / 16;               // r0
    uint8_t* d2 = dest + (shuffleSize * 1) / 16;               // r1
    uint8_t* d3 = dest + (shuffleSize * 2) / 16;               // rind
    uint8_t* d4 = dest + (shuffleSize * 8) / 16;               // g0
    uint8_t* d5 = dest + (shuffleSize * 9) / 16;               // g1
    uint8_t* d6 = dest + (shuffleSize * 10) / 16;              // gind

    for (size_t i = 0; i < numQuads; i++)
    {
        for (size_t x = 0; x < BLOCKS_PER_QUAD; x++)
        {
            WriteShuffledBC5Block(d1, d2, d3, d4, d5, d6, src);
        }
    }

    if (numStragglers)
    {
        memcpy(dest + shuffleSize, src, numStragglers * BC5_BLOCK_SIZE);
    }

    return S_OK;
}
