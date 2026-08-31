//--------------------------------------------------------------------------------------
// Processing.h
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "gacl.h"
#include "bc7.h"

struct ShuffleParameters
{
    uint32_t ChunkSize;
    BC7ModeSplitShufflePattern Patterns[8];
    uint8_t endpointOrderStrategy;
    BC7ModeSplitModeTransform modeTransform;
    bool useSpaceCurve;
};

DXGI_FORMAT IdentifyBCEncodeFormat(const std::string& ident);

void ProcessTexture(
    const std::wstring& inputFilePath,
    std::vector<uint8_t>& shuffledBuffer,
    std::vector<uint8_t>& originalBC7,
    size_t& bc7TextureSizeInBytes,
    size_t& bcTextureWidthInPixels,
    size_t& bcTextureHeightInPixels,
    size_t& bcTextureSubresourceCount,
    DXGI_FORMAT& bcTextureFormat,
    GACL_SHUFFLE_TRANSFORM& bcnTransformId,
    ShuffleParameters const& shuffleParams);