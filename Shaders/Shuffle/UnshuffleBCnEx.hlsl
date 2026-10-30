//--------------------------------------------------------------------------------------
// UnshuffleBCnEx.hlsl
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------
// Unified BC1/3/4/5/7 unshuffle. Reverses (CPU reference: Lib\shuffle\bcnex.cpp):
//   - EX streams, carrying a stream header (Lib\shuffle\streamheader.h), with one transform id per format: 0x0B
//     BC1_EX, 0x0C BC3_EX, 0x0D BC4_EX, 0x0E BC5_EX and 0x0F BC7_EX. Duplicate-block removal, header-coded blocks,
//     optional space curve (header parameter 1), the payload shuffle named by the header transform code (one of the
//     id's format: BCnEx_TransformId; other headers are rejected) and per-half index prediction: the 3-bit indices of
//     BC3 alpha, BC4 and BC5, and the 2-bit color indices of BC1 and BC3 (all of them, or those header parameter 2
//     names).
//   - the headerless shuffles 0x01-0x04 (BC1_224, BC3_116224, BC4_116, BC5_116116), 0x06 (BC7 Join,
//     BC7JoinDecode.hlsli) and 0x08-0x0A (BC1_44, BC3_664, BC5_2266).
//   - curve-only EX streams (BCnEx_EncodeCurveOnly: a curve and header parameter 2 BCNEX_UNSHUFFLED) of BC1, BC3,
//     BC4, BC5 and BC7, which replace the former ZSTD_SC transform: the kept blocks follow the header as they are, so
//     only the space curve, duplicate removal and header-coded blocks are reversed (BC7_EX blocks decoded as BC5
//     blocks: header transform BC5_116116 with parameter 2 exactly BCNEX_UNSHUFFLED).
// Replaces the former UnshuffleBC1x/3x/4x/5x.hlsl, keeping their root constants, bindings and work per thread (two
// blocks for BC1, four for BC3/BC4/BC5, plus one thread per straggler block); BC7 takes one block per thread, so
// the host dispatches ceil((n / G + n % G) / 256) groups for n blocks of B bytes (header blocks included) and G blocks
// per thread, both set by the transform id alone: (B, G) = (8, 2) for BC1 (0x01, 0x08, 0x0B), (16, 4) for BC3 (0x02,
// 0x09, 0x0C) and BC5 (0x04, 0x0A, 0x0E), (8, 4) for BC4 (0x03, 0x0D) and (16, 1) for BC7 (0x06, 0x0F). Curve-only
// BC7_EX streams decode four blocks per thread, their spare threads exiting. ceil(bufferSizeInBytes / 16 / 256)
// groups cover every transform id, with up to 4x the groups BC3/BC4/BC5 need.
// The per-format builds (build options below), which the host picks by transform id, take ceil((n / T + n % T) / TG)
// groups of TG threads, T blocks per thread: UnshuffleBC1Headerless (0x01, 0x08) and UnshuffleBC1Ex (0x0B) T 2, TG 128;
// UnshuffleBC3Headerless (0x02, 0x09) and UnshuffleBC3Ex (0x0C) T 4, TG 256; UnshuffleBC4Headerless (0x03) T 4 and
// UnshuffleBC4Ex (0x0D) T 2, TG 256; UnshuffleBC5Headerless (0x04, 0x0A) and UnshuffleBC5Ex (0x0E) T 4, TG 128;
// UnshuffleBC7Ex (0x06, 0x0F) T 1, TG 256.
// The header is decoded by thread 0 of every group, while for BC7 Join the lanes from 64 up build the gather rows from
// the masks in the stream. Unlike the CPU, the shader does not check that the unused header bits and the header
// padding are zero, and a group decodes the coded block bodies only up to its own last coded block, so it checks the
// bodies and the header's fit only that far.
// bufferOffsetInBytes is rounded down to a multiple of the block size, as the per-format shaders' element offset
// (bufferOffsetInBytes / block size) is. Store addresses are masked to the block alignment: raw buffer stores at
// addresses the driver can't prove 16 / 8-byte aligned can be split into narrower ones (~3x slower on NVIDIA).
//--------------------------------------------------------------------------------------
#include "Shared.hlsli"
#include "SharedDefinitions.hlsli"
#include "BC7JoinDecode.hlsli"
#include "BCnExTables.hlsli"

// Build options: none for the unified build; the per-format builds (UnshuffleBC1Ex.hlsl - UnshuffleBC7Ex.hlsl and
// UnshuffleBC1Headerless.hlsl - UnshuffleBC5Headerless.hlsl) define them before including this file.
//   FIXED_TRANSFORM_ID     the EX id decoded (0x0B - 0x0F). The build also takes its format's headerless ids (BC7_EX:
//                          BC7 Join, 0x06) from the transformId root constant, and decodes any other id there as
//                          FIXED_TRANSFORM_ID; the compiler drops the code of the other formats.
//   EX_ONLY                with FIXED_TRANSFORM_ID 0x0B - 0x0E: every stream decodes as FIXED_TRANSFORM_ID, without
//                          the headerless path
//   HEADERLESS_ONLY        with FIXED_TRANSFORM_ID 0x0B - 0x0E: the build decodes the format's headerless ids alone (it
//                          writes nothing for any other id). Code a stream never runs still changes its timing: apart,
//                          BC4_116 64 KB tiles decoded 1.1-1.2x faster and BC1/BC4/BC5 EX streams up to 1.1x (RTX 5080)
//   THREAD_GROUP_SIZE      threads per group (a plain number, at least 64), instead of TG_THREAD_COUNT (256)
//   EX_BLOCKS_PER_THREAD   blocks per thread of the EX id's dispatch (1u or 2u) when below the format's G (BC1 2,
//                          BC3/BC4/BC5 4); the headerless ids keep G. Threads of fewer than G blocks don't gather whole
//                          pairs / quads, but read their blocks one at a time.
//   EX_GENERAL_DECODE      EX streams always take the general header path (removed duplicates and coded blocks
//                          counted per block: WalkPayloadIndex), never the short header path
//   EX_WIDE_STREAM_BLOCKS  with EX_BLOCKS_PER_THREAD: streams of at least this many blocks (header blocks included)
//                          whose header FastParseHeader doesn't take decode G blocks per thread instead, the groups of
//                          the EX_BLOCKS_PER_THREAD dispatch past those threads exiting before the header parse
#ifdef THREAD_GROUP_SIZE
#undef TG_THREAD_COUNT
#define TG_THREAD_COUNT THREAD_GROUP_SIZE
#endif
// The literal G of main's ExBlocks calls, quad formats and BC1: the format's, or EX_BLOCKS_PER_THREAD below it (with
// EX_WIDE_STREAM_BLOCKS the format's, as a thread then decodes either)
#if defined(EX_BLOCKS_PER_THREAD) && !defined(EX_WIDE_STREAM_BLOCKS)
#define EX_G4 EX_BLOCKS_PER_THREAD
#define EX_G2 (EX_BLOCKS_PER_THREAD < 2u ? EX_BLOCKS_PER_THREAD : 2u)
#else
#define EX_G4 4u
#define EX_G2 2u
#endif

ByteAddressBuffer srcBuffer : register(t0);

RWByteAddressBuffer dstBuffer : register(u0);

cbuffer BCnExInfo : register(b0)
{
    uint bufferSizeInBytes;
    uint bufferOffsetInBytes;
#ifdef FIXED_TRANSFORM_ID
    uint hostTransformId;
#else
    uint transformId;
#endif
    uint widthInPixels;
};

#ifdef FIXED_TRANSFORM_ID
// the build's ids: its format's headerless ids when the host names one, else the EX id
#if FIXED_TRANSFORM_ID == 0x0F
#define transformId (hostTransformId == 0x06u ? 0x06u : 0x0Fu)
#else
#if FIXED_TRANSFORM_ID == 0x0B
#define FIXED_HEADERLESS_A 0x01u
#define FIXED_HEADERLESS_B 0x08u
#elif FIXED_TRANSFORM_ID == 0x0C
#define FIXED_HEADERLESS_A 0x02u
#define FIXED_HEADERLESS_B 0x09u
#elif FIXED_TRANSFORM_ID == 0x0D
#define FIXED_HEADERLESS_A 0x03u
#define FIXED_HEADERLESS_B 0x03u
#elif FIXED_TRANSFORM_ID == 0x0E
#define FIXED_HEADERLESS_A 0x04u
#define FIXED_HEADERLESS_B 0x0Au
#else
#error FIXED_TRANSFORM_ID must be an EX id, 0x0B - 0x0F
#endif
#ifdef EX_ONLY
#define transformId FIXED_TRANSFORM_ID
#else
#define transformId (hostTransformId == FIXED_HEADERLESS_A ? FIXED_HEADERLESS_A : \
    (hostTransformId == FIXED_HEADERLESS_B ? FIXED_HEADERLESS_B : FIXED_TRANSFORM_ID))
#endif
#endif
#endif
#if (defined(EX_ONLY) || defined(HEADERLESS_ONLY)) && !(defined(FIXED_TRANSFORM_ID) && FIXED_TRANSFORM_ID != 0x0F)
#error EX_ONLY and HEADERLESS_ONLY need a BC1 - BC5 FIXED_TRANSFORM_ID, 0x0B - 0x0E
#endif
#if defined(EX_ONLY) && defined(HEADERLESS_ONLY)
#error EX_ONLY and HEADERLESS_ONLY exclude each other
#endif

#define TRANSFORM_BC1_224       0x01u
#define TRANSFORM_BC3_116224    0x02u
#define TRANSFORM_BC4_116       0x03u
#define TRANSFORM_BC5_116116    0x04u
#define TRANSFORM_BC7_JOIN      0x06u
#define TRANSFORM_BC1_44        0x08u
#define TRANSFORM_BC3_664       0x09u
#define TRANSFORM_BC5_2266      0x0Au
#define TRANSFORM_BC1_EX        0x0Bu
#define TRANSFORM_BC3_EX        0x0Cu
#define TRANSFORM_BC4_EX        0x0Du
#define TRANSFORM_BC5_EX        0x0Eu
#define TRANSFORM_BC7_EX        0x0Fu

// The EX transform id of header transform code c (BCnEx_TransformId): BC7_EX for 0 (Join), BC1_EX for 1 / 2, BC3_EX
// for 3 / 4, BC4_EX for 5 and BC5_EX for 6 / 7
uint ExTransformOfCode(uint c)
{
    return TRANSFORM_BC1_EX + ((0x33211004u >> (c << 2)) & 0xFu);
}

#ifdef FIXED_TRANSFORM_ID
// Header transform t (one ParseHeader accepts for FIXED_TRANSFORM_ID) as a literal: the second of the id's two when t
// is, else the first (BC7_EX: BC7 Join, or curve-only BC5_116116)
uint FixedHeaderTransform(uint t)
{
    const uint a = FIXED_TRANSFORM_ID == TRANSFORM_BC1_EX ? TRANSFORM_BC1_224 : FIXED_TRANSFORM_ID == TRANSFORM_BC3_EX ?
        TRANSFORM_BC3_116224 : FIXED_TRANSFORM_ID == TRANSFORM_BC4_EX ? TRANSFORM_BC4_116 :
        FIXED_TRANSFORM_ID == TRANSFORM_BC5_EX ? TRANSFORM_BC5_116116 : TRANSFORM_BC7_JOIN;
    const uint b = FIXED_TRANSFORM_ID == TRANSFORM_BC1_EX ? TRANSFORM_BC1_44 : FIXED_TRANSFORM_ID == TRANSFORM_BC3_EX ?
        TRANSFORM_BC3_664 : FIXED_TRANSFORM_ID == TRANSFORM_BC4_EX ? TRANSFORM_BC4_116 :
        FIXED_TRANSFORM_ID == TRANSFORM_BC5_EX ? TRANSFORM_BC5_2266 : TRANSFORM_BC5_116116;
    return t == b ? b : a;
}
#endif

// Header limit shared with the encoder (BCNEX_MAX_HEADER_BLOCKS in bcnex.h)
#define BCNEX_MAX_HEADER_BLOCKS 64u

// Header parameter 2 bit (BCNEX_UNSHUFFLED in bcnex.h; curve-only streams set it): the kept blocks follow the header
// unshuffled, so main reads every payload block as is
#define BCNEX_UNSHUFFLED 4u

// Stream header byte 0 (streamheader.h): the header transform code (bits 0-2), the parameter presence bits (3-6) and
// the escape bit (7, GACL_STREAMHEADER_ESCAPE), which this format leaves 0; the parameters follow from byte 1.
#define GACL_STREAMHEADER_ESCAPE 0x80u

// The header's bytes 1-15, from its first 16 bytes h: x = bytes 1-4, y = 5-8, z = 9-12, w = 13-15
uint4 HeaderParamWords(uint4 h)
{
    return uint4((h.x >> 8) | (h.y << 24), (h.y >> 8) | (h.z << 24), (h.z >> 8) | (h.w << 24), h.w >> 8);
}

groupshared uint gsValid;
groupshared uint gsTransform;           // headerless transform id of the payload
groupshared uint gsHeaderBlocks;        // k
groupshared uint gsRemovedCount;        // removed duplicates, k - c
groupshared uint gsCodedCount;          // coded blocks, c
groupshared uint gsCurveWidthInPixels;  // 0 = linear
groupshared uint gsPredictMask;         // BCNEX_PREDICT_* mask, plus BCNEX_UNSHUFFLED
groupshared uint gsRemoved[BCNEX_MAX_HEADER_BLOCKS];
groupshared uint gsSources[BCNEX_MAX_HEADER_BLOCKS];
groupshared uint gsCodedIndex[BCNEX_MAX_HEADER_BLOCKS];
groupshared uint4 gsCoded[BCNEX_MAX_HEADER_BLOCKS];   // the bodies up to the group's last coded block
groupshared uint gsUnfoldZ[64];         // UnfoldZEntry, for UnpredictHalf

uint BlockSize(uint id)
{
    return (id == TRANSFORM_BC1_224 || id == TRANSFORM_BC1_44 || id == TRANSFORM_BC4_116) ? 8u :
        (id == TRANSFORM_BC3_116224 || id == TRANSFORM_BC3_664 || id == TRANSFORM_BC5_116116 || id == TRANSFORM_BC5_2266 ||
         id == TRANSFORM_BC7_JOIN) ? 16u : 0u;
}

uint BlocksPerThread(uint id)
{
    return (id == TRANSFORM_BC1_224 || id == TRANSFORM_BC1_44) ? 2u : (id == TRANSFORM_BC7_JOIN ? 1u : 4u);
}

// Blocks per thread of the EX dispatch (header transform id): EX_BLOCKS_PER_THREAD when below G
uint ExThreadBlocks(uint id)
{
#ifdef EX_BLOCKS_PER_THREAD
    return min(BlocksPerThread(id), EX_BLOCKS_PER_THREAD);
#else
    return BlocksPerThread(id);
#endif
}

#ifdef EX_WIDE_STREAM_BLOCKS
// The block size and G of EX id tid
uint ExIdBlockSize(uint tid)
{
    return (tid == TRANSFORM_BC1_EX || tid == TRANSFORM_BC4_EX) ? 8u : 16u;
}

uint ExIdBlocksPerThread(uint tid)
{
    return tid == TRANSFORM_BC1_EX ? 2u : (tid == TRANSFORM_BC7_EX ? 1u : 4u);
}
#endif

// First block of thread t in main's mapping (G blocks for each of the first n / G threads, then one per straggler),
// and n past the last
uint FirstBlockOfThread(uint t, uint n, uint G)
{
    uint whole = n / G;
    return t < whole ? t * G : min(whole * G + (t - whole), n);
}

// ---------------------------------------------------------------------------------------------------------------
// Stream header

// 32 bits of the stream starting at bit position bitPos (LSB first)
uint PeekBits32(uint bitPos)
{
    uint a = (bitPos >> 5) << 2, s = bitPos & 31u;
    uint lo = srcBuffer.Load(a);
    return s ? (lo >> s) | (srcBuffer.Load(a + 4u) << (32u - s)) : lo;
}

// PeekBits32 within the stream; clears ok (and returns 0) when the 8 bytes from bitPos / 8 pass its end. A valid
// header ends at least one block before the end of the stream, so this never rejects one. Guarded loads use if /
// else rather than ?: or &&, which only short-circuit from HLSL 2021 on.
uint PeekBits32Safe(uint bitPos, inout bool ok)
{
    uint bits = 0;
    if ((bitPos >> 3) + 8u <= bufferSizeInBytes)
    {
        bits = PeekBits32(bitPos);
    }
    else
    {
        ok = false;
    }
    return bits;
}

// Exp-Golomb code: up to 31 leading zero bits, as values of 2^31 - 1 and up fail every range check
uint ReadExpGolomb(inout uint bitPos, inout bool ok)
{
    uint w = PeekBits32Safe(bitPos, ok);
    if (w == 0)
    {
        ok = false;
        return 0;
    }
    uint b = firstbitlow(w);
    uint low = 0;
    if (b < 16u)
    {
        // the whole code (2b + 1 bits) is in w: only PeekBits32Safe's bounds check of reading its low bits
        low = (w >> (b + 1u)) & ((1u << b) - 1u);
        if (b != 0 && ((bitPos + b + 1u) >> 3) + 8u > bufferSizeInBytes)
        {
            ok = false;
            low = 0;
        }
    }
    else
    {
        low = PeekBits32Safe(bitPos + b + 1u, ok) & ((1u << b) - 1u);
    }
    bitPos += 2u * b + 1u;
    return ((1u << b) | low) - 1u;
}

// 32-bit word wi of the stream, 0 unless it is all within the stream
uint LoadWordSafe(uint wi)
{
    uint w = 0;
    if (wi * 4u + 4u <= bufferSizeInBytes)
    {
        w = srcBuffer.Load(wi * 4u);
    }
    return w;
}

// Bit reader of the coded block bodies: the stream's words wi, wi + 1 and (prefetched) wi + 2 in registers, s bits
// into word wi
struct BitReader
{
    uint wi, s, lo, hi, nx;
};

BitReader OpenBits(uint bitPos)
{
    BitReader br;
    br.wi = bitPos >> 5;
    br.s = bitPos & 31u;
    br.lo = LoadWordSafe(br.wi);
    br.hi = LoadWordSafe(br.wi + 1u);
    br.nx = LoadWordSafe(br.wi + 2u);
    return br;
}

uint ReaderPos(BitReader br)
{
    return (br.wi << 5) + br.s;
}

// the 32 bits from the reader's position (PeekBits32's when its 8 bytes are within the stream)
uint PeekReader(BitReader br)
{
    return (br.lo >> br.s) | ((br.hi << 1) << (31u - br.s));
}

// n < 32
void SkipBits(inout BitReader br, uint n)
{
    br.s += n;
    if (br.s >= 32u)
    {
        br.s -= 32u;
        br.wi++;
        br.lo = br.hi;
        br.hi = br.nx;
        br.nx = LoadWordSafe(br.wi + 2u);
    }
}

// Coded block of bs bytes (GetCodedBlock in streamheader.cpp): BC7 Join class m (3 bits; BC1/3/4/5 have one class,
// m = 0), then per byte the canonical Huffman code (MSB first) of its log2 bucket in context (m, byte), from the code
// lengths in Lib/shuffle's kBC7JoinBlockCode, kBC1BlockCode (BC1) or kBC5BlockCode (BC3/4/5; BC4 uses its first 8
// entries), followed by the bits below the leading one. BC7 Join byte 15 holds byte15 >> (m + 1), and a Join block is
// always coded byte-remapped, whether or not the stream is (JoinSortedDecode undoes the remap). An 8-byte block is
// returned in .xy. Every code is complete, so any next 8 bits decode (kBCnExBlockCodeDecode). The bits come from a
// BitReader. The byte loop is branch-free: a body is at most 16 x 15 bits from bit s < 32 of word wi, so it ends within
// word wi + 8, and words wi + 3 .. wi + 8 are loaded once, ahead of the loop, and shifted in as the bytes cross words
// (Join -4..5% on coded files and EX coded -3..5% vs per-byte branches and loads, RTX 5080). The current tables'
// longest bodies (Join's, 169 bits after the class) end within word wi + 6, but loading only up to it was no faster.
// On return only br's position is meaningful (its words past wi + 8 read as 0).
uint4 ReadCodedBlock(inout BitReader br, bool join, bool bc1, uint bs, inout bool ok)
{
    uint m = 0;
    if (join)
    {
        m = PeekReader(br) & 7u;
        SkipBits(br, 3u);
    }
    uint4 block = 0;
    const uint ctx = (join ? m : (bc1 ? 9u : 8u)) * 96u;
    uint used = 0;
    uint wi = br.wi, s = br.s, lo = br.lo, hi = br.hi, nx = br.nx;
    // LoadWordSafe(wi + 3 .. wi + 8) without its branch (word 0 is loaded instead and dropped)
    uint w3, w4, w5, w6, w7, w8;
    {
        uint wv[6];
        [unroll]
        for (uint j = 0; j < 6u; j++)
        {
            const uint a = (wi + 3u + j) * 4u;
            const bool inS = a + 4u <= bufferSizeInBytes;
            const uint w = srcBuffer.Load(inS ? a : 0u);
            wv[j] = inS ? w : 0u;
        }
        w3 = wv[0];
        w4 = wv[1];
        w5 = wv[2];
        w6 = wv[3];
        w7 = wv[4];
        w8 = wv[5];
    }
    for (uint p = 0; p < bs; p++)
    {
        const uint q = ctx + p * 6u;
        const uint c0 = kBCnExBlockCodeDecode[q], c1 = kBCnExBlockCodeDecode[q + 1u], c2 = kBCnExBlockCodeDecode[q + 2u];
        const uint e0 = kBCnExBlockCodeDecode[q + 3u], e1 = kBCnExBlockCodeDecode[q + 4u], e2 = kBCnExBlockCodeDecode[q + 5u];
        // a code is at most 8 bits and the bits below the leading one at most 7 (PeekReader)
        const uint bits = (lo >> s) | ((hi << 1) << (31u - s));
        // canonical index of the code: the number of code starts (10-bit lanes of c0..c2) the next 8 bits, MSB first,
        // reach
        const uint r = (reversebits(bits) >> 24) * 0x00100401u;
        const uint sidx = countbits((r + c0) & 0x10040100u) + countbits((r + c1) & 0x10040100u) + countbits((r + c2) & 0x10040100u);
        const uint e = ((sidx < 4u ? e0 : (sidx < 8u ? e1 : e2)) >> ((sidx & 3u) << 3)) & 0xFFu;
        const uint sym = e & 15u;
        used = e >> 4;
        // buckets 0 and 1 have no bits below the leading one
        const uint x = max(sym, 1u) - 1u;
        const uint v = ((bits >> (used - x)) & ((1u << x) - 1u)) | (sym != 0 ? (1u << x) : 0u);
        // SkipBits(used): used < 32, so at most one word is crossed
        s += used;
        const bool cross = s >= 32u;
        s &= 31u;
        lo = cross ? hi : lo;
        hi = cross ? nx : hi;
        nx = cross ? w3 : nx;
        w3 = cross ? w4 : w3;
        w4 = cross ? w5 : w4;
        w5 = cross ? w6 : w5;
        w6 = cross ? w7 : w6;
        w7 = cross ? w8 : w7;
        w8 = cross ? 0u : w8;
        wi += cross ? 1u : 0u;
        // shift the bytes in from the top: byte p ends at byte p (8-byte blocks: at byte 8 + p)
        block = uint4((block.x >> 8) | (block.y << 24), (block.y >> 8) | (block.z << 24), (block.z >> 8) | (block.w << 24),
            (block.w >> 8) | ((v & 0xFFu) << 24));
    }
    if (join)
    {
        // byte 15 holds byte15 >> (m + 1) (v < 256 for every bucket)
        const uint v = block.w >> 24;
        ok = ok && (v >> (7u - m)) == 0;
        block.w = (block.w & 0x00FFFFFFu) | (((v << (m + 1u)) | (1u << m)) << 24);
    }
    br.wi = wi;
    br.s = s;
    br.lo = lo;
    br.hi = hi;
    br.nx = nx;
    // PeekBits32Safe's check of each byte's start (and of the class's, before them): they ascend, so the last decides
    ok = ok && ((ReaderPos(br) - used) >> 3) + 8u <= bufferSizeInBytes;
    return bs == 16u ? block : uint4(block.zw, 0, 0);
}

// ReadCodedBlock at bit position bitPos (advanced past it)
uint4 ReadCodedBlock(inout uint bitPos, bool join, bool bc1, uint bs, inout bool ok)
{
    BitReader br = OpenBits(bitPos);
    const uint4 block = ReadCodedBlock(br, join, bc1, bs, ok);
    bitPos = ReaderPos(br);
    return block;
}

// ReadCodedBlock's decode of one byte in context q from the 32 bits at its position: the bits it uses and its value
void DecodeCodedByte(uint q, uint bits, out uint used, out uint v)
{
    const uint c0 = kBCnExBlockCodeDecode[q], c1 = kBCnExBlockCodeDecode[q + 1u], c2 = kBCnExBlockCodeDecode[q + 2u];
    const uint e0 = kBCnExBlockCodeDecode[q + 3u], e1 = kBCnExBlockCodeDecode[q + 4u], e2 = kBCnExBlockCodeDecode[q + 5u];
    const uint r = (reversebits(bits) >> 24) * 0x00100401u;
    const uint sidx = countbits((r + c0) & 0x10040100u) + countbits((r + c1) & 0x10040100u) + countbits((r + c2) & 0x10040100u);
    const uint e = ((sidx < 4u ? e0 : (sidx < 8u ? e1 : e2)) >> ((sidx & 3u) << 3)) & 0xFFu;
    const uint sym = e & 15u;
    used = e >> 4;
    const uint x = max(sym, 1u) - 1u;
    v = (((bits >> (used - x)) & ((1u << x) - 1u)) | (sym != 0 ? (1u << x) : 0u)) & 0xFFu;
}

// The 128 bits of b shifted down by nb bytes (nb = 1..3, a literal), with the nb bytes of chunk on top
uint4 ShiftInBytes(uint4 b, uint chunk, uint nb)
{
    const uint k = 8u * nb;
    return uint4((b.x >> k) | (b.y << (32u - k)), (b.y >> k) | (b.z << (32u - k)), (b.z >> k) | (b.w << (32u - k)),
        (b.w >> k) | (chunk << (32u - k)));
}

// The word at word index wi of the stream (zero past it)
uint LoadWordZ(uint wi)
{
    const uint a = wi * 4u;
    const bool inS = a + 4u <= bufferSizeInBytes;
    const uint w = srcBuffer.Load(inS ? a : 0u);
    return inS ? w : 0u;
}

// WaveDecodeStep's nb bytes from context q on, decoded by every lane from its bits and then followed along the actual
// offsets with lane reads: the bytes (chunk), the bits they use (adv) and the last byte's (returned)
uint WaveDecodeBytes(uint nb, uint q, uint bits, out uint chunk, out uint adv)
{
    uint u0, v0, u1 = 0, v1 = 0, u2 = 0, v2 = 0;
    DecodeCodedByte(q, bits, u0, v0);
    if (nb > 1u)
    {
        DecodeCodedByte(q + 6u, bits, u1, v1);
    }
    if (nb > 2u)
    {
        DecodeCodedByte(q + 12u, bits, u2, v2);
    }
    const uint L0 = WaveReadLaneAt(u0, 0);
    uint last = L0;
    chunk = WaveReadLaneAt(v0, 0);
    adv = L0;
    if (nb > 1u)
    {
        last = WaveReadLaneAt(u1, adv);
        chunk |= WaveReadLaneAt(v1, adv) << 8;
        adv += last;
    }
    if (nb > 2u)
    {
        last = WaveReadLaneAt(u2, adv);
        chunk |= WaveReadLaneAt(v2, adv) << 16;
        adv += last;
    }
    return last;
}

// One step of ReadCodedBlockWave: nb (a literal, 1..3) bytes from context q on, each lane decoding them at its own bit
// offset from the reader (s + lane, from the two words it loads). Returns the last byte's bits.
uint WaveDecodeStep(uint nb, uint lane, uint q, inout uint s, inout uint wi, inout uint4 block)
{
    const uint o = s + lane;
    const uint wl = wi + (o >> 5), sh = o & 31u;
    const uint a = LoadWordZ(wl), b = LoadWordZ(wl + 1u);
    const uint bits = (a >> sh) | ((b << 1) << (31u - sh));
    uint chunk, adv;
    const uint last = WaveDecodeBytes(nb, q, bits, chunk, adv);
    block = ShiftInBytes(block, chunk, nb);
    s += adv;
    wi += s >> 5;
    s &= 31u;
    return last;
}

// ReadCodedBlock at bit position bitPos (advanced past it) on every lane of a wave of at least 32 lanes, with the same
// position in each: a byte's code and low bits are at most 15 bits, so each step decodes bytes p, p + 1 and p + 2 at
// every lane's offset from p's position (p + 1 starts 1..15 bits on, p + 2 2..30) and follows the actual offsets with
// two lane reads, a third of ReadCodedBlock's serial chain. The first step takes bs % 3 bytes.
uint4 ReadCodedBlockWave(inout uint bitPos, bool join, bool bc1, uint bs, inout bool ok)
{
    const uint lane = WaveGetLaneIndex();
    uint wi = bitPos >> 5, s = bitPos & 31u;
    uint m = 0;
    if (join)
    {
        m = ((LoadWordZ(wi) >> s) | ((LoadWordZ(wi + 1u) << 1) << (31u - s))) & 7u;
        s += 3u;
        const bool s1 = s >= 32u;
        s &= 31u;
        wi += s1 ? 1u : 0u;
    }
    const uint ctx = (join ? m : (bc1 ? 9u : 8u)) * 96u;
    uint4 block = 0;
    uint last;
    uint q = ctx;
    if (bs == 16u)
    {
        last = WaveDecodeStep(1u, lane, q, s, wi, block);
        q += 6u;
        for (uint p = 1; p < 16u; p += 3u)
        {
            last = WaveDecodeStep(3u, lane, q, s, wi, block);
            q += 18u;
        }
    }
    else
    {
        last = WaveDecodeStep(2u, lane, q, s, wi, block);
        q += 12u;
        for (uint p = 2; p < 8u; p += 3u)
        {
            last = WaveDecodeStep(3u, lane, q, s, wi, block);
            q += 18u;
        }
    }
    if (join)
    {
        // byte 15 holds byte15 >> (m + 1) (v < 256 for every bucket)
        const uint v = block.w >> 24;
        ok = ok && (v >> (7u - m)) == 0;
        block.w = (block.w & 0x00FFFFFFu) | (((v << (m + 1u)) | (1u << m)) << 24);
    }
    bitPos = (wi << 5) + s;
    ok = ok && ((bitPos - last) >> 3) + 8u <= bufferSizeInBytes;
    return bs == 16u ? block : uint4(block.zw, 0, 0);
}

// Number of removed duplicates / coded blocks with index below i (binary search of the first count entries)
uint RemovedBefore(uint i, uint count)
{
    uint lo = 0, hi = count;
    while (lo < hi)
    {
        uint mid = (lo + hi) >> 1;
        if (gsRemoved[mid] < i) lo = mid + 1u; else hi = mid;
    }
    return lo;
}

uint CodedBefore(uint i, uint count)
{
    uint lo = 0, hi = count;
    while (lo < hi)
    {
        uint mid = (lo + hi) >> 1;
        if (gsCodedIndex[mid] < i) lo = mid + 1u; else hi = mid;
    }
    return lo;
}

bool IsRemoved(uint i, uint count)
{
    uint r = RemovedBefore(i, count);
    bool found = false;
    if (r < count)
    {
        found = gsRemoved[r] == i;
    }
    return found;
}

bool IsCoded(uint i, uint count)
{
    uint c = CodedBefore(i, count);
    bool found = false;
    if (c < count)
    {
        found = gsCodedIndex[c] == i;
    }
    return found;
}

// A coded block body, from a copy of ReadCodedBlockWave (ReadCodedBlock on waves under 32 lanes) per context kind and
// length, whose table offsets and trip counts are then static (a single generic copy decoded ~1.2x slower, RTX 5080)
uint4 DecodeBody(inout uint bodyPos, bool join, uint G, uint bs, inout bool ok)
{
    uint4 body;
    if (WaveGetLaneCount() >= 32u)
    {
        if (join)
        {
            body = ReadCodedBlockWave(bodyPos, true, false, 16u, ok);
        }
        else if (G == 2u)
        {
            body = ReadCodedBlockWave(bodyPos, false, true, 8u, ok);
        }
        else if (bs == 8u)
        {
            body = ReadCodedBlockWave(bodyPos, false, false, 8u, ok);
        }
        else
        {
            body = ReadCodedBlockWave(bodyPos, false, false, 16u, ok);
        }
        return body;
    }
    if (join)
    {
        body = ReadCodedBlock(bodyPos, true, false, 16u, ok);
    }
    else if (G == 2u)
    {
        // BC1 (BlocksPerThread)
        body = ReadCodedBlock(bodyPos, false, true, 8u, ok);
    }
    else if (bs == 8u)
    {
        body = ReadCodedBlock(bodyPos, false, false, 8u, ok);
    }
    else
    {
        body = ReadCodedBlock(bodyPos, false, false, 16u, ok);
    }
    return body;
}

// Thread 0's wave decodes and validates the header into groupshared memory (StreamHeader_Parse, plus the parameter
// checks of BCnEx_Decode / BC7_JoinStream_Decode), every lane with the same values, so that its lanes decode the coded
// block bodies together (ReadCodedBlockWave). The coded block bodies follow all the coded positions, in
// the same order: a group decodes them up to its own last coded block, and none when it has none (binary searches of
// the positions find its range), so the bodies and the header's fit are checked only as far as the group reads.
// groupThread is the group's first thread.
void ParseHeader(uint groupThread)
{
    // GACL_SHUFFLE_TRANSFORM of each header transform code (kStreamHeaderTransforms, streamheader.h)
    static const uint kTransforms[8] = { TRANSFORM_BC7_JOIN, TRANSFORM_BC1_224, TRANSFORM_BC1_44, TRANSFORM_BC3_116224,
        TRANSFORM_BC3_664, TRANSFORM_BC4_116, TRANSFORM_BC5_116116, TRANSFORM_BC5_2266 };

    bool ok = true;
    uint w0 = PeekBits32Safe(0, ok);
    const uint code = w0 & 7u;
    uint id = kTransforms[code];
    bool join = id == TRANSFORM_BC7_JOIN;
    uint presence = (w0 >> 3) & 0xFu;
    uint bs = BlockSize(id);
    uint n = bufferSizeInBytes / bs;
    // parameters: BC1/3/4/5 take 1 (curve) and 2 (prediction mask); BC7 Join takes 1-4
    // (2 / 3: statics, 4: byte remap with table set 1). The escape bit is 0.
    ok = ok && (w0 & GACL_STREAMHEADER_ESCAPE) == 0 && n >= 2 && bufferSizeInBytes % bs == 0 &&
        (presence & (join ? 0u : 0xCu)) == 0;

    // parameters 1..4 follow untagged in ascending id order: 1 byte each, except 8 bytes for id 3
    uint bytePos = 1;
    uint tiles = 0, statics = 0;
    if (presence & 1u)
    {
        tiles = PeekBits32Safe(bytePos << 3, ok) & 0xFFu;
        bytePos++;
        // the GACL_Shuffle_ApplySpaceCurve geometry: a power of two of micro-tile columns (512 bytes of each block
        // row) and of whole block rows, at least 32 of them, over more than one 16KB micro tile
        uint pitch = max(tiles, 1u) * 512u;
        uint rows = bufferSizeInBytes / pitch;
        ok = ok && tiles != 0 && countbits(tiles) == 1 && bufferSizeInBytes > 16384u && rows * pitch == bufferSizeInBytes &&
            countbits(rows) == 1 && rows >= 32u;
    }
    if (presence & 2u)
    {
        statics = PeekBits32Safe(bytePos << 3, ok) & 0xFFu;
        bytePos++;
    }
    if (presence & 4u)
    {
        // parameter 3 (per-mode masks): JoinBuildRows reads it, ParseHeader only checks its bounds
        PeekBits32Safe(bytePos << 3, ok);
        PeekBits32Safe((bytePos + 4u) << 3, ok);
        bytePos += 8u;
    }
    if (presence & 8u)
    {
        // parameter 4 (byte remap set): 1 byte, 1
        uint set = PeekBits32Safe(bytePos << 3, ok) & 0xFFu;
        ok = ok && set == 1u;
        bytePos++;
    }

    // BC1/3/4/5 parameter 2 replaces the default index prediction mask, every bit that applies (1: the BC4-style
    // halves, 2: the color halves), with a different subset of it, optionally plus BCNEX_UNSHUFFLED (BCnEx_Decode)
    uint choices = (id == TRANSFORM_BC1_224 || id == TRANSFORM_BC1_44) ? 2u : ((id == TRANSFORM_BC3_116224 || id == TRANSFORM_BC3_664) ? 3u : 1u);
    uint predictMask = choices;
    if (!join && (presence & 2u) != 0)
    {
        predictMask = statics;
        ok = ok && predictMask != choices && (predictMask & ~(choices | BCNEX_UNSHUFFLED)) == 0;
    }
    // the transform id names the header's format; BC7_EX also takes curve-only BC7 streams: BC5_116116 blocks with
    // parameter 2 exactly BCNEX_UNSHUFFLED (BCnEx_Decode)
    ok = ok && (ExTransformOfCode(code) == transformId ||
        (transformId == TRANSFORM_BC7_EX && code == 6u && predictMask == BCNEX_UNSHUFFLED));

    // k header blocks: removed duplicates, then header-coded blocks (every header transform)
    uint bitPos = bytePos << 3;
    uint k = ReadExpGolomb(bitPos, ok) + 1u;
    uint coded = ReadExpGolomb(bitPos, ok);
    ok = ok && k < n && coded <= k && k <= BCNEX_MAX_HEADER_BLOCKS;
    uint removed = ok ? k - coded : 0;

    uint prev = 0;
    for (uint j = 0; ok && j < removed; j++)
    {
        uint gap = ReadExpGolomb(bitPos, ok);
        uint dist = ReadExpGolomb(bitPos, ok);
        uint r = j == 0 ? gap : prev + 1u + gap;
        uint d = (dist >> 1) + 1u;
        ok = ok && gap < n && dist < 2u * n && r < n && ((dist & 1u) ? d <= r : r + d < n);
        gsRemoved[j] = r;
        gsSources[j] = (dist & 1u) ? r - d : r + d;
        prev = r;
    }

    uint posBits = firstbithigh(n - 1u) + 1u;
    // the coded block positions, read by the wave's lanes together (each lane checks its position is above the
    // previous one, the last of the previous round for lane 0), and the coded blocks before the group's blocks and up
    // to its last one (in main's mapping: T blocks per thread)
#ifdef EX_WIDE_STREAM_BLOCKS
    const bool wide = bufferSizeInBytes / ExIdBlockSize(transformId) >= EX_WIDE_STREAM_BLOCKS;
    uint G = BlocksPerThread(id), T = wide ? G : ExThreadBlocks(id);
#else
    uint G = BlocksPerThread(id), T = ExThreadBlocks(id);
#endif
    uint first = FirstBlockOfThread(groupThread, n, T), next = FirstBlockOfThread(groupThread + TG_THREAD_COUNT, n, T);
    const uint lane = WaveGetLaneIndex(), lanes = WaveGetLaneCount();
    uint before = 0, upTo = 0;
    prev = 0;
    for (uint c0 = 0; ok && c0 < coded; c0 += lanes)
    {
        const uint c = c0 + lane;
        bool lok = true;
        uint idx = 0;
        if (c < coded)
        {
            idx = PeekBits32Safe(bitPos + c * posBits, lok) & ((1u << posBits) - 1u);
        }
        const uint live = min(lanes, coded - c0);
        uint lower = prev;
        for (uint l = 0; l < live; l++)
        {
            const uint v = WaveReadLaneAt(idx, l);
            gsCodedIndex[c0 + l] = v;
            lower = l + 1u == lane ? v : lower;
            prev = v;
        }
        lok = lok && idx < n && (c == 0 || idx > lower) && !IsRemoved(idx, removed);
        ok = ok && WaveActiveAllTrue(c >= coded || lok);
        before += WaveActiveCountBits(c < coded && idx < first);
        upTo += WaveActiveCountBits(c < coded && idx < next);
    }
    // a group decodes the bodies only if it has coded blocks of its own, and then only up to its last one
    uint bodies = before < upTo ? upTo : 0;
    bitPos += coded * posBits;
    uint bodyPos = bitPos;
    // every source is a kept block
    for (uint s0 = 0; ok && s0 < removed; s0 += lanes)
    {
        const uint s = s0 + lane;
        bool sok = true;
        if (s < removed)
        {
            sok = !IsRemoved(gsSources[s], removed) && !IsCoded(gsSources[s], coded);
        }
        ok = WaveActiveAllTrue(sok);
    }
    // A BC1/3/4/5 group without coded blocks of its own, and at most 2 removed duplicates and coded blocks before it,
    // takes main's short-header path: the coded blocks before it join the removed duplicates there (main's payload
    // index i - (entries below i) skips both), and each source becomes the smallest block that main maps to the
    // source's payload index. (With EX_GENERAL_DECODE, none: main's general path needs gsRemoved sorted.)
    uint removedOut = removed, codedOut = coded;
#ifdef EX_GENERAL_DECODE
    if (false)
#else
    if (ok && !join && before == upTo && removed + before <= 2u)
#endif
    {
        uint total = removed + before;
        uint e0 = removed > 0 ? gsRemoved[0] : gsCodedIndex[0];
        uint e1 = removed > 1u ? gsRemoved[1] : gsCodedIndex[1u - removed];
        uint ex = total > 0 ? e0 : ~0u, ey = total > 1u ? e1 : ~0u;
        uint2 sp = 0;
        for (uint p = 0; p < removed; p++)
        {
            uint target = gsSources[p] - RemovedBefore(gsSources[p], removed) - CodedBefore(gsSources[p], coded);
            uint x = target;
            x += x - (x > ex ? 1u : 0u) - (x > ey ? 1u : 0u) < target ? 1u : 0u;
            x += x - (x > ex ? 1u : 0u) - (x > ey ? 1u : 0u) < target ? 1u : 0u;
            if (p == 0)
            {
                sp.x = x;
            }
            else
            {
                sp.y = x;
            }
        }
        // every lane of thread 0's wave runs ParseHeader, with the same values: all have read the sources before any
        // lane overwrites them
        sp = WaveReadLaneFirst(sp);
        gsRemoved[0] = e0;
        gsRemoved[1] = e1;
        gsSources[0] = sp.x;
        gsSources[1] = sp.y;
        removedOut = total;
        codedOut = 0;
    }

    gsTransform = id;
    gsHeaderBlocks = k;
    gsRemovedCount = removedOut;
    gsCodedCount = codedOut;
    gsCurveWidthInPixels = tiles * (bs == 16 ? 128u : 256u);
    gsPredictMask = predictMask;

    // then the bodies, last, with little else live across them (a short-header group has none; a group whose header
    // doesn't fit fails here)
    for (uint b = 0; ok && b < bodies; b++)
    {
        gsCoded[b] = DecodeBody(bodyPos, join, G, bs, ok);
    }
    bitPos = bodyPos;
    // the header fits its k blocks (as far as it was read)
    ok = ok && ((bitPos + 7u) >> 3) <= k * bs;
#ifdef EX_WIDE_STREAM_BLOCKS
    // 2: main maps G blocks per thread
    gsValid = ok ? (wide ? 2u : 1u) : 0u;
#else
    gsValid = ok ? 1u : 0u;
#endif
}

// The 96-bit window r (r.x first, zero-filled) advanced past an Exp-Golomb code of 2b + 1 bits (1 to 31 for b < 16)
uint3 WindowAdvance(uint3 r, uint b)
{
    const uint s = 2u * b + 1u, t = 31u - 2u * b;
    return uint3((r.x >> s) | (r.y << t), (r.y >> s) | (r.z << t), r.z >> s);
}

// Exp-Golomb code v from the low bits of the shifting window r (the header's bits from position pos), r and pos then
// advanced past it: 1 when the code is at most 31 bits long, else 0 (also without a leading one)
uint ShiftExpGolomb(inout uint3 r, inout uint pos, out uint v)
{
    // firstbitlow(0) is ~0u
    const uint w = r.x, b = firstbitlow(w), m = (1u << b) - 1u;
    v = ((w >> (b + 1u)) & m) + m;
    r = WindowAdvance(r, b);
    pos += 2u * b + 1u;
    return uint(b < 16u);
}

// Removed duplicate rem (first: 0, or the previous one + 1, plus the gap) and its source s from the shifting window
// r: 1 when they pass ParseHeader's checks, else 0
uint ShiftRemoved(inout uint3 r, inout uint pos, uint first, uint n, out uint rem, out uint s)
{
    uint gap, dist;
    uint ok = ShiftExpGolomb(r, pos, gap);
    ok &= ShiftExpGolomb(r, pos, dist);
    rem = first + gap;
    const uint d = (dist >> 1) + 1u;
    const bool back = (dist & 1u) != 0;
    s = back ? rem - d : rem + d;
    return ok & uint(gap < n) & uint(dist < 2u * n) & uint(rem < n) & uint(back ? d <= rem : rem + d < n);
}

// ParseHeader's common case from one 16-byte read and registers: a BC1/3/4/5 EX header (of transformId's format) of
// k <= 2 blocks inside the first 16 bytes, holding removed duplicates only, whose codes take at most 96 bits, without
// BCNEX_UNSHUFFLED. It accepts exactly the headers of that shape that ParseHeader accepts, with the same results, and
// returns false for everything else, which ParseHeader then decodes. The bit-serial parse with a load per field is ~1 us of every group's latency (BC5 289 KB VT groups, RTX 5080).
// Branch-free on purpose (checks as 0 / 1 flags, shifts for the divides, the codes from a shifting window, the second
// duplicate decoded either way): with short-circuit && / ?: and a ?:-selected 32-bit window read per code, the parse
// compiled to 48 conditional branches and cost EX groups 5.5-9% of latency (RTX 5080).
bool FastParseHeader()
{
    if (bufferSizeInBytes < 16u)
    {
        return false;
    }
    const uint4 h = srcBuffer.Load4(0);
    const uint code = h.x & 7u;
    // Join headers (code 0: BC7_EX's, FastParseJoinHeaderLoad) declined up front
    [branch]
    if (code == 0)
    {
        return false;
    }
    // kTransforms, BlockSize and the default prediction mask (BC1 2, BC3 3, BC4 / BC5 1) by header transform code
    const uint id = (0xA4392816u >> (code << 2)) & 0xFu;
    const uint bsLog2 = 3u + ((0xD8u >> code) & 1u);
    const uint bs = 1u << bsLog2;
    const uint n = bufferSizeInBytes >> bsLog2;
    const uint choices = (0x57E8u >> (code << 1)) & 3u;
    // the presence bits of parameters 1-4, then the escape bit (bit 4 here)
    const uint presence = (h.x >> 3) & 0x1Fu;
    const uint curve = presence & 1u, predict = (presence >> 1) & 1u;
    // parameters 1 (tiles) and 2 (the prediction mask): 1 byte each, from byte 1
    const uint4 p = HeaderParamWords(h);
    const uint tiles = (p.x & 0xFFu) * curve;
    const uint predictMask = predict ? ((p.x >> (curve << 3)) & 0xFFu) : choices;
    // no parameter 3 or 4 (Join's) and no escape
    uint ok = uint(n >= 2u) & uint((bufferSizeInBytes & (bs - 1u)) == 0) & uint((presence & 0x1Cu) == 0) &
        uint(ExTransformOfCode(code) == transformId);
    // the curve geometry of ParseHeader: with tiles a power of 2, rows = bufferSizeInBytes / (tiles * 512) is a power
    // of 2 of at least 32 exactly when bufferSizeInBytes is a power of 2 of at least tiles * 16384
    ok &= uint(curve == 0) | (uint(countbits(tiles) == 1) & uint(countbits(bufferSizeInBytes) == 1) &
        uint(bufferSizeInBytes > 16384u) & uint(bufferSizeInBytes >= (tiles << 14)));
    ok &= uint(predict == 0) | (uint(predictMask != choices) & uint((predictMask & ~choices) == 0));
    // k - 1 and c: codes "1" "1" (k = 1) or "010" "1" (k = 2, 8-byte blocks), no coded blocks; anything else declined
    const uint sh = (curve + predict) << 3;
    const uint kc = (p.x >> sh) & 0xFu;
    const bool two = kc == 0xAu;
    const uint k = two ? 2u : 1u;
    ok &= two ? uint(bs == 8u) & uint(n > 2u) : uint((kc & 3u) == 3u);
    const uint skip = sh + 2u * k;
    // the window: header bits 8 + skip to 104 + skip, inside the first 16 bytes
    uint pos = 8u + skip;
    uint3 r = uint3((p.x >> skip) | (p.y << (32u - skip)), (p.y >> skip) | (p.z << (32u - skip)),
        (p.z >> skip) | (p.w << (32u - skip)));
    uint r0, s0, r1, s1;
    ok &= ShiftRemoved(r, pos, 0, n, r0, s0);
    // the second removed duplicate (k = 2): every source is a kept block (s0 != r0 and s1 != r1 by construction)
    uint pos2 = pos;
    const uint ok2 = ShiftRemoved(r, pos2, r0 + 1u, n, r1, s1) & uint(s0 != r1) & uint(s1 != r0);
    ok &= two ? ok2 : 1u;
    pos = two ? pos2 : pos;
    r1 = two ? r1 : 0u;
    s1 = two ? s1 : 0u;
    // the codes end inside the header and inside the window (with k = 2, codes of more than 96 bits can end past it)
    ok &= uint(((pos + 7u) >> 3) <= k * bs) & uint(pos <= 104u + skip);
    if (ok)
    {
        gsValid = 1u;
        gsTransform = id;
        gsHeaderBlocks = k;
        gsRemovedCount = k;
        gsCodedCount = 0;
        gsRemoved[0] = r0;
        gsSources[0] = s0;
        gsRemoved[1] = r1;
        gsSources[1] = s1;
        gsCurveWidthInPixels = tiles << (11u - bsLog2);
        gsPredictMask = predictMask;
    }
    return ok != 0;
}

// FastParseHeader's counterpart for BC7_EX Join headers (header transform code 0): a header of 1 block, so inside the
// window, holding one removed duplicate (every Join header of the RDO VT tile groups). It accepts exactly the headers of
// that shape that ParseHeader accepts, with the same results, and returns false for everything else. The masks
// (parameters 2 / 3) are JoinBuildRows'. Join latency -6%, batched -3.5 to -4%; with the masks moved there, BC1/3/4/5
// coded-block latency -5 to -6% (RTX 5080). Branch-free like FastParseHeader, which cut Join latency and batched 1.7%
// (RTX 5080); it stores its results either way, ParseHeader overwriting them when it declines.
bool FastParseJoinHeader(uint4 h, uint presence, uint n)
{
    const uint curve = presence & 1u;
    const uint4 p = HeaderParamWords(h);
    // parameter 1 (tiles): 1 byte from byte 1
    const uint tiles = (p.x & 0xFFu) * curve;
    // presence holds the escape bit above parameter 4's
    uint ok = uint((presence & 0x10u) == 0) & uint(n >= 2u) & uint((bufferSizeInBytes & 15u) == 0);
    // the curve geometry, as in FastParseHeader
    ok &= uint(curve == 0) | (uint(countbits(tiles) == 1) & uint(countbits(bufferSizeInBytes) == 1) &
        uint(bufferSizeInBytes > 16384u) & uint(bufferSizeInBytes >= (tiles << 14)));
    // after parameters 1, 2 (statics: 1 byte) and 3 (per-mode masks: 8 bytes): the window from bit 8 + 8 x (curve +
    // statics) + 64 x masks, i.e. from p.x (no masks) or p.z (masks) at bit 0, 8 or 16. With masks it runs to the
    // block's end; without them, to bit 104, past every code this accepts (two of at most 31 bits after parameter 4,
    // k - 1 and c: they end by bit 80 + 8 x (curve + statics))
    const bool masks = (presence & 4u) != 0;
    const uint a = masks ? p.z : p.x, b = masks ? p.w : p.y, c = masks ? 0u : p.z;
    const uint s = (curve + ((presence >> 1) & 1u)) << 3;
    const uint3 w = uint3((a >> s) | ((b << 1) << (31u - s)), (b >> s) | ((c << 1) << (31u - s)), c >> s);
    // parameter 4 (byte remap set): 1 byte, 1, then k - 1 and c: codes "1" "1" (k = 1, no coded blocks)
    const uint bytes = presence & 8u;
    const uint lead = bytes + 2u;
    ok &= uint((w.x & ((1u << lead) - 1u)) == ((bytes >> 3) | (3u << bytes)));
    uint pos = 8u + (masks ? 64u : 0u) + s + lead;
    uint3 r = uint3((w.x >> lead) | (w.y << (32u - lead)), (w.y >> lead) | (w.z << (32u - lead)), w.z >> lead);
    uint r0, s0;
    ok &= ShiftRemoved(r, pos, 0, n, r0, s0);
    // the codes end inside the header block
    ok &= uint(pos <= 128u);
    gsValid = ok;
    gsTransform = TRANSFORM_BC7_JOIN;
    gsHeaderBlocks = 1u;
    gsRemovedCount = 1u;
    gsCodedCount = 0;
    gsRemoved[0] = r0;
    gsSources[0] = s0;
    gsCurveWidthInPixels = tiles * 128u;
    gsPredictMask = 1u;
    return ok != 0;
}

// The BC7_EX fast parse: FastParseJoinHeader for a Join header (header transform code 0); ParseHeader decodes the rest
// (curve-only BC7 streams among them)
bool FastParseJoinHeaderLoad()
{
    if (bufferSizeInBytes < 32u)
    {
        return false;
    }
    const uint4 h = srcBuffer.Load4(0);
    if ((h.x & 7u) != 0)
    {
        return false;
    }
    return FastParseJoinHeader(h, (h.x >> 3) & 0x1Fu, bufferSizeInBytes >> 4);
}

// Headerless BC7 Join (transform 0x06): no header blocks, no curve, every mask 0 (JoinBuildRows'), no byte remap
void JoinHeaderless()
{
    gsValid = (bufferSizeInBytes % 16u) == 0 ? 1u : 0u;
    gsTransform = TRANSFORM_BC7_JOIN;
    gsHeaderBlocks = 0;
    gsRemovedCount = 0;
    gsCodedCount = 0;
    gsCurveWidthInPixels = 0;
}

// Payload index of block i (not a coded block): a removed duplicate reads its source, and kept blocks are stored
// in order after the header, i.e. block b is payload block b - (removed and coded blocks before b)
uint PayloadIndex(uint i, uint removedCount, uint codedCount)
{
    uint r = RemovedBefore(i, removedCount);
    bool duplicate = false;
    if (r < removedCount)
    {
        duplicate = gsRemoved[r] == i;
    }
    if (duplicate)
    {
        i = gsSources[r];
        r = RemovedBefore(i, removedCount);
    }
    return i - r - CodedBefore(i, codedCount);
}

// ExBlocks' walk of a thread's blocks in order, c and r being the coded blocks and removed duplicates below block i:
// whether block i is the next coded block
bool NextIsCoded(uint i, uint c, uint codedCount)
{
    bool coded = false;
    if (c < codedCount)
    {
        coded = gsCodedIndex[c] == i;
    }
    return coded;
}

// and PayloadIndex of block i (not a coded block), stepping r past it if it is a removed duplicate
uint WalkPayloadIndex(uint i, inout uint r, uint c, uint removedCount)
{
    uint j = i - r - c;
    bool duplicate = false;
    if (r < removedCount)
    {
        duplicate = gsRemoved[r] == i;
    }
    if (duplicate)
    {
        uint s = gsSources[r];
        j = s - RemovedBefore(s, removedCount) - CodedBefore(s, gsCodedCount);
        r++;
    }
    return j;
}

// ---------------------------------------------------------------------------------------------------------------
// Payload gather (Shuffle_BC1/3/4/5 SoA layouts)

uint LoadByteAt(uint a)
{
    return (srcBuffer.Load(a & ~3u) >> ((a & 3u) << 3)) & 0xFFu;
}

// a is 2-byte aligned
uint LoadU16At(uint a)
{
    return (srcBuffer.Load(a & ~3u) >> ((a & 2u) << 3)) & 0xFFFFu;
}

// 6 bytes at 2-byte aligned a: x = bytes 0-3, y = bytes 4-5
uint2 Load48At(uint a)
{
    if (a & 2u)
    {
        uint w0 = srcBuffer.Load(a - 2u), w1 = srcBuffer.Load(a + 2u);
        return uint2((w0 >> 16) | (w1 << 16), w1 >> 16);
    }
    return uint2(srcBuffer.Load(a), srcBuffer.Load(a + 4u) & 0xFFFFu);
}

// BC4-style half block from its endpoints and 48 index bits
uint2 MakeHalf(uint e0, uint e1, uint2 idx)
{
    return uint2(e0 | (e1 << 8) | (idx.x << 16), (idx.x >> 16) | (idx.y << 16));
}

// v2 split color endpoints (12 major and 4 minor bits per 5:6:5 endpoint) to e0 | e1 << 16
uint UnsplitEndpoints(uint eo)
{
    uint e0 = (((eo >> 28) & 0xFu) << 12) | (((eo >> 24) & 0xFu) << 7) | (((eo >> 20) & 0xFu) << 1) |
              (((eo >> 7) & 0x1u) << 11) | (((eo >> 5) & 0x3u) << 5) | ((eo >> 4) & 0x1u);
    uint e1 = (((eo >> 16) & 0xFu) << 12) | (((eo >> 12) & 0xFu) << 7) | (((eo >> 8) & 0xFu) << 1) |
              (((eo >> 3) & 0x1u) << 11) | (((eo >> 1) & 0x3u) << 5) | (eo & 0x1u);
    return e0 | (e1 << 16);
}

// BC3_664 block from its nibble-split alpha + split color endpoints field d and its alpha and color indices
uint4 MakeBC3_664(uint2 d, uint2 alphaIndices, uint colourIndices)
{
    uint d0 = d.x & 0xFFu, d1 = (d.x >> 8) & 0xFFu;
    uint a0 = (d1 & 0xF0u) | (d0 >> 4);
    uint a1 = ((d1 & 0x0Fu) << 4) | (d0 & 0x0Fu);
    return uint4(MakeHalf(a0, a1, alphaIndices),
        UnsplitEndpoints((((d.x >> 16) & 0xFFu) << 24) | ((d.x >> 24) << 16) | ((d.y & 0xFFu) << 8) | (d.y >> 8)), colourIndices);
}

// Block j of a payload at byte address base, q blocks of which are shuffled into S = q * bs bytes and followed by
// the stragglers as is (BCn_ReadShuffledBlock)
uint4 ReadShuffledBlock(uint id, uint base, uint q, uint bs, uint j)
{
    uint S = q * bs;
    if (j >= q)
    {
        uint a = base + S + (j - q) * bs;
        return bs == 16 ? srcBuffer.Load4(a) : uint4(srcBuffer.Load2(a), 0, 0);
    }

    uint4 b = 0;
    if (id == TRANSFORM_BC1_224)            // e0 1/4, e1 1/4, indices 1/2
    {
        b.x = LoadU16At(base + 2 * j) | (LoadU16At(base + S / 4 + 2 * j) << 16);
        b.y = srcBuffer.Load(base + S / 2 + 4 * j);
    }
    else if (id == TRANSFORM_BC1_44)        // split endpoints 1/2, indices 1/2
    {
        b.x = UnsplitEndpoints(srcBuffer.Load(base + 4 * j));
        b.y = srcBuffer.Load(base + S / 2 + 4 * j);
    }
    else if (id == TRANSFORM_BC3_116224)    // a0 1/16, a1 1/16, alpha indices 6/16, e0 2/16, e1 2/16, color indices 4/16
    {
        b.xy = MakeHalf(LoadByteAt(base + j), LoadByteAt(base + q + j), Load48At(base + 2 * q + 6 * j));
        b.z = LoadU16At(base + 8 * q + 2 * j) | (LoadU16At(base + 10 * q + 2 * j) << 16);
        b.w = srcBuffer.Load(base + 12 * q + 4 * j);
    }
    else if (id == TRANSFORM_BC3_664)       // nibble-split alpha + split color endpoints 6/16, alpha indices 6/16, color indices 4/16
    {
        b = MakeBC3_664(Load48At(base + 6 * j), Load48At(base + 6 * q + 6 * j), srcBuffer.Load(base + 12 * q + 4 * j));
    }
    else if (id == TRANSFORM_BC4_116)       // r0 1/8, r1 1/8, indices 6/8
    {
        b.xy = MakeHalf(LoadByteAt(base + j), LoadByteAt(base + q + j), Load48At(base + 2 * q + 6 * j));
    }
    else if (id == TRANSFORM_BC5_116116)    // r0 1/16, r1 1/16, red indices 6/16, g0 1/16, g1 1/16, green indices 6/16
    {
        b.xy = MakeHalf(LoadByteAt(base + j), LoadByteAt(base + q + j), Load48At(base + 2 * q + 6 * j));
        b.zw = MakeHalf(LoadByteAt(base + 8 * q + j), LoadByteAt(base + 9 * q + j), Load48At(base + 10 * q + 6 * j));
    }
    else if (id == TRANSFORM_BC5_2266)      // r0 r1 2/16, g0 g1 2/16, red indices 6/16, green indices 6/16
    {
        uint r01 = LoadU16At(base + 2 * j), g01 = LoadU16At(base + 2 * q + 2 * j);
        b.xy = MakeHalf(r01 & 0xFFu, r01 >> 8, Load48At(base + 4 * q + 6 * j));
        b.zw = MakeHalf(g01 & 0xFFu, g01 >> 8, Load48At(base + 10 * q + 6 * j));
    }
    return b;
}

// ---------------------------------------------------------------------------------------------------------------
// Index prediction inverse (BCnEx_UnpredictHalf)

// gsUnfoldZ[8 z + p] (entry e = 8 z + p): the value of residual z around prediction p over [0, 7], which is p, p - 1,
// p + 1, p - 2, ... while both sides fit (z <= 2 min(p, 7 - p)), then the remaining side's values. Read from
// groupshared in the loop, one entry per word: the per-position ALU inverse and a dynamically indexed constant cost
// ~1.4x / ~2x the batched time of predicted BC5 on an RTX 5080.
uint UnfoldZEntry(uint e)
{
    const uint z = e >> 3, p = e & 7u;
    return z > 2u * min(p, p ^ 7u) ? z ^ ((p >> 2) * 7u) : p + ((z >> 1) ^ (0u - (z & 1u)));
}

// Ramp position -> index on 3-bit fields packed at the set bits of ones (SWAR): v + 1 with bit 0 flipped where
// v = 0 or 7 is the 8-value mode's { 0, 2, 3, 4, 5, 6, 7, 1 }; the 6-value mode's { 0, 2, 3, 4, 5, 1, 6, 7 } differs
// from it by 7 / 1 / 6 for v = 5 / 6 / 7
uint MapRamp(uint w, uint ones, bool six)
{
    const uint s1 = w >> 1, s2 = w >> 2;
    const uint o6 = six ? ones : 0u;
    const uint inc = ((w & (ones * 3u)) + ones) ^ (w & (ones << 2));
    return inc ^ (~((w ^ s1) | (w ^ s2)) & ones) ^ (s2 & (w ^ s1) & o6) ^ ((s2 & w & o6) * 6u);
}

// Index prediction inverse of a BC4-style half: x = the endpoints (low 16 bits) and index bits 0-15, y = index bits
// 16-47 (index bit k is bit 16 + k of h.x:h.y). Branch free: the causal prediction (BCnEx_PredictHalf's Pred) is MED =
// clamp(l + t - tl, min(l, t), max(l, t)) inside the block, left / top at index 1 / 4, clamp(2a - b) further along the
// first row / column, inverted through gsUnfoldZ. The residuals are read times 8 (the table's row), and the ramp
// positions are added in place and mapped to indices with MapRamp: RTX 5080, against a 4-bit-field table row per
// prediction and a ramp-to-index table lookup per index, batched predicted BC5 -12% (single dispatch -5%), BC3 -7%.
uint2 UnpredictHalf(uint2 h)
{
    uint v[16];
    v[0] = (h.x >> 16) & 7u;
    uint A = v[0];      // ramp positions 0-9 at bits 0-29
    uint B = 0;         // ramp positions 10-15 at bits 14-31
    [unroll]
    for (uint j = 1; j < 16; j++)
    {
        // residual j times 8, from bits 3 j + 16 to 3 j + 18 of h.x:h.y
        const uint bit = 3u * j + 16u;
        uint z8;
        if (bit + 3u <= 32u)
        {
            z8 = (h.x >> (bit - 3u)) & 0x38u;
        }
        else if (bit >= 35u)
        {
            z8 = (h.y >> (bit - 35u)) & 0x38u;
        }
        else if (bit >= 32u)
        {
            z8 = (h.y << (35u - bit)) & 0x38u;
        }
        else
        {
            z8 = ((h.x >> (bit - 3u)) | (h.y << (35u - bit))) & 0x38u;
        }
        const uint x = j & 3u, r = j >> 2;
        int p;
        if (x != 0 && r != 0)
        {
            const int l = int(v[j - 1]), t = int(v[j - 4]);
            p = min(max(l + t - int(v[j - 5]), min(l, t)), max(l, t));
        }
        else if ((x | r) < 2u)
        {
            p = int(x != 0 ? v[j - 1] : v[j - 4]);
        }
        else
        {
            const int a = int(x != 0 ? v[j - 1] : v[j - 4]);
            p = clamp(2 * a - int(x != 0 ? v[j - 2] : v[j - 8]), 0, 7);
        }
        v[j] = gsUnfoldZ[z8 | uint(p)];
        if (j < 10u)
        {
            A += v[j] << (3u * j);
        }
        else
        {
            B += v[j] << (3u * j - 16u);
        }
    }
    const bool six = (h.x & 0xFFu) <= ((h.x >> 8) & 0xFFu);
    A = MapRamp(A, 0x09249249u, six);
    B = MapRamp(B, 0x24924000u, six);
    return uint2((h.x & 0xFFFFu) | (A << 16), (A >> 16) | B);
}

// Color index prediction inverse (UnpredictColourHalf in bcnex.cpp) of a BC1-style half: x = the 5:6:5 endpoints c0
// (low 16 bits) and c1, y = sixteen 2-bit indices. Branch-free: ramp positions are kept times 8 so that they are
// shift amounts into 2-bit tables shifted left 3, kUnfold8 (bounded zigzag inverse over [0, 3]: byte p for
// prediction p, entry z) and kExtrap8 (3 minus the first row / column extrapolation: byte a, entry b for the last
// two positions a and b). Residual j enters the shift amount at bits 1-2 from h.y masked to every other index,
// whose bits above it wrap out of the 5-bit shift amount. Ramp position -> index on all 16 packed positions:
// four-color mode (BC3, or c0 > c1) { 0, 2, 3, 1 } = (H ^ L, H), three-color mode { 0, 2, 1, 3 } = (L, H).
static const uint kUnfold8 = 0x1B36E1E4u << 3;
static const uint kExtrap8 = 0x0054EAFFu << 3;

uint2 UnpredictColourHalf(uint2 h, bool fourColour)
{
    uint ye = h.y & 0x33333333u, yo = h.y & 0xCCCCCCCCu;
    uint v[16];
    v[0] = (h.y << 3) & 0x18u;
    uint w = h.y & 3u;
    [unroll]
    for (uint j = 1; j < 16; j++)
    {
        uint x = j & 3u, r = j >> 2;
        uint p;
        if (x != 0 && r != 0)
        {
            // MED = l + t - median(l, t, tl)
            uint l = v[j - 1], t = v[j - 4], tl = v[j - 5];
            p = l + t - max(min(l, t), min(max(l, t), tl));
        }
        else
        {
            uint a = x != 0 ? v[j - 1] : v[j - 4];
            p = (x | r) < 2u ? a : 24u - ((kExtrap8 >> (a + ((x != 0 ? v[j - 2] : v[j - 8]) >> 2))) & 0x18u);
        }
        v[j] = (kUnfold8 >> (p + (((j & 1u) ? yo : ye) >> (2u * j - 1u)))) & 0x18u;
        w |= j == 1u ? v[1] >> 1 : v[j] << (2u * j - 3u);
    }
    uint s = w >> 1;
    bool four = fourColour || (h.x & 0xFFFFu) > (h.x >> 16);
    return uint2(h.x, (((four ? s ^ w : w) & 0x55555555u) << 1) | (s & 0x55555555u));
}

// ---------------------------------------------------------------------------------------------------------------

// ReverseSpaceCurveFor16ByteBlock (SharedDefinitions.hlsli), also for 8-byte blocks (64x32 block micro tiles), for
// the power-of-two geometries ParseHeader accepts, splitting the z-order tile id with a branch-free Morton decode
// rather than parallelExtract's per-bit loop (which runs ~27 times for square BC1 / BC4 textures: their 64x32 block
// micro tiles leave twice as many tile rows as columns)
uint CurveDestBlock(uint blockId, uint bs, uint widthInPixels)
{
    uint rowShift = bs == 16 ? 5u : 6u;                         // log2 of the micro-tile width in blocks; 32 rows
    uint widthInBlocks = widthInPixels >> 2;
    uint logW = firstbitlow(widthInBlocks) - rowShift;          // log2 of the width and height in micro tiles
    uint logH = firstbitlow(bufferSizeInBytes) - 14u - logW;
    uint k = min(logW, logH);                                   // <= 7: the curve parameter is a byte
    uint tileId = blockId >> (rowShift + 5u);
    // the low 2k tile id bits interleave x (odd) and y (even); the rest belong to the longer side
    uint2 v = uint2(tileId >> 1, tileId) & (((1u << (2u * k)) - 1u) & 0x55555555u);
    v = (v | (v >> 1)) & 0x33333333u;
    v = (v | (v >> 2)) & 0x0F0F0F0Fu;
    v = (v | (v >> 4)) & 0x00FF00FFu;
    uint high = (tileId >> (2u * k)) << k;
    v += logW > logH ? uint2(high, 0) : uint2(0, high);
    uint inTile = blockId & ((1u << (rowShift + 5u)) - 1u);
    return ((v.y << 5) + (inTile >> rowShift)) * widthInBlocks + (v.x << rowShift) + (inTile & ((1u << rowShift) - 1u));
}

// Whole-word loads of a pair's / quad's packed fields from the 4-byte aligned address a. A field run that starts
// misaligned is funnel-shifted into place from one more word, which is always in the stream: the run then also ends
// misaligned, short of the (4-byte aligned) stream end.

// 4 bytes from byte u of the word at a
uint LoadBytes4(uint a, uint u)
{
    if (u == 0)
    {
        return srcBuffer.Load(a);
    }
    uint2 w = srcBuffer.Load2(a);
    return (w.x >> (8u * u)) | (w.y << (32u - 8u * u));
}

// 8 bytes from byte 2 * o of the word at a
uint2 LoadBytes8(uint a, uint o)
{
    if (o == 0)
    {
        return srcBuffer.Load2(a);
    }
    uint3 w = srcBuffer.Load3(a);
    return uint2((w.x >> 16) | (w.y << 16), (w.y >> 16) | (w.z << 16));
}

// 4 consecutive 6-byte fields from byte 2 * o of the word at a
void LoadFields6x4(uint a, uint o, out uint2 f[4])
{
    uint3 x, y;
    if (o == 0)
    {
        x = srcBuffer.Load3(a);
        y = srcBuffer.Load3(a + 12u);
    }
    else
    {
        uint4 v = srcBuffer.Load4(a);
        uint3 w = srcBuffer.Load3(a + 16u);
        x = uint3((v.x >> 16) | (v.y << 16), (v.y >> 16) | (v.z << 16), (v.z >> 16) | (v.w << 16));
        y = uint3((v.w >> 16) | (w.x << 16), (w.x >> 16) | (w.y << 16), (w.y >> 16) | (w.z << 16));
    }
    f[0] = uint2(x.x, x.y & 0xFFFFu);
    f[1] = uint2((x.y >> 16) | (x.z << 16), x.z >> 16);
    f[2] = uint2(y.x, y.y & 0xFFFFu);
    f[3] = uint2((y.y >> 16) | (y.z << 16), y.z >> 16);
}

// byte / 16-bit lane g of a group's packed field
uint ByteOf(uint w, uint g)
{
    return (w >> (8u * g)) & 0xFFu;
}

uint HalfOf(uint2 w, uint g)
{
    return (w[g >> 1] >> (16u * (g & 1u))) & 0xFFFFu;
}

// Blocks j0 to j0 + G - 1 of an EX payload at byte address base, q blocks of which are shuffled (j0 + G <= q),
// gathered as in UnshuffleHeaderless: whole-word loads of each packed field, block j0 + g taking lane g. Each field
// run starts 4-byte aligned, so u = j0 & 3 sets the fields' misalignment: each removed duplicate before the pair /
// quad shifts its j0 down by one.
void GatherBlocks(uint id, uint base, uint q, uint j0, uint u, out uint4 b[4])
{
    uint o = u & 1u;
    // aligned-down offsets of the 1-, 2- and 6-byte field runs
    uint j = j0 - u, h = 2u * (j0 - o), s = 6u * j0 - 2u * o;
    uint2 x[4], y[4];
    b[0] = 0;
    b[1] = 0;
    b[2] = 0;
    b[3] = 0;
    if (id == TRANSFORM_BC1_224)            // e0 1/4, e1 1/4, indices 1/2
    {
        uint e0 = LoadBytes4(base + h, 2u * o), e1 = LoadBytes4(base + 2u * q + h, 2u * o);
        uint2 i = srcBuffer.Load2(base + 4u * q + 4u * j0);
        b[0].xy = uint2((e0 & 0xFFFFu) | (e1 << 16), i.x);
        b[1].xy = uint2((e0 >> 16) | (e1 & 0xFFFF0000u), i.y);
    }
    else if (id == TRANSFORM_BC1_44)        // split endpoints 1/2, indices 1/2
    {
        uint2 e = srcBuffer.Load2(base + 4u * j0), i = srcBuffer.Load2(base + 4u * q + 4u * j0);
        b[0].xy = uint2(UnsplitEndpoints(e.x), i.x);
        b[1].xy = uint2(UnsplitEndpoints(e.y), i.y);
    }
    else if (id == TRANSFORM_BC4_116)       // r0 1/8, r1 1/8, indices 6/8
    {
        uint r0 = LoadBytes4(base + j, u), r1 = LoadBytes4(base + q + j, u);
        LoadFields6x4(base + 2u * q + s, o, x);
        [unroll]
        for (uint g = 0; g < 4; g++)
        {
            b[g].xy = MakeHalf(ByteOf(r0, g), ByteOf(r1, g), x[g]);
        }
    }
    else if (id == TRANSFORM_BC3_116224)    // a0 1/16, a1 1/16, alpha indices 6/16, e0 2/16, e1 2/16, color indices 4/16
    {
        uint a0 = LoadBytes4(base + j, u), a1 = LoadBytes4(base + q + j, u);
        uint2 e0 = LoadBytes8(base + 8u * q + h, o), e1 = LoadBytes8(base + 10u * q + h, o);
        uint4 c = srcBuffer.Load4(base + 12u * q + 4u * j0);
        LoadFields6x4(base + 2u * q + s, o, x);
        [unroll]
        for (uint g = 0; g < 4; g++)
        {
            b[g] = uint4(MakeHalf(ByteOf(a0, g), ByteOf(a1, g), x[g]), HalfOf(e0, g) | (HalfOf(e1, g) << 16), c[g]);
        }
    }
    else if (id == TRANSFORM_BC3_664)       // nibble-split alpha + split color endpoints 6/16, alpha indices 6/16, color indices 4/16
    {
        uint4 c = srcBuffer.Load4(base + 12u * q + 4u * j0);
        LoadFields6x4(base + s, o, x);
        LoadFields6x4(base + 6u * q + s, o, y);
        [unroll]
        for (uint g = 0; g < 4; g++)
        {
            b[g] = MakeBC3_664(x[g], y[g], c[g]);
        }
    }
    else if (id == TRANSFORM_BC5_116116)    // r0 1/16, r1 1/16, red indices 6/16, g0 1/16, g1 1/16, green indices 6/16
    {
        uint r0 = LoadBytes4(base + j, u), r1 = LoadBytes4(base + q + j, u);
        uint g0 = LoadBytes4(base + 8u * q + j, u), g1 = LoadBytes4(base + 9u * q + j, u);
        LoadFields6x4(base + 2u * q + s, o, x);
        LoadFields6x4(base + 10u * q + s, o, y);
        [unroll]
        for (uint g = 0; g < 4; g++)
        {
            b[g] = uint4(MakeHalf(ByteOf(r0, g), ByteOf(r1, g), x[g]), MakeHalf(ByteOf(g0, g), ByteOf(g1, g), y[g]));
        }
    }
    else if (id == TRANSFORM_BC5_2266)      // r0 r1 2/16, g0 g1 2/16, red indices 6/16, green indices 6/16
    {
        uint2 r = LoadBytes8(base + h, o), gr = LoadBytes8(base + 2u * q + h, o);
        LoadFields6x4(base + 4u * q + s, o, x);
        LoadFields6x4(base + 10u * q + s, o, y);
        [unroll]
        for (uint g = 0; g < 4; g++)
        {
            uint r01 = HalfOf(r, g), g01 = HalfOf(gr, g);
            b[g] = uint4(MakeHalf(r01 & 0xFFu, r01 >> 8, x[g]), MakeHalf(g01 & 0xFFu, g01 >> 8, y[g]));
        }
    }
}

void StoreBlock(uint bs, uint j, uint4 b)
{
    uint a = bufferOffsetInBytes + j * bs;
    if (bs == 16)
    {
        dstBuffer.Store4(a & ~15u, b);
    }
    else
    {
        dstBuffer.Store2(a & ~7u, b.xy);
    }
}

// The headerless BC1/3/4/5 shuffles (0x01-0x04, 0x08-0x0A) on their own short path: no header, prediction or curve.
// As in the legacy shaders, a thread gathers its G blocks' fields with whole-word loads (q is a multiple of 4, so all
// are aligned) and block f + g takes lane g of each; the stragglers follow the q shuffled blocks as is. Per-block
// gathers (ReadShuffledBlock) ran 1.1-1.2x slower here (BC5 289 KB VT groups, RTX 5080), and GatherBlocks with one
// generic store loop up to 1.3x slower.
void UnshuffleHeaderless(uint t)
{
    uint id = transformId;
    uint bs = BlockSize(id);
    if (bs == 0)
    {
        return;
    }
    uint G = BlocksPerThread(id);
    uint n = bufferSizeInBytes / bs;
    uint groups = n / G, q = groups * G;
    if (t >= groups)
    {
        uint j = q + (t - groups);
        if (j < n)
        {
            StoreBlock(bs, j, bs == 16 ? srcBuffer.Load4(j * 16u) : uint4(srcBuffer.Load2(j * 8u), 0, 0));
        }
        return;
    }

    uint f = G * t;
    uint2 x[4], y[4];
    if (id == TRANSFORM_BC1_224)            // e0 1/4, e1 1/4, indices 1/2
    {
        uint e0 = srcBuffer.Load(2u * f), e1 = srcBuffer.Load(2u * q + 2u * f);
        uint2 i = srcBuffer.Load2(4u * q + 4u * f);
        StoreBlock(8u, f, uint4((e0 & 0xFFFFu) | (e1 << 16), i.x, 0, 0));
        StoreBlock(8u, f + 1u, uint4((e0 >> 16) | (e1 & 0xFFFF0000u), i.y, 0, 0));
    }
    else if (id == TRANSFORM_BC1_44)        // split endpoints 1/2, indices 1/2
    {
        uint2 e = srcBuffer.Load2(4u * f), i = srcBuffer.Load2(4u * q + 4u * f);
        StoreBlock(8u, f, uint4(UnsplitEndpoints(e.x), i.x, 0, 0));
        StoreBlock(8u, f + 1u, uint4(UnsplitEndpoints(e.y), i.y, 0, 0));
    }
    else if (id == TRANSFORM_BC4_116)       // r0 1/8, r1 1/8, indices 6/8
    {
        uint r0 = srcBuffer.Load(f), r1 = srcBuffer.Load(q + f);
        LoadFields6x4(2u * q + 6u * f, 0, x);
        [unroll]
        for (uint g = 0; g < 4; g++)
        {
            StoreBlock(8u, f + g, uint4(MakeHalf(ByteOf(r0, g), ByteOf(r1, g), x[g]), 0, 0));
        }
    }
    else if (id == TRANSFORM_BC3_116224)    // a0 1/16, a1 1/16, alpha indices 6/16, e0 2/16, e1 2/16, color indices 4/16
    {
        uint a0 = srcBuffer.Load(f), a1 = srcBuffer.Load(q + f);
        uint2 e0 = srcBuffer.Load2(8u * q + 2u * f), e1 = srcBuffer.Load2(10u * q + 2u * f);
        uint4 c = srcBuffer.Load4(12u * q + 4u * f);
        LoadFields6x4(2u * q + 6u * f, 0, x);
        [unroll]
        for (uint g = 0; g < 4; g++)
        {
            StoreBlock(16u, f + g, uint4(MakeHalf(ByteOf(a0, g), ByteOf(a1, g), x[g]), HalfOf(e0, g) | (HalfOf(e1, g) << 16), c[g]));
        }
    }
    else if (id == TRANSFORM_BC3_664)       // nibble-split alpha + split color endpoints 6/16, alpha indices 6/16, color indices 4/16
    {
        uint4 c = srcBuffer.Load4(12u * q + 4u * f);
        LoadFields6x4(6u * f, 0, x);
        LoadFields6x4(6u * q + 6u * f, 0, y);
        [unroll]
        for (uint g = 0; g < 4; g++)
        {
            StoreBlock(16u, f + g, MakeBC3_664(x[g], y[g], c[g]));
        }
    }
    else if (id == TRANSFORM_BC5_116116)    // r0 1/16, r1 1/16, red indices 6/16, g0 1/16, g1 1/16, green indices 6/16
    {
        uint r0 = srcBuffer.Load(f), r1 = srcBuffer.Load(q + f), g0 = srcBuffer.Load(8u * q + f), g1 = srcBuffer.Load(9u * q + f);
        LoadFields6x4(2u * q + 6u * f, 0, x);
        LoadFields6x4(10u * q + 6u * f, 0, y);
        [unroll]
        for (uint g = 0; g < 4; g++)
        {
            StoreBlock(16u, f + g, uint4(MakeHalf(ByteOf(r0, g), ByteOf(r1, g), x[g]), MakeHalf(ByteOf(g0, g), ByteOf(g1, g), y[g])));
        }
    }
    else if (id == TRANSFORM_BC5_2266)      // r0 r1 2/16, g0 g1 2/16, red indices 6/16, green indices 6/16
    {
        uint2 r = srcBuffer.Load2(2u * f), gr = srcBuffer.Load2(2u * q + 2u * f);
        LoadFields6x4(4u * q + 6u * f, 0, x);
        LoadFields6x4(10u * q + 6u * f, 0, y);
        [unroll]
        for (uint g = 0; g < 4; g++)
        {
            uint r01 = HalfOf(r, g), g01 = HalfOf(gr, g);
            StoreBlock(16u, f + g, uint4(MakeHalf(r01 & 0xFFu, r01 >> 8, x[g]), MakeHalf(g01 & 0xFFu, g01 >> 8, y[g])));
        }
    }
}

// BC7 Join, one block per thread: a counting sort of the group's block indices by mode in groupshared memory before
// the decode, so that the waves decode one mode each (but at the mode boundaries) instead of diverging on every mode
// among their blocks. RTX 5080, against a per-thread decode with the rows built during the header parse: single-
// dispatch latency -8%, batched time -20 to -22%. Building the rows during the header parse is up to 7% more of the Join
// gain, but costs EX 3-6% of its latency; sorting the blocks themselves (5 KB more groupshared memory, over ~8 KB in
// all) drops the occupancy from 6 to 5 groups per SM, costing the other paths 17-56% of batched throughput. Through code
// layout alone, this costs headerless BC4 ~20% of batched throughput (still under the legacy shader's). The rank comes
// from a ballot key match (one groupshared atomic round trip per wave, not one per key in the wave), and only the modes
// in the group build their T rows, after the count: against building every mode's rows while the modes load, with an
// atomic per key, latency -6%, batched time -8 to -11% (most masks are of modes a group lacks, and building the rows of
// Modes 1/3/7, four rows each, costs a wave four divergent constant table reads).
groupshared uint gsSortCount[10];                   // blocks of each key: the mode, 8 reserved, 9 past the stream end
groupshared uint gsSortSrc[TG_THREAD_COUNT];        // the group index of the block at each sorted position

// The mask of Mode gi >> 5 (any wave size), for its T rows (JoinBuildMode), from the masks in the stream: parameters 2
// (1 byte) and 3 (8 bytes) at byte 1 + (parameter 1 present), as ParseHeader reads them; lane 0 also stores the mode's
// mask for JoinDecodeBlock (gsJoinMask: read after JoinSortedDecode's barriers), which spares every header parse a mask
// loop. Returns whether the blocks are byte-remapped (parameter 4). The flag lives in a register rather than groupshared
// memory: with the latter, the EX paths' code layout cost them up to 4.5% of latency (BC1 coded blocks; headerless
// +2%, RTX 5080).
bool JoinReadMasks(uint gi, out uint modeMask)
{
    bool remap = false;
    bool perMode = false;
    uint2 modeMasks = 0;
    uint statics = 0;
    if (transformId == TRANSFORM_BC7_EX && bufferSizeInBytes >= 16u)
    {
        const uint4 h = srcBuffer.Load4(0);
        const uint presence = (h.x >> 3) & 0xFu;
        remap = (presence & 8u) != 0;
        const uint3 b = HeaderParamWords(h).xyz;
        const uint p2 = (presence & 1u) * 8u;       // bit offset of parameter 2 in bytes 1..12
        if (presence & 2u)
        {
            statics = (b.x >> p2) & 0xFFu;
        }
        const uint p3 = p2 + (presence & 2u) * 4u;  // bit offset of parameter 3
        perMode = (presence & 4u) != 0;
        if (perMode)
        {
            modeMasks = p3 ? uint2((b.x >> p3) | (b.y << (32u - p3)), (b.y >> p3) | (b.z << (32u - p3))) : b.xy;
        }
    }
    const uint m = gi >> 5;
    const uint mask = perMode ? (m < 4u ? modeMasks.x : modeMasks.y) >> ((m & 3u) << 3) : statics;
    if ((gi & 31u) == 0)
    {
        gsJoinMask[m] = mask & 0xFFu;
    }
    modeMask = mask;
    return remap;
}

void JoinSortedDecode(uint t, uint gi, uint n, uint k, uint removedCount, uint codedCount, uint curveWidthInPixels)
{
    // the sort key from the block's last dword, loaded during the mask read (the block is loaded again after the sort)
    uint key = 9u;
    uint w = 0;
    if (t < n)
    {
        if (IsCoded(t, codedCount))
        {
            w = gsCoded[CodedBefore(t, codedCount)].w;
        }
        else
        {
            w = srcBuffer.Load(k * 16u + 16u * PayloadIndex(t, removedCount, codedCount) + 12u);
        }
    }
    uint modeMask;
    const bool remap = JoinReadMasks(gi, modeMask);
    if (t < n)
    {
        key = firstbitlow((w >> 24) | 0x100u);
    }

    // rank among the group's blocks of the same key: the wave's lanes of each key matched by 4 ballots (keys < 16), and
    // the wave's count of each key added by the key's first lane, the keys' groupshared atomics in one instruction
    uint4 same = WaveActiveBallot(true);
    [unroll]
    for (uint kb = 0; kb < 4u; kb++)
    {
        const bool set = ((key >> kb) & 1u) != 0u;
        const uint4 lanes = WaveActiveBallot(set);
        same &= set ? lanes : ~lanes;
    }
    const uint lane = WaveGetLaneIndex();
    uint below = 0, count = 0;
    [unroll]
    for (uint q = 0; q < 4u; q++)
    {
        const uint first = 32u * q;
        const uint lower = lane >= first + 32u ? ~0u : (lane > first ? (1u << (lane - first)) - 1u : 0u);
        below += countbits(same[q] & lower);
        count += countbits(same[q]);
    }
    const uint leader = same.x != 0u ? firstbitlow(same.x) : (same.y != 0u ? 32u + firstbitlow(same.y) :
                        (same.z != 0u ? 64u + firstbitlow(same.z) : 96u + firstbitlow(same.w)));
    uint before = 0;
    if (below == 0u)
    {
        InterlockedAdd(gsSortCount[key], count, before);
    }
    const uint rank = WaveReadLaneAt(before, leader) + below;
    GroupMemoryBarrierWithGroupSync();
    // the blocks of the lower keys (gsSortCount at wave-uniform addresses), then the rank
    uint slot = rank;
    [unroll]
    for (uint b = 0; b < 9u; b++)
    {
        slot += b < key ? gsSortCount[b] : 0u;
    }
    gsSortSrc[slot] = gi;
    const uint total = TG_THREAD_COUNT - gsSortCount[9];
    // the T rows of the modes in the group
    if (gsSortCount[gi >> 5] != 0u)
    {
        JoinBuildMode(gi & 31u, gi >> 5, modeMask);
    }
    GroupMemoryBarrierWithGroupSync();
    // whole waves decode (the bit gather's wave shuffles), the lanes past the group's blocks with zeros
    if (WaveActiveAnyTrue(gi < total))
    {
        const bool valid = gi < total;
        const uint u = t - gi + gsSortSrc[valid ? gi : 0u];
        uint4 s = 0;
        bool coded = false;
        if (valid)
        {
            coded = IsCoded(u, codedCount);
            if (coded)
            {
                s = gsCoded[CodedBefore(u, codedCount)];
            }
            else
            {
                s = srcBuffer.Load4(k * 16u + 16u * PayloadIndex(u, removedCount, codedCount));
            }
        }
        // coded blocks are always byte-remapped (ReadCodedBlock)
        const uint4 x = JoinDecodeBlock(s, remap || coded);
        if (valid)
        {
            const uint dstBlock = curveWidthInPixels ? CurveDestBlock(u, 16u, curveWidthInPixels) : u;
            dstBuffer.Store4((bufferOffsetInBytes + dstBlock * 16u) & ~15u, x);
        }
    }
}

// The EX blocks of one thread (a pair / quad, or a straggler) of one format. main calls it per format with literal
// block size bs and G, the format's transform ids (fid: a literal, or a select of its two) and the predictions it can
// carry (ParseHeader rejects mask bits outside them): halves (the BC4-style halves of BC3 alpha, BC4 and BC5 red, and
// with high BC5 green) and colour (the BC1-style color halves of BC1, and of BC3, always four-color). Each inlined
// copy then holds only its format's gathers and unpredictions: 3-15% lower EX latency than one loop for every
// format, at the same occupancy. Unpredicting several blocks at once (interleaved for ILP) takes more than the ~40
// registers / thread of 6 groups / SM instead, and at 5 costs 5-50% of batched throughput (RTX 5080).
void ExBlocks(uint fid, uint bs, uint G, bool halves, bool high, bool colour, uint base, uint q, uint first, uint count,
              uint dstBlock, bool shortHeader, uint removedCount, uint codedCount, uint2 rem, uint2 src)
{
    // a whole pair / quad of a short header with no removed duplicate in it (nearly all): its payload blocks j0.. are
    // consecutive and gathered together
    uint4 gathered[4];
    bool whole = false;
    uint j0 = 0;
    // a general header: the removed duplicates and coded blocks before the thread's next block
    uint removedBelow = 0, codedBelow = 0;
    if (shortHeader && count == G && rem.x - first >= G && rem.y - first >= G)
    {
        j0 = first - (first > rem.x ? 1u : 0u) - (first > rem.y ? 1u : 0u);
        whole = j0 + G <= q;
#ifdef EX_BLOCKS_PER_THREAD
        // the gathers are of the format's whole pairs / quads
        whole = whole && G == BlocksPerThread(fid);
#endif
    }
    else if (!shortHeader)
    {
        removedBelow = RemovedBefore(first, removedCount);
        codedBelow = CodedBefore(first, codedCount);
    }
    // chosen per wave: a wave mixing both kinds would run both paths (chosen per thread, up to 1.06x latency)
    whole = WaveActiveAllTrue(whole);
    if (whole)
    {
        GatherBlocks(fid, base, q, j0, j0 & 3u, gathered);
    }

    [unroll]
    for (uint g = 0; g < 4; g++)
    {
        if (g < count)
        {
            // a coded block comes from the header, still predicted
            uint4 b;
            uint i = first + g;
            if (whole)
            {
                b = gathered[g];
            }
            else if (!shortHeader && NextIsCoded(i, codedBelow, codedCount))
            {
                b = gsCoded[codedBelow];
                codedBelow++;
            }
            else
            {
                uint j;
                if (shortHeader)
                {
                    // PayloadIndex from registers
                    i = i == rem.x ? src.x : (i == rem.y ? src.y : i);
                    j = i - (i > rem.x ? 1u : 0u) - (i > rem.y ? 1u : 0u);
                }
                else
                {
                    j = WalkPayloadIndex(i, removedBelow, codedBelow, removedCount);
                }
                b = ReadShuffledBlock(fid, base, q, bs, j);
            }
            if (halves)
            {
                b.xy = UnpredictHalf(b.xy);
                if (high)
                {
                    b.zw = UnpredictHalf(b.zw);
                }
            }
            if (colour)
            {
                // BC3 color is the high half
                uint2 c = UnpredictColourHalf(bs == 16 ? b.zw : b.xy, bs == 16);
                if (bs == 16)
                {
                    b.zw = c;
                }
                else
                {
                    b.xy = c;
                }
            }
            StoreBlock(bs, dstBlock + g, b);
        }
    }
}

[numthreads(TG_THREAD_COUNT, 1, 1)]
[RootSignature(RootSig4c)]
void main(uint3 dtID : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
#ifdef HEADERLESS_ONLY
    UnshuffleHeaderless(dtID.x);
#else
    // Timings are sensitive to code layout, even of code a stream never runs: dispatching with if / else (EX / Join
    // branch first) instead of this early return cut BC7 Join latency 2% but cost the headerless path 5-17% of batched
    // throughput, ParseHeader's owner-only body decode costs headerless BC5 single-dispatch latency 5-10%, and the
    // wave-wide body decode (ReadCodedBlockWave) costs BC5 EX streams without coded blocks 2-3% (RTX 5080).
    // After edits, time single and batched dispatches of every path (headerless, EX with and without coded blocks,
    // BC7 Join). The headerless ids are those below BC1_EX but BC7 Join (ids past BC7_EX skip the header parse below
    // and return at BlockSize 0). With BC7 Join just below the EX ids (the ids before 0x06), testing the BC7 Join -
    // BC7_EX range here instead of the ids below BC7 Join cost headerless single-dispatch latency up to 4% and EX
    // latency 1-4.5%. With BC7 Join at 0x06, a mask test of the headerless ids ((0x7BFu >> transformId) & 1) sped the
    // EX paths up to 11% but cost headerless BC4 tiles 17% of batched throughput.
    if (transformId < TRANSFORM_BC1_EX && transformId != TRANSFORM_BC7_JOIN)
    {
        UnshuffleHeaderless(dtID.x);
        return;
    }
    uint id = transformId;
    uint k = 0, removedCount = 0, codedCount = 0, predictMask = 0;
    uint curveWidthInPixels = 0;
    bool shortHeader = false;
#ifdef EX_WIDE_STREAM_BLOCKS
    bool wide = false;
#endif
    uint2 rem = ~0u, src = 0;
    if (transformId <= TRANSFORM_BC7_EX)
    {
        // thread 0's wave parses the header, every lane with the same values, so that ParseHeader's lanes can decode
        // the coded block bodies together (ReadCodedBlockWave)
        if (gi < WaveGetLaneCount())
        {
            if (transformId != TRANSFORM_BC7_JOIN)
            {
                // the common EX headers from one 16-byte read: BC7_EX Join, or BC1/3/4/5 (one ParseHeader call site,
                // as it is inlined)
                bool parsed;
                if (transformId == TRANSFORM_BC7_EX)
                {
                    parsed = FastParseJoinHeaderLoad();
                }
                else
                {
                    parsed = FastParseHeader();
                }
                if (!parsed)
                {
#ifdef EX_WIDE_STREAM_BLOCKS
                    // a wide stream maps G blocks per thread: the groups past those threads have no blocks
                    const uint nW = bufferSizeInBytes / ExIdBlockSize(transformId), GW = ExIdBlocksPerThread(transformId);
                    if (nW >= EX_WIDE_STREAM_BLOCKS && dtID.x - gi >= nW / GW + nW % GW)
                    {
                        gsValid = 0;
                    }
                    else
#endif
                    {
                        ParseHeader(dtID.x - gi);
                    }
                }
            }
            else
            {
                JoinHeaderless();
            }
        }
        // a warp that isn't parsing the header
#if TG_THREAD_COUNT < 128
        for (uint e = gi; e < 64u; e += TG_THREAD_COUNT)
        {
            gsUnfoldZ[e] = UnfoldZEntry(e);
        }
#else
        if (gi - 64u < 64u)
        {
            gsUnfoldZ[gi - 64u] = UnfoldZEntry(gi - 64u);
        }
#endif
        if (gi - 128u < 10u)
        {
            gsSortCount[gi - 128u] = 0;
        }
        GroupMemoryBarrierWithGroupSync();
        if (gsValid == 0)
        {
            return;
        }
#ifdef EX_WIDE_STREAM_BLOCKS
        wide = gsValid == 2u;
#endif
#ifdef FIXED_TRANSFORM_ID
        id = FixedHeaderTransform(gsTransform);
#else
        id = gsTransform;
#endif
        k = gsHeaderBlocks;
        removedCount = gsRemovedCount;
        codedCount = gsCodedCount;
        curveWidthInPixels = gsCurveWidthInPixels;
        predictMask = transformId != TRANSFORM_BC7_JOIN ? gsPredictMask : 0u;
#if FIXED_TRANSFORM_ID == 0x0F
        // BC7_EX's BC5_116116 blocks are curve-only, and Join doesn't read it
        predictMask = BCNEX_UNSHUFFLED;
#endif
        // the common header (BC1/3/4/5: FastParseHeader's shape): at most 2 removed duplicates and no coded blocks
#ifdef EX_GENERAL_DECODE
        shortHeader = false;
#else
        shortHeader = codedCount == 0 && removedCount <= 2u;
#endif
        rem = uint2(removedCount > 0 ? gsRemoved[0] : ~0u, removedCount > 1u ? gsRemoved[1] : ~0u);
        src = uint2(gsSources[0], gsSources[1]);
    }

    uint bs = BlockSize(id);
    if (bs == 0)
    {
        return;
    }
    uint G = BlocksPerThread(id), T = ExThreadBlocks(id);
    uint n = bufferSizeInBytes / bs;
#ifdef EX_WIDE_STREAM_BLOCKS
    // T is 2 or 4
    T = wide ? G : T;
    uint groups = n >> firstbitlow(T), stragglers = n & (T - 1u);
#else
    uint groups = n / T, stragglers = n % T;
#endif

    if (id == TRANSFORM_BC7_JOIN)
    {
        // every thread of the group, for its barriers
        JoinSortedDecode(dtID.x, gi, n, k, removedCount, codedCount, curveWidthInPixels);
        return;
    }

    uint t = dtID.x;
    uint first, count;
    if (t < groups)
    {
        first = t * T;
        count = T;
    }
    else if (t < groups + stragglers)
    {
        first = groups * T + (t - groups);
        count = 1;
    }
    else
    {
        return;
    }

    // payload: n - k blocks after the header, q of them in whole pairs / quads (none with BCNEX_UNSHUFFLED, as in a
    // curve-only stream: ReadShuffledBlock then reads every block as is)
    uint m = n - k;
    uint q = (predictMask & BCNEX_UNSHUFFLED) != 0 ? 0u : m / G * G;
    uint base = k * bs;

    // a pair / quad never straddles a micro-tile row (32 16-byte or 64 8-byte blocks), so it stays contiguous
    uint dstBlock = first;
    if (curveWidthInPixels)
    {
        dstBlock = CurveDestBlock(first, bs, curveWidthInPixels);
    }

    // EX index prediction: BCNEX_PREDICT_HALVES (mask bit 0) and BCNEX_PREDICT_COLOUR (mask bit 1)
    bool halves = (predictMask & 1u) != 0, colour = (predictMask & 2u) != 0;
    if (id == TRANSFORM_BC5_116116 || id == TRANSFORM_BC5_2266)
    {
        ExBlocks(id == TRANSFORM_BC5_116116 ? TRANSFORM_BC5_116116 : TRANSFORM_BC5_2266, 16u, EX_G4, halves, true, false,
                 base, q, first, count, dstBlock, shortHeader, removedCount, codedCount, rem, src);
    }
    else if (id == TRANSFORM_BC3_116224 || id == TRANSFORM_BC3_664)
    {
        ExBlocks(id == TRANSFORM_BC3_116224 ? TRANSFORM_BC3_116224 : TRANSFORM_BC3_664, 16u, EX_G4, halves, false, colour,
                 base, q, first, count, dstBlock, shortHeader, removedCount, codedCount, rem, src);
    }
    else if (id == TRANSFORM_BC4_116)
    {
        ExBlocks(TRANSFORM_BC4_116, 8u, EX_G4, halves, false, false,
                 base, q, first, count, dstBlock, shortHeader, removedCount, codedCount, rem, src);
    }
    else
    {
        // BC1 (TRANSFORM_BC1_224 / TRANSFORM_BC1_44)
        ExBlocks(id == TRANSFORM_BC1_224 ? TRANSFORM_BC1_224 : TRANSFORM_BC1_44, 8u, EX_G2, false, false, colour,
                 base, q, first, count, dstBlock, shortHeader, removedCount, codedCount, rem, src);
    }
#endif
}
