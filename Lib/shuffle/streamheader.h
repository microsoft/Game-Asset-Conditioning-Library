//-------------------------------------------------------------------------------------
// streamheader.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

// In-stream parameter header for BCn block streams (experimental).
//
// Space for the header is made by removing blocks, so the stream size is unchanged and every block
// remains individually addressable:
//
//   - The header occupies the first k whole blocks. Exactly k blocks are removed: k - c duplicates of a
//     block that is kept, then (when the stream runs out of duplicates) c blocks stored inside the header
//     itself with a static entropy code (coded entries). The encoder codes as many blocks as its cheapest choice
//     needs, picked to lie in as few GPU decoder thread groups as possible: only the groups holding coded blocks
//     decode coded bodies.
//   - Kept blocks keep their relative order and fill blocks [k, n). A kept block at original index i is
//     therefore stored at i + (number of removed indices greater than i); blocks after the last removed
//     index are unchanged.
//   - A removed duplicate is read from the stored location of its (kept) source block; a coded block is
//     decoded from its header entry.
//
// Header layout (bytes):
//   [0]     bits [2:0] header transform code (the uncurved transform of the payload): 0 BC7 Join, 1 BC1_224,
//           2 BC1_44, 3 BC3_116224, 4 BC3_664, 5 BC4_116, 6 BC5_116116, 7 BC5_2266 (kStreamHeaderTransforms;
//           both micro-shuffle patterns of each format, all 8 codes used)
//           bits [6:3] parameter presence: bit 3 + (id - 1) is set when parameter id (1..4) is present
//           bit 7 escape (GACL_STREAMHEADER_ESCAPE): 0. Reserved for a future revision of the format (for example a
//           second set of transform codes, or an extension after the parameters); decoders reject streams setting it.
//   [1]...  The present parameters, untagged in ascending id order, each with its fixed size
//           (kStreamHeaderParamSize: 8 bytes for BC7_JOIN_MODE_STATICS, 1 byte for the others).
//           Removed-block descriptor, an LSB-first bit stream:
//             EG(k - 1), EG(c) (c <= k), then per removed duplicate j (ascending index r_j, j < k - c):
//               EG(gap_j)  where gap_0 = r_0 and gap_j = r_j - r_(j-1) - 1
//               EG(dist_j) where d = source_j - r_j (never 0) and dist_j = 2 * (|d| - 1) + (d < 0)
//             then the coded blocks: every index first (ascending, ceil(log2(n)) bits each), so a decoder finds them
//             all without decoding a body, then per coded block in the same order its body:
//               class m    BC7 Join: 3 bits; m = trailing zero count of byte 15 (the BC7 mode), so byte 15 is nonzero.
//                          BC1/3/4/5: none (one class, m = 0).
//               bytes      per byte (0..blockSize - 1), the canonical Huffman code (MSB first; codes assigned in
//                          (length, bucket) order as in DEFLATE) of its log2 bucket b in context (m, byte position),
//                          then the b - 1 bits below its leading one (LSB first). The buckets are 0 | 1 | 2-3 | 4-7 |
//                          ... | 128-255. BC7 Join codes byte 15 as byte15 >> (m + 1), its low m + 1 bits being
//                          implied by m, and codes the bytes of a stream without the byte remap (no parameter 4) as
//                          remapped with set 1 (BC7_ModeJoin_ByteRemap, which keeps those bits): always the remapped
//                          domain kBC7JoinBlockCode was trained on.
//             Duplicates and coded blocks are disjoint, and no source is a removed or coded block.
//           Zero padding to k * blockSize bytes.
//
// EG(v): with x = v + 1 and b = floor(log2(x)), b zero bits, a one bit, then the low b bits of x (LSB first).
// The coded-block code is a static table shared by encoder and decoder (StreamHeaderBlockCode): kBC7JoinBlockCode
// (bc7joinblockcode.h) for BC7 Join, kBC1BlockCode for BC1 and kBC5BlockCode for BC3/4/5 (bcnexblockcode.h; EX
// header codes 1-7, bcnex.h; BC4 uses kBC5BlockCode's first 8 byte positions).
// Decoding the descriptor (including coded entries) is a serial bit-stream walk done once per stream; afterwards
// every block maps independently (StreamHeader_MapBlock). Header-bearing streams use the EX transform id of their
// format, GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX - _BC7_EX (bcnex.h); the GPU decoder is
// Shaders\Shuffle\UnshuffleBCnEx.hlsl, which parses the header in thread 0 of every thread group, decoding the coded
// bodies only up to the group's last coded block (it checks only the bodies it decodes, and doesn't check that the
// unused bits and the padding are zero).

constexpr size_t GACL_STREAMHEADER_FIXED_BYTES = 1;     // transform code, parameter presence and escape bit
constexpr uint8_t GACL_STREAMHEADER_ESCAPE = 0x80;      // byte 0 bit reserved for a future revision: set, the stream isn't this format

// GACL_SHUFFLE_TRANSFORM ids by header transform code
constexpr uint8_t kStreamHeaderTransforms[] =
{
    0x06,   // BC7 Join
    0x01,   // BC1_224
    0x08,   // BC1_44
    0x02,   // BC3_116224
    0x09,   // BC3_664
    0x03,   // BC4_116
    0x04,   // BC5_116116
    0x0A,   // BC5_2266
};

enum GACL_STREAMHEADER_PARAM : uint8_t
{
    GACL_STREAMHEADER_PARAM_CURVE_WIDTH_TILES = 0x01,   // 1 byte: texture width in 16KB micro-tiles. Present only when the payload is curved.
    GACL_STREAMHEADER_PARAM_BC7_JOIN_STATICS  = 0x02,   // 1 byte: BC7 Join group-wide (Statics | LowEntropy) field mask; BC1/3/4/5: index
                                                        // prediction mask, which may also flag an unshuffled payload (bcnex.h
                                                        // BCNEX_PARAM_PREDICTION), present only when not the default
    GACL_STREAMHEADER_PARAM_BC7_JOIN_MODE_STATICS = 0x03, // 8 bytes: BC7 Join per-mode (Statics | LowEntropy) masks, mode 0 first; replaces BC7_JOIN_STATICS
    GACL_STREAMHEADER_PARAM_BC7_JOIN_REMAP = 0x04,      // 1 byte: BC7 Join byte remap, table set + 1 (1..BC7_ModeJoin_RemapSets(): only 1); absent = no remap
};

// Value size of each parameter, by id
constexpr uint8_t kStreamHeaderParamSize[] = { 0, 1, 1, 8, 1 };

struct StreamHeaderParam
{
    uint8_t Id;
    std::vector<uint8_t> Value;
};

struct StreamHeaderInfo
{
    uint8_t TransformId;                 // GACL_SHUFFLE_TRANSFORM id (from the header transform code)
    std::vector<StreamHeaderParam> Params;  // ascending id order
    size_t HeaderBytes;                 // bytes used before the zero padding
    size_t HeaderBlocks;                // k
    std::vector<size_t> Removed;        // removed duplicates: ascending original block indices
    std::vector<size_t> Sources;        // original block index of the kept copy of each removed duplicate
    std::vector<size_t> Coded;          // coded blocks: ascending original block indices
    std::vector<uint8_t> CodedBlocks;   // their contents, blockSize bytes each
};

// Static code for coded blocks. Lengths[16 * m + p] packs the code lengths of the 9 log2 buckets of byte p in class
// m, (length - 1) in 3 bits each, bucket 0 lowest; lengths must form a complete code. With ModeClasses (BC7 Join,
// 16-byte blocks only) a block's class is its BC7 mode, written as 3 class bits, and blocks with byte 15 zero can't be
// coded; without it (BC1/3/4/5) there is one class, no class bits, and every byte is coded whole.
// Map, when set, is the per-block bijection the code sees blocks through: a body codes Map(block, false, params) and
// decodes to Map(body, true, params), params being the header's parameters (ascending id order). It must keep the
// class of a block (with ModeClasses, the bits of byte 15 up to its lowest set bit).
struct StreamHeaderBlockCode
{
    const uint32_t* Lengths;            // 8 classes x 16 byte positions, or blockSize byte positions without ModeClasses
    bool ModeClasses = true;
    void (*Map)(uint8_t* block, bool inverse, const std::vector<StreamHeaderParam>& params) = nullptr;
};

// Encodes src (srcSize bytes of blockSize-byte blocks) into dest (same size) with the header in front. Duplicate
// blocks are removed first; with a block code, coded blocks make up the rest of the room: as many as the cheapest
// blocks need, in as few of the GPU decoder's thread groups (the geometry of transformId) as possible.
// transformId must be in kStreamHeaderTransforms; params have ids 1..4 (each at most once, any order) and their
// kStreamHeaderParamSize sizes. Returns false, leaving dest empty, for invalid arguments or when there is no room for
// the header.
bool StreamHeader_Encode(const uint8_t* src, size_t srcSize, size_t blockSize, uint8_t transformId,
    const std::vector<StreamHeaderParam>& params, std::vector<uint8_t>& dest, StreamHeaderInfo* infoOut = nullptr,
    const StreamHeaderBlockCode* blockCode = nullptr);

// Parses and validates the header at the start of src. Returns false for a malformed header or coded blocks without
// a block code.
bool StreamHeader_Parse(const uint8_t* src, size_t srcSize, size_t blockSize, StreamHeaderInfo& info,
    const StreamHeaderBlockCode* blockCode = nullptr);

// Maps an original block index to the block index it is stored at in the encoded stream, or SIZE_MAX for a
// coded block (stored in info.CodedBlocks). This is the per-block lookup a shader performs; it is O(k).
size_t StreamHeader_MapBlock(const StreamHeaderInfo& info, size_t block);

// Parses src and restores the original block stream (header removed, removed blocks restored) into dest.
bool StreamHeader_Decode(const uint8_t* src, size_t srcSize, size_t blockSize, StreamHeaderInfo& info, std::vector<uint8_t>& dest,
    const StreamHeaderBlockCode* blockCode = nullptr);

// Returns the parameter with the given id, or nullptr.
const StreamHeaderParam* StreamHeader_FindParam(const StreamHeaderInfo& info, uint8_t id);
