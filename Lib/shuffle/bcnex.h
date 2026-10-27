//-------------------------------------------------------------------------------------
// bcnex.h
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

// The extensible (EX) streams GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX, _BC3_EX, _BC4_EX and _BC5_EX, one id per format
// (BCnEx_TransformId). The block stream (space-curved when the curve width parameter is present) goes through:
//   1. per-block index prediction (see BCnEx_DefaultPrediction): the 3-bit indices of the BC4-style halves (BC4, BC3
//      alpha and both BC5 channels) and the 2-bit indices of the BC1-style color halves (BC1, BC3 color)
//   2. the stream header (streamheader.h): duplicate blocks are removed to make room for the header in the first k
//      blocks, and streams short of duplicates code their cheapest blocks in the header (kBC1BlockCode for BC1,
//      kBC5BlockCode for BC3/4/5, bc5blockcode.h); the header transform code names one of the format's shuffle patterns
//   3. the format's SoA shuffle (Shuffle_BC1/3/4/5, version by pattern) of the n - k kept blocks, stored after the header
// so the decoded size equals the input size. BC7 Join streams carrying a header are GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_EX
// streams (header code 0). With BCNEX_UNSHUFFLED in the header's prediction mask the kept blocks skip step 3 and follow
// the header as they are: curve-only streams (BCnEx_EncodeCurveOnly), which also skip step 1, of any of these formats,
// BC7 included (BC7_EX streams of header transform BC5_116116).
// Decoding is block-independent: a block's stored index comes from StreamHeader_MapBlock, and the block is gathered
// from the SoA payload with BCn_ReadShuffledBlock (what UnshuffleBCnEx.hlsl does per thread).

// Block size (8 or 16) of a headerless BC1/3/4/5 transform id (0x01-0x04, 0x08-0x0A), or 0
size_t BCn_BlockSize(uint8_t transformId);

// The EX id (GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX - _BC7_EX) of the format of a headerless BC1/3/4/5 transform id
// (0x01-0x04, 0x08-0x0A) or GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN, or 0
uint8_t BCnEx_TransformId(uint8_t transformId);

// Most header blocks (k) of an EX stream, BC7 Join included; the GPU decoder (UnshuffleBCnEx.hlsl) keeps the
// removed-block and coded-block tables in groupshared memory. Streams needing more fall back to another transform.
constexpr size_t BCNEX_MAX_HEADER_BLOCKS = 64;

// True when EX streams of this header transform (a headerless BC1/3/4/5 id) carry index prediction
bool BCnEx_Predicted(uint8_t transformId);

// Index prediction masks: a stream's transform implies BCnEx_DefaultPrediction (every BCNEX_PREDICT_* bit that applies
// to the format). A BC1/3/4/5 EX stream may carry header parameter 2 (1 byte, a mask of BCNEX_PREDICT_*) that
// replaces the default with a different subset of it, optionally plus BCNEX_UNSHUFFLED; decoders (BCnEx_Decode,
// UnshuffleBCnEx.hlsl) reject any other value. GACL_ShuffleCompress_BCn picks it per stream: BC1 / BC3 candidates are
// also tried without color prediction, and curve-only streams set BCNEX_UNSHUFFLED alone.
constexpr uint8_t BCNEX_PREDICT_HALVES = 1;     // BC4-style halves: BC3 alpha, BC4, both BC5 channels
constexpr uint8_t BCNEX_PREDICT_COLOUR = 2;     // BC1-style color halves: BC1, BC3 color
constexpr uint8_t BCNEX_UNSHUFFLED = 4;         // not a prediction: the kept blocks follow the header unshuffled (any
                                                // format, with or without a curve or prediction)
constexpr uint8_t BCNEX_PARAM_PREDICTION = 2;   // header parameter id of the mask (BC1/3/4/5 header transforms only)
uint8_t BCnEx_DefaultPrediction(uint8_t transformId);
uint8_t BCnEx_PredictionChoices(uint8_t transformId);  // the mask bits that apply to the format

// Index prediction of one BC4-style half block (bytes 0-1 endpoints, bytes 2-7 sixteen 3-bit indices): indices are
// reordered along the ramp, then replaced by bounded zigzag residuals against a causal in-block predictor (MED inside,
// linear extrapolation on the first row / column). Endpoints are unchanged. The BC1-style color halves (bytes 4-7
// sixteen 2-bit indices, ramp order by color mode; BC3's are always four-color) are predicted the same way, with
// half-step extrapolation on the first row / column.
void BCnEx_PredictHalf(uint8_t* half);
void BCnEx_UnpredictHalf(uint8_t* half);

// Gathers block j (0-based) of a payload of blockCount blocks shuffled by Shuffle_BCx for the headerless transform id
// (0x01-0x04, 0x08-0x0A) into out (one block)
void BCn_ReadShuffledBlock(const uint8_t* payload, size_t blockCount, uint8_t transformId, size_t j, uint8_t* out);

// Reverses a headerless BC1/3/4/5 shuffle (ids 0x01-0x04, 0x08-0x0A). Returns false for other ids or sizes.
bool BCn_Unshuffle(const uint8_t* src, size_t size, uint8_t transformId, std::vector<uint8_t>& dest);

// Encodes src (size bytes; already curved when curveWidthTiles != 0) as an EX stream (id BCnEx_TransformId(transformId))
// whose payload uses the headerless transform id's shuffle pattern. curveWidthTiles is the texture width in 16KB
// micro-tiles (0 = linear).
// Returns false, leaving dest empty, when there is no room for the header (too few duplicate and coded blocks within
// BCNEX_MAX_HEADER_BLOCKS), or when the curve doesn't fit the stream (GACL_Shuffle_ApplySpaceCurve's geometry), which
// the decoders would reject.
// prediction is the index prediction mask, -1 = BCnEx_DefaultPrediction; another subset of BCnEx_PredictionChoices,
// optionally plus BCNEX_UNSHUFFLED (the kept blocks then stay unshuffled), is written as header parameter 2.
// headerBlocksOut receives the header's block count (k), codedBlocksOut how many blocks the header codes (0 when
// removing duplicate blocks made the room; GACL_ShuffleCompress_BCn only uses BC1 streams with coded blocks at or below
// the default zstd level when they save at least 1%).
bool BCnEx_Encode(const uint8_t* src, size_t size, uint8_t transformId, uint8_t curveWidthTiles, std::vector<uint8_t>& dest,
    size_t* headerBlocksOut = nullptr, int prediction = -1, size_t* codedBlocksOut = nullptr);

// Encodes src (size bytes, already curved) as a curve-only EX stream (the replacement for the former
// GACL_SHUFFLE_TRANSFORM_ZSTD_SC; id BCnEx_TransformId(transformId)): BCnEx_Encode with prediction BCNEX_UNSHUFFLED
// (no prediction, no shuffle) for the format's first header transform (BC1_224, BC3_116224, BC4_116, BC5_116116), so
// the header carries the curve width and parameter 2 and the kept blocks follow it as they are. transformId is any
// headerless BC1/3/4/5 id of the format, or GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN for BC7 (a BC7_EX stream), whose
// 16-byte blocks the stream holds as BC5_116116 blocks (coded with kBC5BlockCode). curveWidthTiles must be nonzero.
// Returns false like BCnEx_Encode.
bool BCnEx_EncodeCurveOnly(const uint8_t* src, size_t size, uint8_t transformId, uint8_t curveWidthTiles, std::vector<uint8_t>& dest,
    size_t* headerBlocksOut = nullptr, size_t* codedBlocksOut = nullptr);

// Decodes an EX stream (after zstd decompression) of id transformId (GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX - _BC7_EX),
// curve-only streams included. The header must name a shuffle pattern of the id's format (BCnEx_TransformId); BC7_EX
// streams hold Join (header code 0) or, as curve-only streams, BC5_116116 blocks with parameter 2 exactly
// BCNEX_UNSHUFFLED. Curved streams are uncurved unless uncurve is false. transformOut receives the header's transform id
// (GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116 for a curve-only BC7 stream), curveWidthTilesOut the curve width.
bool BCnEx_Decode(const uint8_t* src, size_t size, uint8_t transformId, std::vector<uint8_t>& dest, bool uncurve = true,
    uint8_t* transformOut = nullptr, uint8_t* curveWidthTilesOut = nullptr);
