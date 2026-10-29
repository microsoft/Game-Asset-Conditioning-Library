//-------------------------------------------------------------------------------------
// bc7modejoin.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "gacl.h"
#include "../BCnBlockDefs.h"
#include "bc7.h"
#include "bc7anchors.h"

#include <intrin.h>
#include <immintrin.h>

#include <chrono>
#include <cassert>
#include <algorithm>
#include <windows.h>

namespace 
{

// _pdep_u64 deposit masks that interleave n endpoint fields bit by bit (the non-static fields of the Join layouts): bit b of
// maskTable[n][0] is set when b % n == 0, so field i's bits go to bits i, i + n, i + 2n, ... (mask << i). [1] continues the
// pattern into a second 64-bit word (bit b set when (64 + b) % n == 0) for the layouts that overflow 64 bits; rows 15, 10 and
// 14 (Modes 2, 3 and 7) read it. Row 0 is unused.
const uint64_t maskTable[17][2] = {
    {0b0000000000000000000000000000000000000000000000000000000000000000, // 0 non-static bits
     0b0000000000000000000000000000000000000000000000000000000000000000},

    {0b1111111111111111111111111111111111111111111111111111111111111111, // 1 non-static bit
     0b1111111111111111111111111111111111111111111111111111111111111111},

    {0b0101010101010101010101010101010101010101010101010101010101010101, // 2 non-static bits
     0b0101010101010101010101010101010101010101010101010101010101010101},

    {0b1001001001001001001001001001001001001001001001001001001001001001, // 3 non-static bits
     0b0100100100100100100100100100100100100100100100100100100100100100},

    {0b0001000100010001000100010001000100010001000100010001000100010001, // 4 non-static bits
     0b0001000100010001000100010001000100010001000100010001000100010001},
    
    {0b0001000010000100001000010000100001000010000100001000010000100001, // 5 non-static bits
     0b0010000100001000010000100001000010000100001000010000100001000010},

    {0b0001000001000001000001000001000001000001000001000001000001000001, // 6 non-static bits
     0b0100000100000100000100000100000100000100000100000100000100000100},
    
    {0b1000000100000010000001000000100000010000001000000100000010000001, // 7 non-static bits
     0b0100000010000001000000100000010000001000000100000010000001000000},

    {0b0000000100000001000000010000000100000001000000010000000100000001, // 8 non-static bits
     0b0000000100000001000000010000000100000001000000010000000100000001},

    {0b1000000001000000001000000001000000001000000001000000001000000001, // 9 non-static bits
     0b0100000000100000000100000000100000000100000000100000000100000000},

    {0b0001000000000100000000010000000001000000000100000000010000000001, // 10 non-static bits
     0b0000000100000000010000000001000000000100000000010000000001000000},

    {0b0000000010000000000100000000001000000000010000000000100000000001, // 11 non-static bits
     0b0000001000000000010000000000100000000001000000000010000000000100},

    {0b0001000000000001000000000001000000000001000000000001000000000001,  // 12 non-static bits
     0b0000000100000000000100000000000100000000000100000000000100000000},

    {0b0000000000010000000000001000000000000100000000000010000000000001, // 13 non-static bits
	 0b0000000000100000000000010000000000001000000000000100000000000010}, 

    {0b0000000100000000000001000000000000010000000000000100000000000001, // 14 non-static bits
	 0b0100000000000001000000000000010000000000000100000000000001000000},

    {0b0001000000000000001000000000000001000000000000001000000000000001, // 15 non-static bits
	 0b0000000100000000000000100000000000000100000000000000100000000000},

    {0b0000000000000001000000000000000100000000000000010000000000000001, // 16 non-static bits
     0b0000000000000001000000000000000100000000000000010000000000000001}
};

// The switch defaults are the production Join configuration (BC7_ModeJoin_FieldConfig() == 1, whose field transforms
// bc7modejoinreverse.cpp undoes). bc7modejoinlayout.cpp compiles this file again with the field transforms off
// (FieldConfig 0) to probe the pure bit layout. Only those two configurations are implemented: other switch values may
// not compile, and FieldConfig reports -1 (no reverse) for any that do.
#ifndef GACL_IED
#define GACL_IED 2
#endif
#ifndef GACL_IEDX
#define GACL_IEDX 0x8F  // IEDX: bit m enables the endpoint delta in Mode m (Modes 0-4, 7)
#endif
#ifndef GACL_IEB
#define GACL_IEB 1      // IEB: endpoint delta uses the range-bounded fold (see IZB)
#endif
#if GACL_IED
// IED (2): intra-block endpoint delta, E1 := range-bounded fold (IEB) of E1 - E0
static inline uint64_t BC7_ModeJoin_EndpointDelta(uint64_t e1, uint64_t e0, unsigned bits)
{
    const uint64_t m = (1ull << bits) - 1;
#if GACL_IEB && GACL_IED == 2
    const int64_t p = int64_t(e0 & m), dd = int64_t(e1 & m) - p, ad = dd < 0 ? -dd : dd, sb = p < int64_t(m) - p ? p : int64_t(m) - p;
    return uint64_t(ad <= sb ? (dd > 0 ? 2 * dd : (dd < 0 ? 2 * ad - 1 : 0)) : sb + ad);
#endif
}
#ifndef GACL_IEP
#define GACL_IEP 2      // IEP (partitioned): subset s > 0 E0 coded as a delta from subset 0's E0 (1) or subset 0's midpoint (2)
#endif
#ifndef GACL_IEPM
#define GACL_IEPM 0x8F  // IEP scope: bit m = Mode m
#endif
#ifndef GACL_IEPA
#define GACL_IEPA 1     // IEPA (with IEP): subset s > 0 E1 delta is plain zigzag sd = E1 - E0 mod 2^bits, and E0 is predicted as mid_0 - floor(sd / 2) (midpoint-aligned)
#endif
#ifndef GACL_IMS
#define GACL_IMS 3      // IMS: subset-0 dynamic pairs stored as (wrapped midpoint, zigzag(E1 - E0)): 1 = RGB, 2 = Mode 5/7 alpha
#endif
#ifndef GACL_IMSM
#define GACL_IMSM 0x60  // IMS scope: bit m = Mode m (Mode 4 has no dynamic endpoint deltas)
#endif
#ifndef GACL_IMSP
#define GACL_IMSP 1     // with IMS: IEP predicts subset s > 0 E0s from the stored midpoint
#endif
#if GACL_IMS && GACL_IED != 2
#error "GACL_IMS requires GACL_IED=2"
#endif
#if GACL_IMS
// IMS: (E0, E1) -> (mid, E1') where sd = E1 - E0 as a signed w-bit value, mid = E0 + floor(sd / 2) mod 2^w,
// and E1' is chosen so the layout's later EndpointDelta(E1', mid) emits zigzag(sd).
// Decode: sd = unzigzag(code), E0 = mid - floor(sd / 2), E1 = E0 + sd (mod 2^w).
static inline void BC7_ModeJoin_MidSpan(uint64_t& e0, uint64_t& e1, unsigned w)
{
    const int64_t M = (1ll << w) - 1;
    int64_t sd = int64_t((e1 - e0) & uint64_t(M));
    if (sd > M / 2)
        sd -= M + 1;
    const int64_t h = sd >= 0 ? sd / 2 : -((1 - sd) / 2);
    const int64_t mid = (int64_t(e0) + h) & M;
#if GACL_IEB
    // invert the range-bounded fold around mid
    const int64_t t = sd >= 0 ? 2 * sd : -2 * sd - 1, sb = mid < M - mid ? mid : M - mid;
    const int64_t dd = t <= 2 * sb ? ((t & 1) ? -(t + 1) / 2 : t / 2) : (mid < M - mid ? t - sb : sb - t);
    e1 = uint64_t(mid + dd);
#endif
    e0 = uint64_t(mid);
}
#endif
#if GACL_IEDX
// Delta on consecutive (E0, E1) pairs of a channel-major field array (epc endpoints per channel).
// Mask bit 2c covers channel c's E0s and bit 2c+1 its E1s; a pair is transformed only when both are dynamic.
static inline void BC7_ModeJoin_PairDelta(uint64_t* fv, unsigned channels, unsigned epc, unsigned bits, unsigned statics, unsigned mode = 8, bool alphaFirst = false)
{
    (void)mode;
    (void)alphaFirst;
    for (unsigned c = 0; c < channels; c++)
    {
        uint64_t* f = fv + c * epc;
#if GACL_IEP
        const uint64_t e00 = f[0], e01 = f[1];
#endif
#if GACL_IEPA
        int64_t sdv[4] = {};
        const bool hasd = !(statics & (3u << (2 * c))) && mode < 8 && (GACL_IEPM & (1u << mode));
#endif
        if (!(statics & (3u << (2 * c))))
            for (unsigned p = 0; p < epc; p += 2)
            {
#if GACL_IEPA
                if (p > 0 && hasd)
                {
                    const uint64_t mm = (1ull << bits) - 1, dl = (f[p + 1] - f[p]) & mm;
                    sdv[p >> 1] = (dl >> (bits - 1)) ? int64_t(dl) - int64_t(mm + 1) : int64_t(dl);
                    f[p + 1] = ((dl << 1) ^ (0 - (dl >> (bits - 1)))) & mm;
                    continue;
                }
#endif
                f[p + 1] = BC7_ModeJoin_EndpointDelta(f[p + 1], f[p], bits);
            }
#if GACL_IEP
        // Subset E0s after the E1 deltas (which use the raw E0s); the decoder restores subset 0 first.
        if (mode < 8 && (GACL_IEPM & (1u << mode)) && !(statics & (1u << (2 * c))))
        {
#if GACL_IMS && GACL_IMSP
            // IMS already stored subset 0's midpoint in E0 (fv channel c is alpha first when alphaFirst)
            const unsigned ch = alphaFirst ? (c + 3) & 3 : c;
            const bool ims = (((GACL_IMSM & GACL_IEDX) >> mode) & 1) && !(statics & (3u << (2 * c))) && (GACL_IMS & (ch < 3 ? 1 : 2));
            const uint64_t pr = (GACL_IEP == 2 && !ims) ? (e00 + e01) >> 1 : e00;
#endif
            for (unsigned p = 2; p < epc; p += 2)
            {
#if GACL_IEPA
                if (hasd)
                {
                    // IEPA: align subset s's midpoint with subset 0's (sd is decodable before E0_s)
                    const int64_t sd = sdv[p >> 1], h = sd >= 0 ? sd / 2 : -((1 - sd) / 2), mm = (1ll << bits) - 1;
                    const int64_t g = int64_t(pr) - h;
                    f[p] = BC7_ModeJoin_EndpointDelta(f[p], uint64_t(g < 0 ? 0 : (g > mm ? mm : g)), bits);
                    continue;
                }
#endif
                f[p] = BC7_ModeJoin_EndpointDelta(f[p], pr, bits);
            }
        }
#endif
    }
}
#endif
#endif
#define GACL_IEDM(m) (GACL_IED && (GACL_IEDX & (1 << (m))))
#ifndef GACL_IEDL
#define GACL_IEDL 7     // IEDL: E1 - E0 delta on channel pairs with flagged (static/LE) fields: 1 = both flagged, 2 = E0 only, 4 = E1 only
#endif
#if GACL_IEDL || GACL_IMS
static inline uint64_t BC7_ModeJoin_RawGet(const uint64_t* r, unsigned p, unsigned w)
{
    const unsigned s = p & 63;
    uint64_t v = r[p >> 6] >> s;
    if (s + w > 64)
        v |= r[1] << (64 - s);
    return v & ((1ull << w) - 1);
}
static inline void BC7_ModeJoin_RawPut(uint64_t* r, unsigned p, unsigned w, uint64_t v)
{
    const uint64_t m = (1ull << w) - 1;
    const unsigned s = p & 63;
    v &= m;
    r[p >> 6] = (r[p >> 6] & ~(m << s)) | (v << s);
    if (s + w > 64)
        r[1] = (r[1] & ~(m >> (64 - s))) | (v >> (64 - s));
}
// Raw-block pre-pass (after IIP, before the layout), so every layout path, including the all-flagged fast paths, sees it.
static inline void BC7_ModeJoin_RawPre(uint64_t* r, unsigned mode, unsigned statics)
{
#if GACL_IEDL
    // Endpoint fields of each mode: channel c's endpoint k at bit kStart + kW * (c * kEpc + k), kEpc endpoints per channel
    static const uint8_t kStart[8] = { 5, 8, 9, 10, 8, 8, 7, 14 }, kW[8] = { 4, 6, 5, 7, 5, 7, 7, 5 }, kEpc[8] = { 6, 4, 6, 4, 2, 2, 2, 4 };
    if (mode == 5 || mode == 6 || (GACL_IEDX & (1u << mode)))
    {
        const unsigned S = mode < 4 ? (statics & 0x3f) : statics, w = kW[mode], epc = kEpc[mode];
        const unsigned chans = mode == 7 ? 4 : 3;
        for (unsigned c = 0; c < chans; c++)
        {
            const unsigned fl = (S >> (2 * c)) & 3;
            if (!fl)
                continue;
            const bool dl = (GACL_IEDL & (fl == 3 ? 1 : (fl == 1 ? 2 : 4))) != 0;
            const unsigned pc = kStart[mode] + w * c * epc;
            uint64_t e[6] = {};
            for (unsigned k = 0; k < epc; k++)
                e[k] = BC7_ModeJoin_RawGet(r, pc + w * k, w);
            if (dl)
                for (unsigned k = 0; k < epc; k += 2)
                {
                    BC7_ModeJoin_RawPut(r, pc + w * (k + 1), w, BC7_ModeJoin_EndpointDelta(e[k + 1], e[k], w));
                }
        }
        const unsigned fa = (S >> 6) & 3;
        if (mode >= 4 && mode <= 6 && fa && (GACL_IEDL & (fa == 3 ? 1 : (fa == 1 ? 2 : 4))))
        {
            if (mode == 6 && fa == 3)
                BC7_ModeJoin_RawPut(r, 56, 7, BC7_ModeJoin_EndpointDelta(BC7_ModeJoin_RawGet(r, 56, 7), BC7_ModeJoin_RawGet(r, 49, 7), 7));   // 7-bit A lanes (P-bits stored apart)
            else if (mode == 6)
            {
                // 8-bit P | A << 1 lanes
                const uint64_t a0 = BC7_ModeJoin_RawGet(r, 63, 1) | (BC7_ModeJoin_RawGet(r, 49, 7) << 1), a1 = BC7_ModeJoin_RawGet(r, 64, 1) | (BC7_ModeJoin_RawGet(r, 56, 7) << 1);
                const uint64_t dl = BC7_ModeJoin_EndpointDelta(a1, a0, 8);
                BC7_ModeJoin_RawPut(r, 64, 1, dl);
                BC7_ModeJoin_RawPut(r, 56, 7, dl >> 1);
            }
            else
            {
                const unsigned a0 = mode == 4 ? 38 : 50, a1 = mode == 4 ? 44 : 58, aw = mode == 4 ? 6 : 8;
                BC7_ModeJoin_RawPut(r, a1 + 1, aw - 1, BC7_ModeJoin_EndpointDelta(BC7_ModeJoin_RawGet(r, a1 + 1, aw - 1), BC7_ModeJoin_RawGet(r, a0 + 1, aw - 1), aw - 1));
            }
        }
    }
#endif
#if GACL_IMS
    if (((GACL_IMSM >> mode) & 1) && (mode == 5 || mode == 6 || (GACL_IEDX & (1u << mode))))
    {
        // the endpoint fields of kStart / kW / kEpc (which only exist with IEDL)
        static const uint8_t kStartM[8] = { 5, 8, 9, 10, 8, 8, 7, 14 }, kWM[8] = { 4, 6, 5, 7, 5, 7, 7, 5 }, kEpcM[8] = { 6, 4, 6, 4, 2, 2, 2, 4 };
        const unsigned S = mode < 4 ? (statics & 0x3f) : statics, w = kWM[mode];
        const unsigned chans = (mode == 7 && (GACL_IMS & 2)) ? 4 : 3;
        for (unsigned c = ((GACL_IMS & 1) ? 0 : 3); c < chans; c++)
        {
            if ((S >> (2 * c)) & 3)
                continue;
            const unsigned pc = kStartM[mode] + w * c * kEpcM[mode];
            uint64_t e0 = BC7_ModeJoin_RawGet(r, pc, w), e1 = BC7_ModeJoin_RawGet(r, pc + w, w);
            BC7_ModeJoin_MidSpan(e0, e1, w);
            BC7_ModeJoin_RawPut(r, pc, w, e0);
            BC7_ModeJoin_RawPut(r, pc + w, w, e1);
        }
#if GACL_IMS & 2
        if (mode == 5 && !(S & 0xC0))
        {
            // upper 7 alpha bits (the ex LSBs stay raw)
            uint64_t a0 = BC7_ModeJoin_RawGet(r, 51, 7), a1 = BC7_ModeJoin_RawGet(r, 59, 7);
            BC7_ModeJoin_MidSpan(a0, a1, 7);
            BC7_ModeJoin_RawPut(r, 51, 7, a0);
            BC7_ModeJoin_RawPut(r, 59, 7, a1);
        }
#endif
    }
#endif
}
#endif

#ifndef GACL_IIP
#define GACL_IIP 2      // IIP (2): index residuals vs the MED spatial predictor, folded (IZB)
#endif
#ifndef GACL_IIPM
#define GACL_IIPM 511   // IIP scope: 1 = Mode 6, 2 = Mode 5 color, 4 = Mode 5 alpha, 8 = Mode 4, 16/32/64/128 = Modes 0/1/2/3, 256 = Mode 7
#endif
#ifndef GACL_IZB
#define GACL_IZB 1      // IZB: map index residuals with a range-bounded fold
#endif
#ifndef GACL_IXE
#define GACL_IXE 4      // IXE (4): row-0 / column-0 pixels with two same-subset predecessors extrapolate a + k(a - b), clamped; k = 3/4
#endif
#ifndef GACL_IXEK2
#define GACL_IXEK2 4    // IXEK2 (with IXE): slope in quarters for 2-bit indices (0 = same as IXE)
#endif
#ifndef GACL_IXEK4
#define GACL_IXEK4 7    // IXEK4 (with IXE): slope in eighths for 4-bit indices (0 = same as IXE)
#endif
#ifndef GACL_IOF
#define GACL_IOF 2      // IOF (2, partitioned): predict over all causal neighbours, ignoring the partition
#endif
#ifndef GACL_IOFM
#define GACL_IOFM 0xFF  // IOF scope: bit m = Mode m
#endif
#ifndef GACL_IOFA
#define GACL_IOFA 1     // IOFA (with IOF 2): subset s > 0 anchors predicted too (raster order), residual folded within their bits - 1 range
#endif
#ifndef GACL_IAS
#define GACL_IAS 2      // IAS (2, partitioned): pixels with no same-subset left / top neighbour use the nearest same-subset causal pixel or anchor
#endif
#if GACL_IIP && GACL_IXE
static inline int BC7_ModeJoin_Extrap(int a, int b, int m)
{
#if GACL_IXE == 4
    int g = a + 3 * (a - b) / 4;
#endif
#if GACL_IXEK2
    if (m == 3)
        g = a + GACL_IXEK2 * (a - b) / 4;   // IXEK2: slope (in quarters) for 2-bit indices
#endif
#if GACL_IXEK4
    if (m == 15)
        g = a + GACL_IXEK4 * (a - b) / 8;   // IXEK4: slope (in eighths) for 4-bit indices
#endif
    return g < 0 ? 0 : (g > m ? m : g);
}
#endif
#if GACL_IIP
// v = 16 packed indices LSB-first; anchor pixels (set bits of 'anchors') have (bits - 1) bits and are left unchanged,
// the rest have 'bits' bits and become zigzag(idx - pred) mod 2^bits. part holds the 2-bit subset id of each pixel;
// the predictor only uses causal neighbours of the same subset. With one subset this is left (row 0), top (column 0),
// else the GACL_IIP predictor.
static inline uint64_t BC7_ModeJoin_IndexResidual(uint64_t v, unsigned bits, uint32_t part = 0, uint32_t anchors = 1, bool iof = true)
{
    const int m = (1 << bits) - 1;
    int idx[16];
    unsigned pos = 0;
    for (int i = 0; i < 16; i++)
    {
        const unsigned w = ((anchors >> i) & 1) ? bits - 1 : bits;
        idx[i] = int((v >> pos) & ((1ull << w) - 1));
        pos += w;
    }
    bool iofOn = false;
    (void)iof; (void)iofOn;
#if GACL_IOF
    // IOF: anchors stay raw; every other pixel is predicted ignoring the partition
    if (part && iof)
    {
        iofOn = true;
        part = 0;
    }
#endif
    uint64_t out = 0;
    pos = 0;
    for (int i = 0; i < 16; i++)
    {
        const bool anc = (anchors >> i) & 1;
#if GACL_IOFA && GACL_IOF == 2
        const bool ancPred = anc && iofOn && i > 0;     // no forward neighbour use in the partition-free view
#endif
        if (anc && !ancPred)
        {
            out |= uint64_t(idx[i]) << pos;
            pos += bits - 1;
            continue;
        }
        const int x = i & 3, y = i >> 2;
        const uint32_t s = (part >> (2 * i)) & 3;
        const bool hasL = x > 0 && ((part >> (2 * (i - 1))) & 3) == s;
        const bool hasT = y > 0 && ((part >> (2 * (i - 4))) & 3) == s;
        const bool hasTL = x > 0 && y > 0 && ((part >> (2 * (i - 5))) & 3) == s;
        int vL = hasL ? idx[i - 1] : -1, vT = hasT ? idx[i - 4] : -1, vTL = hasTL ? idx[i - 5] : -1;
        int p = -1;
        if (vL >= 0 && vT >= 0 && vTL >= 0)
        {
            const int L = vL, T = vT, TL = vTL;
            const int mx = L > T ? L : T, mn = L < T ? L : T;
            p = TL >= mx ? mn : (TL <= mn ? mx : L + T - TL);
        }
        else if (vL >= 0)
        {
            p = vL;
#if GACL_IXE
            if (hasL && !hasT && x >= 2 && ((part >> (2 * (i - 2))) & 3) == s)
                p = BC7_ModeJoin_Extrap(idx[i - 1], idx[i - 2], m);
#endif
        }
        else if (vT >= 0)
        {
            p = vT;
#if GACL_IXE
            if (hasT && !hasL && y >= 2 && ((part >> (2 * (i - 8))) & 3) == s)
                p = BC7_ModeJoin_Extrap(idx[i - 4], idx[i - 8], m);
#endif
        }
        else
        {
#if GACL_IAS == 2
            // nearest (Manhattan, lowest index on ties) same-subset pixel that is causal or an anchor (anchors are stored raw)
            int bd = 99;
            for (int j = 0; j < 16; j++)
                if (j != i && ((part >> (2 * j)) & 3) == s && (j < i || ((anchors >> j) & 1)))
                {
                    const int dx = (j & 3) - x, dy = (j >> 2) - y;
                    const int dd = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                    if (dd < bd) { bd = dd; p = idx[j]; }
                }
#endif
        }
        uint64_t z = uint64_t(idx[i]);
        const int mm = ancPred ? (m >> 1) : m;
        const unsigned wb = ancPred ? bits - 1 : bits;
#if GACL_IOFA
        if (ancPred && p > mm)
            p = mm;
#endif
        if (p >= 0)
        {
#if GACL_IZB
            // IZB: range-bounded fold; symmetric zigzag while both sides fit, then the remaining one-sided distances
            const int dd = idx[i] - p, ad = dd < 0 ? -dd : dd, sb = p < mm - p ? p : mm - p;
            z = uint64_t(ad <= sb ? (dd > 0 ? 2 * dd : (dd < 0 ? 2 * ad - 1 : 0)) : sb + ad);
#endif
        }
        out |= z << pos;
        pos += wb;
    }
    return out;
}
#if GACL_IIPM & 0x1F0
// BC7 partition tables of the 2- and 3-subset modes, by partition number: the subset of pixel i at bits 2i (the anchor pixel
// masks, kBC7Anchor2/3, are in bc7anchors.h)
static const uint32_t kBC7Part2[64] = {
    0x50505050, 0x40404040, 0x54545454, 0x54505040, 0x50404000, 0x55545450, 0x55545040, 0x54504000,
    0x50400000, 0x55555450, 0x55544000, 0x54400000, 0x55555440, 0x55550000, 0x55555500, 0x55000000,
    0x55150100, 0x00004054, 0x15010000, 0x00405054, 0x00004050, 0x15050100, 0x05010000, 0x40505054,
    0x00404050, 0x05010100, 0x14141414, 0x05141450, 0x01155440, 0x00555500, 0x15014054, 0x05414150,
    0x44444444, 0x55005500, 0x11441144, 0x05055050, 0x05500550, 0x11114444, 0x41144114, 0x44111144,
    0x15055054, 0x01055040, 0x05041050, 0x05455150, 0x14414114, 0x50050550, 0x41411414, 0x00141400,
    0x00041504, 0x00105410, 0x10541000, 0x04150400, 0x50410514, 0x41051450, 0x05415014, 0x14054150,
    0x41050514, 0x41505014, 0x40011554, 0x54150140, 0x50505500, 0x00555050, 0x15151010, 0x54540404,
};
static const uint32_t kBC7Part3[64] = {
    0xAA685050, 0x6A5A5040, 0x5A5A4200, 0x5450A0A8, 0xA5A50000, 0xA0A05050, 0x5555A0A0, 0x5A5A5050,
    0xAA550000, 0xAA555500, 0xAAAA5500, 0x90909090, 0x94949494, 0xA4A4A4A4, 0xA9A59450, 0x2A0A4250,
    0xA5945040, 0x0A425054, 0xA5A5A500, 0x55A0A0A0, 0xA8A85454, 0x6A6A4040, 0xA4A45000, 0x1A1A0500,
    0x0050A4A4, 0xAAA59090, 0x14696914, 0x69691400, 0xA08585A0, 0xAA821414, 0x50A4A450, 0x6A5A0200,
    0xA9A58000, 0x5090A0A8, 0xA8A09050, 0x24242424, 0x00AA5500, 0x24924924, 0x24499224, 0x50A50A50,
    0x500AA550, 0xAAAA4444, 0x66660000, 0xA5A0A5A0, 0x50A050A0, 0x69286928, 0x44AAAA44, 0x66666600,
    0xAA444444, 0x54A854A8, 0x95809580, 0x96969600, 0xA85454A8, 0x80959580, 0xAA141414, 0x96960000,
    0xAAAA1414, 0xA05050A0, 0xA0A5A5A0, 0x96000000, 0x40804080, 0xA9A8A9A8, 0xAAAAAA44, 0x2A4A5254,
};
// Index bits [ipos, ipos + ilen) of raw[1] -> residuals; partition id at raw[0] bits [pshift, pshift + 6) & pmask.
static inline void BC7_ModeJoin_IndexResidualPart(uint64_t* raw, unsigned pshift, unsigned pmask, const uint32_t* part, const uint16_t* anchors, unsigned ipos, unsigned ilen, unsigned bits)
{
    const unsigned pi = unsigned(raw[0] >> pshift) & pmask;
    const uint64_t lm = (1ull << ilen) - 1;
    const unsigned mode = pshift == 8 ? 7 : pshift - 1;     // partition id follows the mode bits (Mode 7: 8)
    const bool iof = ((GACL_IOFM >> mode) & 1) != 0;
    raw[1] = (raw[1] & ~(lm << ipos)) | (BC7_ModeJoin_IndexResidual((raw[1] >> ipos) & lm, bits, part[pi], anchors[pi], iof) << ipos);
}
#endif
#endif

// Mode 4 index field (bits 50..127) -> first 24 bits (ee) and remaining 54 bits (es)
static inline void BC7_ModeJoin_M4Index(const uint64_t* raw, uint64_t& es, uint64_t& ee)
{
    uint64_t lo32 = ((raw[0] >> 50) | (raw[1] << 14)) & 0xffffffffull;
    ee = lo32 & 0xffffff;
    es = (lo32 >> 24) | ((raw[1] >> 18) << 8);
}
// Mode 5 index field (bits 66..127) -> first 24 bits (ee) and remaining 38 bits (es)
static inline void BC7_ModeJoin_M5Index(const uint64_t* raw, uint64_t& es, uint64_t& ee)
{
    uint64_t f = raw[1] >> 2;
    ee = f & 0xffffff;
    es = f >> 24;
}

void BC7_ModeJoin_Shuffle_Fast_Mode0(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = (metrics.Statics | metrics.LowEntropy) & 0x3f;
#if GACL_IIP && (GACL_IIPM & 16)
    BC7m0 iipBlk = *reinterpret_cast<const BC7m0*>(src);
    BC7_ModeJoin_IndexResidualPart(iipBlk.Raw, 1, 0xf, kBC7Part3, kBC7Anchor3, 19, 45, 3);
#if GACL_IEDL || GACL_IMS
    BC7_ModeJoin_RawPre(iipBlk.Raw, 0, statics);
#endif
    const BC7m0* b = &iipBlk;
#else
    const BC7m0* b = reinterpret_cast<const BC7m0*>(src);
#endif
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData[2] = { 0, 0 };
    if (statics)
    {
        if (staticCount == 6) // All color channel pairs are static - simple copy case
        {
            eppData[0] = (b->Raw[0] >> 5) | (b->Raw[1] << 59);
            eppData[1] = ((b->Raw[1] >> 5) & 0xff);
        }
        else
        {
            // Mixed static/dynamic fields - need to separate and reorganize
            // Precomputed masks handle interleaving patterns for different field counts
            const uint8_t nonStaticCount = 6 - staticCount;

            // Separate fields into static and non-static lists for processing
            uint8_t staticFields[18] = {};
            uint8_t nonStaticFields[18] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Group endpoint pairs by static/dynamic status
            // Each field covers 3 endpoint pairs (e.g., R0+R2+R4, R1+R3+R5)
            for (uint8_t field = 0; field < 6; field++)
            {
                uint8_t epp0Index = field + (field - field % 2) * 2;
                uint8_t epp1Index = epp0Index + 2;
                uint8_t epp2Index = epp0Index + 4;
                if (statics & (1 << field))
                {
                    staticFields[staticIndex++] = epp0Index;
                    staticFields[staticIndex++] = epp1Index;
                    staticFields[staticIndex++] = epp2Index;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = epp0Index;
                    nonStaticFields[nonStaticIndex++] = epp1Index;
                    nonStaticFields[nonStaticIndex++] = epp2Index;
                }
            }

            // Extract all field values from the source block
#if GACL_IEDM(0)
            uint64_t fieldValues[18] = { b->R0, b->R1, b->R2, b->R3, b->R4, b->R5,
                                              b->G0, b->G1, b->G2, b->G3, b->G4, b->G5,
                                              b->B0, b->B1, b->B2(), b->B3, b->B4, b->B5 };
            BC7_ModeJoin_PairDelta(fieldValues, 3, 6, 4, statics, 0);
#else
            const uint64_t fieldValues[18] = { b->R0, b->R1, b->R2, b->R3, b->R4, b->R5,
                                              b->G0, b->G1, b->G2, b->G3, b->G4, b->G5,
                                              b->B0, b->B1, b->B2(), b->B3, b->B4, b->B5 };
#endif

            // ORD2: channel-major order (LSB first) R0..R5 G0..G5 B0..B5
            const uint8_t mapToShuffledOrder[18] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 };

            // Sort non-static fields by their target shuffled positions
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount * 3,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Keep static fields in original order for contiguous placement
            std::sort(staticFields, staticFields + staticCount * 3);

            // Build interleaved endpoint data for non-static fields using bit deposition
            uint64_t nonStaticEpp = 0;
            if (nonStaticCount > 0)
            {
                const uint8_t totalNonStaticFields = nonStaticCount * 3;
                const uint64_t mask = maskTable[totalNonStaticFields][0];
                for (uint8_t i = 0; i < totalNonStaticFields; i++)
                {
                    const uint8_t fieldIdx = nonStaticFields[i];
                    nonStaticEpp |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                }
            }

            // Pack static fields consecutively (4 bits each)
            uint64_t staticData[2] = { 0, 0 };
            for (uint8_t i = 0; i < uint8_t(staticCount * 3); i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                if (i * 4 < 64)
                {
                    staticData[0] |= (uint64_t(fieldValues[fieldIdx]) << (i * 4));
                    if (i * 4 > 60)
                    {
                        staticData[1] |= (uint64_t(fieldValues[fieldIdx]) >> (64 - i * 4));
                    }
                }
                else
                {
                    staticData[1] |= (uint64_t(fieldValues[fieldIdx]) << (i * 4 - 64));
                }
            }

            // Combine interleaved non-static data with packed static data
            const uint8_t nonStaticBits = nonStaticCount * 3 * 4;

            eppData[0] = nonStaticEpp | (staticData[0] << nonStaticBits);
            eppData[1] = (staticData[0] >> (64 - nonStaticBits)) | (staticData[1] << nonStaticBits);
        }

        uint64_t extra = b->Raw[1] >> 13;                     // Extract P-bits and index data (51 bits)
        uint64_t mode_part = (b->Raw[0] & 0x1f);                     // Extract mode and partition bits (5 bits)

        // Assemble final shuffled layout with endpoint data positioned optimally
        d[0] = (extra >> 27) |
            (eppData[0] << 24);

        d[1] = (eppData[0] >> 40) |
            (eppData[1] << 24) |
            (mode_part << 32) |
            (extra << 37);
    }
    else
    {
        // All fields dynamic - join all 18 endpoint fields into one plane interleave
        // (4 planes x 18 lanes, channel-major R0..R5 G0..G5 B0..B5) at bits 24-95, using the static path frame.
#if GACL_IEDM(0)
        uint64_t fieldValues[18] = { b->R0, b->R1, b->R2, b->R3, b->R4, b->R5,
                                          b->G0, b->G1, b->G2, b->G3, b->G4, b->G5,
                                          b->B0, b->B1, b->B2(), b->B3, b->B4, b->B5 };
        BC7_ModeJoin_PairDelta(fieldValues, 3, 6, 4, 0, 0);
#else
        const uint64_t fieldValues[18] = { b->R0, b->R1, b->R2, b->R3, b->R4, b->R5,
                                          b->G0, b->G1, b->G2, b->G3, b->G4, b->G5,
                                          b->B0, b->B1, b->B2(), b->B3, b->B4, b->B5 };
#endif
        const uint64_t planeMask = 0x0000001000040001ull;     // bits 0, 18, 36 (planes 0-2)

        uint64_t p3 = 0;
        for (uint8_t k = 0; k < 18; k++)
        {
            eppData[0] |= _pdep_u64(fieldValues[k], planeMask << k);   // field bits 0-2 -> planes 0-2
            p3 |= ((fieldValues[k] >> 3) & 1) << k;                    // field bit 3 -> plane 3
        }
        eppData[0] |= p3 << 54;
        eppData[1] = p3 >> 10;

        uint64_t extra = b->Raw[1] >> 13;                     // Extract P-bits and index data (51 bits)
        uint64_t mode_part = (b->Raw[0] & 0x1f);                     // Extract mode and partition bits (5 bits)

        d[0] = (extra >> 27) |
            (eppData[0] << 24);

        d[1] = (eppData[0] >> 40) |
            (eppData[1] << 24) |
            (mode_part << 32) |
            (extra << 37);
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode1(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = (metrics.Statics | metrics.LowEntropy) & 0x3f;
#if GACL_IIP && (GACL_IIPM & 32)
    BC7m1 iipBlk = *reinterpret_cast<const BC7m1*>(src);
    BC7_ModeJoin_IndexResidualPart(iipBlk.Raw, 2, 0x3f, kBC7Part2, kBC7Anchor2, 18, 46, 3);
#if GACL_IEDL || GACL_IMS
    BC7_ModeJoin_RawPre(iipBlk.Raw, 1, statics);
#endif
    const BC7m1* b = &iipBlk;
#else
    const BC7m1* b = reinterpret_cast<const BC7m1*>(src);
#endif
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData[2] = { 0, 0 };
    if (statics)
    {
        if (staticCount == 6) // All color channel pairs are static - direct extraction
        {
            eppData[0] = (b->Raw[0] >> 8) | (b->Raw[1] << 56);
            eppData[1] = ((b->Raw[1] >> 8) & 0xff);
        }
        else
        {
            // Mixed static/dynamic case requiring field separation and reorganization
            const uint8_t nonStaticCount = 6 - staticCount;

            // Build separate lists for static and dynamic field processing
            uint8_t staticFields[12] = {};
            uint8_t nonStaticFields[12] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Classify fields based on static mask (2 endpoint pairs per field)
            for (uint8_t field = 0; field < 6; field++)
            {
                uint8_t epp0Index = (field % 2 ? field * 2 - 1 : field * 2);
                uint8_t epp1Index = epp0Index + 2;
                if (statics & (1 << field))
                {
                    staticFields[staticIndex++] = epp0Index;
                    staticFields[staticIndex++] = epp1Index;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = epp0Index;
                    nonStaticFields[nonStaticIndex++] = epp1Index;
                }
            }

            // Extract field values from source block structure
#if GACL_IEDM(1)
            uint64_t fieldValues[12] = { b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3, b->B0, b->B1(), b->B2, b->B3 };
            BC7_ModeJoin_PairDelta(fieldValues, 3, 4, 6, statics, 1);
#else
            const uint64_t fieldValues[12] = { b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3, b->B0, b->B1(), b->B2, b->B3 };
#endif

            // ORD2: channel-major order (LSB first) R0..R3 G0..G3 B0..B3
            const uint8_t mapToShuffledOrder[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

            // Sort non-static fields by their target positions for optimal layout
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount * 2,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Maintain original order for static fields (contiguous placement)
            std::sort(staticFields, staticFields + staticCount * 2);

            // Create interleaved endpoint pattern for non-static fields
            uint64_t nonStaticEpp = 0;
            if (nonStaticCount > 0)
            {
                const uint8_t totalNonStaticFields = nonStaticCount * 2;
                const uint64_t mask = maskTable[totalNonStaticFields][0];
                for (uint8_t i = 0; i < totalNonStaticFields; i++)
                {
                    const uint8_t fieldIdx = nonStaticFields[i];
                    nonStaticEpp |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                }
            }

            // Pack static fields consecutively (6 bits each for Mode 1)
            uint64_t staticData[2] = { 0, 0 };
            for (uint8_t i = 0; i < uint8_t(staticCount * 2); i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                if (i * 6 < 64)
                {
                    staticData[0] |= (uint64_t(fieldValues[fieldIdx]) << (i * 6));
                    if (i * 6 > 58)
                    {
                        staticData[1] |= (uint64_t(fieldValues[fieldIdx]) >> (64 - i * 6));
                    }
                }
                else
                {
                    staticData[1] |= (uint64_t(fieldValues[fieldIdx]) << (i * 6 - 64));
                }
            }

            // Combine interleaved and static data with proper bit alignment
            const uint8_t nonStaticBits = nonStaticCount * 2 * 6;

            eppData[0] = nonStaticEpp | (staticData[0] << nonStaticBits);
            eppData[1] = (staticData[0] >> (64 - nonStaticBits)) | (staticData[1] << nonStaticBits);
        }

        uint64_t extra = b->Raw[1] >> 16;                     // Extract P-bits and index data (48 bits)
        uint64_t mode_part = (b->Raw[0] & 0xff);                     // Extract mode and partition (8 bits)

        // Assemble final layout with endpoint data positioned for compression efficiency
        d[0] = (extra >> 24) |
            (eppData[0] << 24);

        d[1] = (eppData[0] >> 40) |
            (eppData[1] << 24) |
            (mode_part << 32) |
            (extra << 40);
    }
    else
    {
        // All fields dynamic - join all 12 endpoint fields into one 72-bit plane interleave (6 planes x 12 lanes)
        // at bits 24-95, using the same frame as the static path. Lane order is channel-major B0..B3 R0..R3 G0..G3,
        // so reading down from the mode byte gives G, R, B (matching Mode 3).
#if GACL_IEDM(1)
        uint64_t fieldValues[12] = { b->B0, b->B1(), b->B2, b->B3, b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3 };
        BC7_ModeJoin_PairDelta(fieldValues, 3, 4, 6, 0, 1);
#else
        const uint64_t fieldValues[12] = { b->B0, b->B1(), b->B2, b->B3, b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3 };
#endif
        const uint64_t planeMask = 0x0001001001001001ull;     // bits 0, 12, 24, 36, 48 (planes 0-4)

        uint64_t lo = 0, top = 0;
        for (uint8_t k = 0; k < 12; k++)
        {
            lo |= _pdep_u64(fieldValues[k], planeMask << k);    // field bits 0-4 -> planes 0-4
            top |= ((fieldValues[k] >> 5) & 1) << k;            // field bit 5 -> plane 5
        }
        eppData[0] = lo | (top << 60);
        eppData[1] = top >> 4;

        uint64_t extra = b->Raw[1] >> 16;                     // Extract P-bits and index (48 bits)
        uint64_t mode_part = (b->Raw[0] & 0xff);                     // Extract mode and partition (8 bits)

        d[0] = (extra >> 24) |
            (eppData[0] << 24);

        d[1] = (eppData[0] >> 40) |
            (eppData[1] << 24) |
            (mode_part << 32) |
            (extra << 40);
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode2(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = (metrics.Statics | metrics.LowEntropy) & 0x3f;
#if GACL_IIP && (GACL_IIPM & 64)
    BC7m2 iipBlk = *reinterpret_cast<const BC7m2*>(src);
    BC7_ModeJoin_IndexResidualPart(iipBlk.Raw, 3, 0x3f, kBC7Part3, kBC7Anchor3, 35, 29, 2);
#if GACL_IEDL || GACL_IMS
    BC7_ModeJoin_RawPre(iipBlk.Raw, 2, statics);
#endif
    const BC7m2* b = &iipBlk;
#else
    const BC7m2* b = reinterpret_cast<const BC7m2*>(src);
#endif
    //uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData[2] = { 0, 0 };
    if (statics)
    {
        if (staticCount == 6) // All color fields static - simple copy optimization
        {
            eppData[0] = (b->Raw[0] >> 9) | (b->Raw[1] << 55);
            eppData[1] = ((b->Raw[1] >> 9) & 0x3ffffff);
        }
        else
        {
            // Mixed static/dynamic requiring complex field reorganization
            const uint8_t nonStaticCount = 6 - staticCount;

            // Separate processing lists for static vs dynamic fields
            uint8_t staticFields[18] = {};
            uint8_t nonStaticFields[18] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Mode 2 has 3 endpoint pairs per color field grouping
            for (uint8_t field = 0; field < 6; field++)
            {
                uint8_t epp0Index = field + (field - field % 2) * 2;
                uint8_t epp1Index = epp0Index + 2;
                uint8_t epp2Index = epp0Index + 4;
                if (statics & (1 << field))
                {
                    staticFields[staticIndex++] = epp0Index;
                    staticFields[staticIndex++] = epp1Index;
                    staticFields[staticIndex++] = epp2Index;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = epp0Index;
                    nonStaticFields[nonStaticIndex++] = epp1Index;
                    nonStaticFields[nonStaticIndex++] = epp2Index;
                }
            }

            // Extract 5-bit color values from all endpoint positions
#if GACL_IEDM(2)
            uint64_t fieldValues[18] = { b->R0, b->R1, b->R2, b->R3, b->R4, b->R5,
                                              b->G0, b->G1, b->G2, b->G3, b->G4, b->G5,
                                              b->B0, b->B1, b->B2, b->B3, b->B4, b->B5 };
            BC7_ModeJoin_PairDelta(fieldValues, 3, 6, 5, statics, 2);
#else
            const uint64_t fieldValues[18] = { b->R0, b->R1, b->R2, b->R3, b->R4, b->R5,
                                              b->G0, b->G1, b->G2, b->G3, b->G4, b->G5,
                                              b->B0, b->B1, b->B2, b->B3, b->B4, b->B5 };
#endif

            // ORD2: channel-major order (LSB first) R0..R5 G0..G5 B0..B5
            const uint8_t mapToShuffledOrder[18] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 };

            // Sort fields by target shuffled positions for layout optimization
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount * 3,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Keep static fields in original order for contiguous placement
            std::sort(staticFields, staticFields + staticCount * 3);

            // Build interleaved pattern for non-static fields (may span two uint64s)
            uint64_t nonStaticEpp[2] = { 0, 0 };
            if (nonStaticCount > 0)
            {
                const uint8_t totalNonStaticFields = nonStaticCount * 3;

                if (totalNonStaticFields <= 12)  // Fits in single uint64 with interleaving
                {
                    const uint64_t mask = maskTable[totalNonStaticFields][0];
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        nonStaticEpp[0] |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                    }
                }
                else  // More fields require spanning two uint64s with split masks
                {
                    const uint64_t mask15_0 = maskTable[15][0];
                    const uint64_t mask15_1 = maskTable[15][1];

                    // Process using split mask approach for >60 bit patterns
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        const uint64_t fieldValue = fieldValues[fieldIdx];

                        // Deposit first 4 bits using primary mask
                        nonStaticEpp[0] |= _pdep_u64(fieldValue, mask15_0 << i);

                        // Deposit 5th bit using secondary mask for overflow
                        if (i > 3)
                            nonStaticEpp[1] |= _pdep_u64((fieldValue >> 4u) & 0x1u, mask15_1 >> (15u - i));
                    }
                }
            }

            // Pack static fields consecutively (5 bits each for Mode 2)
            uint64_t staticData[2] = { 0, 0 };
            for (uint8_t i = 0; i < uint8_t(staticCount * 3); i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                if (i * 5 < 64)
                {
                    staticData[0] |= (uint64_t(fieldValues[fieldIdx]) << (i * 5));
                    if (i * 5 > 59)
                    {
                        staticData[1] |= (uint64_t(fieldValues[fieldIdx]) >> (64 - i * 5));
                    }
                }
                else
                {
                    staticData[1] |= (uint64_t(fieldValues[fieldIdx]) << (i * 5 - 64));
                }
            }

            // Combine data with proper alignment for >64 bit cases
            const uint8_t nonStaticBits = nonStaticCount * 3 * 5;

            if (nonStaticBits > 64)
            {
                eppData[0] = nonStaticEpp[0];
                eppData[1] = nonStaticEpp[1] | (staticData[0] << (nonStaticBits - 64));
            }
            else
            {
                eppData[0] = nonStaticEpp[0] | (staticData[0] << nonStaticBits);
                eppData[1] = (staticData[0] >> (64 - nonStaticBits)) | (staticData[1] << nonStaticBits);
            }
        }

        uint64_t* d = reinterpret_cast<uint64_t*>(dest);
        uint64_t extra = b->Raw[1] >> 35;                     // Extract index data (29 bits)
        uint64_t mode_part = (b->Raw[0] & 0x1ff);                     // Extract mode and partition (9 bits)

        // Position data for optimal compression with byte-aligned mode placement
        d[0] = (extra >> 23) |
            (eppData[0] << 6);

        d[1] = (eppData[0] >> 58) |
            (eppData[1] << 6) |
            (mode_part << 32) |
            (extra << 41);
    }
    else
    {
        // All fields dynamic - join all 18 endpoint fields into one plane interleave
        // (5 planes x 18 lanes, channel-major R0..R5 G0..G5 B0..B5) at bits 6-95, using the static path frame.
#if GACL_IEDM(2)
        uint64_t fieldValues[18] = { b->R0, b->R1, b->R2, b->R3, b->R4, b->R5,
                                          b->G0, b->G1, b->G2, b->G3, b->G4, b->G5,
                                          b->B0, b->B1, b->B2, b->B3, b->B4, b->B5 };
        BC7_ModeJoin_PairDelta(fieldValues, 3, 6, 5, 0, 2);
#else
        const uint64_t fieldValues[18] = { b->R0, b->R1, b->R2, b->R3, b->R4, b->R5,
                                          b->G0, b->G1, b->G2, b->G3, b->G4, b->G5,
                                          b->B0, b->B1, b->B2, b->B3, b->B4, b->B5 };
#endif
        const uint64_t planeMask = 0x0000001000040001ull;     // bits 0, 18, 36 (planes 0-2)

        uint64_t p3 = 0, p4 = 0;
        for (uint8_t k = 0; k < 18; k++)
        {
            eppData[0] |= _pdep_u64(fieldValues[k], planeMask << k);   // field bits 0-2 -> planes 0-2
            p3 |= ((fieldValues[k] >> 3) & 1) << k;                    // field bit 3 -> plane 3
            p4 |= ((fieldValues[k] >> 4) & 1) << k;                    // field bit 4 -> plane 4
        }
        eppData[0] |= p3 << 54;
        eppData[1] = (p3 >> 10) | (p4 << 8);                           // 26 bits

        uint64_t* d = reinterpret_cast<uint64_t*>(dest);
        uint64_t extra = b->Raw[1] >> 35;                     // Extract index data (29 bits)
        uint64_t mode_part = (b->Raw[0] & 0x1ff);                     // Extract mode and partition (9 bits)

        d[0] = (extra >> 23) |
            (eppData[0] << 6);

        d[1] = (eppData[0] >> 58) |
            (eppData[1] << 6) |
            (mode_part << 32) |
            (extra << 41);
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode3(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = (metrics.Statics | metrics.LowEntropy) & 0x3f;
#if GACL_IIP && (GACL_IIPM & 128)
    BC7m3 iipBlk = *reinterpret_cast<const BC7m3*>(src);
    BC7_ModeJoin_IndexResidualPart(iipBlk.Raw, 4, 0x3f, kBC7Part2, kBC7Anchor2, 34, 30, 2);
#if GACL_IEDL || GACL_IMS
    BC7_ModeJoin_RawPre(iipBlk.Raw, 3, statics);
#endif
    const BC7m3* b = &iipBlk;
#else
    const BC7m3* b = reinterpret_cast<const BC7m3*>(src);
#endif
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData[2] = { 0, 0 };
    uint8_t bMsbPos[4] = { 62, 69, 76, 83 };   // M3BX: B0.6..B3.6 positions in eppData (all-static layout)
    if (statics)
    {
        if (staticCount == 6) // All color fields static - optimized direct copy
        {
            eppData[0] = (b->Raw[0] >> 10) | (b->Raw[1] << 54);
            eppData[1] = ((b->Raw[1] >> 10) & 0xfffff);
        }
        else
        {
            // Mixed static/dynamic fields requiring reorganization
            const uint8_t nonStaticCount = 6 - staticCount;

            // Build processing lists for static vs dynamic field handling
            uint8_t staticFields[12] = {};
            uint8_t nonStaticFields[12] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Mode 3 has 2 endpoint pairs per color field grouping
            for (uint8_t field = 0; field < 6; field++)
            {
                uint8_t epp0Index = (field % 2 ? field * 2 - 1 : field * 2);
                uint8_t epp1Index = epp0Index + 2;
                if (statics & (1 << field))
                {
                    staticFields[staticIndex++] = epp0Index;
                    staticFields[staticIndex++] = epp1Index;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = epp0Index;
                    nonStaticFields[nonStaticIndex++] = epp1Index;
                }
            }

            // Extract 7-bit color values from all endpoint positions
#if GACL_IEDM(3)
            uint64_t fieldValues[12] = { b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3(), b->B0, b->B1, b->B2, b->B3 };
            BC7_ModeJoin_PairDelta(fieldValues, 3, 4, 7, statics, 3);
#else
            const uint64_t fieldValues[12] = { b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3(), b->B0, b->B1, b->B2, b->B3 };
#endif

            // ORD2: channel-major order (LSB first) R0..R3 G0..G3 B0..B3
            const uint8_t mapToShuffledOrder[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

            // Sort fields by target positions for layout optimization
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount * 2,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Maintain original order for static fields
            std::sort(staticFields, staticFields + staticCount * 2);

            // Create interleaved pattern for non-static fields (may need two uint64s)
            uint64_t nonStaticEpp[2] = { 0, 0 };
            if (nonStaticCount > 0)
            {
                const uint8_t totalNonStaticFields = nonStaticCount * 2;

                if (totalNonStaticFields <= 8)  // Fits in single uint64 with 7-bit spacing
                {
                    const uint64_t mask = maskTable[totalNonStaticFields][0];
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        nonStaticEpp[0] |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                    }
                }
                else  // Requires spanning two uint64s with split mask handling
                {
                    const uint64_t mask10_0 = maskTable[10][0];
                    const uint64_t mask10_1 = maskTable[10][1];

                    // Process with split masks for fields exceeding 64-bit capacity
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        const uint64_t fieldValue = fieldValues[fieldIdx];

                        // Deposit first 6 bits using primary mask
                        nonStaticEpp[0] |= _pdep_u64(fieldValue, mask10_0 << i);

                        // Deposit 7th bit using secondary mask for overflow
                        if (i > 3)
                            nonStaticEpp[1] |= _pdep_u64((fieldValue >> 6u) & 0x1u, mask10_1 >> (10u - i));
                    }
                }
            }

            // Pack static fields consecutively (7 bits each for Mode 3)
            uint64_t staticData[2] = { 0, 0 };
            for (uint8_t i = 0; i < uint8_t(staticCount * 2); i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                if (i * 7 < 64)
                {
                    staticData[0] |= (uint64_t(fieldValues[fieldIdx]) << (i * 7));
                    if (i * 7 > 57)
                    {
                        staticData[1] |= (uint64_t(fieldValues[fieldIdx]) >> (64 - i * 7));
                    }
                }
                else
                {
                    staticData[1] |= (uint64_t(fieldValues[fieldIdx]) << (i * 7 - 64));
                }
            }

            // Combine with proper alignment handling for >64 bit cases
            const uint8_t nonStaticBits = nonStaticCount * 2 * 7;

            if (nonStaticBits > 64)
            {
                eppData[0] = nonStaticEpp[0];
                eppData[1] = nonStaticEpp[1] | (staticData[0] << (nonStaticBits - 64));
            }
            else
            {
                eppData[0] = nonStaticEpp[0] | (staticData[0] << nonStaticBits);
                eppData[1] = (staticData[0] >> (64 - nonStaticBits)) | (staticData[1] << nonStaticBits);
            }

            // M3BX: interleaved bit k of lane i sits at k * lanes + i; static fields are packed above
            const uint8_t totalNonStatic = uint8_t(nonStaticCount * 2);
            for (uint8_t j = 0; j < 4; j++)
            {
                const uint8_t idx = uint8_t(8 + j);
                for (uint8_t i = 0; i < totalNonStatic; i++)
                    if (nonStaticFields[i] == idx)
                        bMsbPos[j] = uint8_t(6 * totalNonStatic + i);
                for (uint8_t s = 0; s < uint8_t(staticCount * 2); s++)
                    if (staticFields[s] == idx)
                        bMsbPos[j] = uint8_t(nonStaticBits + 7 * s + 6);
            }
        }

        // M3BX: layout = [index 16][epp 80][Mode | B0.6 B1.6 B2.6 B3.6][partition 6][P 4 + index 14]
        uint64_t modeFill = 0, mLo = 0, mHi = 0;
        for (uint8_t j = 0; j < 4; j++)
        {
            const uint8_t p = bMsbPos[j];
            if (p < 64)
            {
                modeFill |= ((eppData[0] >> p) & 1) << j;
                mLo |= 1ull << p;
            }
            else
            {
                modeFill |= ((eppData[1] >> (p - 64)) & 1) << j;
                mHi |= 1ull << (p - 64);
            }
        }
        const uint32_t cLo = uint32_t(_mm_popcnt_u64(mLo));
        const uint64_t pLo = _pext_u64(eppData[0], ~mLo);
        const uint64_t pHi = _pext_u64(eppData[1], ~mHi);
        const uint64_t epp80Lo = cLo ? (pLo | (pHi << (64 - cLo))) : pLo;
        const uint64_t epp80Hi = cLo ? (pHi >> cLo) : pHi;          // 16 bits

        const uint64_t extra = b->Raw[1] >> 30;                     // P-bits and index (34 bits)
        const uint64_t mode_fill = (b->Raw[0] & 0xf) | (modeFill << 4);
        const uint64_t part = (b->Raw[0] >> 4) & 0x3f;

        d[0] = ((extra >> 18) & 0xffff) |
            (epp80Lo << 16);

        d[1] = (epp80Lo >> 48) |
            (epp80Hi << 16) |
            (mode_fill << 32) |
            (part << 40) |
            ((extra & 0x3ffff) << 46);
    }
    else
    {
        // All fields dynamic - join all 12 endpoint fields into one plane interleave, channel-major
        // (R0..R3 G0..G3 B0..B3): planes 0-5 x 12 lanes, plane 6 = R.6/G.6 (8 lanes); B0.6..B3.6 fill the mode byte (M3BX).
#if GACL_IEDM(3)
        uint64_t fieldValues[12] = { b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3(), b->B0, b->B1, b->B2, b->B3 };
        BC7_ModeJoin_PairDelta(fieldValues, 3, 4, 7, 0, 3);
#else
        const uint64_t fieldValues[12] = { b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3(), b->B0, b->B1, b->B2, b->B3 };
#endif
        const uint64_t planeMask = 0x0001001001001001ull;     // bits 0, 12, 24, 36, 48 (planes 0-4)

        uint64_t lo = 0, p5 = 0, p6 = 0, modeFill = 0;
        for (uint8_t k = 0; k < 12; k++)
        {
            const uint64_t v = fieldValues[k];
            lo |= _pdep_u64(v, planeMask << k);                 // field bits 0-4 -> planes 0-4
            p5 |= ((v >> 5) & 1) << k;                          // field bit 5 -> plane 5
            if (k < 8)
                p6 |= ((v >> 6) & 1) << k;                      // R/G bit 6 -> plane 6
            else
                modeFill |= ((v >> 6) & 1) << (k - 8);          // B bit 6 -> mode byte
        }
        const uint64_t epp80Lo = lo | (p5 << 60);
        const uint64_t epp80Hi = (p5 >> 4) | (p6 << 8);           // 16 bits

        const uint64_t extra = b->Raw[1] >> 30;                     // P-bits and index (34 bits)
        const uint64_t mode_fill = (b->Raw[0] & 0xf) | (modeFill << 4);
        const uint64_t part = (b->Raw[0] >> 4) & 0x3f;

        d[0] = ((extra >> 18) & 0xffff) |
            (epp80Lo << 16);

        d[1] = (epp80Lo >> 48) |
            (epp80Hi << 16) |
            (mode_fill << 32) |
            (part << 40) |
            ((extra & 0x3ffff) << 46);
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode4(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = (metrics.Statics | metrics.LowEntropy);
#if GACL_IIP && (GACL_IIPM & 8)
    // 2-bit indices at bits 50..80, 3-bit indices at 81..127
    BC7m4_Derotated iipBlk = *reinterpret_cast<const BC7m4_Derotated*>(src);
    {
        const uint64_t i2r = (iipBlk.Raw[0] >> 50) | ((iipBlk.Raw[1] & 0x1ffff) << 14), i3r = iipBlk.Raw[1] >> 17;
        uint64_t i2 = BC7_ModeJoin_IndexResidual(i2r, 2);
        uint64_t i3 = BC7_ModeJoin_IndexResidual(i3r, 3);
        iipBlk.Raw[0] = (iipBlk.Raw[0] & ((1ull << 50) - 1)) | (i2 << 50);
        iipBlk.Raw[1] = (i2 >> 14) | (i3 << 17);
    }
#if GACL_IEDL || GACL_IMS
    BC7_ModeJoin_RawPre(iipBlk.Raw, 4, statics);
#endif
    const BC7m4_Derotated* b = &iipBlk;
#else
    const BC7m4_Derotated* b = reinterpret_cast<const BC7m4_Derotated*>(src);
#endif
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData = 0;
    if (statics)
    {
        uint64_t m4Ex1 = (b->Raw[0] >> 44) & 0x1;
        if (staticCount == 8) // All RGBA fields static - direct extraction optimized
        {
            eppData = (b->Raw[0] >> 8 & 0x3fffffff);   // R0..B1 (30 bits); ex0 (bit 38) is carried separately
            eppData |= (b->Raw[0] >> 39 & 0x1f) << 30;
            eppData |= (b->Raw[0] >> 45 & 0x1f) << 35;
        }
        else
        {
            // Mixed static/dynamic requiring field separation and reorganization
            const uint8_t nonStaticCount = 8 - staticCount;

            // Build processing lists for 8 RGBA fields (2 endpoints each)
            uint8_t staticFields[8] = {};
            uint8_t nonStaticFields[8] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Classify each RGBA field as static or dynamic
            for (uint8_t field = 0; field < 8; field++)
            {
                if ((metrics.Statics | metrics.LowEntropy) & (1 << field))
                {
                    staticFields[staticIndex++] = field;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = field;
                }
            }

            // Extract 5-bit RGBA values from derotated block structure
            const uint64_t fieldValues[8] = { b->R0, b->R1, b->G0, b->G1, b->B0, b->B1, b->A0, b->A1 };

            // ORD23: ORD2 (LSB first R0 R1 G0 G1 B0 B1 A0 A1), reversed (ORD3) when only A0+A1 are static
            static constexpr uint8_t kOrd2[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
            static constexpr uint8_t kOrd3[8] = { 7, 6, 5, 4, 3, 2, 1, 0 };
            const uint8_t* mapToShuffledOrder = (statics == 0xC0) ? kOrd3 : kOrd2;

            // Sort non-static fields by optimal shuffle positions
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Create interleaved pattern for non-static fields
            uint64_t nonStaticEpp = 0;
            if (nonStaticCount > 0)
            {
                const uint64_t mask = maskTable[nonStaticCount][0];
                for (uint8_t i = 0; i < nonStaticCount; i++)
                {
                    const uint8_t fieldIdx = nonStaticFields[i];
                    nonStaticEpp |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                }
            }

            // Pack static fields consecutively (5 bits each for Mode 4)
            uint64_t staticData = 0;
            for (uint8_t i = 0; i < staticCount; i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                staticData |= (uint64_t(fieldValues[fieldIdx]) << (i * 5));
            }

            // Combine interleaved and static data with proper bit positioning
            const uint8_t nonStaticBits = nonStaticCount * 5;

            eppData = nonStaticEpp | (staticData << nonStaticBits);
        }

        // M4MX: layout = [index 54][rot+idx 3][ex 2][epp 37][Mode | 3 bits][index 24]
        // M4SLN: the static tail runs contiguously up into the mode byte (its top 3 bits fill it).
        constexpr uint8_t kFillLo = 2;   // all-static case: first field bits 2..4 (MSBs) move to the mode byte
        const uint8_t nsb = (staticCount == 8) ? kFillLo : uint8_t(37);
        const uint64_t modeFill = (eppData >> nsb) & 0x7;
        const uint64_t epp37 = (eppData & ((1ull << nsb) - 1)) | ((eppData >> (nsb + 3)) << nsb);

        uint64_t extra_start, extra_end;
        BC7_ModeJoin_M4Index(b->Raw, extra_start, extra_end);       // Remaining index bits (54 bits), first index bits (24 bits)
        const uint64_t rot_idx = (b->Raw[0] >> 5) & 0x7;             // Rotation(2) | IdxMode(1)
        const uint64_t mode_fill = (b->Raw[0] & 0x1f) | (modeFill << 5);
        const uint64_t ex_bits = ((b->Raw[0] >> 38) & 0x1) |
            (m4Ex1 << 1);                                           // Extract ex0 and ex1 bits (2 bits)

        d[0] = extra_start | (rot_idx << 54) | (ex_bits << 57) | (epp37 << 59);
        d[1] = (epp37 >> 5) | (mode_fill << 32) | (extra_end << 40);
    }
    else
    {
        // All fields dynamic - use parallel extraction for RGBA endpoint pair
        // Single endpoint pair is 40 bits (5 bits/channel * 4 channels * 2 endpoints)
        // 8-bit spacing accommodates all RGBA channel bits in interleaved pattern
        const uint64_t r0m = 0b0000000100000001000000010000000100000001;
        const uint64_t g0m = 0b0000001000000010000000100000001000000010;
        const uint64_t b0m = 0b0000010000000100000001000000010000000100;
        const uint64_t r1m = 0b0000100000001000000010000000100000001000;
        const uint64_t g1m = 0b0001000000010000000100000001000000010000;
        const uint64_t b1m = 0b0010000000100000001000000010000000100000;
        const uint64_t a0m = 0b0100000001000000010000000100000001000000;
        const uint64_t a1m = 0b1000000010000000100000001000000010000000;

        // Create RGBA endpoint pair using parallel bit deposit for optimal interleaving
        const uint64_t m4Ex1 = (b->Raw[0] >> 44) & 0x1;
        uint64_t epp0 =
            _pdep_u64(b->R0, r0m) |
            _pdep_u64(b->R1, r1m) |
            _pdep_u64(b->G0, g0m) |
            _pdep_u64(b->G1, g1m) |
            _pdep_u64(b->B0, b0m) |
            _pdep_u64(b->B1, b1m) |
            _pdep_u64(b->A0, a0m) |
            _pdep_u64(b->A1, a1m);

        // M4RX: byte-aligned endpoint planes matching Mode 5/6, rows 7..11 = A0 A1 R0 G0 B0 R1 G1 B1 (LSB first)
        // layout = [index 54][rot0 | ix][planes 40][Mode | A0.4 | A1.4 | rot0^rot1][index 24]
        const uint64_t ex_bits = ((b->Raw[0] >> 38) & 0x1) | (m4Ex1 << 1);
        const uint64_t planes = (ex_bits | (epp0 << 2)) & 0xFFFFFFFFFFull;   // 6-bit alpha: ex is plane 0; drops A0.4/A1.4
        const uint64_t rot = (b->Raw[0] >> 5) & 0x3;
        const uint64_t ix = (b->Raw[0] >> 7) & 0x1;
        const uint64_t alphaMsb = ((epp0 >> 38) & 1) | (((epp0 >> 39) & 1) << 1);
        const uint64_t modeFill = alphaMsb | (((rot ^ (rot >> 1)) & 1) << 2);
        const uint64_t control = (rot & 1) | (ix << 1);

        uint64_t extra_start, extra_end;
        BC7_ModeJoin_M4Index(b->Raw, extra_start, extra_end);       // Remaining index bits (54 bits), first index bits (24 bits)
        const uint64_t mode_fill = (b->Raw[0] & 0x1f) | (modeFill << 5);

        d[0] = extra_start | (control << 54) | (planes << 56);
        d[1] = (planes >> 8) | (mode_fill << 32) | (extra_end << 40);
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode5(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = (metrics.Statics | metrics.LowEntropy);
#if GACL_IIP && (GACL_IIPM & 6)
    BC7m5_Derotated iipBlk = *reinterpret_cast<const BC7m5_Derotated*>(src);
    {
        uint64_t r1 = iipBlk.Raw[1];
#if GACL_IIPM & 2
        r1 = (r1 & ~(0x7fffffffull << 2)) | (BC7_ModeJoin_IndexResidual((r1 >> 2) & 0x7fffffff, 2) << 2);
#endif
#if GACL_IIPM & 4
        r1 = (r1 & 0x1ffffffffull) | (BC7_ModeJoin_IndexResidual(r1 >> 33, 2) << 33);
#endif
        iipBlk.Raw[1] = r1;
    }
#if GACL_IEDL || GACL_IMS
    BC7_ModeJoin_RawPre(iipBlk.Raw, 5, statics);
#endif
    const BC7m5_Derotated* b = &iipBlk;
#else
    const BC7m5_Derotated* b = reinterpret_cast<const BC7m5_Derotated*>(src);
#endif
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData = 0;
    if (statics)
    {
        uint64_t m5Ex1 = (b->Raw[0] >> 58) & 0x1;
        if (staticCount == 8) // All RGBA fields static - optimized direct extraction
        {
            eppData = (b->Raw[0] >> 8 & 0x3ffffffffff);
            eppData |= (b->A0) << 42;
            eppData |= (b->A1()) << 49;
        }
        else
        {
            // Mixed static/dynamic requiring complex field reorganization
            const uint8_t nonStaticCount = 8 - staticCount;

            // Build processing lists for 8 RGBA fields
            uint8_t staticFields[8] = {};
            uint8_t nonStaticFields[8] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Classify RGBA fields by static/dynamic status
            for (uint8_t field = 0; field < 8; field++)
            {
                if ((metrics.Statics | metrics.LowEntropy) & (1 << field))
                {
                    staticFields[staticIndex++] = field;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = field;
                }
            }

            // Extract 7-bit RGBA values from derotated block
            uint64_t fieldValues[8] = { b->R0, b->R1, b->G0, b->G1, b->B0, b->B1, b->A0, b->A1() };
#if GACL_IED
            for (uint8_t c = 0; c < 4; c++)
            {
                if (!(statics & (3 << (2 * c))))
                    fieldValues[2 * c + 1] = BC7_ModeJoin_EndpointDelta(fieldValues[2 * c + 1], fieldValues[2 * c], 7);
            }
#endif

            // ORD23: ORD2 (LSB first R0 R1 G0 G1 B0 B1 A0 A1), reversed (ORD3) when only A0+A1 are static
            static constexpr uint8_t kOrd2[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
            static constexpr uint8_t kOrd3[8] = { 7, 6, 5, 4, 3, 2, 1, 0 };
            const uint8_t* mapToShuffledOrder = (statics == 0xC0) ? kOrd3 : kOrd2;

            // Sort non-static fields by target shuffle positions
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Create interleaved pattern for non-static fields
            uint64_t nonStaticEpp = 0;
            if (nonStaticCount > 0)
            {
                const uint64_t mask = maskTable[nonStaticCount][0];
                for (uint8_t i = 0; i < nonStaticCount; i++)
                {
                    const uint8_t fieldIdx = nonStaticFields[i];
                    nonStaticEpp |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                }
            }

            // Pack static fields consecutively (7 bits each for Mode 5)
            uint64_t staticData = 0;
            for (uint8_t i = 0; i < staticCount; i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                staticData |= (uint64_t(fieldValues[fieldIdx]) << (i * 7));
            }

            // Combine interleaved and static data with proper positioning
            const uint8_t nonStaticBits = nonStaticCount * 7;

            eppData = nonStaticEpp | (staticData << nonStaticBits);
        }

        // M5MX: layout = [index 38][rot 2][ex 2][epp 54][Mode | 2 bits][index 24]
        // SLN: the static tail runs contiguously up into the mode byte (its top 2 bits fill it).
        const uint8_t nsb = (staticCount == 8) ? uint8_t(5) : uint8_t(54);
        const uint64_t modeFill = (eppData >> nsb) & 0x3;
        const uint64_t epp54 = (eppData & ((1ull << nsb) - 1)) | ((eppData >> (nsb + 2)) << nsb);

        uint64_t extra_start, extra_end;
        BC7_ModeJoin_M5Index(b->Raw, extra_start, extra_end);       // Remaining index bits (38 bits), first index bits (24 bits)
        const uint64_t rot = (b->Raw[0] >> 6) & 0x3;                 // Rotation (2 bits)
        const uint64_t mode_fill = (b->Raw[0] & 0x3f) | (modeFill << 6);
        const uint64_t ex_bits = ((b->Raw[0] >> 50) & 0x1) |
            (m5Ex1 << 1);                                           // Extract ex0 and ex1 bits (2 bits)

        d[0] = extra_start | (rot << 38) | (ex_bits << 40) | (epp54 << 42);
        d[1] = (epp54 >> 22) | (mode_fill << 32) | (extra_end << 40);
    }
    else
    {
        // All fields dynamic - parallel extraction for RGBA endpoint pair
        // Single endpoint pair is 56 bits (7 bits/channel * 4 channels * 2 endpoints)
        // 8-bit spacing accommodates all RGBA bits in interleaved pattern
        const uint64_t r0m = 0b00000001000000010000000100000001000000010000000100000001;
        const uint64_t g0m = 0b00000010000000100000001000000010000000100000001000000010;
        const uint64_t b0m = 0b00000100000001000000010000000100000001000000010000000100;
        const uint64_t r1m = 0b00001000000010000000100000001000000010000000100000001000;
        const uint64_t g1m = 0b00010000000100000001000000010000000100000001000000010000;
        const uint64_t b1m = 0b00100000001000000010000000100000001000000010000000100000;
        const uint64_t a0m = 0b01000000010000000100000001000000010000000100000001000000;
        const uint64_t a1m = 0b10000000100000001000000010000000100000001000000010000000;


        // Create RGBA endpoint pair using parallel bit deposit for optimal interleaving
#if GACL_IED
        const uint64_t m5A1 = BC7_ModeJoin_EndpointDelta(b->A1(), b->A0, 7);
        const uint64_t m5Ex1 = (b->Raw[0] >> 58) & 0x1;
        uint64_t m5fv[6] = { b->R0, b->R1, b->G0, b->G1, b->B0, b->B1 };
        for (int c = 0; c < 3; c++)
        {
            m5fv[2 * c + 1] = BC7_ModeJoin_EndpointDelta(m5fv[2 * c + 1], m5fv[2 * c], 7);
        }
        uint64_t epp0 =
            _pdep_u64(m5fv[0], r0m) |
            _pdep_u64(m5fv[1], r1m) |
            _pdep_u64(m5fv[2], g0m) |
            _pdep_u64(m5fv[3], g1m) |
            _pdep_u64(m5fv[4], b0m) |
            _pdep_u64(m5fv[5], b1m) |
            _pdep_u64(b->A0, a0m) |
            _pdep_u64(m5A1, a1m);
#else
        const uint64_t m5Ex1 = (b->Raw[0] >> 58) & 0x1;
        uint64_t epp0 =
            _pdep_u64(b->R0, r0m) |
            _pdep_u64(b->R1, r1m) |
            _pdep_u64(b->G0, g0m) |
            _pdep_u64(b->G1, g1m) |
            _pdep_u64(b->B0, b0m) |
            _pdep_u64(b->B1, b1m) |
            _pdep_u64(b->A0, a0m) |
            _pdep_u64(b->A1(), a1m);
#endif

        // M5MX: layout = [index 38][rot 2][ex 2][epp 54][Mode | A0.6 | A1.6][index 24]
        // A0.6 and A1.6 are the top two bits of the interleave (bits 54, 55)
        const uint64_t modeFill = epp0 >> 54;
        const uint64_t epp54 = epp0 & ((1ull << 54) - 1);

        uint64_t extra_start, extra_end;
        BC7_ModeJoin_M5Index(b->Raw, extra_start, extra_end);       // Remaining index bits (38 bits), first index bits (24 bits)
        const uint64_t rot = (b->Raw[0] >> 6) & 0x3;                 // Rotation (2 bits)
        const uint64_t mode_fill = (b->Raw[0] & 0x3f) | (modeFill << 6);
        const uint64_t ex_bits = ((b->Raw[0] >> 50) & 0x1) |
            (m5Ex1 << 1);                                           // Extract ex0 and ex1 bits (2 bits)

        d[0] = extra_start | (rot << 38) | (ex_bits << 40) | (epp54 << 42);
        d[1] = (epp54 >> 22) | (mode_fill << 32) | (extra_end << 40);
    }
}


void BC7_ModeJoin_Shuffle_Fast_Mode6(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = (metrics.Statics | metrics.LowEntropy);
#if GACL_IIP && (GACL_IIPM & 1)
    BC7m6 iipBlk = *reinterpret_cast<const BC7m6*>(src);
    {
        iipBlk.Raw[1] = (iipBlk.Raw[1] & 1) | (BC7_ModeJoin_IndexResidual(iipBlk.Raw[1] >> 1, 4) << 1);
    }
#if GACL_IEDL || GACL_IMS
    BC7_ModeJoin_RawPre(iipBlk.Raw, 6, statics);
#endif
    const BC7m6* b = &iipBlk;
#else
    const BC7m6* b = reinterpret_cast<const BC7m6*>(src);
#endif
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    // M6AH: A0.6 fills the mode byte, A1.6 leads the high index region.
    // Alpha non-static (M6AY): P0/P1 become the LSB of 7-bit alpha lanes (P | A.0..A.5 << 1).
    // A0+A1 static (M6AX): 6-bit alpha fields, P0/P1 take the bottom 2 bits of the endpoint region (M6AHB).
    const bool alphaStatic = (statics & 0xC0) == 0xC0;
    const uint64_t a06 = (b->A0 >> 6) & 1;
    const uint64_t a16 = (b->A1 >> 6) & 1;
    const uint64_t a0p = (b->Raw[0] >> 63) | (uint64_t(b->A0 & 0x3f) << 1);
    const uint64_t a1p = (b->Raw[1] & 1) | (uint64_t(b->A1 & 0x3f) << 1);
    const uint64_t pBits = (b->Raw[0] >> 63) | ((b->Raw[1] & 1) << 1);
    const uint64_t idx23 = (b->Raw[1] >> 1) & 0x7fffff;
    const uint64_t modeByte = (b->Raw[0] & 0x7f) | (a06 << 7);

    uint64_t eppData = 0;
    if (statics)
    {
        uint64_t modeFill = a06;   // mode byte bit 7
        uint64_t idxLead = a16;    // row 13 bit 0
        if (staticCount == 8) // All RGBA fields static - optimized direct extraction
        {
            eppData = (b->Raw[0] >> 7 & 0x3ffffffffff) | (uint64_t(b->A0 & 0x3f) << 42) | (uint64_t(b->A1 & 0x3f) << 48);
            eppData = (eppData << 2) | pBits;   // M6AHB: P0/P1 at the bottom of the endpoint region
        }
        else
        {
            // SLN: 58-bit region V = rows 5..11 (V0..55), mode byte bit 7 (V56), row 13 bit 0 (V57).
            // V (LSB up) = [P0 P1 if A0+A1 static][non-static planes][A.6 of non-static alpha lanes][static fields, ascending]
            // so the static tail runs contiguously up into the mode byte and the row above it.
            // Alpha fields: 7-bit A when A0+A1 are static (P0/P1 go to the bottom, M6AHB), else P | A.0..A.5 << 1 | A.6 << 7.
            uint64_t fieldValues[8] = { b->R0, b->R1, b->G0, b->G1, b->B0, b->B1,
                alphaStatic ? uint64_t(b->A0 & 0x7f) : (a0p | (a06 << 7)), alphaStatic ? uint64_t(b->A1 & 0x7f) : (a1p | (a16 << 7)) };
#if GACL_IED
            for (uint8_t c = 0; c < 4; c++)
            {
                if (!(statics & (3 << (2 * c))))
                    fieldValues[2 * c + 1] = BC7_ModeJoin_EndpointDelta(fieldValues[2 * c + 1], fieldValues[2 * c], c == 3 ? 8 : 7);
            }
#endif

            const uint8_t nonStaticCount = 8 - staticCount;
            uint8_t nonStaticFields[8] = {};
            uint8_t nonStaticIndex = 0;
            for (uint8_t field = 0; field < 8; field++)
            {
                if (!(statics & (1 << field)))
                {
                    nonStaticFields[nonStaticIndex++] = field;
                }
            }

            // ORD23: ORD2 (LSB first R0 R1 G0 G1 B0 B1 A0 A1), reversed (ORD3) when only A0+A1 are static
            static constexpr uint8_t kOrd2[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
            static constexpr uint8_t kOrd3[8] = { 7, 6, 5, 4, 3, 2, 1, 0 };
            const uint8_t* mapToShuffledOrder = (statics == 0xC0) ? kOrd3 : kOrd2;

            // Sort non-static fields by target shuffled positions
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            uint64_t V = alphaStatic ? pBits : 0;
            uint8_t pos = alphaStatic ? 2 : 0;

            // Interleave the low 7 bits of the non-static fields as planes
            const uint64_t mask = maskTable[nonStaticCount][0];
            for (uint8_t i = 0; i < nonStaticCount; i++)
            {
                V |= _pdep_u64(fieldValues[nonStaticFields[i]] & 0x7f, mask << (i + pos));
            }
            pos = uint8_t(pos + nonStaticCount * 7);

            // A.6 of non-static alpha lanes (only possible when alpha is not static)
            for (uint8_t i = 0; i < nonStaticCount; i++)
            {
                if (nonStaticFields[i] >= 6)
                {
                    V |= (fieldValues[nonStaticFields[i]] >> 7) << pos++;
                }
            }

            // Static fields packed consecutively
            for (uint8_t field = 0; field < 8; field++)
            {
                if (statics & (1 << field))
                {
                    V |= fieldValues[field] << pos;
                    pos = uint8_t(pos + ((field >= 6 && !alphaStatic) ? 8 : 7));
                }
            }

            eppData = V & ((1ull << 56) - 1);
            modeFill = (V >> 56) & 1;
            idxLead = (V >> 57) & 1;
        }

        uint64_t extra_start = b->Raw[1] >> 24;                      // Remaining index bits (40 bits)

        d[0] = extra_start |                          // Remaining index data in low bits
            (eppData << 40);                           // Start endpoint pair (24 bits here)

        d[1] = (eppData >> 24) |                         // Complete endpoint pair (32 bits)
            (((b->Raw[0] & 0x7f) | (modeFill << 7)) << 32) |   // Mode + fill bit at byte boundary
            ((idxLead | (idx23 << 1)) << 40);      // Lead bit and first index bits
    }
    else
    {
        // All fields dynamic - use parallel extraction for RGBA endpoint pair
        // Single endpoint pair is 56 bits (7 bits/channel * 4 channels * 2 endpoints), 7 planes x 8 lanes
        // Lane order A0 A1 R0 G0 B0 R1 G1 B1 (same as Mode 4/5)
        const uint64_t lane = 0x01010101010101ull;

        // Create RGBA endpoint pair using parallel bit deposit for optimal interleaving
#if GACL_IED
        const uint64_t ia1 = BC7_ModeJoin_EndpointDelta(a1p | (a16 << 7), a0p | (a06 << 7), 8);
        const uint64_t na16 = ia1 >> 7;
        uint64_t m6fv[6] = { b->R0, b->R1, b->G0, b->G1, b->B0, b->B1 };
        for (int c = 0; c < 3; c++)
        {
            m6fv[2 * c + 1] = BC7_ModeJoin_EndpointDelta(m6fv[2 * c + 1], m6fv[2 * c], 7);
        }
        uint64_t epp0 =
            _pdep_u64(a0p, lane) |
            _pdep_u64(ia1 & 0x7f, lane << 1) |
            _pdep_u64(m6fv[0], lane << 2) |
            _pdep_u64(m6fv[2], lane << 3) |
            _pdep_u64(m6fv[4], lane << 4) |
            _pdep_u64(m6fv[1], lane << 5) |
            _pdep_u64(m6fv[3], lane << 6) |
            _pdep_u64(m6fv[5], lane << 7);
#else
        const uint64_t na16 = a16;
        uint64_t epp0 =
            _pdep_u64(a0p, lane) |
            _pdep_u64(a1p, lane << 1) |
            _pdep_u64(b->R0, lane << 2) |
            _pdep_u64(b->G0, lane << 3) |
            _pdep_u64(b->B0, lane << 4) |
            _pdep_u64(b->R1, lane << 5) |
            _pdep_u64(b->G1, lane << 6) |
            _pdep_u64(b->B1, lane << 7);
#endif

        uint64_t extra_start = b->Raw[1] >> 24;                      // Remaining index bits (40 bits)

        // Assemble final layout with 56-bit endpoint data spanning both uint64s
        d[0] = extra_start |                          // Fill low bits with remaining index
            (epp0 << 40);                           // First 24 bits of endpoint data

        d[1] = (epp0 >> 24) |                         // Remaining 32 bits of endpoint data
            (modeByte << 32) |                 // Mode + A0.6 at byte boundary
            ((na16 | (idx23 << 1)) << 40);      // A1.6 and first index bits
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode7(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
#if GACL_IIP && (GACL_IIPM & 256)
    BC7m7 iipBlk = *reinterpret_cast<const BC7m7*>(src);
    BC7_ModeJoin_IndexResidualPart(iipBlk.Raw, 8, 0x3f, kBC7Part2, kBC7Anchor2, 34, 30, 2);
#if GACL_IEDL || GACL_IMS
    BC7_ModeJoin_RawPre(iipBlk.Raw, 7, (metrics.Statics | metrics.LowEntropy));
#endif
    const BC7m7* b = &iipBlk;
#else
    const BC7m7* b = reinterpret_cast<const BC7m7*>(src);
#endif
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);

    uint64_t eppData[2] = { 0, 0 };
    if ((metrics.Statics | metrics.LowEntropy))
    {
        if ((metrics.Statics | metrics.LowEntropy) == 0xff) // All RGBA fields static - optimized direct extraction
        {
            eppData[0] = (b->Raw[0] >> 14) | (b->Raw[1] << 50);
            eppData[1] = ((b->Raw[1] >> 14) & 0xffff);
        }
        else
        {
            // Mixed static/dynamic requiring complex field reorganization
            const uint8_t staticCount = uint8_t(_mm_popcnt_u64((metrics.Statics | metrics.LowEntropy)));
            const uint8_t nonStaticCount = 8 - staticCount;

            // Build processing lists for static vs dynamic fields (16 total endpoints)
            uint8_t staticFields[16] = {};
            uint8_t nonStaticFields[16] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Mode 7 has 2 endpoint pairs per color field grouping
            for (uint8_t field = 0; field < 8; field++)
            {
                uint8_t epp0Index = (field % 2 ? field * 2 - 1 : field * 2);
                uint8_t epp1Index = epp0Index + 2;
                if ((metrics.Statics | metrics.LowEntropy) & (1 << field))
                {
                    staticFields[staticIndex++] = epp0Index;
                    staticFields[staticIndex++] = epp1Index;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = epp0Index;
                    nonStaticFields[nonStaticIndex++] = epp1Index;
                }
            }

            // Extract 5-bit RGBA values from all endpoint positions
#if GACL_IEDM(7)
            uint64_t fieldValues[16] = { b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3, b->B0, b->B1, b->B2, b->B3, b->A0, b->A1, b->A2, b->A3 };
            BC7_ModeJoin_PairDelta(fieldValues, 4, 4, 5, (metrics.Statics | metrics.LowEntropy), 7);
#else
            const uint64_t fieldValues[16] = { b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3, b->B0, b->B1, b->B2, b->B3, b->A0, b->A1, b->A2, b->A3 };
#endif

            // ORD23: ORD2 (LSB first R0..R3 G0..G3 B0..B3 A0..A3), reversed (ORD3) when only A0+A1 are static
            static constexpr uint8_t kOrd2[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
            static constexpr uint8_t kOrd3[16] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
            const uint8_t* mapToShuffledOrder = ((metrics.Statics | metrics.LowEntropy) == 0xC0) ? kOrd3 : kOrd2;

            // Sort non-static fields by target shuffled positions
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount * 2,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Keep static fields in original order for contiguous placement
            std::sort(staticFields, staticFields + staticCount * 2);

            // Build interleaved pattern for non-static fields (may span two uint64s)
            uint64_t nonStaticEpp[2] = { 0, 0 };
            if (nonStaticCount > 0)
            {
                const uint8_t totalNonStaticFields = nonStaticCount * 2;

                if (totalNonStaticFields <= 12)  // Fits in single uint64 with interleaving
                {
                    const uint64_t mask = maskTable[totalNonStaticFields][0];
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        nonStaticEpp[0] |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                    }
                }
                else  // More than 12 fields require spanning two uint64s
                {
                    const uint64_t mask14_0 = maskTable[14][0];
                    const uint64_t mask14_1 = maskTable[14][1];

                    // Process using split mask approach for >60 bit patterns
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        const uint64_t fieldValue = fieldValues[fieldIdx];

                        // Deposit first 4 bits using primary mask
                        nonStaticEpp[0] |= _pdep_u64(fieldValue, mask14_0 << i);

                        // Deposit 5th bit using secondary mask for overflow
                        if (i > 7)
                            nonStaticEpp[1] |= _pdep_u64((fieldValue >> 4u) & 0x1u, mask14_1 >> (14u - i));
                    }
                }
            }

            // Pack static fields consecutively (5 bits each for Mode 7)
            uint64_t staticData[2] = { 0 };
            for (uint8_t i = 0; i < uint8_t(staticCount * 2); i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                if (i * 5 < 64)
                {
                    staticData[0] |= (uint64_t(fieldValues[fieldIdx]) << (i * 5));
                    if (i * 5 > 59)
                    {
                        staticData[1] |= (uint64_t(fieldValues[fieldIdx]) >> (64 - i * 5));
                    }
                }
                else
                {
                    staticData[1] |= (uint64_t(fieldValues[fieldIdx]) << (i * 5 - 64));
                }
            }

            // Combine interleaved and static data with proper alignment
            const uint8_t staticBits = staticCount * 2 * 5;
            const uint8_t nonStaticBits = nonStaticCount * 2 * 5;

            if (nonStaticBits)
            {
                if (nonStaticBits <= 64)
                {
                    eppData[0] = nonStaticEpp[0] | (staticData[0] << nonStaticBits);
                    if (nonStaticBits + staticBits > 64)
                    {
                        eppData[1] = (staticData[0] >> (64 - nonStaticBits)) | (staticData[1] << nonStaticBits);
                    }
                }
                else  // nonStaticBits > 64 for complex cases
                {
                    eppData[0] = nonStaticEpp[0];
                    eppData[1] = nonStaticEpp[1] | (staticData[0] << (nonStaticBits - 64));
                }
            }
            else
            {
                eppData[0] = staticData[0];
                eppData[1] = staticData[1];
            }
        }

        // Extract mode, partition, and index data from source block
        uint64_t extra_start = b->Raw[1] >> 48;                      // Remaining index bits (16 bits)
        uint64_t extra_end = (b->Raw[1] >> 30) & 0x3ffff;           // First index bits (18 bits)
        uint64_t mode_part = b->Raw[0] & 0x3fff;                      // Mode and partition (14 bits)

        // Assemble with endpoint pairs positioned to fill two 40-bit sections
        d[0] = (extra_start) |                           // Remaining index data (16 bits)
            eppData[0] << 16;                           // First endpoint data (48 bits)

        d[1] = (eppData[0] >> 48) |                         // Remaining endpoint data (16 bits)
            (eppData[1] << 16) |                           // Second endpoint data (48 bits)
            (mode_part << 32) |                     // Mode and partition at byte boundary
            (extra_end << 46);                     // First index bits (18 bits)
    }
    else
    {
        // All fields dynamic - join all 16 endpoint fields into one 80-bit plane interleave
        // (5 planes x 16 lanes, channel-major A0..A3 R0..R3 G0..G3 B0..B3) at bits 16-95, using the static path frame.
        // Reading down from the mode byte gives B, G, R, A (matching Mode 6).
#if GACL_IEDM(7)
        uint64_t fieldValues[16] = { b->A0, b->A1, b->A2, b->A3, b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3, b->B0, b->B1, b->B2, b->B3 };
        BC7_ModeJoin_PairDelta(fieldValues, 4, 4, 5, 0, 7, true);
#else
        const uint64_t fieldValues[16] = { b->A0, b->A1, b->A2, b->A3, b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3, b->B0, b->B1, b->B2, b->B3 };
#endif
        const uint64_t planeMask = 0x0001000100010001ull;     // bits 0, 16, 32, 48 (planes 0-3)

        for (uint8_t k = 0; k < 16; k++)
        {
            eppData[0] |= _pdep_u64(fieldValues[k], planeMask << k);   // field bits 0-3 -> planes 0-3
            eppData[1] |= ((fieldValues[k] >> 4) & 1) << k;            // field bit 4 -> plane 4
        }

        uint64_t extra_start = b->Raw[1] >> 48;                      // Remaining index bits (16 bits)
        uint64_t extra_end = (b->Raw[1] >> 30) & 0x3ffff;           // First index bits (18 bits)
        uint64_t mode_part = b->Raw[0] & 0x3fff;                      // Mode and partition (14 bits)

        d[0] = (extra_start) |
            eppData[0] << 16;

        d[1] = (eppData[0] >> 48) |
            (eppData[1] << 16) |
            (mode_part << 32) |
            (extra_end << 46);
    }
}

void (*BC7_ModeJoin_Shuffle_Fast[8])(const void* src, void* dest, const BC7TextureMetrics& metrics) =
{
    &BC7_ModeJoin_Shuffle_Fast_Mode0,
    &BC7_ModeJoin_Shuffle_Fast_Mode1,
    &BC7_ModeJoin_Shuffle_Fast_Mode2,
    &BC7_ModeJoin_Shuffle_Fast_Mode3,
    &BC7_ModeJoin_Shuffle_Fast_Mode4,
    &BC7_ModeJoin_Shuffle_Fast_Mode5,
    &BC7_ModeJoin_Shuffle_Fast_Mode6,
    &BC7_ModeJoin_Shuffle_Fast_Mode7,
};
static_assert(_countof(BC7_ModeJoin_Shuffle_Fast) == 8, "BC7_ModeJoin_Shuffle_Fast size mismatch");

} // namespace

#ifndef GACL_JROT
#define GACL_JROT 24    // JROT: final block rotation (bits; only 24 is implemented)
#endif
#ifndef GACL_JBO
#define GACL_JBO 4      // JBO (after the 24-bit rotation): 4 = reverse the index bits (0 = off)
#endif
#ifndef GACL_JBOM
#define GACL_JBOM 0x8F  // JBO scope: bit m = Mode m
#endif
#ifndef GACL_JFP4
#define GACL_JFP4 0xd8250704 // JFP (Mode 4/5/6 index fields, after the rotation): 1 = reverse texel order (symbols intact),
#endif                  // 2 = MSB-first symbols, 4 = swap the two index groups (Modes 4/5), 8 = Mode 6 filler bit 0 to bit 63,
                        // 16 / 32 = flags 1 and 2 only for group 0 / 1, 64 = Mode 5 rotation bit g pads group g's anchor (row-aligned
                        // bytes) / Modes 1/3/7 P bits after the indices, 128 = interleave the two groups per texel; 3 = 7 = JBO 4 for Mode 6;
                        // bits 8-10 (+ bits 16-17 as scan bits 3-4) = texel scan (kJfpScan), 0x800 = anchor last, bits 12-13 = interleave run (1 << n texels),
                        // Mode 5: 0x4000 = alpha ^= color, 0x8000 = color ^= alpha (per texel, before the permutation);
                        // bits 20-26 = rotate block bits [0, L) left by n after the permutation (the mode byte stays put), bits 27-31 = L / 4 (0 = 120), bit 18 = L + 2,
                        // bit 19 = rotate [L, 120) instead; bits 32-38 / 39-45 / 46-52 = a second rotation of bits [lo, hi) left by n, after the first
                        // (64-bit value, e.g. 0x...ull)
#ifndef GACL_JFP5
#define GACL_JFP5 0x782d0685
#endif
#ifndef GACL_JFP6
#define GACL_JFP6 0x2000A
#endif
// Modes 1/3/7 (2-subset; anchors = texel 0 + the partition's fix-up texel): same v encoding, applied in the natural
// index order (a JBO 4 reversal is undone around the permutation, so v = 0 keeps the JBO layout)
#ifndef GACL_JFP1
#define GACL_JFP1 0x603
#endif
#ifndef GACL_JFP3
#define GACL_JFP3 0x10603
#endif
#ifndef GACL_JFP7
#define GACL_JFP7 0x20200
#endif

// JFP texel scan orders: raster texel index per scan position (all start at the anchor texel 0). A JFP setting v selects
// row ((v >> 8) & 7) | ((v >> 16) & 3) << 3 (BC7_ModeJoin_IndexFieldScan); the defaults use rows 6 (GACL_JFP1), 14
// (GACL_JFP3 and GACL_JFP5), 15 (GACL_JFP4), 16 (GACL_JFP6) and 18 (GACL_JFP7). Rows 19-31 are zero, not valid scans.
static const uint8_t kJfpScan[32][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },   // 0: raster
    { 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15 },   // 1: column-major
    { 0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15 },   // 2: Z-order (2x2 quads)
    { 0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11, 15, 14, 13, 12 },   // 3: serpentine rows
    { 0, 1, 2, 3, 4, 8, 12, 5, 6, 7, 9, 10, 11, 13, 14, 15 },   // 4: edges first (row 0, column 0), then interior raster
    { 0, 5, 6, 7, 9, 10, 11, 13, 14, 15, 1, 2, 3, 4, 8, 12 },   // 5: interior raster first, then edges
    { 0, 4, 1, 5, 2, 6, 3, 7, 8, 12, 9, 13, 10, 14, 11, 15 },   // 6: row pairs, column-major within (2x4 strips)
    { 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15 },   // 7: column pairs (two 2-wide strips)
    { 0, 1, 2, 3, 4, 8, 12, 5, 9, 13, 6, 10, 14, 7, 11, 15 },   // 8: row 0, column 0, interior column-major
    { 0, 4, 8, 12, 1, 2, 3, 5, 9, 13, 6, 10, 14, 7, 11, 15 },   // 9: column 0, row 0, interior column-major
    { 0, 4, 8, 12, 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 },   // 10: column 0, row 0, interior raster
    { 0, 1, 4, 2, 8, 3, 12, 5, 6, 7, 9, 10, 11, 13, 14, 15 },   // 11: edges by distance from the anchor, interior raster
    { 0, 1, 4, 2, 8, 3, 12, 5, 6, 9, 7, 10, 13, 11, 14, 15 },   // 12: edges by distance, interior anti-diagonals
    { 0, 1, 2, 3, 4, 8, 12, 5, 6, 9, 7, 10, 13, 11, 14, 15 },   // 13: row 0, column 0, interior anti-diagonals
    { 0, 4, 8, 12, 1, 2, 3, 5, 6, 7, 11, 10, 9, 13, 14, 15 },   // 14: column 0, row 0, interior serpentine rows
    { 0, 4, 8, 12, 1, 2, 3, 5, 9, 13, 14, 10, 6, 7, 11, 15 },   // 15: column 0, row 0, interior serpentine columns
    { 0, 4, 8, 12, 3, 2, 1, 5, 6, 7, 11, 10, 9, 13, 14, 15 },   // 16: column 0, row 0 reversed, interior serpentine rows
    { 0, 1, 2, 3, 4, 8, 12, 5, 6, 7, 11, 10, 9, 13, 14, 15 },   // 17: row 0, column 0, interior serpentine rows
    { 0, 3, 2, 1, 4, 8, 12, 5, 6, 7, 11, 10, 9, 13, 14, 15 },   // 18: row 0 reversed, column 0, interior serpentine rows
};

// Index field permutation of a rotated Mode 1/3/4/5/6/7 block (JFP); inverse = true undoes it (BC7_ModeJoin_Reverse).
// scan = texel order (raster texel index per scan position); BC7_ModeJoin_IndexFieldPermute takes it from v (kJfpScan).
void BC7_ModeJoin_IndexFieldPermuteScan(uint64_t* x, unsigned mode, uint64_t v64, bool inverse, const uint8_t* scan)
{
    if (mode != 1 && mode != 3 && (mode < 4 || mode > 7))
    {
        return;
    }
    const unsigned v = unsigned(v64);
    // bits 20-26: rotate block bits [0, L) left by this amount after the permutation; bits 27-31: L / 4 (0 = 120, everything
    // below the mode byte), bit 18: L += 2; bit 19: rotate [L, 120) instead (the region above L, below the mode byte)
    const unsigned shift = (v >> 20) & 127;
    const unsigned rlenN = (v >> 27) & 31;
    const unsigned rL = rlenN && rlenN < 30 ? rlenN * 4 + ((v >> 17) & 2) : 120;
    const unsigned rLo = (v & 0x80000) ? rL : 0, rHi = (v & 0x80000) ? 120 : rL;
    const unsigned rlen = rHi > rLo ? rHi - rLo : 0;
    // bits 32-38 / 39-45 / 46-52: a second rotation of bits [lo, hi) left by n, applied after the first (hi <= 120)
    const unsigned lo2 = unsigned(v64 >> 32) & 127, hi2 = unsigned(v64 >> 39) & 127, shift2 = unsigned(v64 >> 46) & 127;
    const unsigned rlen2 = hi2 <= 120 && hi2 > lo2 ? hi2 - lo2 : 0;
    auto rotL = [](uint64_t* y, unsigned lo, unsigned len, unsigned s)
    {
        // bit lo + k moves to lo + (k + s) % len
        uint64_t o[2] = { y[0], y[1] };
        for (unsigned k = 0; k < len; k++)
        {
            const unsigned from = lo + k, to = lo + (k + s) % len;
            o[to >> 6] = (o[to >> 6] & ~(1ull << (to & 63))) | (((y[from >> 6] >> (from & 63)) & 1) << (to & 63));
        }
        y[0] = o[0];
        y[1] = o[1];
    };
    if (inverse && shift2 > 0 && shift2 < rlen2)
    {
        rotL(x, lo2, rlen2, rlen2 - shift2);
    }
    if (inverse && shift > 0 && shift < rlen)
    {
        rotL(x, rLo, rlen, rlen - shift);
    }
    // index groups: start bit, anchor width, symbol width; anchor texels (bit t) use the anchor width
    unsigned gs[2] = { 1, 0 }, ga[2] = { 3, 0 }, gw[2] = { 4, 0 }, ng = 1;
    uint32_t anchors = 1;
    unsigned rlo = 0, rhi = 0;      // JBO-reversed bit region, undone around the permutation
    if (mode == 4)
    {
        ng = 2; gs[0] = 0; ga[0] = 1; gw[0] = 2; gs[1] = 31; ga[1] = 2; gw[1] = 3;
    }
    else if (mode == 5)
    {
        ng = 2; gs[0] = 0; ga[0] = 1; gw[0] = 2; gs[1] = 31; ga[1] = 1; gw[1] = 2;
    }
    else if (mode == 1 || mode == 3 || mode == 7)
    {
        // rotated layout: Mode 1 = P bits 0-1, indices 2-47, partition 122-127; Modes 3/7 = partition 0-5, P bits 6-9, indices 10-39.
        // The field positions depend on the partition's second anchor (its anchor class): the GPU has a T row per class.
        anchors = kBC7Anchor2[mode == 1 ? unsigned(x[1] >> 58) : unsigned(x[0] & 63)];
        gs[0] = mode == 1 ? 2 : 10; gw[0] = mode == 1 ? 3 : 2; ga[0] = gw[0] - 1;
#if GACL_JBO == 4
        if ((GACL_JBOM >> mode) & 1)
        {
            rlo = mode == 1 ? 0 : 6;
            rhi = mode == 1 ? 48 : 40;
        }
#endif
    }
    else if (mode != 6)
    {
        return;
    }
    auto reverseRegion = [rlo, rhi](uint64_t* y)
    {
        uint64_t o[2] = { y[0], y[1] };
        for (unsigned k = rlo; k < rhi; k++)
        {
            const unsigned j = rlo + rhi - 1 - k;
            o[j >> 6] = (o[j >> 6] & ~(1ull << (j & 63))) | (((y[k >> 6] >> (k & 63)) & 1) << (j & 63));
        }
        y[0] = o[0];
        y[1] = o[1];
    };
    if (rhi)
    {
        reverseRegion(x);
    }
    // ord[k] = source bit of output bit start + k (a permutation of the region)
    uint8_t ord[128];
    unsigned n = 0;
    // Modes 1/3/7 flag 64: the P bits (just below the indices in the natural order) move after the indices
    const unsigned np = mode == 1 ? 2 : (mode == 3 || mode == 7) ? 4 : 0;
    const bool pAfter = np && (v & 64);
    const unsigned start = (mode == 6 && (v & 8)) ? 0 : pAfter ? gs[0] - np : gs[0];
    const unsigned run = 1u << ((v >> 12) & 3);
    auto sym = [&](unsigned g, unsigned t)
    {
        const bool sel = !(v & 48) || ((v & 16) && g == 0) || ((v & 32) && g == 1);
        const unsigned s = (v & 0x800) ? (t + 1) & 15 : t;
        const unsigned tt = scan[(sel && (v & 1)) ? 15 - s : s];
        const unsigned w = ((anchors >> tt) & 1) ? ga[g] : gw[g];
        const unsigned p = gs[g] + tt * gw[g] - unsigned(__popcnt(anchors & ((1u << tt) - 1)));
        uint8_t bits[5];
        unsigned nb = 0;
        for (unsigned b = 0; b < w; b++)
        {
            bits[nb++] = uint8_t(p + b);
        }
        if (tt == 0 && mode == 5 && (v & 64))
        {
            bits[nb++] = uint8_t(62 + g);
        }
        for (unsigned b = 0; b < nb; b++)
        {
            ord[n++] = (sel && (v & 2)) ? bits[nb - 1 - b] : bits[b];
        }
    };
    if (v & 128)
    {
        for (unsigned t0 = 0; t0 < 16; t0 += run)
            for (unsigned gi = 0; gi < ng; gi++)
                for (unsigned t = t0; t < t0 + run; t++)
                    sym((v & 4) ? ng - 1 - gi : gi, t);
    }
    else
    {
        for (unsigned gi = 0; gi < ng; gi++)
            for (unsigned t = 0; t < 16; t++)
                sym((v & 4) ? ng - 1 - gi : gi, t);
    }
    if (mode == 6 && (v & 8))
    {
        ord[n++] = 0;
    }
    for (unsigned b = 0; pAfter && b < np; b++)
    {
        ord[n++] = uint8_t(gs[0] - np + b);
    }
    // Mode 5 per-texel group XOR (a value transform, applied before the permutation): 0x4000 = alpha ^= color, 0x8000 = color ^= alpha
    auto xorA = [](uint64_t* y) { y[0] ^= (y[0] & 0x7fffffffull) << 31; };
    auto xorC = [](uint64_t* y) { y[0] ^= (y[0] >> 31) & 0x7fffffffull; };
    if (mode == 5 && !inverse)
    {
        if (v & 0x4000)
            xorA(x);
        if (v & 0x8000)
            xorC(x);
    }
    uint64_t o[2] = { x[0], x[1] };
    for (unsigned k = 0; k < n; k++)
    {
        const unsigned to = inverse ? ord[k] : start + k, from = inverse ? start + k : ord[k];
        o[to >> 6] = (o[to >> 6] & ~(1ull << (to & 63))) | (((x[from >> 6] >> (from & 63)) & 1) << (to & 63));
    }
    x[0] = o[0];
    x[1] = o[1];
    if (rhi)
    {
        reverseRegion(x);
    }
    if (mode == 5 && inverse)
    {
        if (v & 0x8000)
            xorC(x);
        if (v & 0x4000)
            xorA(x);
    }
    if (!inverse && shift > 0 && shift < rlen)
    {
        rotL(x, rLo, rlen, shift);
    }
    if (!inverse && shift2 > 0 && shift2 < rlen2)
    {
        rotL(x, lo2, rlen2, shift2);
    }
}

const uint8_t* BC7_ModeJoin_IndexFieldScan(uint64_t v)
{
    return kJfpScan[unsigned((v >> 8) & 7) | unsigned(((v >> 16) & 3) << 3)];
}

void BC7_ModeJoin_IndexFieldPermute(uint64_t* x, unsigned mode, uint64_t v, bool inverse)
{
    BC7_ModeJoin_IndexFieldPermuteScan(x, mode, v, inverse, BC7_ModeJoin_IndexFieldScan(v));
}

// JFP setting of a mode (0 = none)
uint64_t BC7_ModeJoin_IndexFieldPerm(unsigned mode)
{
    return mode == 1 ? uint64_t(GACL_JFP1) : mode == 3 ? uint64_t(GACL_JFP3) : mode == 4 ? uint64_t(GACL_JFP4) : mode == 5 ? uint64_t(GACL_JFP5) :
        mode == 6 ? uint64_t(GACL_JFP6) : mode == 7 ? uint64_t(GACL_JFP7) : 0;
}

#ifndef GACL_JBRT
#define GACL_JBRT 1     // fixed per mode / byte position remap of each finished Join block (bc7joinremap.h), for streams that signal it (metrics.JoinRemap)
#endif
#if GACL_JBRT
#include "bc7joinremap.h"
#endif

// The mode byte (byte 15) keeps its mode bit (only the data bits above it are remapped), so the decoder finds the mode before
// undoing the remap. Streams with metrics.JoinRemap 0 aren't remapped.
void BC7_ModeJoin_ByteRemap(uint8_t* block, bool inverse, const BC7TextureMetrics& metrics)
{
#if GACL_JBRT
    if (metrics.JoinRemap == 0)
    {
        return;
    }
    struct Inverse
    {
        uint8_t T[8][16][256];
        Inverse()
        {
            for (unsigned m = 0; m < 8; m++)
                for (unsigned p = 0; p < 16; p++)
                    for (unsigned v = 0; v < 256; v++)
                        T[m][p][kBC7JoinRemap[m][p][v]] = uint8_t(v);
        }
    };
    static const Inverse kInv;
    unsigned long m;
    _BitScanForward(&m, block[15] | 0x100u);
    if (m >= 8)
    {
        return;
    }
    for (unsigned p = 0; p < 16; p++)
    {
        block[p] = (inverse ? kInv.T[m][p] : kBC7JoinRemap[m][p])[block[p]];
    }
#else
    UNREFERENCED_PARAMETER(block);
    UNREFERENCED_PARAMETER(inverse);
    UNREFERENCED_PARAMETER(metrics);
#endif
}

unsigned BC7_ModeJoin_RemapSets()
{
#if GACL_JBRT
    return 1;
#else
    return 0;
#endif
}

void BC7_ModeJoin_Transform(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, BC7TextureMetrics& metrics, std::vector<uint8_t>& controlBytes)
{
    UNREFERENCED_PARAMETER(controlBytes); // eventually used for exporting transform parameters
    const size_t block_count = srcSize / 16;

    dest.resize(srcSize);

    uint8_t* pDest = dest.data();

    // each mode is shuffled with its own mask (per-mode masks, or the group-wide one)
    BC7TextureMetrics modeMetrics[8];
    for (size_t m = 0; m < 8; m++)
    {
        modeMetrics[m] = metrics;
        modeMetrics[m].Statics = BC7_JoinModeMask(metrics, m);
        modeMetrics[m].LowEntropy = 0;
        modeMetrics[m].JoinModeMasks = false;
    }

    for (size_t i = 0; i < block_count; ++i, src += 16, pDest += 16)
    {
        uint64_t d[2] = {};

        // Look for first set bit in [7:0].  If none set, return 8 (invalid).
        unsigned long mode;
        _BitScanForward(&mode, *src | 0x100u); 

        if (mode == 8)
        {
            // direct copy, shifting by 12 bytes
            const uint64_t* s64 = reinterpret_cast<const uint64_t*>(src);
            d[0] = (s64[0] >> 32) | (s64[1] << 32);
            d[1] = (s64[1] >> 32) | (s64[0] << 32);
        }
        else
        {
            BC7_ModeJoin_Shuffle_Fast[mode](src, d, modeMetrics[mode]);
        }


        // Rotate left 24 bits so the index bits (first 24 at the top of d[1], the rest at the bottom of d[0])
        // become contiguous in natural order; other byte rotations measured 0.4%-1.4% worse.
#if GACL_JROT == 24
        uint64_t sh = 24;
        uint64_t ds[2] = { (d[0] << sh) | (d[1] >> (64 - sh)) , (d[1] << sh) | (d[0] >> (64 - sh)) };
#endif
#if GACL_JBO == 4
        // After the 24-bit rotation the index bits sit low in the block and the mode byte is byte 15: bits [kIdxLo,
        // kIdxHi) hold the index bits and the P bits stored just below them (Mode 0: its last 3, Modes 1/3/7: all), and
        // bit lo + k swaps with bit hi - 1 - k (BC7_ModeJoin_Reverse repeats the table)
        if (mode < 8 && ((GACL_JBOM >> mode) & 1))
        {
            static const uint8_t kIdxLo[8] = { 0, 0, 1, 6, 0, 0, 1, 6 }, kIdxHi[8] = { 48, 48, 30, 40, 78, 62, 64, 40 };
            const unsigned lo = kIdxLo[mode], hi = kIdxHi[mode];
            uint64_t o[2] = { ds[0], ds[1] };
            for (unsigned k = lo; k < hi; k++)
            {
                const unsigned j = lo + hi - 1 - k;
                o[j >> 6] = (o[j >> 6] & ~(1ull << (j & 63))) | (((ds[k >> 6] >> (k & 63)) & 1) << (j & 63));
            }
            ds[0] = o[0];
            ds[1] = o[1];
        }
#endif
#if GACL_JFP1 || GACL_JFP3 || GACL_JFP4 || GACL_JFP5 || GACL_JFP6 || GACL_JFP7
        if (mode < 8)
        {
            const uint64_t jfp = BC7_ModeJoin_IndexFieldPerm(mode);
            if (jfp)
                BC7_ModeJoin_IndexFieldPermute(ds, mode, jfp, false);
        }
#endif
#if GACL_JBRT
        (BC7_ModeJoin_ByteRemap)(reinterpret_cast<uint8_t*>(ds), false, metrics);     // parenthesized: no ADL (this file is also compiled inside namespace BC7JoinLayout)
#endif
        memcpy_s(pDest, 16, ds, 16);
    }
}

// Field-transform configuration compiled into the fast paths above, for BC7_ModeJoin_Reverse (bc7modejoinreverse.cpp):
// 0 = layout only (every field transform off), 1 = the production field transforms, -1 = any other combination (no reverse).
// Scope and sub-option macros (IEPM, IMSP, IOFM) only matter with their transform on; some aren't defined without it.
int BC7_ModeJoin_FieldConfig()
{
#define BC7J_OTHERS_OFF ((GACL_JBO == 0 || GACL_JBO == 4) && GACL_JROT == 24)
#if BC7J_OTHERS_OFF && !GACL_IED && !GACL_IEDX && !GACL_IEB && !GACL_IEP && !GACL_IEPA && !GACL_IMS && !GACL_IEDL && \
    !GACL_IIP && !GACL_IZB && !GACL_IXE && !GACL_IXEK2 && !GACL_IXEK4 && !GACL_IOF && !GACL_IOFA && !GACL_IAS
    return 0;
#elif BC7J_OTHERS_OFF && GACL_IED == 2 && GACL_IEDX == 0x8F && GACL_IEB == 1 && GACL_IEP == 2 && GACL_IEPA == 1 && \
    GACL_IEPM == 0x8F && GACL_IMS == 3 && GACL_IMSM == 0x60 && GACL_IMSP == 1 && GACL_IEDL == 7 && GACL_IIP == 2 && \
    GACL_IIPM == 511 && GACL_IZB == 1 && GACL_IXE == 4 && GACL_IXEK2 == 4 && GACL_IXEK4 == 7 && GACL_IOF == 2 && \
    GACL_IOFM == 0xFF && GACL_IOFA == 1 && GACL_IAS == 2
    return 1;
#else
    return -1;
#endif
#undef BC7J_OTHERS_OFF
}

// Modes whose index bits BC7_ModeJoin_Transform stores reversed (JBO=4); undone in BC7_ModeJoin_Reverse.
unsigned BC7_ModeJoin_IndexReverseModes()
{
    return GACL_JBO == 4 ? unsigned(GACL_JBOM) & 0xFF : 0;
}
