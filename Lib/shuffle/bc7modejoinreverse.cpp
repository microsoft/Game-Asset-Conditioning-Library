//-------------------------------------------------------------------------------------
// bc7modejoinreverse.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

// Reverse of the BC7 Join transform: Transform = Layout o Fields, where Layout is the bare bit layout
// (bc7modejoinlayout.cpp) and Fields is the per-block field transform compiled into the fast paths
// (index residuals, endpoint deltas). The layout is inverted with bit maps probed from the layout itself; the
// field transform is inverted here for the configurations BC7_ModeJoin_FieldConfig() reports.

#include "gacl.h"
#include "../BCnBlockDefs.h"
#include "bc7.h"
#include "bc7anchors.h"
#include "streamheader.h"
#include "bcnex.h"
#include "bc7joinblockcode.h"
#include <intrin.h>
#include <random>
#include <utility>
#include <cstring>

namespace
{
    // Inverse layout for one mode. The layout is linear over GF(2) on the data bits (bits above the mode bit): mostly a
    // bit permutation, but some paths store XORs (Mode 4 without statics keeps rot0 and rot0 ^ rot1). Col[b] is the set
    // of block bits to flip when shuffled bit b is set, relative to the block holding only the mode bit.
    struct LayoutInverse
    {
        uint64_t Col[128][2];
    };

    // Probes the layout of every mode for its mask and inverts it by Gauss-Jordan elimination. The mode bit must
    // land at bit 96 + mode, no data may reach bits 96..96 + mode (where the reverse reads the mode), and the map from
    // the data bits onto the remaining shuffled bits must be a bijection.
    bool BuildLayoutInverse(const BC7TextureMetrics& metrics, LayoutInverse (&inv)[8])
    {
        struct Row
        {
            uint64_t Out[2];    // shuffled bits
            uint64_t In[2];     // block bits producing them
        };
        Row rows[128];
        unsigned pivot[128];
        for (unsigned mode = 0; mode < 8; mode++)
        {
            BC7TextureMetrics modeMetrics = metrics;
            modeMetrics.Statics = BC7_JoinModeMask(metrics, mode);
            modeMetrics.LowEntropy = 0;
            modeMetrics.JoinModeMasks = false;
            memset(inv[mode].Col, 0, sizeof(inv[mode].Col));
            const uint64_t in[2] = { 1ull << mode, 0 };
            uint64_t base[2] = { 0, 0 };   // the fast paths may OR into the output, as BC7_ModeJoin_Transform zeroes it
            BC7_ModeJoin_LayoutShuffle(mode, in, base, modeMetrics);
            if (base[0] != 0 || base[1] != (1ull << (32 + mode)))
            {
                return false;
            }

            const unsigned n = 127 - mode;
            for (unsigned k = 0; k < n; k++)
            {
                const unsigned j = mode + 1 + k;
                uint64_t t[2] = { in[0], in[1] }, o[2] = { 0, 0 };
                t[j >> 6] |= 1ull << (j & 63);
                BC7_ModeJoin_LayoutShuffle(mode, t, o, modeMetrics);
                rows[k] = { { o[0] ^ base[0], o[1] ^ base[1] }, { 0, 0 } };
                rows[k].In[j >> 6] = 1ull << (j & 63);
            }

            unsigned rank = 0;
            for (unsigned b = 0; b < 128 && rank < n; b++)
            {
                const unsigned h = b >> 6;
                const uint64_t bm = 1ull << (b & 63);
                unsigned r = rank;
                while (r < n && !(rows[r].Out[h] & bm))
                {
                    r++;
                }
                if (r == n)
                {
                    continue;
                }
                if (b >= 96 && b <= 96 + mode)
                {
                    return false;
                }
                std::swap(rows[r], rows[rank]);
                for (unsigned k = 0; k < n; k++)
                {
                    if (k != rank && (rows[k].Out[h] & bm))
                    {
                        rows[k].Out[0] ^= rows[rank].Out[0];
                        rows[k].Out[1] ^= rows[rank].Out[1];
                        rows[k].In[0] ^= rows[rank].In[0];
                        rows[k].In[1] ^= rows[rank].In[1];
                    }
                }
                pivot[rank++] = b;
            }
            if (rank != n)
            {
                return false;
            }
            for (unsigned k = 0; k < n; k++)
            {
                const unsigned b = pivot[k];
                if (rows[k].Out[b >> 6] != (1ull << (b & 63)) || rows[k].Out[(b >> 6) ^ 1] != 0)
                {
                    return false;
                }
                inv[mode].Col[b][0] = rows[k].In[0];
                inv[mode].Col[b][1] = rows[k].In[1];
            }
        }
        return true;
    }
    inline uint64_t Get(const uint64_t* x, unsigned p, unsigned w)
    {
        const uint64_t v = p >= 64 ? x[1] >> (p - 64) : ((x[0] >> p) | (p ? x[1] << (64 - p) : 0));
        return w >= 64 ? v : v & ((1ull << w) - 1);
    }

    inline void Put(uint64_t* x, unsigned p, unsigned w, uint64_t v)
    {
        const uint64_t m = w >= 64 ? ~0ull : (1ull << w) - 1;
        v &= m;
        if (p >= 64)
        {
            x[1] = (x[1] & ~(m << (p - 64))) | (v << (p - 64));
            return;
        }
        x[0] = (x[0] & ~(m << p)) | (v << p);
        if (p + w > 64)
        {
            const unsigned s = 64 - p;
            x[1] = (x[1] & ~(m >> s)) | (v >> s);
        }
    }

    // Range-bounded fold (GACL_IZB / GACL_IEB): v ranked by distance from p within [0, M], ties smaller value first.
    inline int Fold(int v, int p, int M)
    {
        const int dd = v - p, ad = dd < 0 ? -dd : dd, sb = p < M - p ? p : M - p;
        return ad <= sb ? (dd > 0 ? 2 * dd : (dd < 0 ? 2 * ad - 1 : 0)) : sb + ad;
    }

    inline int Unfold(int z, int p, int M)
    {
        if (z == 0)
        {
            return p;
        }
        const int sb = p < M - p ? p : M - p;
        if (z <= 2 * sb)
        {
            const int d = (z + 1) >> 1;
            return (z & 1) ? p - d : p + d;
        }
        return p > M - p ? p - (z - sb) : p + (z - sb);
    }

    // Inverse of the mod-2^w zigzag: the signed w-bit difference
    inline int UnZigzag(uint64_t z, unsigned w)
    {
        const uint64_t m = (1ull << w) - 1;
        const uint64_t d = ((z >> 1) ^ (0 - (z & 1))) & m;
        return (d >> (w - 1)) ? int(d) - int(m) - 1 : int(d);
    }

    inline int FloorHalf(int d)
    {
        return d >= 0 ? d / 2 : -((1 - d) / 2);
    }

    // GACL_IMS: (E0, E1) stored as (E0 + floor(sd / 2) mod 2^w, zigzag(sd)) with sd = E1 - E0 as a signed w-bit value
    inline void MidSpanInverse(uint64_t* x, unsigned p0, unsigned p1, unsigned w)
    {
        const int M = (1 << w) - 1;
        const int d = UnZigzag(Get(x, p1, w), w);
        const int e0 = (int(Get(x, p0, w)) - FloorHalf(d)) & M;
        Put(x, p0, w, uint64_t(e0));
        Put(x, p1, w, uint64_t((e0 + d) & M));
    }

    // Production index residuals (GACL_IIP=2 MED, IZB fold, IXE=4 with IXEK2=4 / IXEK4=7 edge extrapolation, IOF=2
    // partition-free view with IOFA anchors). Pixel 0 is stored raw; every other pixel is predicted from causal
    // neighbours as if the block had one subset. Anchors i > 0 (partitioned modes) use w - 1 bits, prediction
    // capped at M / 2.
    inline int Extrapolate(int a, int b, int M)
    {
        const int g = M == 3 ? a + 4 * (a - b) / 4 : (M == 7 ? a + 3 * (a - b) / 4 : a + 7 * (a - b) / 8);
        return g < 0 ? 0 : (g > M ? M : g);
    }

    uint64_t IndexInverse(uint64_t r, unsigned bits, uint32_t anchors)
    {
        const int M = (1 << bits) - 1, MA = M >> 1;
        int res[16], idx[16];
        unsigned pos = 0;
        for (int i = 0; i < 16; i++)
        {
            const unsigned w = ((anchors >> i) & 1) ? bits - 1 : bits;
            res[i] = int((r >> pos) & ((1ull << w) - 1));
            pos += w;
        }
        idx[0] = res[0];
        for (int i = 1; i < 16; i++)
        {
            const int x = i & 3, y = i >> 2;
            int p;
            if (x && y)
            {
                const int L = idx[i - 1], T = idx[i - 4], TL = idx[i - 5];
                const int mx = L > T ? L : T, mn = L < T ? L : T;
                p = TL >= mx ? mn : (TL <= mn ? mx : L + T - TL);
            }
            else if (x)
            {
                p = x >= 2 ? Extrapolate(idx[i - 1], idx[i - 2], M) : idx[i - 1];
            }
            else
            {
                p = y >= 2 ? Extrapolate(idx[i - 4], idx[i - 8], M) : idx[i - 4];
            }
            idx[i] = ((anchors >> i) & 1) ? Unfold(res[i], p < MA ? p : MA, MA) : Unfold(res[i], p, M);
        }
        uint64_t out = 0;
        pos = 0;
        for (int i = 0; i < 16; i++)
        {
            out |= uint64_t(idx[i]) << pos;
            pos += ((anchors >> i) & 1) ? bits - 1 : bits;
        }
        return out;
    }

    // Endpoint fields: channel c, endpoint k at start + w * (c * epc + k)
    struct EndpointLayout { uint8_t Start, W, Epc; };
    const EndpointLayout kEndpoints[8] = { { 5, 4, 6 }, { 8, 6, 4 }, { 9, 5, 6 }, { 10, 7, 4 }, { 8, 5, 2 }, { 8, 7, 2 }, { 7, 7, 2 }, { 14, 5, 4 } };

    // Inverse of the production endpoint transforms (S = Statics | LowEntropy; bits 2c / 2c + 1 flag channel c's E0s / E1s):
    //   flagged channels (GACL_IEDL=7): every E1 folded around its E0
    //   unflagged channels: Modes 5/6 (GACL_IMS=3) midpoint + zigzag span; other modes E1_0 folded around E0_0
    //     and, for subsets s > 0 (GACL_IEPA), zigzag span + E0 folded around the span-aligned subset-0 midpoint
    //   channels with only E1 flagged (GACL_IEP=2): subsets s > 0 E0 folded around the stored subset-0 midpoint
    //   Mode 5/6 alpha: Mode 6 A1 (8-bit P | A << 1) folded around A0, or 7-bit A1 around A0 when both are flagged;
    //     Mode 5 7-bit A1 folded around A0 when flagged, else midpoint + zigzag span
    void EndpointInverse(uint64_t* x, unsigned mode, unsigned S)
    {
        const EndpointLayout& l = kEndpoints[mode];
        const unsigned w = l.W;
        const int M = (1 << w) - 1;
        const auto F = [&](unsigned c, unsigned k) { return unsigned(l.Start + w * (c * l.Epc + k)); };
        const auto UnfoldField = [&](unsigned f, int p) { Put(x, f, w, uint64_t(Unfold(int(Get(x, f, w)), p, M))); };

        if (mode == 5 || mode == 6)
        {
            const unsigned fa = (S >> 6) & 3;
            if (mode == 6 && fa == 3)
            {
                Put(x, 56, 7, uint64_t(Unfold(int(Get(x, 56, 7)), int(Get(x, 49, 7)), 127)));
            }
            else if (mode == 6)
            {
                const int a0 = int(Get(x, 63, 1) | (Get(x, 49, 7) << 1)), z = int(Get(x, 64, 1) | (Get(x, 56, 7) << 1));
                const int a1 = Unfold(z, a0, 255);
                Put(x, 64, 1, uint64_t(a1 & 1));
                Put(x, 56, 7, uint64_t(a1 >> 1));
            }
            else if (fa)
            {
                Put(x, 59, 7, uint64_t(Unfold(int(Get(x, 59, 7)), int(Get(x, 51, 7)), 127)));
            }
            else
            {
                MidSpanInverse(x, 51, 59, 7);
            }
            for (unsigned c = 0; c < 3; c++)
            {
                if ((S >> (2 * c)) & 3)
                {
                    UnfoldField(F(c, 1), int(Get(x, F(c, 0), w)));
                }
                else
                {
                    MidSpanInverse(x, F(c, 0), F(c, 1), w);
                }
            }
            return;
        }

        const unsigned chans = mode == 7 ? 4 : 3;
        for (unsigned c = 0; c < chans; c++)
        {
            const unsigned fl = (S >> (2 * c)) & 3;
            if (fl == 0)
            {
                const int e00 = int(Get(x, F(c, 0), w));
                UnfoldField(F(c, 1), e00);
                const int pr = (e00 + int(Get(x, F(c, 1), w))) >> 1;
                for (unsigned k = 2; k < l.Epc; k += 2)
                {
                    const int d = UnZigzag(Get(x, F(c, k + 1), w), w);
                    int q = pr - FloorHalf(d);
                    q = q < 0 ? 0 : (q > M ? M : q);
                    const int e0 = Unfold(int(Get(x, F(c, k), w)), q, M);
                    Put(x, F(c, k), w, uint64_t(e0));
                    Put(x, F(c, k + 1), w, uint64_t((e0 + d) & M));
                }
                continue;
            }
            if (fl == 2)
            {
                // the subset-0 midpoint is taken from the stored (E1-delta-coded) fields
                const int pr = int(Get(x, F(c, 0), w) + Get(x, F(c, 1), w)) >> 1;
                for (unsigned k = 2; k < l.Epc; k += 2)
                {
                    UnfoldField(F(c, k), pr);
                }
            }
            for (unsigned k = 0; k < l.Epc; k += 2)
            {
                UnfoldField(F(c, k + 1), int(Get(x, F(c, k), w)));
            }
        }
    }

    // Index residuals are computed from the original endpoints, so they're undone after the endpoints.
    void IndexInverseBlock(uint64_t* x, unsigned mode)
    {
        const auto Field = [&](unsigned p, unsigned len, unsigned bits, uint32_t anchors) { Put(x, p, len, IndexInverse(Get(x, p, len), bits, anchors)); };
        switch (mode)
        {
        case 0: Field(83, 45, 3, kBC7Anchor3[Get(x, 1, 4)]); break;
        case 1: Field(82, 46, 3, kBC7Anchor2[Get(x, 2, 6)]); break;
        case 2: Field(99, 29, 2, kBC7Anchor3[Get(x, 3, 6)]); break;
        case 3: Field(98, 30, 2, kBC7Anchor2[Get(x, 4, 6)]); break;
        case 4: Field(50, 31, 2, 1); Field(81, 47, 3, 1); break;
        case 5: Field(66, 31, 2, 1); Field(97, 31, 2, 1); break;
        case 6: Field(65, 63, 4, 1); break;
        default: Field(98, 30, 2, kBC7Anchor2[Get(x, 8, 6)]); break;
        }
    }
}

bool BC7_ModeJoin_Reverse(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, const BC7TextureMetrics& metrics)
{
    const int config = BC7_ModeJoin_FieldConfig();
    if (config < 0 || BC7_ModeJoin_LayoutFieldConfig() != 0 || (srcSize % 16) != 0)
    {
        return false;
    }

    LayoutInverse inv[8];
    if (!BuildLayoutInverse(metrics, inv))
    {
        return false;
    }

    dest.resize(srcSize);
    uint8_t* pDest = dest.data();
    const size_t blockCount = srcSize / 16;
    const unsigned idxRevModes = BC7_ModeJoin_IndexReverseModes();
    uint64_t fieldPerm[8];
    for (unsigned m = 0; m < 8; m++)
    {
        fieldPerm[m] = BC7_ModeJoin_IndexFieldPerm(m);
    }
    for (size_t i = 0; i < blockCount; ++i, src += 16, pDest += 16)
    {
        // undo the 24-bit rotation applied in BC7_ModeJoin_Transform
        uint64_t s[2];
        memcpy(s, src, 16);
        BC7_ModeJoin_ByteRemap(reinterpret_cast<uint8_t*>(s), true, metrics);     // undo any byte remap (metrics.JoinRemap; applied last)
        {
            // undo the index field permutation (applied last; the mode byte, byte 15, is untouched)
            unsigned long m;
            _BitScanForward(&m, (s[1] >> 56) | 0x100u);
            if (m < 8 && fieldPerm[m])
            {
                BC7_ModeJoin_IndexFieldPermute(s, m, fieldPerm[m], true);
            }
        }
        if (idxRevModes)
        {
            // index bits [lo, hi) stored reversed (BC7_ModeJoin_Transform's ranges, which include the P bits stored just
            // below the indices); the mode byte (byte 15) is untouched
            unsigned long m;
            _BitScanForward(&m, (s[1] >> 56) | 0x100u);
            if (m < 8 && ((idxRevModes >> m) & 1))
            {
                static const uint8_t kIdxLo[8] = { 0, 0, 1, 6, 0, 0, 1, 6 }, kIdxHi[8] = { 48, 48, 30, 40, 78, 62, 64, 40 };
                const unsigned lo = kIdxLo[m], hi = kIdxHi[m];
                uint64_t o[2] = { s[0], s[1] };
                for (unsigned k = lo; k < hi; k++)
                {
                    const unsigned j = lo + hi - 1 - k;
                    o[j >> 6] = (o[j >> 6] & ~(1ull << (j & 63))) | (((s[k >> 6] >> (k & 63)) & 1) << (j & 63));
                }
                s[0] = o[0];
                s[1] = o[1];
            }
        }
        const uint64_t ss[2] = { (s[0] >> 24) | (s[1] << 40), (s[1] >> 24) | (s[0] << 40) };
        const uint8_t* ssb = reinterpret_cast<const uint8_t*>(ss);

        // the layout puts the block's mode byte at byte 12
        unsigned long mode;
        _BitScanForward(&mode, ssb[12] | 0x100u);
        if (mode == 8)
        {
            for (size_t b = 0; b < 16; b++)
            {
                pDest[b] = ssb[(b + 12) % 16];
            }
            continue;
        }

        uint64_t x[2] = { 1ull << mode, 0 };
        const uint64_t y[2] = { ss[0], ss[1] & ~(1ull << (32 + mode)) };
        for (unsigned h = 0; h < 2; h++)
        {
            for (uint64_t bits = y[h]; bits; bits &= bits - 1)
            {
                const uint64_t* c = inv[mode].Col[64 * h + unsigned(_tzcnt_u64(bits))];
                x[0] ^= c[0];
                x[1] ^= c[1];
            }
        }

        if (config == 1)
        {
            if (mode != 4)
            {
                const unsigned S = BC7_JoinModeMask(metrics, mode);
                EndpointInverse(x, mode, mode < 4 ? (S & 0x3f) : S);
            }
            IndexInverseBlock(x, mode);
        }
        memcpy(pDest, x, 16);
    }
    return true;
}

// Coded blocks are always coded in the domain of remap set 1 (the GPU's kBC7JoinInvRemap), which kBC7JoinBlockCode was
// trained on: the blocks of a stream without the remap (no parameter 4) are remapped for coding. (With GACL_JBRT off
// the remap, and so this map, is the identity.)
static void JoinCodedBlockMap(uint8_t* block, bool inverse, const std::vector<StreamHeaderParam>& params)
{
    for (const StreamHeaderParam& p : params)
    {
        if (p.Id == GACL_STREAMHEADER_PARAM_BC7_JOIN_REMAP)
        {
            return;
        }
    }
    static const BC7TextureMetrics kSet1 = [] { BC7TextureMetrics m = {}; m.JoinRemap = 1; return m; }();
    BC7_ModeJoin_ByteRemap(block, inverse, kSet1);
}

const StreamHeaderBlockCode* BC7_JoinBlockCode()
{
    static const StreamHeaderBlockCode code = { &kBC7JoinBlockCode[0][0], true, JoinCodedBlockMap };
    return &code;
}

bool BC7_JoinStream_Decode(const uint8_t* src, size_t srcSize, uint8_t transformId, std::vector<uint8_t>& dest, bool uncurve, uint8_t* curveWidthTilesOut)
{
    BC7TextureMetrics metrics = {};
    uint8_t curveWidthTiles = 0;
    std::vector<uint8_t> payload;
    const uint8_t* join = src;
    size_t joinSize = srcSize;

    if (transformId == uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX))
    {
        StreamHeaderInfo info;
        if (!StreamHeader_Decode(src, srcSize, 16, info, payload, BC7_JoinBlockCode()) || info.TransformId != uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN) ||
            info.HeaderBlocks > BCNEX_MAX_HEADER_BLOCKS)
        {
            return false;
        }
        if (const StreamHeaderParam* p = StreamHeader_FindParam(info, GACL_STREAMHEADER_PARAM_BC7_JOIN_STATICS))
        {
            if (p->Value.size() != 1)
            {
                return false;
            }
            metrics.Statics = p->Value[0];
        }
        if (const StreamHeaderParam* p = StreamHeader_FindParam(info, GACL_STREAMHEADER_PARAM_BC7_JOIN_MODE_STATICS))
        {
            if (p->Value.size() != 8)
            {
                return false;
            }
            metrics.JoinModeMasks = true;
            memcpy(metrics.JoinModeMask, p->Value.data(), 8);
        }
        if (const StreamHeaderParam* p = StreamHeader_FindParam(info, GACL_STREAMHEADER_PARAM_CURVE_WIDTH_TILES))
        {
            if (p->Value.size() != 1 || p->Value[0] == 0)
            {
                return false;
            }
            curveWidthTiles = p->Value[0];
        }
        if (const StreamHeaderParam* p = StreamHeader_FindParam(info, GACL_STREAMHEADER_PARAM_BC7_JOIN_REMAP))
        {
            if (p->Value.size() != 1 || p->Value[0] == 0 || p->Value[0] > BC7_ModeJoin_RemapSets())
            {
                return false;
            }
            metrics.JoinRemap = p->Value[0];
        }
        join = payload.data();
        joinSize = payload.size();
    }
    else if (transformId != uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN))
    {
        return false;
    }

    std::vector<uint8_t> reordered45;
    if (!BC7_ModeJoin_Reverse(join, joinSize, reordered45, metrics))
    {
        return false;
    }

    if (curveWidthTiles && uncurve)
    {
        // a curve tile row is 32 BC7 blocks (128 pixels) wide
        std::vector<uint8_t> curved;
        BC7_Mode45_ReorderRGBA(reordered45.data(), reordered45.size(), curved);
        dest.resize(curved.size());
        if (!GACL_Shuffle_ApplySpaceCurve(dest.data(), curved.data(), curved.size(), 16, size_t(curveWidthTiles) * 128, false))
        {
            return false;
        }
    }
    else
    {
        BC7_Mode45_ReorderRGBA(reordered45.data(), reordered45.size(), dest);
    }

    if (curveWidthTilesOut)
    {
        *curveWidthTilesOut = curveWidthTiles;
    }
    return true;
}

// Random-block round trip of BC7_ModeJoin_Transform / BC7_ModeJoin_Reverse for every mode and group-wide mask, plus random
// per-mode masks and the fold, each with a random byte remap set (or none).
// Returns the number of failures (0 when the reverse is unavailable for this build: nothing to test).
size_t BC7_ModeJoin_ReverseSelfTest()
{
    size_t failures = 0;
    for (int M : { 1, 3, 7, 15, 31, 63, 127, 255 })
    {
        for (int p = 0; p <= M; p++)
        {
            for (int v = 0; v <= M; v++)
            {
                const int z = Fold(v, p, M);
                failures += (z < 0 || z > M || Unfold(z, p, M) != v) ? 1 : 0;
            }
        }
    }

    if (BC7_ModeJoin_FieldConfig() < 0)
    {
        return failures;
    }

    std::mt19937_64 rng(0x6ac1);
    constexpr size_t kBlocksPerMode = 48;
    std::vector<uint8_t> blocks(9 * kBlocksPerMode * 16), shuffled, restored, ctrl;
    for (unsigned mask = 0; mask < 256 + 64; mask++)
    {
        uint64_t* b = reinterpret_cast<uint64_t*>(blocks.data());
        for (unsigned mode = 0; mode <= 8; mode++)
        {
            for (size_t i = 0; i < kBlocksPerMode; i++, b += 2)
            {
                b[0] = rng();
                b[1] = rng();
                b[0] = mode == 8 ? b[0] & ~0xffull : (b[0] & ~((2ull << mode) - 1)) | (1ull << mode);
            }
        }
        BC7TextureMetrics metrics = {};
        metrics.Statics = uint8_t(mask & 0x55);
        metrics.LowEntropy = uint8_t(mask & 0xaa);
        metrics.JoinRemap = uint8_t(rng() % (BC7_ModeJoin_RemapSets() + 1));
        if (mask >= 256)
        {
            metrics.JoinModeMasks = true;
            for (unsigned mode = 0; mode < 8; mode++)
            {
                metrics.JoinModeMask[mode] = uint8_t(rng());
            }
        }
        BC7_ModeJoin_Transform(blocks.data(), blocks.size(), shuffled, metrics, ctrl);
        if (!BC7_ModeJoin_Reverse(shuffled.data(), shuffled.size(), restored, metrics))
        {
            failures += 9 * kBlocksPerMode;
            continue;
        }
        for (size_t i = 0; i < 9 * kBlocksPerMode; i++)
        {
            failures += memcmp(blocks.data() + 16 * i, restored.data() + 16 * i, 16) ? 1 : 0;
        }
    }
    return failures;
}
