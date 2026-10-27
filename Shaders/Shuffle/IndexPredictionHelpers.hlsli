//--------------------------------------------------------------------------------------
// IndexPredictionHelpers.hlsli
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------

// Inverse of the BC7 mode split index prediction (SplitIndexPrediction in Lib\shuffle\bc7modesplit.cpp), enabled by
// bit 3 of the stream header extension byte.  Each index field holds residuals against a causal spatial predictor:
//  - pixel 0 is stored raw
//  - row 0 / column 0 pixels extrapolate the previous two pixels a, b of the row / column: clamp(a + k * (a - b) / 8),
//    k = 8 / 6 / 7 for 2 / 3 / 4-bit indices (pixels 1 and 4 copy pixel 0)
//  - interior pixels use MED(left, top, top-left)
//  - an anchor's prediction is capped at the anchor maximum (bits - 1 bits)
//  - residuals are folded around the prediction (0, -1, +1, -2, +2, ... then the one-sided remainder)
// The Mode 4 2-bit and Mode 6 residual fields are stored bit reversed.

// Anchor pixel masks of the 2 / 3 subset partitions (bit 0 = the subset 0 anchor)
static const uint kIndexPredictionAnchor2[64] =
{
    0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001,
    0x8001, 0x0005, 0x0101, 0x0005, 0x0005, 0x0101, 0x0101, 0x8001, 0x0005, 0x0101, 0x0005, 0x0005, 0x0101, 0x0101, 0x0005, 0x0005,
    0x8001, 0x8001, 0x0041, 0x0101, 0x0005, 0x0101, 0x8001, 0x8001, 0x0005, 0x0101, 0x0005, 0x0005, 0x0005, 0x8001, 0x8001, 0x0041,
    0x0041, 0x0005, 0x0041, 0x0101, 0x8001, 0x8001, 0x0005, 0x0005, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x0005, 0x0005, 0x8001,
};

static const uint kIndexPredictionAnchor3[64] =
{
    0x8009, 0x0109, 0x8101, 0x8009, 0x8101, 0x8009, 0x8009, 0x8101, 0x8101, 0x8101, 0x8041, 0x8041, 0x8041, 0x8021, 0x8009, 0x0109,
    0x8009, 0x0109, 0x8101, 0x8009, 0x8009, 0x0109, 0x8041, 0x0501, 0x0029, 0x8101, 0x0141, 0x0441, 0x8101, 0x8021, 0x8401, 0x8101,
    0x8101, 0x8009, 0x8009, 0x0421, 0x0441, 0x0501, 0x0301, 0x8401, 0x8041, 0x8009, 0x8101, 0x8021, 0x8009, 0x8041, 0x8041, 0x8101,
    0x8009, 0x8009, 0x8021, 0x8021, 0x8021, 0x8101, 0x8021, 0x8401, 0x8021, 0x8401, 0x8101, 0xA001, 0x8009, 0x9001, 0x8009, 0x0109,
};

uint indexPredictionReadBits(uint4 value, uint bitOffset, uint bitCount)
{
    const uint dwordIndex = bitOffset >> 5;
    const uint shift = bitOffset & 31u;
    uint result = value[dwordIndex] >> shift;
    if (shift + bitCount > 32u)
    {
        result |= value[min(dwordIndex + 1u, 3u)] << (32u - shift);
    }
    return result & ((1u << bitCount) - 1u);
}

void indexPredictionWriteBits(inout uint4 value, uint bitOffset, uint bitCount, uint bits)
{
    const uint dwordIndex = bitOffset >> 5;
    const uint shift = bitOffset & 31u;
    const uint mask = (1u << bitCount) - 1u;
    value[dwordIndex] = (value[dwordIndex] & ~(mask << shift)) | ((bits & mask) << shift);
    if (shift + bitCount > 32u)
    {
        const uint highIndex = min(dwordIndex + 1u, 3u);
        value[highIndex] = (value[highIndex] & ~(mask >> (32u - shift))) | ((bits & mask) >> (32u - shift));
    }
}

// Inverse of the residual fold around prediction p, for values in [0, maxValue]
int indexPredictionUnfold(int z, int p, int maxValue)
{
    const int room = min(p, maxValue - p);
    if (z <= 2 * room)
    {
        const int d = (z + 1) >> 1;
        return (z & 1) ? p - d : p + d;
    }
    return (p > maxValue - p) ? p - (z - room) : p + (z - room);
}

// Dword i of v, selected rather than dynamically indexed
uint indexPredictionDword(uint4 v, uint i)
{
    return i == 0u ? v.x : (i == 1u ? v.y : (i == 2u ? v.z : v.w));
}

// 64 bits of v from bit p (bits past 127 are undefined)
uint2 indexPredictionExtract64(uint4 v, uint p)
{
    const uint d = p >> 5, s = p & 31u;
    const uint a = indexPredictionDword(v, d), b = indexPredictionDword(v, min(d + 1u, 3u)), c = indexPredictionDword(v, min(d + 2u, 3u));
    return s ? uint2((a >> s) | (b << (32u - s)), (b >> s) | (c << (32u - s))) : uint2(a, b);
}

// v bits [p, p + n) = f (n <= 64, p + n <= 128)
void indexPredictionInsert64(inout uint4 v, uint p, uint n, uint2 f)
{
    const uint2 m = n >= 32u ? uint2(~0u, n > 32u ? (1u << (n - 32u)) - 1u : 0u) : uint2((1u << n) - 1u, 0u);
    f &= m;
    const uint d = p >> 5, s = p & 31u;
    // the field and its mask over dwords d, d + 1, d + 2
    const uint3 fm = s ? uint3(m.x << s, (m.x >> (32u - s)) | (m.y << s), m.y >> (32u - s)) : uint3(m, 0u);
    const uint3 fv = s ? uint3(f.x << s, (f.x >> (32u - s)) | (f.y << s), f.y >> (32u - s)) : uint3(f, 0u);
    v.x = d == 0u ? (v.x & ~fm.x) | fv.x : v.x;
    v.y = d == 1u ? (v.y & ~fm.x) | fv.x : (d == 0u ? (v.y & ~fm.y) | fv.y : v.y);
    v.z = d == 2u ? (v.z & ~fm.x) | fv.x : (d == 1u ? (v.z & ~fm.y) | fv.y : (d == 0u ? (v.z & ~fm.z) | fv.z : v.z));
    v.w = d == 3u ? (v.w & ~fm.x) | fv.x : (d == 2u ? (v.w & ~fm.y) | fv.y : (d == 1u ? (v.w & ~fm.z) | fv.z : v.w));
}

// The inverse of a field of 16 indices from bit fieldPos whose only anchor is pixel 0 (in index order), with bits a literal
// at the call site: the indices sit at fixed offsets, so they are extracted and inserted directly.  Residuals are read
// from src, indices are written to dst
void indexPredictionInverseFieldA1(uint4 src, inout uint4 dst, uint fieldPos, uint bits)
{
    const int M = int((1u << bits) - 1u);
    const uint2 f = indexPredictionExtract64(src, fieldPos);
    int idx[16];
    uint2 o = 0;
    [unroll]
    for (uint i = 0u; i < 16u; ++i)
    {
        const uint width = i ? bits : bits - 1u;
        const uint off = i ? i * bits - 1u : 0u;
        uint v;
        if (off >= 32u)
        {
            v = f.y >> (off - 32u);
        }
        else if (off + width > 32u)
        {
            v = (f.x >> off) | (f.y << (32u - off));
        }
        else
        {
            v = f.x >> off;
        }
        int value = int(v & ((1u << width) - 1u));
        if (i > 0u)
        {
            const uint x = i & 3u;
            const uint y = i >> 2;
            int p;
            if (x == 0u || y == 0u)
            {
                // row 0 / column 0 - extrapolate along the row / column
                const uint stride = (x != 0u) ? 1u : 4u;
                const int a = idx[i - stride];
                if (((x != 0u) ? x : y) < 2u)
                {
                    p = a;
                }
                else
                {
                    const int d = a - idx[i - 2u * stride];
                    p = clamp(M == 3 ? a + d : a + ((M == 7) ? 6 : 7) * d / 8, 0, M);
                }
            }
            else
            {
                // interior - MED(left, top, top-left): the median of L, T and L + T - TL
                const int L = idx[i - 1u];
                const int T = idx[i - 4u];
                p = max(min(L, T), min(max(L, T), L + T - idx[i - 5u]));
            }
            if (M == 3)
            {
                // indexPredictionUnfold(value, p, 3) for p, value in 0..3
                value = int((0x1B36E1E4u >> ((uint(p) * 4u + uint(value)) * 2u)) & 3u);
            }
            else
            {
                value = indexPredictionUnfold(value, p, M);
            }
        }
        idx[i] = value;
        if (off >= 32u)
        {
            o.y |= uint(value) << (off - 32u);
        }
        else
        {
            o.x |= uint(value) << off;
            if (off + width > 32u)
            {
                o.y |= uint(value) >> (32u - off);
            }
        }
    }
    indexPredictionInsert64(dst, fieldPos, 16u * bits - 1u, o);
}

// ---------------------------------------------------------------------------------------------------------------
// Index residual inverse of every mode through one loop (the join decode runs it for any mode, the split unshuffle for
// its literal mode). The last index field of every mode ends at bit 127, so its start s alone fixes it: length
// L = 128 - s, index width w = (L + 15) / 16 and 16 w - L anchors (1: pixel 0 alone; 2 / 3: the anchor table entry of
// the 2 / 3 subset partition at bit m + 1). Modes 4 and 5 add a 2-bit field ending at s, pixel 0 its only anchor.

// s - 64 of the last index field of Modes 0-3 / 4-7, one byte each: 19, 18, 35, 34 / 17, 33, 1, 34
static const uint kIndexPredictionStart03 = 0x22231213u;
static const uint kIndexPredictionStart47 = 0x22012111u;

// u with a zero bit inserted at position p (bits p and up move up one; u < 2^31)
uint indexPredictionInsertZero(uint u, uint p)
{
    return u + (u & (0xFFFFFFFFu << p));
}

// u without bit p (bits above p move down one)
uint indexPredictionRemoveBit(uint u, uint p)
{
    const uint hi = 0xFFFFFFFFu << p;
    return (u & ~hi) | ((u >> 1) & hi);
}

// indexPredictionUnfold(z, min(p, cap), cap) for a cap of 2^b - 1 (b > 0), p >= 0 and z <= cap.  A prediction past the
// midpoint is mirrored (cap - v = cap ^ v) so that its two-sided range 2 min(p, cap - p) is 2 (pc ^ mask)
int indexPredictionUnfoldCapped(int z, int p, int cap)
{
    const int pc = min(p, cap);
    const int mask = (2 * pc > cap) ? cap : 0;
    const int zz = (z >> 1) ^ -(z & 1);         // 0, -1, +1, -2, +2, ...
    return z <= 2 * (pc ^ mask) ? pc + zz : z ^ mask;
}

// Restores the index fields of a mode m (0-7) block x from their residuals, the fields in index order (not bit reversed).
// w2Lookup unfolds 2-bit indices by table lookup: worth it with m a literal (the split unshuffle, the join decode's
// Mode 5 waves), but the join decode's dynamic mode would pay for the width test on every pixel (measured slower), so
// it keeps the arithmetic.
void indexPredictionInverseBlock(inout uint4 x, uint m, bool w2Lookup)
{
    const uint r = ((m < 4u ? kIndexPredictionStart03 : kIndexPredictionStart47) >> ((m & 3u) << 3)) & 0xFFu;  // s - 64
    const uint L = 64u - r;
    const uint w = (L + 15u) >> 4;
    const uint nA = 16u * w - L;
    const int M = int((1u << w) - 1u);
    const int k = int((0x768u >> ((w - 2u) << 2)) & 15u);     // edge extrapolation weight in eighths: 8 / 6 / 7
    const uint part = (x.x >> (m + 1u)) & (m != 0u ? 63u : 15u);
    const uint anchors = nA == 1u ? 1u : (nA == 2u ? kIndexPredictionAnchor2[part] : kIndexPredictionAnchor3[part]);

    // the field from bit 0 (r is never 0 or 32)
    const uint rs = r & 31u;
    const uint flo = (x.z >> rs) | (x.w << (32u - rs));
    const uint fhi = x.w >> rs;
    const uint f0 = r < 32u ? flo : fhi;
    const uint f1 = r < 32u ? fhi : 0u;

    // pixels 0-7 / 8-15 in u0 / u1 at w bits each: the anchors' missing top bits inserted as zeros
    const uint n0 = 8u * w - countbits(anchors & 0xFFu);      // stored bits of pixels 0-7 (13-31)
    uint u0 = f0 & ((1u << n0) - 1u);
    uint u1 = (f0 >> n0) | (f1 << (32u - n0));
    u0 = indexPredictionInsertZero(u0, w - 1u);
    const uint a1 = firstbitlow(anchors & 0xFFFEu), a2 = firstbithigh(anchors);
    if (nA > 1u)
    {
        const uint p1 = (a1 & 7u) * w + w - 1u;
        const uint t1 = indexPredictionInsertZero(a1 < 8u ? u0 : u1, p1);
        u0 = a1 < 8u ? t1 : u0;
        u1 = a1 < 8u ? u1 : t1;
        if (nA > 2u)
        {
            const uint p2 = (a2 & 7u) * w + w - 1u;
            const uint t2 = indexPredictionInsertZero(a2 < 8u ? u0 : u1, p2);
            u0 = a2 < 8u ? t2 : u0;
            u1 = a2 < 8u ? u1 : t2;
        }
    }

    int idx[16];
    idx[0] = int(u0 & uint(M));
    uint o0 = uint(idx[0]), o1 = 0u;
    [unroll]
    for (uint i = 1u; i < 16u; ++i)
    {
        const uint sh = (i & 7u) * w;
        const int z = int(((i < 8u ? u0 : u1) >> sh) & uint(M));
        const uint px = i & 3u;
        const uint py = i >> 2;
        int p;
        if (px == 0u || py == 0u)
        {
            // row 0 / column 0 - extrapolate along the row / column
            const uint stride = (px != 0u) ? 1u : 4u;
            const int a = idx[i - stride];
            if (((px != 0u) ? px : py) < 2u)
            {
                p = a;
            }
            else
            {
                p = clamp(a + k * (a - idx[i - 2u * stride]) / 8, 0, M);
            }
        }
        else
        {
            // interior - MED(left, top, top-left): the median of L, T and L + T - TL
            const int Lf = idx[i - 1u];
            const int T = idx[i - 4u];
            p = max(min(Lf, T), min(max(Lf, T), Lf + T - idx[i - 5u]));
        }
        const uint anchor = (anchors >> i) & 1u;
        int v;
        if (w2Lookup && w == 2u)
        {
            // indexPredictionUnfoldCapped(z, p, 3 >> anchor) for p, z in 0..3: 2 bits at 8 p + 2 z
            v = int(((anchor != 0u ? 0x01010104u : 0x1B36E1E4u) >> ((uint(p) << 3) | (uint(z) << 1))) & 3u);
        }
        else
        {
            v = indexPredictionUnfoldCapped(z, p, M >> int(anchor));
        }
        idx[i] = v;
        if (i < 8u)
        {
            o0 |= uint(v) << sh;
        }
        else
        {
            o1 |= uint(v) << sh;
        }
    }

    // back to the stored widths, highest anchor first
    if (nA > 1u)
    {
        if (nA > 2u)
        {
            const uint p2 = (a2 & 7u) * w + w - 1u;
            const uint t2 = indexPredictionRemoveBit(a2 < 8u ? o0 : o1, p2);
            o0 = a2 < 8u ? t2 : o0;
            o1 = a2 < 8u ? o1 : t2;
        }
        const uint p1 = (a1 & 7u) * w + w - 1u;
        const uint t1 = indexPredictionRemoveBit(a1 < 8u ? o0 : o1, p1);
        o0 = a1 < 8u ? t1 : o0;
        o1 = a1 < 8u ? o1 : t1;
    }
    o0 = indexPredictionRemoveBit(o0, w - 1u);
    const uint g0 = o0 | (o1 << n0);
    const uint g1 = o1 >> (32u - n0);

    // stored from bit r of (x.z, x.w) to bit 63
    const uint keep = (1u << rs) - 1u;
    if (r < 32u)
    {
        x.z = (x.z & keep) | (g0 << rs);
        x.w = (g0 >> (32u - rs)) | (g1 << rs);
    }
    else
    {
        x.w = (x.w & keep) | (g0 << rs);
    }

    if (m == 4u || m == 5u)
    {
        // the 2-bit field ending at s (bits 50-80 / 66-96)
        const uint4 src = x;
        indexPredictionInverseFieldA1(src, x, r + 33u, 2u);
    }
}

// Restores the index fields of a raw BC7 block of the given mode (0-7) as the split transform stores them
void inversePredictIndices(uint mode, inout uint4 rawOut)
{
    if (mode == 6u)
    {
        // the residual field (bits 65-127) is stored bit reversed
        const uint lo = reversebits(rawOut.w), hi = reversebits(rawOut.z);
        rawOut.z = (rawOut.z & 1u) | (lo << 1);
        rawOut.w = (lo >> 31) | (hi << 1);
    }
    if (mode == 4u)
    {
        // the 2-bit residual field (bits 50-80) is stored bit reversed
        const uint l = reversebits((rawOut.y >> 18) | (rawOut.z << 14)) >> 1;
        rawOut.y = (rawOut.y & 0x3FFFFu) | (l << 18);
        rawOut.z = (rawOut.z & ~0x1FFFFu) | (l >> 14);
    }
    if (mode < 8u)
    {
        indexPredictionInverseBlock(rawOut, mode, true);
    }
}