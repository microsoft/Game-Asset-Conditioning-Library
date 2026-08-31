//-------------------------------------------------------------------------------------
// bc3.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "gacl.h"
#include <vector>

union BC3 {
    uint64_t Raw[2];
    struct
    {
        uint64_t Alpha_0 : 8;
        uint64_t Alpha_1 : 8;

        uint64_t Alpha_A : 3;
        uint64_t Alpha_B : 3;
        uint64_t Alpha_C : 3;
        uint64_t Alpha_D : 3;
        uint64_t Alpha_E : 3;
        uint64_t Alpha_F : 3;
        uint64_t Alpha_G : 3;
        uint64_t Alpha_H : 3;

        uint64_t Alpha_I : 3;
        uint64_t Alpha_J : 3;
        uint64_t Alpha_K : 3;
        uint64_t Alpha_L : 3;
        uint64_t Alpha_M : 3;
        uint64_t Alpha_N : 3;
        uint64_t Alpha_O : 3;
        uint64_t Alpha_P : 3;

        uint64_t Color_0R : 5;
        uint64_t Color_0G : 6;
        uint64_t Color_0B : 5;
        uint64_t Color_1R : 5;
        uint64_t Color_1G : 6;
        uint64_t Color_1B : 5;

        uint64_t Color_A : 2;
        uint64_t Color_B : 2;
        uint64_t Color_C : 2;
        uint64_t Color_D : 2;
        uint64_t Color_E : 2;
        uint64_t Color_F : 2;
        uint64_t Color_G : 2;
        uint64_t Color_H : 2;

        uint64_t Color_I : 2;
        uint64_t Color_J : 2;
        uint64_t Color_K : 2;
        uint64_t Color_L : 2;
        uint64_t Color_M : 2;
        uint64_t Color_N : 2;
        uint64_t Color_O : 2;
        uint64_t Color_P : 2;
    } F;
};


static void WriteShuffledBC3Blockv1(uint8_t*& d1, uint8_t*& d2, uint8_t*& d3,
    uint8_t*& d4, uint8_t*& d5, uint8_t*& d6, const uint8_t*& src)
{
    *d1++ = *src++;                                 // a0 (1B)
    *d2++ = *src++;                                 // a1 (1B)
    *d3++ = *src++; *d3++ = *src++; *d3++ = *src++; // aind (6B)
    *d3++ = *src++; *d3++ = *src++; *d3++ = *src++;
    *d4++ = *src++; *d4++ = *src++;                 // e0 (2B)
    *d5++ = *src++; *d5++ = *src++;                 // e1 (2B)
    *d6++ = *src++; *d6++ = *src++;                 // eind (4B)
    *d6++ = *src++; *d6++ = *src++;
}


static void WriteShuffledBC3Blockv2(uint8_t*& d1, uint8_t*& d2, uint8_t*& d3,
    const uint8_t*& src)
{
    const BC3* pp = reinterpret_cast<const BC3*>(src); pp;

    *d1++ = (src[0] << 4u) | (src[1] & 0xf);            // Alpha0[0-3],  Alpha1[0-3],    high entropy
    *d1++ = (src[0] & 0xf0) | (src[1] >> 4u);           // Alpha0[4-7], Alpha1[4-7]
    src += 2;

    *d2++ = *src++; *d2++ = *src++; *d2++ = *src++;     // aind (6B)
    *d2++ = *src++; *d2++ = *src++; *d2++ = *src++;

    const uint16_t e0 = *reinterpret_cast<const uint16_t*>(src); src += 2;
    const uint16_t e1 = *reinterpret_cast<const uint16_t*>(src); src += 2;

    static constexpr uint16_t eMajor = 0b1111011110011110ul;
    static constexpr uint16_t eMinor = uint16_t(~eMajor);

    uint32_t eo =
        (_pext_u32(e0, eMajor) << 20) |
        (_pext_u32(e1, eMajor) << 8) |
        (_pext_u32(e0, eMinor) << 4) |
        (_pext_u32(e1, eMinor) << 0);

    *d1++ = (eo >> 24u) & 0xffu;                         // d1 bytes 4/6
    *d1++ = (eo >> 16u) & 0xffu;
    *d1++ = (eo >> 8u) & 0xffu;
    *d1++ = eo & 0xffu;


    *d3++ = *src++; *d3++ = *src++;                 // eind (4B)
    *d3++ = *src++; *d3++ = *src++;
}


HRESULT Shuffle_BC3(
    uint8_t* dest,
    const uint8_t* src,
    size_t size,
    size_t version
)
{
    if (nullptr == src || nullptr == dest || (size % 16) != 0 || version < 1 || version > 2)
    {
        return E_INVALIDARG;
    }

    static const size_t BC3_BLOCK_SIZE = 16;
    static const size_t BLOCKS_PER_QUAD = 4;

    size_t totalBlocks = size / BC3_BLOCK_SIZE;
    size_t numQuads = totalBlocks / BLOCKS_PER_QUAD;
    size_t numStragglers = totalBlocks % BLOCKS_PER_QUAD;
    size_t shuffleSize = (numQuads * BC3_BLOCK_SIZE * BLOCKS_PER_QUAD);

    uint8_t* d1 = dest;                                         // a0       1/16
    uint8_t* d2 = dest + shuffleSize / 16;                      // a1       1/16
    uint8_t* d3 = dest + shuffleSize / 8;                       // aind     6/16
    uint8_t* d4 = dest + shuffleSize / 2;                       // e0       2/16
    uint8_t* d5 = dest + shuffleSize / 2 + shuffleSize / 8;     // e1       2/16
    uint8_t* d6 = dest + shuffleSize / 2 + shuffleSize / 4;     // eind     4/16

    if (2 == version)  // 6:6:4
    {
        d2 = dest + (shuffleSize / 16) * 6;
        d3 = dest + (shuffleSize / 16) * 12;
    }

    for (size_t i = 0; i < numQuads; i++)
    {
        for (size_t x = 0; x < BLOCKS_PER_QUAD; x++)
        {
            if (1 == version)
            {
                WriteShuffledBC3Blockv1(d1, d2, d3, d4, d5, d6, src);
            }
            else
            {
                WriteShuffledBC3Blockv2(d1, d2, d3, src);
            }
        }
    }

    if (numStragglers)
    {
        memcpy(dest + shuffleSize, src, numStragglers * BC3_BLOCK_SIZE);
    }

    return S_OK;
}
