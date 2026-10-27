//-------------------------------------------------------------------------------------
// bc7.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#include "gacl.h"

#include <vector>

struct ColorRotation
{
    uint8_t E[8];
};

// Describes ordering strategies where we trade 1-4 scrap bits to flip some channels
union EndpointOrderStrategy 
{
    struct
    {
        uint16_t B0R : 1;
        uint16_t B0G : 1;
        uint16_t B0B : 1;
        uint16_t B0A : 1;

        uint16_t B1R : 1;
        uint16_t B1G : 1;
        uint16_t B1B : 1;
        uint16_t B1A : 1;

        uint16_t B2R : 1;
        uint16_t B2G : 1;
        uint16_t B2B : 1;
        uint16_t B2A : 1;

        uint16_t B3R : 1;
        uint16_t B3G : 1;
        uint16_t B3B : 1;
        uint16_t B3A : 1;
    } BitChannels;
    struct
    {
        uint16_t B0 : 4;
        uint16_t B1 : 4;
        uint16_t B2 : 4;
        uint16_t B3 : 4;
    } Bits;
    struct
    {
        uint8_t B01;
        uint8_t B23;
    } BitPairs;
    uint16_t Raw;
    uint16_t BitsUsed() const
    {
        return Bits.B3 != 0 ? 4 : (Bits.B2 != 0 ? 3 : (Bits.B1 != 0 ? 2 : (Bits.B0 != 0 ? 1 : 0)));
    }
};

struct BC7TextureMetrics
{
    size_t ModeCounts[9];

    struct {
        uint8_t Statics;
        uint8_t LowEntropy;
        EndpointOrderStrategy OrderingStrategies[4];
    } M[8];

    uint8_t Statics;
    uint8_t LowEntropy;

    // Join per-mode (Statics | LowEntropy) masks; when JoinModeMasks is set they replace the group-wide mask
    bool JoinModeMasks;
    uint8_t JoinModeMask[8];

    // Join byte remap (bc7joinremap.h): 0 = none, s = table set s - 1 (1..BC7_ModeJoin_RemapSets()); nonzero needs the stream header.
    // The BC7 shuffle only tries it above the default zstd level: it costs the GPU Join decode a fifth of its throughput.
    uint8_t JoinRemap;
};

// The Join (Statics | LowEntropy) mask the blocks of a mode are shuffled with.
inline uint8_t BC7_JoinModeMask(const BC7TextureMetrics& m, size_t mode)
{
    return m.JoinModeMasks ? m.JoinModeMask[mode] : uint8_t(m.Statics | m.LowEntropy);
}

struct BC7ModeJoinShuffleOptions
{
    bool AltEncode;
};

// BC7 shuffle tries a variety of transforms and keeps the best.
// Since the compress is already done as part of transform selection, a "transform-only" version of BC7 is not included

void BC7_Mode45_ReorderRGBA(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest);
void BC7_CollectTextureMetrics(const uint8_t* src, size_t srcSize, BC7TextureMetrics& m);

void BC7_ModeJoin_RotateColors(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, bool altEncoding, size_t regionSize);
void BC7_ModeJoin_Transform(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics, std::vector<uint8_t>& controlBytes);
// Exact inverse of BC7_ModeJoin_Transform for the mask the stream was shuffled with (bc7modejoinreverse.cpp). Returns false
// when this build's Join field transforms have no reverse (BC7_ModeJoin_FieldConfig() < 0), for AltEncode, or for
// data that isn't a Join stream of this mask.
bool BC7_ModeJoin_Reverse(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, const BC7ModeJoinShuffleOptions& opt, const BC7TextureMetrics& metrics);
// Decodes a complete GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN or ..._BC7_EX Join stream (after zstd decompression) back to
// BC7 blocks (BC7_EX streams of a curve-only header are BCnEx_Decode's). Curved streams are uncurved unless uncurve is
// false; curveWidthTilesOut receives the curve width parameter.
bool BC7_JoinStream_Decode(const uint8_t* src, size_t srcSize, uint8_t transformId, std::vector<uint8_t>& dest, bool uncurve = true, uint8_t* curveWidthTilesOut = nullptr);
// Encodes BC7 blocks the way a Join candidate of the BC7 shuffle is (Mode 4/5 RGBA reorder, Join transform with the
// metrics' masks, stream header): a headerless GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN stream when no parameter is
// needed, otherwise a ..._BC7_EX stream (transformIdOut). src must already be space curved when curveWidthTiles
// (texture width in 128-pixel tiles) is nonzero. codedOut receives the number of blocks the header codes itself.
// Returns false, leaving dest empty, when the header doesn't fit in BCNEX_MAX_HEADER_BLOCKS blocks, or when the curve
// doesn't fit the stream (GACL_Shuffle_ApplySpaceCurve's geometry).
bool BC7_JoinStream_Encode(const uint8_t* src, size_t srcSize, const BC7TextureMetrics& metrics, uint8_t curveWidthTiles,
    std::vector<uint8_t>& dest, uint8_t* transformIdOut, size_t* codedOut = nullptr);
// Static code of the blocks a Join stream header stores itself (coded entries, streamheader.h; table in bc7joinblockcode.h)
struct StreamHeaderBlockCode;
const StreamHeaderBlockCode* BC7_JoinBlockCode();
int BC7_ModeJoin_FieldConfig();             // 0 = layout only, 1 = production field transforms, -1 = no reverse
unsigned BC7_ModeJoin_IndexReverseModes();  // mode mask of index-bit reversal (JBO=4), applied after either config
uint64_t BC7_ModeJoin_IndexFieldPerm(unsigned mode);    // index field permutation (JFP) of a mode, 0 = none
void BC7_ModeJoin_IndexFieldPermute(uint64_t* x, unsigned mode, uint64_t v, bool inverse);
const uint8_t* BC7_ModeJoin_IndexFieldScan(uint64_t v);    // texel scan (16 raster texel indices) selected by a JFP setting
void BC7_ModeJoin_IndexFieldPermuteScan(uint64_t* x, unsigned mode, uint64_t v, bool inverse, const uint8_t* scan);   // explicit texel scan
void BC7_ModeJoin_ByteRemap(uint8_t* block, bool inverse, const BC7TextureMetrics& metrics);  // fixed per mode/byte remap of a finished Join block with set metrics.JoinRemap - 1; no-op for JoinRemap 0 or JBRT off
unsigned BC7_ModeJoin_RemapSets();          // number of byte-remap table sets, the largest valid JoinRemap: 1 (base table), 0 with JBRT off
size_t BC7_ModeJoin_ReverseSelfTest();      // random-block round trip; returns the failure count
void BC7_ModeJoin_LayoutShuffle(unsigned mode, const void* src, void* dest, const BC7TextureMetrics& metrics);  // layout only (no field transforms)
int BC7_ModeJoin_LayoutFieldConfig();       // always 0


enum BC7ModeSplitModeTransform
{
    BC7ModeSplitModeTransformA = 1,
    BC7ModeSplitModeTransformB = 2,
    BC7ModeSplitModeTransformAny = 3,
};

enum BC7ModeSplitShufflePattern : uint8_t
{
    EndpointPair4bit,
    ColorPlane4bit,
    EndpointPairSignificantBitInderleaved,
    EndpointQuadSignificantBitInderleaved,
    EndpointQuadSignificantBitInderleavedAlt,
    StableIsland,
};

enum BC7ModeSplitFieldOrderStrategy
{
    GroupCorrelatedFields = 1,
    DropLeastEntropic = 2,
};

struct BC7ModeSplitShuffleOptions
{
    BC7ModeSplitModeTransform ModeTransform;
    BC7ModeSplitShufflePattern Patterns[8];
    uint8_t EndpointOrderStrategy;
    uint32_t RotationRegionSize;
    uint32_t SpaceCurveWidthInPixels;   // 0 if the source is linear, else the width the source was space curved with (recorded in the stream header, max 16384)
    uint8_t IndexPrediction;            // 1 = index fields are stored as spatial prediction residuals (recorded in the stream header)
    uint8_t SectionLayout;              // per-mode section layout (recorded in the stream header): 0 off, 1 chosen by zstd trials, 2 forced SectionSelectors (testing)
    uint8_t SectionTrialLevel;          // zstd level of the SectionLayout 1 trials (0 = 18)
    uint8_t SectionSelectors[8];        // SectionLayout 2 selector per mode, see SplitSectionLayout (invalid units fall back to none)
};



void BC7_ModeSplit_Transform(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& destA, std::vector<uint8_t>& destB, BC7ModeSplitShuffleOptions& opt, BC7TextureMetrics& metrics);
void BC7_ModeSplit_Reverse(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, size_t destSize, const uint8_t* ref);

void BC7_ModeSplit_Validate_CopyBitOrders();


