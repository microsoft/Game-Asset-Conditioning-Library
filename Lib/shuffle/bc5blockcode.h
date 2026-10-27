//-------------------------------------------------------------------------------------
// bc5blockcode.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#include <cstdint>

// Static codes for the blocks a BC1/3/4/5 EX stream header stores itself (coded entries, see streamheader.h), used
// when a stream has too few duplicate blocks to make room for its header. Byte p of an index-predicted block (bcnex.h)
// is coded as its log2 bucket b (0 | 1 | 2-3 | 4-7 | ... | 128-255) with a canonical Huffman code, then the b - 1 bits
// below its leading one; there is one class (no class bits). BC1 has its own code (kBC1BlockCode); BC3, BC4 and BC5
// share kBC5BlockCode, BC4 using entries 0-7.
// kBC5BlockCode[p] packs the 9 bucket code lengths, (length - 1) in 3 bits each, bucket 0 lowest (64 bytes).
// Trained only on non-RDO streams that need coded blocks (RDO-encoded streams almost always have enough duplicates).
constexpr uint32_t kBC5BlockCode[16] =
{
    0x029cdfd, 0x109cbbf, 0x129396a, 0x18d46d1, 0x34ab760, 0x255bb18, 0x2ad68b0, 0x34ab760,
    0x029cbbf, 0x109cbbf, 0x24d2521, 0x145eb31, 0x44ab758, 0x2512899, 0x14958e9, 0x34ab760
};

// BC1's code, packed as kBC5BlockCode (32 bytes). Trained on non-RDO BC1 64 KB tiles (128x64 blocks) that need coded
// blocks: game textures whose non-solid blocks rarely repeat one of the previous 16 blocks' index bits (RDO encoders
// make them repeat). Against kBC5BlockCode's first 8 entries it fits a header in 1.5x as many of them, with a third
// fewer coded blocks in the tiles both fit.
constexpr uint32_t kBC1BlockCode[8] =
{
    0x02a4b36, 0x02ae775, 0x02b4935, 0x10b797b, 0x4712452, 0x49ae888, 0x49656c8, 0x47244d0
};
