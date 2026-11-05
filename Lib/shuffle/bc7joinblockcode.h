//-------------------------------------------------------------------------------------
// bc7joinblockcode.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#include <cstdint>

// Static code for blocks stored inside the BC7 Join in-band stream header (coded entries, see streamheader.h).
// Byte p of a Join-shuffled block of mode m (trailing zero count of byte 15) is coded as its log2 bucket b
// (0 | 1 | 2-3 | 4-7 | ... | 128-255) with a canonical Huffman code, then the b - 1 bits below its leading one.
// Byte 15 is coded as byte15 >> (m + 1), its low m + 1 bits being implied by the mode. The block is coded byte-remapped
// with set 1 (bc7joinremap.h), the stream's own remap or, without one, applied for coding (BC7_JoinBlockCode's Map).
// kBC7JoinBlockCode[m][p] packs the 9 bucket code lengths, (length - 1) in 3 bits each, bucket 0 lowest (512 bytes).
// Modes 0 and 2 have flat codes.
constexpr uint32_t kBC7JoinBlockCode[8][16] =
{
    { 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b },
    { 0x125b76c, 0x1253976, 0x129272d, 0x12936e4, 0x129272d, 0x2252924, 0x1293723, 0x12936e4, 0x14538e3, 0x225c89b, 0x24934d2, 0x369c851, 0x14db49a, 0x34db491, 0x369a691, 0x5a4a51a },
    { 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b, 0x249249b },
    { 0x225371c, 0x125b76c, 0x1293723, 0x1293723, 0x129272d, 0x22528ed, 0x125492d, 0x22538e3, 0x2493692, 0x3693652, 0x369c851, 0x5a93851, 0x34da691, 0x4893491, 0x5b1b259, 0x6db4688 },
    { 0x1253976, 0x32526e4, 0x22536e4, 0x34914db, 0x348a4db, 0x348a4db, 0x348a522, 0x3492521, 0x44924e1, 0x44924e1, 0x109cbbf, 0x029cbbf, 0x029efec, 0x12dba65, 0x2ae7c39, 0x5b76458 },
    { 0x349249a, 0x448a4e2, 0x349249a, 0x349249a, 0x349249a, 0x3452522, 0x34524db, 0x5a894a3, 0x1253976, 0x125a976, 0x22996e4, 0x24d1522, 0x2493322, 0x568a529, 0x5a9a718, 0x496da88 },
    { 0x2253723, 0x129276c, 0x225276c, 0x2252924, 0x225276c, 0x225276c, 0x225276c, 0x525252b, 0x12936e4, 0x125ab6d, 0x248b969, 0x3293863, 0x24d365a, 0x56d44c5, 0x48a4683, 0x3924908 },
    { 0x345269b, 0x22926db, 0x22926db, 0x1293723, 0x029cbf7, 0x125b924, 0x129276c, 0x125b924, 0x125c8e4, 0x249365b, 0x049b924, 0x22928a3, 0x345269b, 0x472089a, 0x46db622, 0x36db6d8 }
};
