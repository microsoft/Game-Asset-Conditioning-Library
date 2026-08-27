//--------------------------------------------------------------------------------------
// Processing.cpp
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "Processing.h"

using namespace DirectX;
using namespace std;

void ShuffleBC7Texture(
    std::vector<uint8_t>& shuffledBuffer,
    uint8_t const* src,
    size_t const srcSize,
    size_t m_bcTextureWidthInPixels, 
    ShuffleParameters const& shuffleParams)
{
    BC7_ModeSplit_Validate_CopyBitOrders();

    vector<uint8_t> curved(srcSize);
    if (shuffleParams.useSpaceCurve)
    {
        GACL_Shuffle_ApplySpaceCurve(curved.data(), src, srcSize, 16, m_bcTextureWidthInPixels, true);
        src = curved.data();
    }

    BC7TextureMetrics metrics = {};
    BC7_CollectTextureMetrics(src, srcSize, metrics);

    BC7ModeSplitShuffleOptions opt = {};
    opt.ModeTransform = BC7ModeSplitModeTransformB;
    memcpy(opt.Patterns, shuffleParams.Patterns, sizeof(opt.Patterns));
    opt.EndpointOrderStrategy = (BC7ModeSplitFieldOrderStrategy)shuffleParams.endpointOrderStrategy;
    opt.RotationRegionSize = shuffleParams.ChunkSize;

    // Call the GACL lib to do the shuffling
    vector<uint8_t> splitModeA, splitModeB;
    BC7_ModeSplit_Transform(src, srcSize, splitModeA, splitModeB, opt, metrics);

    // Copy result into shuffled buffer
    size_t shuffledBufferSizeInBytes = splitModeB.size();
    shuffledBuffer.resize(shuffledBufferSizeInBytes);
    memcpy(shuffledBuffer.data(), &splitModeB[0], shuffledBufferSizeInBytes);
}