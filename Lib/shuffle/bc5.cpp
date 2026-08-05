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

/* Experimental shuffle pattern, may be removed in future releases */
static void WriteShuffledBC5x4Blocks(uint8_t*& d1, uint8_t*& d2, uint8_t*& d3,
    uint8_t*& d4, uint8_t*& d5, uint8_t*& d6, const uint8_t*& src)
{
    d2; d5;


    struct BC5x4
    {
        union Red
        {
            uint64_t c;
            uint8_t bytes[8];
        } r;
        uint8_t rIdx[24];
        union Green
        {
            uint64_t c;
            uint8_t bytes[8];
        } g;
        uint8_t gIdx[24];
    } bc5x2 = {};


    bc5x2.r.c =
        (((src[0] >> 6ull) & 0x3ull) << 62ull) |
        (((src[1] >> 6ull) & 0x3ull) << 60ull) |
        (((src[16] >> 6ull) & 0x3ull) << 58ull) |
        (((src[17] >> 6ull) & 0x3ull) << 56ull) |
        (((src[32] >> 6ull) & 0x3ull) << 54ull) |
        (((src[33] >> 6ull) & 0x3ull) << 52ull) |
        (((src[48] >> 6ull) & 0x3ull) << 50ull) |
        (((src[49] >> 6ull) & 0x3ull) << 48ull) |

        (((src[0] >> 4ull) & 0x3ull) << 46ull) |
        (((src[1] >> 4ull) & 0x3ull) << 44ull) |
        (((src[16] >> 4ull) & 0x3ull) << 42ull) |
        (((src[17] >> 4ull) & 0x3ull) << 40ull) |
        (((src[32] >> 4ull) & 0x3ull) << 38ull) |
        (((src[33] >> 4ull) & 0x3ull) << 36ull) |
        (((src[48] >> 4ull) & 0x3ull) << 34ull) |
        (((src[49] >> 4ull) & 0x3ull) << 32ull) |

        (((src[0] >> 2ull) & 0x3ull) << 30ull) |
        (((src[1] >> 2ull) & 0x3ull) << 28ull) |
        (((src[16] >> 2ull) & 0x3ull) << 26ull) |
        (((src[17] >> 2ull) & 0x3ull) << 24ull) |
        (((src[32] >> 2ull) & 0x3ull) << 22ull) |
        (((src[33] >> 2ull) & 0x3ull) << 20ull) |
        (((src[48] >> 2ull) & 0x3ull) << 18ull) |
        (((src[49] >> 2ull) & 0x3ull) << 16ull) |

        (((src[0] >> 0ull) & 0x3ull) << 14ull) |
        (((src[1] >> 0ull) & 0x3ull) << 12ull) |
        (((src[16] >> 0ull) & 0x3ull) << 10ull) |
        (((src[17] >> 0ull) & 0x3ull) << 8ull) |
        (((src[32] >> 0ull) & 0x3ull) << 6ull) |
        (((src[33] >> 0ull) & 0x3ull) << 4ull) |
        (((src[48] >> 0ull) & 0x3ull) << 2ull) |
        (((src[49] >> 0ull) & 0x3ull) << 0ull);

    memcpy(bc5x2.rIdx, src + 2, 6);
    memcpy(bc5x2.rIdx + 6, src + 18, 6);
    memcpy(bc5x2.rIdx + 12, src + 34, 6);
    memcpy(bc5x2.rIdx + 18, src + 50, 6);

    bc5x2.g.c =
        (((src[8] >> 6ull) & 0x3ull) << 62ull) |
        (((src[9] >> 6ull) & 0x3ull) << 60ull) |
        (((src[24] >> 6ull) & 0x3ull) << 58ull) |
        (((src[25] >> 6ull) & 0x3ull) << 56ull) |
        (((src[40] >> 6ull) & 0x3ull) << 54ull) |
        (((src[41] >> 6ull) & 0x3ull) << 52ull) |
        (((src[56] >> 6ull) & 0x3ull) << 50ull) |
        (((src[57] >> 6ull) & 0x3ull) << 48ull) |

        (((src[8] >> 4ull) & 0x3ull) << 46ull) |
        (((src[9] >> 4ull) & 0x3ull) << 44ull) |
        (((src[24] >> 4ull) & 0x3ull) << 42ull) |
        (((src[25] >> 4ull) & 0x3ull) << 40ull) |
        (((src[40] >> 4ull) & 0x3ull) << 38ull) |
        (((src[41] >> 4ull) & 0x3ull) << 36ull) |
        (((src[56] >> 4ull) & 0x3ull) << 34ull) |
        (((src[57] >> 4ull) & 0x3ull) << 32ull) |

        (((src[8] >> 2ull) & 0x3ull) << 30ull) |
        (((src[9] >> 2ull) & 0x3ull) << 28ull) |
        (((src[24] >> 2ull) & 0x3ull) << 26ull) |
        (((src[25] >> 2ull) & 0x3ull) << 24ull) |
        (((src[40] >> 2ull) & 0x3ull) << 22ull) |
        (((src[41] >> 2ull) & 0x3ull) << 20ull) |
        (((src[56] >> 2ull) & 0x3ull) << 18ull) |
        (((src[57] >> 2ull) & 0x3ull) << 16ull) |

        (((src[8] >> 0ull) & 0x3ull) << 14ull) |
        (((src[9] >> 0ull) & 0x3ull) << 12ull) |
        (((src[24] >> 0ull) & 0x3ull) << 10ull) |
        (((src[25] >> 0ull) & 0x3ull) << 8ull) |
        (((src[40] >> 0ull) & 0x3ull) << 6ull) |
        (((src[41] >> 0ull) & 0x3ull) << 4ull) |
        (((src[56] >> 0ull) & 0x3ull) << 2ull) |
        (((src[57] >> 0ull) & 0x3ull) << 0ull);

    memcpy(bc5x2.gIdx, src + 10, 6);
    memcpy(bc5x2.gIdx + 6, src + 26, 6);
    memcpy(bc5x2.gIdx + 12, src + 42, 6);
    memcpy(bc5x2.gIdx + 18, src + 58, 6);


    for (size_t i = 0; i < _countof(bc5x2.r.bytes); i++)
        *d1++ = bc5x2.r.bytes[i];

    for (size_t i = 0; i < _countof(bc5x2.rIdx); i++)
        *d3++ = bc5x2.rIdx[i];

    for (size_t i = 0; i < _countof(bc5x2.g.bytes); i++)
        *d4++ = bc5x2.g.bytes[i];

    for (size_t i = 0; i < _countof(bc5x2.gIdx); i++)
        *d6++ = bc5x2.gIdx[i];

    src += 64;
}

HRESULT Shuffle_BC5(
    _Out_writes_all_(size) uint8_t* dest,
    _In_reads_(size) const uint8_t* src,
    size_t size,
    size_t version
)
{
    if (nullptr == src || nullptr == dest || (size % 16) != 0 || version != 1 /*|| version > 2*/)
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
        if (version == 2)
        {
            WriteShuffledBC5x4Blocks(d1, d2, d3, d4, d5, d6, src);
            continue;
        }
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
