//-------------------------------------------------------------------------------------
// bcnex.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "gacl.h"
#include "bcnex.h"
#include "bcnexblockcode.h"
#include "bc7.h"
#include "streamheader.h"
#include <intrin.h>
#include <cassert>
#include <cstdlib>
#include <cstring>

HRESULT Shuffle_BC1(uint8_t* dest, const uint8_t* src, size_t size, size_t version);
HRESULT Shuffle_BC3(uint8_t* dest, const uint8_t* src, size_t size, size_t version);
HRESULT Shuffle_BC4(uint8_t* dest, const uint8_t* src, size_t size, size_t version);
HRESULT Shuffle_BC5(uint8_t* dest, const uint8_t* src, size_t size, size_t version);

namespace
{
    // bounded zigzag around p over [0, top]: symmetric while both sides fit, then the remaining one-sided distances
    inline int Fold(int v, int p, int top = 7)
    {
        const int d = v - p, a = d < 0 ? -d : d, sb = p < top - p ? p : top - p;
        return a <= sb ? (d > 0 ? 2 * d : (d < 0 ? 2 * a - 1 : 0)) : sb + a;
    }

    inline int Unfold(int z, int p, int top = 7)
    {
        const int sb = p < top - p ? p : top - p;
        if (z <= 2 * sb)
        {
            return (z & 1) ? p - (z + 1) / 2 : p + z / 2;
        }
        const int a = z - sb;
        return p - a >= 0 ? p - a : p + a;
    }

    // ramp order: 8-value mode (e0 > e1) e0, 2..7, e1; 6-value mode e0, 2..5, e1, then the 0 / 255 constants
    const uint8_t kPos8[8] = { 0, 7, 1, 2, 3, 4, 5, 6 }, kIdx8[8] = { 0, 2, 3, 4, 5, 6, 7, 1 };
    const uint8_t kPos6[8] = { 0, 5, 1, 2, 3, 4, 6, 7 }, kIdx6[8] = { 0, 2, 3, 4, 5, 1, 6, 7 };

    // causal prediction of index i (> 0) over [0, top]: MED inside the block, left / top on the first row / column,
    // extrapolated from the two previous indices when there are two: the whole step for the 3-bit indices (top 7),
    // half of it (truncated) for the 2-bit color indices (top 3)
    inline int Pred(const int* v, int i, int top = 7)
    {
        const int x = i & 3, y = i >> 2;
        if (x && y)
        {
            const int l = v[i - 1], t = v[i - 4], tl = v[i - 5], mx = l > t ? l : t, mn = l < t ? l : t;
            return tl >= mx ? mn : (tl <= mn ? mx : l + t - tl);
        }
        const int a = x ? v[i - 1] : v[i - 4];
        if (x ? x < 2 : y < 2)
        {
            return a;
        }
        const int d = a - (x ? v[i - 2] : v[i - 8]);
        const int g = a + (top == 3 ? d / 2 : d);
        return g < 0 ? 0 : (g > top ? top : g);
    }

    inline void Unpack(const uint8_t* b, int* v)
    {
        uint64_t bits = 0;
        for (int k = 0; k < 6; k++) bits |= uint64_t(b[2 + k]) << (8 * k);
        for (int i = 0; i < 16; i++) v[i] = int((bits >> (3 * i)) & 7);
    }

    inline void Pack(uint8_t* b, const int* v)
    {
        uint64_t bits = 0;
        for (int i = 0; i < 16; i++) bits |= uint64_t(v[i]) << (3 * i);
        for (int k = 0; k < 6; k++) b[2 + k] = uint8_t(bits >> (8 * k));
    }

    // BC1-style color half (bytes 0-3 two 5:6:5 endpoints, bytes 4-7 sixteen 2-bit indices). Ramp order: four-color
    // mode (BC3, or BC1 with c0 > c1) c0, 2, 3, c1; three-color mode c0, 2, c1, then 3 (black).
    const uint8_t kPos4[4] = { 0, 3, 1, 2 }, kIdx4[4] = { 0, 2, 3, 1 };
    const uint8_t kPos3[4] = { 0, 2, 1, 3 };    // its own inverse

    inline const uint8_t* ColourRamp(const uint8_t* half, bool fourColour, bool inverse)
    {
        const uint32_t c0 = half[0] | (uint32_t(half[1]) << 8), c1 = half[2] | (uint32_t(half[3]) << 8);
        return fourColour || c0 > c1 ? (inverse ? kIdx4 : kPos4) : kPos3;
    }

    void PredictColourHalf(uint8_t* half, bool fourColour)
    {
        const uint8_t* pos = ColourRamp(half, fourColour, false);
        uint32_t bits;
        memcpy(&bits, half + 4, 4);
        int v[16], r[16];
        for (int i = 0; i < 16; i++) v[i] = pos[(bits >> (2 * i)) & 3];
        r[0] = v[0];
        for (int i = 1; i < 16; i++) r[i] = Fold(v[i], Pred(v, i, 3), 3);
        bits = 0;
        for (int i = 0; i < 16; i++) bits |= uint32_t(r[i]) << (2 * i);
        memcpy(half + 4, &bits, 4);
    }

    void UnpredictColourHalf(uint8_t* half, bool fourColour)
    {
        const uint8_t* idx = ColourRamp(half, fourColour, true);
        uint32_t bits;
        memcpy(&bits, half + 4, 4);
        int v[16];
        for (int i = 0; i < 16; i++) v[i] = int((bits >> (2 * i)) & 3);
        for (int i = 1; i < 16; i++) v[i] = Unfold(v[i], Pred(v, i, 3), 3);
        bits = 0;
        for (int i = 0; i < 16; i++) bits |= uint32_t(idx[v[i]]) << (2 * i);
        memcpy(half + 4, &bits, 4);
    }

    // BC1 / BC3 color endpoint split of the v2 patterns: 12 major and 4 minor bits per 5:6:5 endpoint
    constexpr uint32_t kMajor = 0b1111011110011110u;
    constexpr uint32_t kMinor = 0b0000100001100001u;

    inline void UnsplitEndpoints(uint32_t eo, uint8_t* out)
    {
        const uint16_t e0 = uint16_t(_pdep_u32(eo >> 20, kMajor) | _pdep_u32((eo >> 4) & 0xf, kMinor));
        const uint16_t e1 = uint16_t(_pdep_u32((eo >> 8) & 0xfff, kMajor) | _pdep_u32(eo & 0xf, kMinor));
        memcpy(out, &e0, 2);
        memcpy(out + 2, &e1, 2);
    }

    size_t GroupBlocks(uint8_t transformId)
    {
        return BCn_BlockSize(transformId) == 8 && transformId != uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116) ? 2 : 4;
    }

    // Code of the blocks a header may store itself (coded blocks): kBC1BlockCode for BC1, kBC5BlockCode for BC3/4/5
    // (BC4 using its first 8 entries)
    const StreamHeaderBlockCode* BlockCodeFor(uint8_t transformId)
    {
        static const StreamHeaderBlockCode bc1 = { kBC1BlockCode, false }, bc5 = { kBC5BlockCode, false };
        if (transformId == uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224) || transformId == uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44))
        {
            return &bc1;
        }
        return BCn_BlockSize(transformId) != 0 ? &bc5 : nullptr;
    }

    // Header transform of a curve-only stream of transformId's format: the format's first pattern (BC1_224,
    // BC3_116224, BC4_116, BC5_116116), BC5_116116 for BC7 (0x06: the same 16-byte blocks, 4 per GPU decoder thread);
    // 0 for other ids
    uint8_t CurveOnlyTransform(uint8_t transformId)
    {
        switch (transformId)
        {
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44: return uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224);
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664: return uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224);
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116: return uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116);
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266:
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN: return uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116);
        default: return 0;
        }
    }

    HRESULT ShuffleFor(uint8_t transformId, uint8_t* dest, const uint8_t* src, size_t size)
    {
        switch (transformId)
        {
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224: return Shuffle_BC1(dest, src, size, 1);
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44: return Shuffle_BC1(dest, src, size, 2);
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224: return Shuffle_BC3(dest, src, size, 1);
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664: return Shuffle_BC3(dest, src, size, 2);
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116: return Shuffle_BC4(dest, src, size, 1);
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116: return Shuffle_BC5(dest, src, size, 1);
        case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266: return Shuffle_BC5(dest, src, size, 2);
        default: return E_INVALIDARG;
        }
    }

    // Applies (forward) or removes the index prediction (mask of BCNEX_PREDICT_*) of every block
    void PredictBlocks(uint8_t* data, size_t size, uint8_t transformId, uint8_t mask, bool forward)
    {
        const size_t bs = BCn_BlockSize(transformId);
        const bool both = transformId == uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116) || transformId == uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266);
        const bool halves = (mask & BCNEX_PREDICT_HALVES) != 0, colour = (mask & BCNEX_PREDICT_COLOUR) != 0;
        for (size_t o = 0; o + bs <= size; o += bs)
        {
            for (size_t h = 0; halves && h < (both ? 16u : 8u); h += 8)
            {
                if (forward)
                {
                    BCnEx_PredictHalf(data + o + h);
                }
                else
                {
                    BCnEx_UnpredictHalf(data + o + h);
                }
            }
            if (colour)
            {
                // BC3's color half (bytes 8-15) always decodes in four-color mode
                if (forward)
                {
                    PredictColourHalf(data + o + bs - 8, bs == 16);
                }
                else
                {
                    UnpredictColourHalf(data + o + bs - 8, bs == 16);
                }
            }
        }
    }
}

size_t BCn_BlockSize(uint8_t transformId)
{
    switch (transformId)
    {
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:
        return 8;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266:
        return 16;
    default:
        return 0;
    }
}

uint8_t BCnEx_TransformId(uint8_t transformId)
{
    switch (transformId)
    {
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44:
        return uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX);
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664:
        return uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_EX);
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:
        return uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_EX);
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266:
        return uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_EX);
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN:
        return uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX);
    default:
        return 0;
    }
}

bool BCnEx_Predicted(uint8_t transformId)
{
    return BCnEx_DefaultPrediction(transformId) != 0;
}

uint8_t BCnEx_DefaultPrediction(uint8_t transformId)
{
    return BCnEx_PredictionChoices(transformId);
}

uint8_t BCnEx_PredictionChoices(uint8_t transformId)
{
    switch (transformId)
    {
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44:
        return BCNEX_PREDICT_COLOUR;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664:
        return BCNEX_PREDICT_HALVES | BCNEX_PREDICT_COLOUR;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266:
        return BCNEX_PREDICT_HALVES;
    default:
        return 0;
    }
}

void BCnEx_PredictHalf(uint8_t* half)
{
    const uint8_t* pos = half[0] > half[1] ? kPos8 : kPos6;
    int v[16], r[16];
    Unpack(half, v);
    for (int i = 0; i < 16; i++) v[i] = pos[v[i]];
    r[0] = v[0];
    for (int i = 1; i < 16; i++) r[i] = Fold(v[i], Pred(v, i));
    Pack(half, r);
}

void BCnEx_UnpredictHalf(uint8_t* half)
{
    const uint8_t* idx = half[0] > half[1] ? kIdx8 : kIdx6;
    int v[16];
    Unpack(half, v);
    for (int i = 1; i < 16; i++) v[i] = Unfold(v[i], Pred(v, i));
    for (int i = 0; i < 16; i++) v[i] = idx[v[i]];
    Pack(half, v);
}

void BCn_ReadShuffledBlock(const uint8_t* p, size_t blockCount, uint8_t transformId, size_t j, uint8_t* out)
{
    const size_t bs = BCn_BlockSize(transformId);
    const size_t q = blockCount / GroupBlocks(transformId) * GroupBlocks(transformId);   // blocks in whole pairs / quads
    const size_t S = q * bs;                                                                // shuffled bytes; stragglers follow as is
    if (j >= q)
    {
        memcpy(out, p + S + (j - q) * bs, bs);
        return;
    }

    switch (transformId)
    {
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:       // e0 1/4, e1 1/4, indices 1/2
        memcpy(out, p + 2 * j, 2);
        memcpy(out + 2, p + S / 4 + 2 * j, 2);
        memcpy(out + 4, p + S / 2 + 4 * j, 4);
        break;

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44:        // split endpoints 1/2, indices 1/2
    {
        uint32_t eo;
        memcpy(&eo, p + 4 * j, 4);
        UnsplitEndpoints(eo, out);
        memcpy(out + 4, p + S / 2 + 4 * j, 4);
        break;
    }

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:    // a0 1/16, a1 1/16, alpha indices 6/16, e0 2/16, e1 2/16, color indices 4/16
        out[0] = p[j];
        out[1] = p[S / 16 + j];
        memcpy(out + 2, p + S / 8 + 6 * j, 6);
        memcpy(out + 8, p + S / 2 + 2 * j, 2);
        memcpy(out + 10, p + S / 2 + S / 8 + 2 * j, 2);
        memcpy(out + 12, p + S / 2 + S / 4 + 4 * j, 4);
        break;

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664:       // nibble-split alpha endpoints + split color endpoints 6/16, alpha indices 6/16, color indices 4/16
    {
        const uint8_t* d = p + 6 * j;
        out[0] = uint8_t((d[1] & 0xf0) | (d[0] >> 4));
        out[1] = uint8_t(((d[1] & 0x0f) << 4) | (d[0] & 0x0f));
        memcpy(out + 2, p + S / 16 * 6 + 6 * j, 6);
        UnsplitEndpoints((uint32_t(d[2]) << 24) | (uint32_t(d[3]) << 16) | (uint32_t(d[4]) << 8) | d[5], out + 8);
        memcpy(out + 12, p + S / 16 * 12 + 4 * j, 4);
        break;
    }

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:       // r0 1/8, r1 1/8, indices 6/8
        out[0] = p[j];
        out[1] = p[S / 8 + j];
        memcpy(out + 2, p + S / 4 + 6 * j, 6);
        break;

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:    // r0 1/16, r1 1/16, red indices 6/16, g0 1/16, g1 1/16, green indices 6/16
        out[0] = p[j];
        out[1] = p[S / 16 + j];
        memcpy(out + 2, p + S / 8 + 6 * j, 6);
        out[8] = p[S / 2 + j];
        out[9] = p[S / 16 * 9 + j];
        memcpy(out + 10, p + S / 16 * 10 + 6 * j, 6);
        break;

    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_2266:      // r0 r1 2/16, g0 g1 2/16, red indices 6/16, green indices 6/16
        memcpy(out, p + 2 * j, 2);
        memcpy(out + 8, p + S / 8 + 2 * j, 2);
        memcpy(out + 2, p + S / 4 + 6 * j, 6);
        memcpy(out + 10, p + S / 16 * 10 + 6 * j, 6);
        break;

    default:
        memset(out, 0, bs);
        break;
    }
}

bool BCn_Unshuffle(const uint8_t* src, size_t size, uint8_t transformId, std::vector<uint8_t>& dest)
{
    dest.clear();
    const size_t bs = BCn_BlockSize(transformId);
    if (bs == 0 || src == nullptr || size % bs != 0)
    {
        return false;
    }
    dest.resize(size);
    const size_t n = size / bs;
    for (size_t j = 0; j < n; j++)
    {
        BCn_ReadShuffledBlock(src, n, transformId, j, dest.data() + j * bs);
    }
    return true;
}

bool BCnEx_Encode(const uint8_t* src, size_t size, uint8_t transformId, uint8_t curveWidthTiles, std::vector<uint8_t>& dest,
    size_t* headerBlocksOut, int prediction, size_t* codedBlocksOut)
{
    dest.clear();
    const size_t bs = BCn_BlockSize(transformId);
    if (bs == 0 || src == nullptr || size % bs != 0)
    {
        return false;
    }
    if (curveWidthTiles && !GACL_Shuffle_ApplySpaceCurve(nullptr, nullptr, size, bs, size_t(curveWidthTiles) * (bs == 16 ? 128 : 256), true))
    {
        return false;
    }
    const uint8_t defaultMask = BCnEx_DefaultPrediction(transformId);
    const uint8_t mask = prediction < 0 ? defaultMask : uint8_t(prediction);
    if (prediction > 0xFF || (mask & ~(BCnEx_PredictionChoices(transformId) | BCNEX_UNSHUFFLED)) != 0)
    {
        return false;
    }

    std::vector<uint8_t> blocks(src, src + size);
    if (mask & ~BCNEX_UNSHUFFLED)
    {
        PredictBlocks(blocks.data(), size, transformId, mask, true);
    }

    std::vector<StreamHeaderParam> params;
    if (curveWidthTiles)
    {
        params.push_back({ GACL_STREAMHEADER_PARAM_CURVE_WIDTH_TILES, { curveWidthTiles } });
    }
    if (mask != defaultMask)
    {
        params.push_back({ BCNEX_PARAM_PREDICTION, { mask } });
    }

    std::vector<uint8_t> withHeader;
    StreamHeaderInfo info;
    if (!StreamHeader_Encode(blocks.data(), size, bs, transformId, params, withHeader, &info, BlockCodeFor(transformId)) ||
        info.HeaderBlocks > BCNEX_MAX_HEADER_BLOCKS)
    {
        return false;
    }

    // the header stays in front; the kept blocks after it are shuffled as a stream of their own, unless unshuffled
    if (mask & BCNEX_UNSHUFFLED)
    {
        dest.swap(withHeader);
    }
    else
    {
        const size_t head = info.HeaderBlocks * bs;
        dest.resize(size);
        memcpy(dest.data(), withHeader.data(), head);
        if (FAILED(ShuffleFor(transformId, dest.data() + head, withHeader.data() + head, size - head)))
        {
            dest.clear();
            return false;
        }
    }
    if (headerBlocksOut)
    {
        *headerBlocksOut = info.HeaderBlocks;
    }
    if (codedBlocksOut)
    {
        *codedBlocksOut = info.Coded.size();
    }
    return true;
}

bool BCnEx_EncodeCurveOnly(const uint8_t* src, size_t size, uint8_t transformId, uint8_t curveWidthTiles, std::vector<uint8_t>& dest,
    size_t* headerBlocksOut, size_t* codedBlocksOut)
{
    dest.clear();
    const uint8_t headerId = CurveOnlyTransform(transformId);
    return headerId != 0 && curveWidthTiles != 0 &&
        BCnEx_Encode(src, size, headerId, curveWidthTiles, dest, headerBlocksOut, BCNEX_UNSHUFFLED, codedBlocksOut);
}

bool BCnEx_Decode(const uint8_t* src, size_t size, uint8_t streamTransformId, std::vector<uint8_t>& dest, bool uncurve,
    uint8_t* transformOut, uint8_t* curveWidthTilesOut)
{
    dest.clear();
    if (src == nullptr || size < GACL_STREAMHEADER_FIXED_BYTES)
    {
        return false;
    }
    const uint8_t transformId = kStreamHeaderTransforms[src[0] & 7];
    if (transformOut)
    {
        *transformOut = transformId;
    }
    // the header's pattern must be one of the stream id's format; BC7 streams also carry curve-only BC5_116116 blocks
    const bool bc7AsBC5 = streamTransformId == uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX) &&
        transformId == uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116);
    if (BCnEx_TransformId(transformId) != streamTransformId && !bc7AsBC5)
    {
        return false;
    }
    if (transformId == uint8_t(GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN))
    {
        return BC7_JoinStream_Decode(src, size, streamTransformId, dest, uncurve, curveWidthTilesOut);
    }

    const size_t bs = BCn_BlockSize(transformId);
    StreamHeaderInfo info;
    if (bs == 0 || !StreamHeader_Parse(src, size, bs, info, BlockCodeFor(transformId)) || info.HeaderBlocks > BCNEX_MAX_HEADER_BLOCKS)
    {
        return false;
    }
    uint8_t curveWidthTiles = 0;
    uint8_t mask = BCnEx_DefaultPrediction(transformId);
    for (const StreamHeaderParam& p : info.Params)
    {
        if (p.Id == GACL_STREAMHEADER_PARAM_CURVE_WIDTH_TILES && p.Value[0] != 0)
        {
            curveWidthTiles = p.Value[0];
        }
        else if (p.Id == BCNEX_PARAM_PREDICTION && p.Value[0] != mask &&
            (p.Value[0] & ~(BCnEx_PredictionChoices(transformId) | BCNEX_UNSHUFFLED)) == 0)
        {
            mask = p.Value[0];
        }
        else
        {
            return false;
        }
    }
    if (bc7AsBC5 && mask != BCNEX_UNSHUFFLED)
    {
        return false;   // BC7 blocks are only carried as they are
    }

    // stored index of every block: kept blocks fill [k, n) in order, a removed block reads its source's copy, and a
    // coded block (SIZE_MAX) is stored in the header
    const size_t n = size / bs, k = info.HeaderBlocks;
    std::vector<size_t> stored(n);
    for (size_t i = 0, r = 0, c = 0, o = k; i < n; i++)
    {
        if (r < info.Removed.size() && info.Removed[r] == i)
        {
            r++;
            continue;
        }
        if (c < info.Coded.size() && info.Coded[c] == i)
        {
            stored[i] = SIZE_MAX;
            c++;
            continue;
        }
        stored[i] = o++;
    }
    for (size_t j = 0; j < info.Removed.size(); j++)
    {
        stored[info.Removed[j]] = stored[info.Sources[j]];
    }

    std::vector<uint8_t> blocks(size);
    for (size_t i = 0, c = 0; i < n; i++)
    {
        assert(stored[i] == StreamHeader_MapBlock(info, i));
        if (stored[i] == SIZE_MAX)
        {
            memcpy(blocks.data() + i * bs, info.CodedBlocks.data() + c++ * bs, bs);
            continue;
        }
        if (mask & BCNEX_UNSHUFFLED)
        {
            memcpy(blocks.data() + i * bs, src + stored[i] * bs, bs);
            continue;
        }
        BCn_ReadShuffledBlock(src + k * bs, n - k, transformId, stored[i] - k, blocks.data() + i * bs);
    }
    if (mask & ~BCNEX_UNSHUFFLED)
    {
        PredictBlocks(blocks.data(), size, transformId, mask, false);
    }

    if (curveWidthTiles && uncurve)
    {
        // a 16KB micro-tile row is 32 16-byte blocks (128 pixels) or 64 8-byte blocks (256 pixels) wide
        dest.resize(size);
        if (!GACL_Shuffle_ApplySpaceCurve(dest.data(), blocks.data(), size, bs, size_t(curveWidthTiles) * (bs == 16 ? 128 : 256), false))
        {
            dest.clear();
            return false;
        }
    }
    else
    {
        dest.swap(blocks);
    }
    if (curveWidthTilesOut)
    {
        *curveWidthTilesOut = curveWidthTiles;
    }
    return true;
}
