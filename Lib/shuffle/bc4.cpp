//-------------------------------------------------------------------------------------
// bc4.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "gacl.h"
#include <vector>



static void WriteShuffledBC4Block(uint8_t*& d1, uint8_t*& d2, uint8_t*& d3, const uint8_t*& src)
{
    *d1++ = *src++;                                 // r0 (1B)
    *d2++ = *src++;                                 // r1 (1B)
    *d3++ = *src++; *d3++ = *src++; *d3++ = *src++; // ind (6B)
    *d3++ = *src++; *d3++ = *src++; *d3++ = *src++;
}


HRESULT Shuffle_BC4(
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
    static const size_t BC4_BLOCK_SIZE = 8;
    static const size_t BLOCKS_PER_QUAD = 4;

    size_t totalBlocks = size / BC4_BLOCK_SIZE;
    size_t numQuads = totalBlocks / BLOCKS_PER_QUAD;
    size_t numStragglers = totalBlocks % BLOCKS_PER_QUAD;
    size_t shuffleSize = (numQuads * BC4_BLOCK_SIZE * BLOCKS_PER_QUAD);

    uint8_t* d1 = dest;
    uint8_t* d2 = dest + shuffleSize / 8;       // r0, r1
    uint8_t* d3 = dest + shuffleSize / 4;       // ind

    for (size_t i = 0; i < numQuads; i++)
    {
        for (size_t x = 0; x < BLOCKS_PER_QUAD; x++)
        {
            WriteShuffledBC4Block(d1, d2, d3, src);
        }
    }

    if (numStragglers)
    {
        memcpy(dest + shuffleSize, src, numStragglers * BC4_BLOCK_SIZE);
    }

    return S_OK;
}
