//-------------------------------------------------------------------------------------
// bc1.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "gacl.h"
#include <vector>


static void WriteShuffledBC1Blockv1(uint8_t*& d1, uint8_t*& d2, uint8_t*& d3, const uint8_t*& src)
{
    *d1++ = *src++; *d1++ = *src++; // e0 (2B)
    *d2++ = *src++; *d2++ = *src++; // e1 (2B)
    *d3++ = *src++; *d3++ = *src++; // ind (4B)
    *d3++ = *src++; *d3++ = *src++;
}


static void WriteShuffledBC1Blockv2(uint8_t*& d1, uint8_t*& d2, const uint8_t*& src)
{
    // R[15:11] G[10:5] B[4:0]    - 5:6:5
    // R4 R3 R2 R1 R0 G5 G4 G3 G2 G1 G0 B4 B3 B2 B1 B0
    // 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
    //  M  M  M  M  L  M  M  M  M  L  L  M  M  M  M  L

    const uint16_t* e0 = reinterpret_cast<const uint16_t*>(src); src += 2;
    const uint16_t* e1 = reinterpret_cast<const uint16_t*>(src); src += 2;

    // most significant 4 bits from first two endpoint color fields
    *d1++ =
        (((*e0 >> 12u) & 0xfu) << 4u) +
        (((*e0 >> 7u) & 0xfu) << 0u);

    // most significant 4 bits from next two endpoint color fields
    *d1++ =
        (((*e0 >> 1u) & 0xfu) << 4u) +
        (((*e1 >> 12u) & 0xfu) << 0u);

    // ...repeat
    *d1++ =
        (((*e1 >> 7u) & 0xfu) << 4u) +
        (((*e1 >> 1u) & 0xfu) << 0u);

    // least significant 1-2 bits from all color fields
    *d1++ =
        (((*e0 >> 11u) & 0x1u) << 7u) +
        (((*e0 >> 5u) & 0x3u) << 5u) +
        (((*e0 >> 0u) & 0x1u) << 4u) +
        (((*e1 >> 11u) & 0x1u) << 3u) +
        (((*e1 >> 5u) & 0x3u) << 1u) +
        (((*e1 >> 0u) & 0x1u) << 0u);

    *d2++ = *src++; *d2++ = *src++; // index (4B)
    *d2++ = *src++; *d2++ = *src++;
}


HRESULT Shuffle_BC1(
    uint8_t* dest,
    const uint8_t* src,
    size_t size,
    size_t version
)
{
    if (nullptr == src || nullptr == dest || (size % 8) != 0 || version < 1 || version > 2)
    {
        return E_INVALIDARG;
    }

    static const size_t BC1_BLOCK_SIZE = 8;

    size_t totalBlocks = size / BC1_BLOCK_SIZE;
    size_t numPairs = totalBlocks / 2;
    size_t numStragglers = totalBlocks % 2;
    size_t shuffleSize = (numPairs * BC1_BLOCK_SIZE * 2);

    uint8_t* d1 = dest;                     // e0
    uint8_t* d2 = dest + shuffleSize / 4;   // e1
    uint8_t* d3 = dest + shuffleSize / 2;   // indexes

    for (size_t i = 0; i < numPairs; i++)
    {
        if (version == 1)
        {
            WriteShuffledBC1Blockv1(d1, d2, d3, src);
            WriteShuffledBC1Blockv1(d1, d2, d3, src);
        }
        else
        {
            WriteShuffledBC1Blockv2(d1, d3, src);
            WriteShuffledBC1Blockv2(d1, d3, src);
        }
    }

    if (numStragglers)
    {
        memcpy(dest + shuffleSize, src, numStragglers * BC1_BLOCK_SIZE);
    }

    return S_OK;
}
