//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//

#pragma once
#include <string>
#include <dxgiformat.h>
#include <dstorage.h>


// from zstd.h
#define ZSTD_BLOCKSIZELOG_MAX  17
#define ZSTD_BLOCKSIZE_MAX     (1<<ZSTD_BLOCKSIZELOG_MAX)
#define ZSTD_TARGETCBLOCKSIZE_MIN   1340
#define ZSTD_TARGETCBLOCKSIZE_MAX   ZSTD_BLOCKSIZE_MAX

struct Parameter
{
    uint32_t zstdCompressionLevel; // 1 to 22, 0 = auto adjust
    uint32_t zstdBlockSize;        // ZSTD_TARGETCBLOCKSIZE_MIN to ZSTD_TARGETCBLOCKSIZE_MAX. 0 = auto adjust
    uint32_t chunkSize;            // 64KiB to 1024KiB
    bool bCompress;
    bool bShuffle;

    Parameter()
        : zstdCompressionLevel(0)
        , zstdBlockSize(0)
        , chunkSize(512 * 1024)
        , bCompress(true)
        , bShuffle(true)
    {
    }
};

// Processing image functions.

HRESULT LoadInputImage(DirectX::ScratchImage& image, const wchar_t* pFileName);
HRESULT ShuffleAndCompress(DirectX::ScratchImage& srcForNonCurveTransforms, Parameter& param,
    std::vector<uint8_t>& buffer, std::vector<ChunkMetadata>& chunkMetadata);

// Utility functions.

void ParseParameter(int argc, wchar_t* argv[], Parameter& param);
DSTORAGE_GACL_SHUFFLE_TRANSFORM_TYPE GetTransformType(GACL_SHUFFLE_TRANSFORM gaclShuffleTransform);

const wchar_t* DXGIFormatToString(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_BC1_TYPELESS: return L"BC1_TYPELESS";
    case DXGI_FORMAT_BC1_UNORM: return L"BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB: return L"BC1_UNORM_SRGB";
    case DXGI_FORMAT_BC2_TYPELESS: return L"BC2_TYPELESS";
    case DXGI_FORMAT_BC2_UNORM: return L"BC2_UNORM";
    case DXGI_FORMAT_BC2_UNORM_SRGB: return L"BC2_UNORM_SRGB";
    case DXGI_FORMAT_BC3_TYPELESS: return L"BC3_TYPELESS";
    case DXGI_FORMAT_BC3_UNORM: return L"BC3_UNORM";
    case DXGI_FORMAT_BC3_UNORM_SRGB: return L"BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_TYPELESS: return L"BC4_TYPELESS";
    case DXGI_FORMAT_BC4_UNORM: return L"BC4_UNORM";
    case DXGI_FORMAT_BC4_SNORM: return L"BC4_SNORM";
    case DXGI_FORMAT_BC5_TYPELESS: return L"BC5_TYPELESS";
    case DXGI_FORMAT_BC5_UNORM: return L"BC5_UNORM";
    case DXGI_FORMAT_BC5_SNORM: return L"BC5_SNORM";
    case DXGI_FORMAT_BC6H_TYPELESS: return L"BC6H_TYPELESS";
    case DXGI_FORMAT_BC6H_UF16: return L"BC6H_UF16";
    case DXGI_FORMAT_BC6H_SF16: return L"BC6H_SF16";
    case DXGI_FORMAT_BC7_TYPELESS: return L"BC7_TYPELESS";
    case DXGI_FORMAT_BC7_UNORM: return L"BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB: return L"BC7_UNORM_SRGB";
    default: return L"UNSUPPORTED_FORMAT";
    }
}

// File I/O functions.

void FindFiles(std::wstring sourcePath, std::vector<std::wstring>& results);
void LoadFileList(std::wstring listFileName, std::wstring sourcePath, std::vector<std::wstring>& files, std::vector<Parameter>& parameters);
