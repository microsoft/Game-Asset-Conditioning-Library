//--------------------------------------------------------------------------------------
// BC7JoinDecode.hlsli
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------
// GPU reverse of the BC7 Join shuffle (CPU reference: BC7_ModeJoin_Reverse and BC7_JoinStream_Decode in
// Lib\shuffle\bc7modejoinreverse.cpp, production field configuration BC7_ModeJoin_FieldConfig() == 1). A stored
// block s of mode m (the lowest set bit of byte 15, which the transform keeps in place) is restored by
//   1. undoing the byte remap (kBC7JoinInvRemap) of streams that have one (header parameter 4) and of the header's
//      coded blocks (always coded remapped),
//   2. one bit gather x[r] = s[T(r)] composing the inverses of the index field permutation (JFP), the index bit
//      reversal (JBO), the 24-bit block rotation and the bit layout of the mode's statics mask,
//   3. undoing the endpoint residual transform and the Mode 4/5 RGBA reorder, then the index residual transform.
// Reserved (mode 8) blocks are only rotated. T depends on the mode, its mask and, for Modes 1/3/7, the anchor class
// of the partition (the JFP symbol widths depend on it). Each thread group builds the 17 T rows in groupshared
// memory with JoinBuildMode, from the masks in the stream while its blocks' modes load: the rows of mask 0
// (kBC7JoinGather0) with the bits whose layout depends on the mask recomputed from the layout and the JFP / JBO inverse
// (JoinMaskedJ). On waves of 32 lanes or more the gather runs bit-sliced (JoinGatherSliced: a wave shuffle per stored
// bit position for each distinct T row of the wave), the blocks sorted by mode beforehand (JoinSortedDecode in
// UnshuffleBCnEx.hlsl) so that a wave sees few rows and modes.
// Requires SharedDefinitions.hlsli (TG_THREAD_COUNT).
//--------------------------------------------------------------------------------------
#ifndef BC7_JOIN_DECODE_HLSLI
#define BC7_JOIN_DECODE_HLSLI

#include "BC7JoinTables.hlsli"
#include "IndexPredictionHelpers.hlsli"

groupshared uint gsJoinMask[8];     // (Statics | LowEntropy) Join mask of each mode
groupshared uint gsJoinT[544];      // T rows (bytes), 17 x 128: raw bit r is stored bit T(r)

// First T row of each mode (Modes 1/3/7 have one per anchor class) and the mode of each row
static const uint kJoinRowBase[8] = { 0, 1, 5, 6, 10, 11, 12, 13 };
static const uint kJoinRowMode[17] = { 0, 1, 1, 1, 1, 2, 3, 3, 3, 3, 4, 5, 6, 7, 7, 7, 7 };

// Endpoint fields: channel c, endpoint k at start + width * (c * count + k)
static const uint kJoinEndpointStart[8] = { 5, 8, 9, 10, 8, 8, 7, 14 };
static const uint kJoinEndpointWidth[8] = { 4, 6, 5, 7, 5, 7, 7, 5 };
static const uint kJoinEndpointCount[8] = { 6, 4, 6, 4, 2, 2, 2, 4 };

uint JoinGet(uint4 x, uint p, uint w)
{
    return indexPredictionReadBits(x, p, w);
}

void JoinPut(inout uint4 x, uint p, uint w, uint v)
{
    indexPredictionWriteBits(x, p, w, v);
}

uint JoinUnfold(uint z, uint p, uint maxValue)
{
    return uint(indexPredictionUnfold(int(z), int(p), int(maxValue)));
}

// ---------------------------------------------------------------------------------------------------------------
// Layout map (BC7_ModeJoin_LayoutShuffle) of a mode with statics: position in the un-rotated layout of raw bit r of a
// mode m block, for the raw bits whose position depends on the mask (kJoinMaskedLo / kJoinMaskedHi; the others keep
// their mask 0 positions, as in kBC7JoinGather0). In the endpoint data, the non-static fields are bit-interleaved in
// lane order, and the static fields follow, packed in field order.

// Non-static endpoint fields of mode m (Modes 0..5 and 7): field f, endpoint f & 1 of channel f / epc, is static when
// mask bit 2 * (f / epc) + (f & 1) is set
uint JoinMaskedNonStatic(uint m, uint mask)
{
    const uint epc = kJoinEndpointCount[m];
    uint ns = 0;
    [unroll]
    for (uint c = 0; c < 4u; c++)
    {
        const uint keep = (((mask >> (2u * c)) & 1u) ? 0u : 0x5555u) | (((mask >> (2u * c + 1u)) & 1u) ? 0u : 0xAAAAu);
        ns |= (keep & ((1u << epc) - 1u)) << (c * epc);
    }
    return ns & ((1u << (epc * (m < 4u ? 3u : 4u))) - 1u);
}

// Position in the endpoint data of bit j of field f (w bits; ns = JoinMaskedNonStatic, N = countbits(ns)), from the
// number nb of non-static fields below f
uint JoinMaskedEndpointPos(uint f, uint j, uint w, uint ns, uint N, bool descending)
{
    const uint nb = countbits(ns & ((1u << f) - 1u));
    return ((ns >> f) & 1u) ? j * N + (descending ? N - 1u - nb : nb) : (N + f - nb) * w + j;
}

// Mode 6 endpoint data position V (0..57) of raw bit r in [7, 65), for a nonzero mask; branch-free in r, which differs
// across the lanes of JoinBuildMode
uint JoinMaskedLayoutMode6(uint mask, uint r)
{
    if (mask == 0xFFu)
    {
        const uint x = r - 49u;
        const uint a = x / 7u, j = x - 7u * a;
        const uint v = j == 6u ? 56u + a : 44u + 6u * a + j;
        return r < 49u ? r - 5u : (r < 63u ? v : r - 63u);
    }

    // alpha values are P | A << 1 unless both alpha fields are static; raw bits 7..48 hold the color fields, 49..62
    // the alpha fields and 63 / 64 the P bits
    const bool alphaStatic = (mask & 0xC0u) == 0xC0u;
    const uint pos0 = alphaStatic ? 2u : 0u;
    const bool colour = r < 49u, alpha = r < 63u;
    const uint x = colour ? r - 7u : r - 49u;
    const uint fx = x / 7u, jx = x - 7u * fx;
    const uint f = colour ? fx : (alpha ? 6u + fx : r - 57u);
    const uint j = colour ? jx : (alpha ? jx + (alphaStatic ? 0u : 1u) : 0u);
    const uint ns = ~mask & 0xFFu;
    const uint N = countbits(ns);
    const uint below = (1u << f) - 1u;
    // bit 7 of the non-static alpha values follows bit 6 of every non-static field, in lane order
    const uint lanes = j < 7u ? ns : ns & 0xC0u;
    const uint vNonStatic = pos0 + j * N + (mask == 0xC0u ? countbits(lanes >> (f + 1u)) : countbits(lanes & below));
    // the static fields follow, 7 bits each, 8 for the alpha values unless both are static
    const uint vStatic = pos0 + 7u * N + countbits(ns & 0xC0u) + 7u * countbits(mask & below) +
                         (alphaStatic ? 0u : countbits(mask & 0xC0u & below)) + j;
    const uint v = ((mask >> f) & 1u) ? vStatic : vNonStatic;
    return (alphaStatic && !alpha) ? r - 63u : v;
}

// Layout position of raw bit r in [kJoinMaskedLo[m], kJoinMaskedHi[m]) of a mode m block with a nonzero mask, given
// ns = JoinMaskedNonStatic(m, mask) and, for Mode 3, b6 = the endpoint data positions of its B0..B3 bit 6s
uint JoinMaskedLayout(uint m, uint mask, uint ns, uint4 b6, uint r)
{
    if (m == 6u)
    {
        // endpoint data V < 56 -> 40 + V, V 56 / 57 -> 103 / 104
        const uint V = JoinMaskedLayoutMode6(mask, r);
        return V < 56u ? 40u + V : 103u + (V - 56u);
    }
    uint f, j, w;
    if (m == 0u)
    {
        f = (r - 5u) >> 2;
        j = (r - 5u) & 3u;
        w = 4u;
    }
    else if (m == 1u)
    {
        f = (r - 8u) / 6u;
        j = (r - 8u) % 6u;
        w = 6u;
    }
    else if (m == 2u)
    {
        f = (r - 9u) / 5u;
        j = (r - 9u) % 5u;
        w = 5u;
    }
    else if (m == 3u)
    {
        f = (r - 10u) / 7u;
        j = (r - 10u) % 7u;
        w = 7u;
        if (f >= 8u && j == 6u)
        {
            // the B0..B3 bit 6s move to the mode byte (100 + j), the rest is compacted
            return 100u + (f - 8u);
        }
    }
    else if (m == 4u)
    {
        // rotation and index mode bits, then the low bits of the 6-bit alpha fields (r 38 / 44)
        if (r < 8u)
        {
            return 54u + (r - 5u);
        }
        if (r == 38u || r == 44u)
        {
            return r == 38u ? 57u : 58u;
        }
        const uint x = r - 8u - (r > 38u ? 1u : 0u) - (r > 44u ? 1u : 0u);
        f = x / 5u;
        j = x % 5u;
        w = 5u;
    }
    else if (m == 5u)
    {
        // the low bits of the 8-bit alpha fields (r 50 / 58)
        if (r == 50u || r == 58u)
        {
            return r == 50u ? 40u : 41u;
        }
        const uint x = r - 8u - (r > 50u ? 1u : 0u) - (r > 58u ? 1u : 0u);
        f = x / 7u;
        j = x % 7u;
        w = 7u;
    }
    else
    {
        f = (r - 14u) / 5u;
        j = (r - 14u) % 5u;
        w = 5u;
    }
    const uint e = JoinMaskedEndpointPos(f, j, w, ns, countbits(ns), m >= 4u && mask == 0xC0u);
    if (m == 3u)
    {
        return 16u + e - (b6.x < e ? 1u : 0u) - (b6.y < e ? 1u : 0u) - (b6.z < e ? 1u : 0u) - (b6.w < e ? 1u : 0u);
    }
    if (m == 4u || m == 5u)
    {
        // the k endpoint data bits from position nsb move to the mode byte
        const uint k = m == 4u ? 3u : 2u;
        const uint nsb = mask == 0xFFu ? (m == 4u ? 2u : 5u) : (m == 4u ? 37u : 54u);
        if (e - nsb < k)
        {
            return (m == 4u ? 101u : 102u) + (e - nsb);
        }
        return (m == 4u ? 59u : 42u) + (e < nsb ? e : e - k);
    }
    return (m == 2u ? 6u : (m == 7u ? 16u : 24u)) + e;
}

// ---------------------------------------------------------------------------------------------------------------
// Gather table

// Raw bits [lo, hi) of each mode whose layout depends on its mask (the endpoint fields, and Mode 4's rotation and index
// mode bits): their stored positions don't depend on the anchor class, and the JFP / JBO inverse leaves them in place
// but in Modes 4..6
static const uint kJoinMaskedLo[8] = { 5, 8, 9, 10, 5, 8, 7, 14 };
static const uint kJoinMaskedHi[8] = { 77, 80, 99, 94, 50, 66, 65, 94 };

// Stored position of bit q of a rotated mode m block, for the positions q that the masked layouts move the raw bits
// [lo, hi) to: the J rows of Modes 4..6 (kBC7JoinPerm) restricted to those positions, the identity in the other modes
// (ShuffleTables.BC7JoinShaderTables checks it for every mask and anchor class)
uint JoinMaskedJ(uint m, uint q)
{
    if (m == 4u)
    {
        return q < 108u ? q + 2u : (q < 110u ? q - 108u : q);
    }
    if (m == 5u)
    {
        return q < 118u ? q + 2u : (q < 120u ? q - 56u : q);
    }
    if (m == 6u)
    {
        return q == 0u ? 63u : q;
    }
    return q;
}

// Stored positions (bytes) of the raw bits 4 u .. 4 u + 3 of a mode m block with a nonzero mask, and the mask of those
// bytes in [lo, hi) (the others keep their mask 0 positions). Called with a literal m; unrolled and branch-free, as r
// differs across the lanes.
uint2 JoinMaskedBytes(uint m, uint mask, uint ns, uint4 b6, uint u)
{
    const uint lo = kJoinMaskedLo[m], hi = kJoinMaskedHi[m];
    uint bytes = 0, sel = 0;
    [unroll]
    for (uint b = 0; b < 4u; b++)
    {
        const uint r = u * 4u + b;
        const bool masked = r >= lo && r < hi;
        const uint j = JoinMaskedJ(m, (JoinMaskedLayout(m, mask, ns, b6, r) + 24u) & 127u);
        bytes |= masked ? j << (b * 8u) : 0u;
        sel |= masked ? 0xFFu << (b * 8u) : 0u;
    }
    return uint2(bytes, sel);
}

// Builds the T rows of mode m (mask: its mask byte) on lane u < 32 of a wave, which writes T word u (raw bits 4 u ..
// 4 u + 3) of each of the mode's rows: T(r) = J(L(r) + 24 mod 128), the final 24-bit block rotation moving layout bit p
// to p + 24. The rows are those of mask 0 (kBC7JoinGather0) with the bits whose layout depends on the mask replaced.
void JoinBuildMode(uint u, uint m, uint mask)
{
    mask &= 0xFFu;
    const uint row0 = kJoinRowBase[m];
    const uint rows = (m == 1u || m == 3u || m == 7u) ? 4u : 1u;
    uint bytes = 0, sel = 0;
    if ((mask & (m < 4u ? 0x3Fu : 0xFFu)) != 0u)
    {
        const uint ns = JoinMaskedNonStatic(m, mask);
        uint4 b6 = 0;
        if (m == 3u)
        {
            const uint N = countbits(ns);
            b6 = uint4(JoinMaskedEndpointPos(8u, 6u, 7u, ns, N, false), JoinMaskedEndpointPos(9u, 6u, 7u, ns, N, false),
                       JoinMaskedEndpointPos(10u, 6u, 7u, ns, N, false), JoinMaskedEndpointPos(11u, 6u, 7u, ns, N, false));
        }
        // the layout specialized for each mode (m is uniform in the wave)
        uint2 bs;
        switch (m)
        {
        case 0u: bs = JoinMaskedBytes(0u, mask, ns, b6, u); break;
        case 1u: bs = JoinMaskedBytes(1u, mask, ns, b6, u); break;
        case 2u: bs = JoinMaskedBytes(2u, mask, ns, b6, u); break;
        case 3u: bs = JoinMaskedBytes(3u, mask, ns, b6, u); break;
        case 4u: bs = JoinMaskedBytes(4u, mask, ns, b6, u); break;
        case 5u: bs = JoinMaskedBytes(5u, mask, ns, b6, u); break;
        case 6u: bs = JoinMaskedBytes(6u, mask, ns, b6, u); break;
        default: bs = JoinMaskedBytes(7u, mask, ns, b6, u); break;
        }
        bytes = bs.x;
        sel = bs.y;
    }

    // per-lane table reads serialize by distinct address: the class 0 word is read at a lane-uniform address when all
    // of its bytes are replaced
    const uint base = row0 * 32u;
    const uint w0 = kBC7JoinGather0[sel == ~0u ? base : base + u];
    gsJoinT[base + u] = (w0 & ~sel) | bytes;
    if (rows > 1u)
    {
        // the anchor class rows differ from the class 0 row only in the words of index bits set in cw1..cw3, which they
        // read; their other words are the class 0 row's
        const uint cw1 = m == 1u ? 0xFFC00000u : (m == 3u ? 0x74000000u : 0x77000000u);
        const uint cw2 = m == 1u ? 0xFC000000u : (m == 3u ? 0x7F000000u : 0x7C000000u);
        const uint cw3 = m == 1u ? 0xFEC00000u : 0x70000000u;
        [unroll]
        for (uint c = 1; c < 4u; c++)
        {
            const uint rb = (row0 + c) * 32u;
            if ((((c == 1u ? cw1 : (c == 2u ? cw2 : cw3)) >> u) & 1u) != 0u)
            {
                gsJoinT[rb + u] = (kBC7JoinGather0[rb + u] & ~sel) | bytes;
            }
            else
            {
                gsJoinT[rb + u] = (w0 & ~sel) | bytes;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Field transforms (BC7_ModeJoin_Reverse, field configuration 1)

// Inverse of the mod-2^w zigzag: the signed w-bit difference
int JoinUnZigzag(uint z, uint w)
{
    const uint M = (1u << w) - 1u;
    const uint d = ((z >> 1) ^ (0u - (z & 1u))) & M;
    return (d >> (w - 1u)) ? int(d) - int(M) - 1 : int(d);
}

// (E0, E1) stored as (E0 + floor(d / 2) mod 2^w, zigzag(d)) with d = E1 - E0 as a signed w-bit value
void JoinMidSpanInverse(inout uint4 x, uint p0, uint p1, uint w)
{
    const int M = int((1u << w) - 1u);
    const int d = JoinUnZigzag(JoinGet(x, p1, w), w);
    const int e0 = (int(JoinGet(x, p0, w)) - (d >> 1)) & M;
    JoinPut(x, p0, w, uint(e0));
    JoinPut(x, p1, w, uint((e0 + d) & M));
}

// Inverse of the endpoint transforms (EndpointInverse; S = the mode's mask, bits 2c / 2c + 1 flag channel c's E0s / E1s)
void JoinEndpointInverse(inout uint4 x, uint m, uint S)
{
    const uint w = kJoinEndpointWidth[m], epc = kJoinEndpointCount[m];
    const uint M = (1u << w) - 1u;
    if (m == 5u || m == 6u)
    {
        const uint fa = (S >> 6) & 3u;
        if (m == 6u && fa == 3u)
        {
            JoinPut(x, 56u, 7u, JoinUnfold(JoinGet(x, 56u, 7u), JoinGet(x, 49u, 7u), 127u));
        }
        else if (m == 6u)
        {
            // A1 as the 8-bit value P1 | A1 << 1, folded around P0 | A0 << 1
            const uint a1 = JoinUnfold(JoinGet(x, 64u, 1u) | (JoinGet(x, 56u, 7u) << 1), JoinGet(x, 63u, 1u) | (JoinGet(x, 49u, 7u) << 1), 255u);
            JoinPut(x, 64u, 1u, a1 & 1u);
            JoinPut(x, 56u, 7u, a1 >> 1);
        }
        else if (fa != 0u)
        {
            JoinPut(x, 59u, 7u, JoinUnfold(JoinGet(x, 59u, 7u), JoinGet(x, 51u, 7u), 127u));
        }
        else
        {
            JoinMidSpanInverse(x, 51u, 59u, 7u);
        }
        for (uint c = 0; c < 3u; c++)
        {
            const uint f0 = kJoinEndpointStart[m] + 2u * w * c;
            if ((S >> (2u * c)) & 3u)
            {
                JoinPut(x, f0 + w, w, JoinUnfold(JoinGet(x, f0 + w, w), JoinGet(x, f0, w), M));
            }
            else
            {
                JoinMidSpanInverse(x, f0, f0 + w, w);
            }
        }
        return;
    }

    const uint chans = m == 7u ? 4u : 3u;
    for (uint c = 0; c < chans; c++)
    {
        const uint fl = (S >> (2u * c)) & 3u;
        const uint f0 = kJoinEndpointStart[m] + w * epc * c;
        if (fl == 0u)
        {
            // E1 of subset 0 folded around its E0; the other subsets as a zigzag span and an E0 folded around the
            // span-aligned subset 0 midpoint
            const uint e00 = JoinGet(x, f0, w);
            const uint e01 = JoinUnfold(JoinGet(x, f0 + w, w), e00, M);
            JoinPut(x, f0 + w, w, e01);
            const int pr = int(e00 + e01) >> 1;
            for (uint k = 2u; k < epc; k += 2u)
            {
                const uint fk = f0 + k * w;
                const int d = JoinUnZigzag(JoinGet(x, fk + w, w), w);
                const uint e0 = JoinUnfold(JoinGet(x, fk, w), uint(clamp(pr - (d >> 1), 0, int(M))), M);
                JoinPut(x, fk, w, e0);
                JoinPut(x, fk + w, w, uint(int(e0) + d) & M);
            }
            continue;
        }
        if (fl == 2u)
        {
            // the other subsets' E0 folded around the stored subset 0 midpoint
            const uint pr = (JoinGet(x, f0, w) + JoinGet(x, f0 + w, w)) >> 1;
            for (uint k = 2u; k < epc; k += 2u)
            {
                JoinPut(x, f0 + k * w, w, JoinUnfold(JoinGet(x, f0 + k * w, w), pr, M));
            }
        }
        for (uint k = 0; k < epc; k += 2u)
        {
            const uint fk = f0 + k * w;
            JoinPut(x, fk + w, w, JoinUnfold(JoinGet(x, fk + w, w), JoinGet(x, fk, w), M));
        }
    }
}

// The index residual inverse is indexPredictionInverseBlock (IndexPredictionHelpers.hlsli, shared with the split unshuffle).
// One copy of its loop serves the lanes of every mode, after the mode tails, where a copy per mode diverged at the mode
// boundaries of the sorted waves. RTX 5080, against a copy per mode: single-dispatch latency -6%, batched time -1 to -4%.
// Waves of Mode 5 blocks alone take a literal Mode 5 copy instead (JoinDecodeBlock).

// Inverse of BC7_Mode45_ReorderRGBA (an involution): rotation r > 0 swaps channel r - 1 back with alpha
void JoinRestoreRGBA(inout uint4 x, uint m)
{
    const uint rot = (x.x >> (m == 4u ? 5u : 6u)) & 3u;
    if ((m != 4u && m != 5u) || rot == 0u)
    {
        return;
    }
    const uint w = m == 4u ? 5u : 7u;
    const uint c0 = 8u + 2u * w * (rot - 1u), a0 = m == 4u ? 39u : 51u;
    const uint e0 = JoinGet(x, c0, w), e1 = JoinGet(x, c0 + w, w);
    JoinPut(x, c0, w, JoinGet(x, a0, w));
    JoinPut(x, c0 + w, w, JoinGet(x, a0 + w + 1u, w));
    JoinPut(x, a0, w, e0);
    JoinPut(x, a0 + w + 1u, w, e1);
}

// 32 bits of x gathered from s through T row word base rw (8 words), one lane's block alone (waves under 32 lanes)
uint JoinGatherWord(uint4 s, uint rw)
{
    uint o = 0;
    for (uint w = 0; w < 8u; w++)
    {
        const uint T4 = gsJoinT[rw + w];
        [unroll]
        for (uint b = 0; b < 4u; b++)
        {
            const uint p = (T4 >> (b << 3)) & 0x7Fu;
            const uint word = (p & 64u) ? ((p & 32u) ? s.w : s.z) : ((p & 32u) ? s.y : s.x);
            o |= ((word >> (p & 31u)) & 1u) << ((w << 2) + b);
        }
    }
    return o;
}

// One step of a 32 x 32 bit matrix transpose across each 32-lane group of the wave (Hacker's Delight transpose32 with
// rows in lanes): swaps the bits of lanes l and l ^ j that are in the other's j-column blocks
uint JoinTransposeStep(uint v, uint lane, uint j, uint m)
{
    const uint y = WaveReadLaneAt(v, lane ^ j);
    return (lane & j) ? ((v & ~m) | ((y >> j) & m)) : ((v & m) | ((y << j) & ~m));
}

// Bit-slices x (an involution): afterwards, bit i of word k on lane l of a 32-lane group is bit 32 k + l of the input
// of lane i of the group. Every lane of the wave must be active.
uint4 JoinTranspose(uint4 x, uint lane)
{
    [unroll]
    for (uint k = 0; k < 4u; k++)
    {
        uint v = x[k];
        v = JoinTransposeStep(v, lane, 16u, 0x0000FFFFu);
        v = JoinTransposeStep(v, lane, 8u, 0x00FF00FFu);
        v = JoinTransposeStep(v, lane, 4u, 0x0F0F0F0Fu);
        v = JoinTransposeStep(v, lane, 2u, 0x33333333u);
        v = JoinTransposeStep(v, lane, 1u, 0x55555555u);
        x[k] = v;
    }
    return x;
}

// x = the gather of t through T row word base rw (~0: none, x undefined) of every lane of the wave, in bit-sliced
// form: each lane of a 32-lane group gathers one raw bit position of all 32 blocks of the group per word, a wave
// shuffle of the bit-sliced t per stored bit position, for each distinct row in the wave. Every lane of the wave must
// be active (the shuffles read every lane).
uint4 JoinGatherSliced(uint4 t, uint rw)
{
    const uint lane = WaveGetLaneIndex();
    const uint group = lane & ~31u;
    const uint4 c = JoinTranspose(t, lane);
    uint4 x = 0;
    uint key = rw;
    for (;;)
    {
        const uint r0 = WaveActiveMin(key);
        if (r0 == ~0u)
        {
            break;
        }
        const uint rowMask = indexPredictionDword(WaveActiveBallot(key == r0), lane >> 5);
        [unroll]
        for (uint d = 0; d < 4u; d++)
        {
            // raw bit 32 d + (lane & 31) is stored bit p
            const uint p = (gsJoinT[r0 + 8u * d + ((lane & 31u) >> 2)] >> ((lane & 3u) << 3)) & 0x7Fu;
            const uint4 v4 = WaveReadLaneAt(c, group | (p & 31u));
            const uint v = (p & 64u) ? ((p & 32u) ? v4.w : v4.z) : ((p & 32u) ? v4.y : v4.x);
            x[d] = (x[d] & ~rowMask) | (v & rowMask);
        }
        if (key == r0)
        {
            key = ~0u;
        }
    }
    return JoinTranspose(x, lane);
}

// The endpoint inverse and the Mode 4/5 RGBA reorder of a mode m (0-7) block after the gather (the index fields and
// the partition are left to JoinIndexInverse)
uint4 JoinDecodeTail(uint4 x, uint m, uint mask)
{
    if (m == 4u)
    {
        if (mask == 0u)
        {
            x.x ^= (x.x & 0x20u) << 1;  // stored rot0 ^ rot1
        }
    }
    else
    {
        JoinEndpointInverse(x, m, mask & (m < 4u ? 0x3Fu : 0xFFu));
    }
    JoinRestoreRGBA(x, m);
    return x;
}

// Restores one BC7 block from its Join stored form (after JoinBuildMode), byte-remapped when remap is set (the
// stream's remap, or a header-coded block, which is always coded remapped). Every lane of the wave must call this
// together (the bit gather's wave shuffles), the lanes without a block with any s.
uint4 JoinDecodeBlock(uint4 s, bool remap)
{
    const uint m = firstbitlow((s.w >> 24) | 0x100u);

    // undo any byte remap; byte 15 keeps its low m + 1 bits (the mode)
    uint4 t = s;
    uint rw = ~0u;
    if (m < 8u)
    {
        if (remap)
        {
            t = 0;
            [unroll]
            for (uint j = 0; j < 16u; j++)
            {
                const uint v = (s[j >> 2] >> ((j & 3u) << 3)) & 0xFFu;
                const uint mj = m * 16u + j;
                const uint idx = v < 64u ? (mj << 4) | (v >> 2) : 2048u + mj * 48u + ((v - 64u) >> 2);
                const uint u = (kBC7JoinInvRemap[idx] >> ((v & 3u) << 3)) & 0xFFu;
                t[j >> 2] |= u << ((j & 3u) << 3);
            }
        }

        // anchor class of Modes 1/3/7 (partition bits 122-127 / 0-5 are not moved by the index permutations)
        uint cls = 0;
        if (m == 1u || m == 3u || m == 7u)
        {
            const uint second = firstbithigh(kIndexPredictionAnchor2[m == 1u ? t.w >> 26 : t.x & 63u]);
            cls = second == 15u ? 0u : (second == 2u ? 1u : (second == 8u ? 2u : 3u));
        }
        rw = (kJoinRowBase[m] + cls) << 5;
    }

    uint4 x;
    if (WaveGetLaneCount() >= 32u)
    {
        x = JoinGatherSliced(t, rw);
    }
    else
    {
        const uint r = rw == ~0u ? 0u : rw;
        x = uint4(JoinGatherWord(t, r), JoinGatherWord(t, r + 8u), JoinGatherWord(t, r + 16u), JoinGatherWord(t, r + 24u));
    }
    if (m >= 8u)
    {
        // reserved mode: only the 24-bit block rotation (stored byte b + 15 mod 16 is block byte b)
        return uint4((s.x << 8) | (s.w >> 24), (s.y << 8) | (s.x >> 24), (s.z << 8) | (s.y >> 24), (s.w << 8) | (s.z >> 24));
    }

    // A wave of Mode 5 blocks alone (the most common mode in RDO content, so most waves after the mode sort) through a
    // copy of the tail and the index inverse with the mode a literal: its field positions fixed, its 2-bit indices
    // unfolded by table lookup. RTX 5080: batched -4 to -5%, single dispatch unchanged; further copies for Modes 6 / 7
    // cost +2 to 3% single dispatch for ~0.5% more batched, a per-lane m == 5 test diverged at the mode boundaries.
    if (WaveActiveAllTrue(m == 5u))
    {
        uint4 y5 = JoinDecodeTail(x, 5u, gsJoinMask[5]);
        indexPredictionInverseBlock(y5, 5u, true);
        return y5;
    }

    // Other waves: every mode through a copy of the tail with the mode a literal, its field positions and widths known:
    // wave-uniform after the mode sort but at the mode boundaries. RTX 5080, against literal copies for Modes 5-7 alone
    // and one shared copy for the rest: single dispatch -3%, batched -2 to -3%. The index inverse follows once for the
    // lanes of every mode.
    const uint mask = gsJoinMask[m];
    uint4 y;
    switch (m)
    {
    case 0u: y = JoinDecodeTail(x, 0u, mask); break;
    case 1u: y = JoinDecodeTail(x, 1u, mask); break;
    case 2u: y = JoinDecodeTail(x, 2u, mask); break;
    case 3u: y = JoinDecodeTail(x, 3u, mask); break;
    case 4u: y = JoinDecodeTail(x, 4u, mask); break;
    case 5u: y = JoinDecodeTail(x, 5u, mask); break;
    case 6u: y = JoinDecodeTail(x, 6u, mask); break;
    default: y = JoinDecodeTail(x, 7u, mask); break;
    }
    indexPredictionInverseBlock(y, m, false);
    return y;
}

#endif // BC7_JOIN_DECODE_HLSLI
