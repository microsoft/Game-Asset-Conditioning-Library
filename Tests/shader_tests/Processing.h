//--------------------------------------------------------------------------------------
// Processing.h
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "../../Helpers/Utility.h"
#include "FileUtility.h"
#include "DirectXTex.h"

#include "gacl.h"
#include "bc7.h"

struct ShuffleParameters
{
    bool useSpaceCurve;
    uint32_t ChunkSize;
    uint8_t endpointOrderStrategy;
    BC7ModeSplitShufflePattern* Patterns;
};

void ShuffleBC7Texture(
    std::vector<uint8_t>& shuffledBuffer,
    uint8_t const* src,
    size_t const srcSize,
    size_t m_bcTextureWidthInPixels, 
    ShuffleParameters const& shuffleParams);


// Pull in internal GACL APIs for round trip validation.
// Normal usage would instead go through GACL_ShuffleCompress_BCn()
HRESULT Shuffle_BC1(uint8_t* dest, const uint8_t* src, size_t size, size_t version);
HRESULT Shuffle_BC3(uint8_t* dest, const uint8_t* src, size_t size, size_t version);
HRESULT Shuffle_BC4(uint8_t* dest, const uint8_t* src, size_t size, size_t version);
HRESULT Shuffle_BC5(uint8_t* dest, const uint8_t* src, size_t size, size_t version);

