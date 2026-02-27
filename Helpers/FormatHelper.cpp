//-------------------------------------------------------------------------------------
// FormatHelper.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "FormatHelper.h"

DXGI_FORMAT GetBaseFormat( DXGI_FORMAT Format )
{
    switch (Format)
    {
        // 32:32:32:32
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return DXGI_FORMAT_R32G32B32A32_TYPELESS;

        // 32:32:32
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return DXGI_FORMAT_R32G32B32_TYPELESS;

        // 16:16:16:16
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
        return DXGI_FORMAT_R16G16B16A16_TYPELESS;

        // 32:32
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
        return DXGI_FORMAT_R32G32_TYPELESS;

        // 10:10:10:2
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
        return DXGI_FORMAT_R10G10B10A2_TYPELESS;

        // 8:8:8:8
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;

        // 16:16
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
        return DXGI_FORMAT_R16G16_TYPELESS;

        // 32
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
        return DXGI_FORMAT_R32_TYPELESS;

        // 8:8
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
        return DXGI_FORMAT_R8G8_TYPELESS;

        // 16
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SINT:
        return DXGI_FORMAT_R16_TYPELESS;

        // 8
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
        return DXGI_FORMAT_R8_TYPELESS;

        // BCn
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return DXGI_FORMAT_BC1_TYPELESS;
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
        return DXGI_FORMAT_BC2_TYPELESS;
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        return DXGI_FORMAT_BC3_TYPELESS;
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        return DXGI_FORMAT_BC4_TYPELESS;
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
        return DXGI_FORMAT_BC5_TYPELESS;
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
        return DXGI_FORMAT_BC6H_TYPELESS;
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return DXGI_FORMAT_BC7_TYPELESS;

        // BGRA
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;

        // BGRX
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_TYPELESS;

    default:
        return Format;
    }
}

DXGI_FORMAT GetUAVFormat( DXGI_FORMAT Format )
{
    switch (Format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;

    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;

    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_UNORM;

    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return DXGI_FORMAT_UNKNOWN;

    default:
        return Format;
    }
}

bool IsBlockCompressed(DXGI_FORMAT Format)
{
    switch (Format)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return true;
    default:
        return false;
    }
}

bool IsGammaFormat(DXGI_FORMAT Format)
{
    switch (Format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return true;
    default:
        return false;
    }
}

bool HasGammaFormat(DXGI_FORMAT Format)
{
    switch (Format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return true;
    default:
        return false;
    }
}

DXGI_FORMAT DisableSRGB(DXGI_FORMAT Format)
{
    switch (Format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return DXGI_FORMAT_BC1_UNORM;
    case DXGI_FORMAT_BC2_UNORM_SRGB:
        return DXGI_FORMAT_BC2_UNORM;
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        return DXGI_FORMAT_BC3_UNORM;
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return DXGI_FORMAT_BC7_UNORM;
    default:
        return Format;
    }
}

DXGI_FORMAT EnableSRGB(DXGI_FORMAT Format)
{
    switch (Format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    case DXGI_FORMAT_BC1_UNORM:
        return DXGI_FORMAT_BC1_UNORM_SRGB;
    case DXGI_FORMAT_BC2_UNORM:
        return DXGI_FORMAT_BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_UNORM:
        return DXGI_FORMAT_BC3_UNORM_SRGB;
    case DXGI_FORMAT_BC7_UNORM:
        return DXGI_FORMAT_BC7_UNORM_SRGB;
    default:
        return Format;
    }
}

const char* ToString(DXGI_FORMAT Format)
{
    switch (Format)
    {
    default:
    case DXGI_FORMAT_UNKNOWN                      : return "DXGI_FORMAT_UNKNOWN";
    case DXGI_FORMAT_R32G32B32A32_FLOAT           : return "DXGI_FORMAT_R32G32B32A32_FLOAT";
    case DXGI_FORMAT_R32G32B32A32_UINT            : return "DXGI_FORMAT_R32G32B32A32_UINT";
    case DXGI_FORMAT_R32G32B32A32_SINT            : return "DXGI_FORMAT_R32G32B32A32_SINT";
    case DXGI_FORMAT_R32G32B32_FLOAT              : return "DXGI_FORMAT_R32G32B32_FLOAT";
    case DXGI_FORMAT_R32G32B32_UINT               : return "DXGI_FORMAT_R32G32B32_UINT";
    case DXGI_FORMAT_R32G32B32_SINT               : return "DXGI_FORMAT_R32G32B32_SINT";
    case DXGI_FORMAT_R16G16B16A16_FLOAT           : return "DXGI_FORMAT_R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_UNORM           : return "DXGI_FORMAT_R16G16B16A16_UNORM";
    case DXGI_FORMAT_R16G16B16A16_UINT            : return "DXGI_FORMAT_R16G16B16A16_UINT";
    case DXGI_FORMAT_R16G16B16A16_SNORM           : return "DXGI_FORMAT_R16G16B16A16_SNORM";
    case DXGI_FORMAT_R16G16B16A16_SINT            : return "DXGI_FORMAT_R16G16B16A16_SINT";
    case DXGI_FORMAT_R32G32_FLOAT                 : return "DXGI_FORMAT_R32G32_FLOAT";
    case DXGI_FORMAT_R32G32_UINT                  : return "DXGI_FORMAT_R32G32_UINT";
    case DXGI_FORMAT_R32G32_SINT                  : return "DXGI_FORMAT_R32G32_SINT";
    case DXGI_FORMAT_R10G10B10A2_UNORM            : return "DXGI_FORMAT_R10G10B10A2_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UINT             : return "DXGI_FORMAT_R10G10B10A2_UINT";
    case DXGI_FORMAT_R11G11B10_FLOAT              : return "DXGI_FORMAT_R11G11B10_FLOAT";
    case DXGI_FORMAT_R8G8B8A8_UNORM               : return "DXGI_FORMAT_R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB          : return "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R8G8B8A8_UINT                : return "DXGI_FORMAT_R8G8B8A8_UINT";
    case DXGI_FORMAT_R8G8B8A8_SNORM               : return "DXGI_FORMAT_R8G8B8A8_SNORM";
    case DXGI_FORMAT_R8G8B8A8_SINT                : return "DXGI_FORMAT_R8G8B8A8_SINT";
    case DXGI_FORMAT_R16G16_FLOAT                 : return "DXGI_FORMAT_R16G16_FLOAT";
    case DXGI_FORMAT_R16G16_UNORM                 : return "DXGI_FORMAT_R16G16_UNORM";
    case DXGI_FORMAT_R16G16_UINT                  : return "DXGI_FORMAT_R16G16_UINT";
    case DXGI_FORMAT_R16G16_SNORM                 : return "DXGI_FORMAT_R16G16_SNORM";
    case DXGI_FORMAT_R16G16_SINT                  : return "DXGI_FORMAT_R16G16_SINT";
    case DXGI_FORMAT_R32_FLOAT                    : return "DXGI_FORMAT_R32_FLOAT";
    case DXGI_FORMAT_R32_UINT                     : return "DXGI_FORMAT_R32_UINT";
    case DXGI_FORMAT_R32_SINT                     : return "DXGI_FORMAT_R32_SINT";
    case DXGI_FORMAT_R8G8_UNORM                   : return "DXGI_FORMAT_R8G8_UNORM";
    case DXGI_FORMAT_R8G8_UINT                    : return "DXGI_FORMAT_R8G8_UINT";
    case DXGI_FORMAT_R8G8_SNORM                   : return "DXGI_FORMAT_R8G8_SNORM";
    case DXGI_FORMAT_R8G8_SINT                    : return "DXGI_FORMAT_R8G8_SINT";
    case DXGI_FORMAT_R16_FLOAT                    : return "DXGI_FORMAT_R16_FLOAT";
    case DXGI_FORMAT_R16_UNORM                    : return "DXGI_FORMAT_R16_UNORM";
    case DXGI_FORMAT_R16_UINT                     : return "DXGI_FORMAT_R16_UINT";
    case DXGI_FORMAT_R16_SNORM                    : return "DXGI_FORMAT_R16_SNORM";
    case DXGI_FORMAT_R16_SINT                     : return "DXGI_FORMAT_R16_SINT";
    case DXGI_FORMAT_R8_UNORM                     : return "DXGI_FORMAT_R8_UNORM";
    case DXGI_FORMAT_R8_UINT                      : return "DXGI_FORMAT_R8_UINT";
    case DXGI_FORMAT_R8_SNORM                     : return "DXGI_FORMAT_R8_SNORM";
    case DXGI_FORMAT_R8_SINT                      : return "DXGI_FORMAT_R8_SINT";
    case DXGI_FORMAT_A8_UNORM                     : return "DXGI_FORMAT_A8_UNORM";
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP           : return "DXGI_FORMAT_R9G9B9E5_SHAREDEXP";
    case DXGI_FORMAT_BC1_UNORM                    : return "DXGI_FORMAT_BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB               : return "DXGI_FORMAT_BC1_UNORM_SRGB";
    case DXGI_FORMAT_BC2_UNORM                    : return "DXGI_FORMAT_BC2_UNORM";
    case DXGI_FORMAT_BC2_UNORM_SRGB               : return "DXGI_FORMAT_BC2_UNORM_SRGB";
    case DXGI_FORMAT_BC3_UNORM                    : return "DXGI_FORMAT_BC3_UNORM";
    case DXGI_FORMAT_BC3_UNORM_SRGB               : return "DXGI_FORMAT_BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_UNORM                    : return "DXGI_FORMAT_BC4_UNORM";
    case DXGI_FORMAT_BC4_SNORM                    : return "DXGI_FORMAT_BC4_SNORM";
    case DXGI_FORMAT_BC5_UNORM                    : return "DXGI_FORMAT_BC5_UNORM";
    case DXGI_FORMAT_BC5_SNORM                    : return "DXGI_FORMAT_BC5_SNORM";
    case DXGI_FORMAT_B5G6R5_UNORM                 : return "DXGI_FORMAT_B5G6R5_UNORM";
    case DXGI_FORMAT_B5G5R5A1_UNORM               : return "DXGI_FORMAT_B5G5R5A1_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM               : return "DXGI_FORMAT_B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8X8_UNORM               : return "DXGI_FORMAT_B8G8R8X8_UNORM";
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM   : return "DXGI_FORMAT_R10G10B10_XR_BIAS_A2_NORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB          : return "DXGI_FORMAT_B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB          : return "DXGI_FORMAT_B8G8R8X8_UNORM_SRGB";
    case DXGI_FORMAT_BC6H_UF16                    : return "DXGI_FORMAT_BC6H_UF16";
    case DXGI_FORMAT_BC6H_SF16                    : return "DXGI_FORMAT_BC6H_SF16";
    case DXGI_FORMAT_BC7_UNORM                    : return "DXGI_FORMAT_BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB               : return "DXGI_FORMAT_BC7_UNORM_SRGB";
    }
}

const wchar_t* ToStringW(DXGI_FORMAT Format)
{
    switch (Format)
    {
    default:
    case DXGI_FORMAT_UNKNOWN                      : return L"DXGI_FORMAT_UNKNOWN";
    case DXGI_FORMAT_R32G32B32A32_FLOAT           : return L"DXGI_FORMAT_R32G32B32A32_FLOAT";
    case DXGI_FORMAT_R32G32B32A32_UINT            : return L"DXGI_FORMAT_R32G32B32A32_UINT";
    case DXGI_FORMAT_R32G32B32A32_SINT            : return L"DXGI_FORMAT_R32G32B32A32_SINT";
    case DXGI_FORMAT_R32G32B32_FLOAT              : return L"DXGI_FORMAT_R32G32B32_FLOAT";
    case DXGI_FORMAT_R32G32B32_UINT               : return L"DXGI_FORMAT_R32G32B32_UINT";
    case DXGI_FORMAT_R32G32B32_SINT               : return L"DXGI_FORMAT_R32G32B32_SINT";
    case DXGI_FORMAT_R16G16B16A16_FLOAT           : return L"DXGI_FORMAT_R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_UNORM           : return L"DXGI_FORMAT_R16G16B16A16_UNORM";
    case DXGI_FORMAT_R16G16B16A16_UINT            : return L"DXGI_FORMAT_R16G16B16A16_UINT";
    case DXGI_FORMAT_R16G16B16A16_SNORM           : return L"DXGI_FORMAT_R16G16B16A16_SNORM";
    case DXGI_FORMAT_R16G16B16A16_SINT            : return L"DXGI_FORMAT_R16G16B16A16_SINT";
    case DXGI_FORMAT_R32G32_FLOAT                 : return L"DXGI_FORMAT_R32G32_FLOAT";
    case DXGI_FORMAT_R32G32_UINT                  : return L"DXGI_FORMAT_R32G32_UINT";
    case DXGI_FORMAT_R32G32_SINT                  : return L"DXGI_FORMAT_R32G32_SINT";
    case DXGI_FORMAT_R10G10B10A2_UNORM            : return L"DXGI_FORMAT_R10G10B10A2_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UINT             : return L"DXGI_FORMAT_R10G10B10A2_UINT";
    case DXGI_FORMAT_R11G11B10_FLOAT              : return L"DXGI_FORMAT_R11G11B10_FLOAT";
    case DXGI_FORMAT_R8G8B8A8_UNORM               : return L"DXGI_FORMAT_R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB          : return L"DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R8G8B8A8_UINT                : return L"DXGI_FORMAT_R8G8B8A8_UINT";
    case DXGI_FORMAT_R8G8B8A8_SNORM               : return L"DXGI_FORMAT_R8G8B8A8_SNORM";
    case DXGI_FORMAT_R8G8B8A8_SINT                : return L"DXGI_FORMAT_R8G8B8A8_SINT";
    case DXGI_FORMAT_R16G16_FLOAT                 : return L"DXGI_FORMAT_R16G16_FLOAT";
    case DXGI_FORMAT_R16G16_UNORM                 : return L"DXGI_FORMAT_R16G16_UNORM";
    case DXGI_FORMAT_R16G16_UINT                  : return L"DXGI_FORMAT_R16G16_UINT";
    case DXGI_FORMAT_R16G16_SNORM                 : return L"DXGI_FORMAT_R16G16_SNORM";
    case DXGI_FORMAT_R16G16_SINT                  : return L"DXGI_FORMAT_R16G16_SINT";
    case DXGI_FORMAT_R32_FLOAT                    : return L"DXGI_FORMAT_R32_FLOAT";
    case DXGI_FORMAT_R32_UINT                     : return L"DXGI_FORMAT_R32_UINT";
    case DXGI_FORMAT_R32_SINT                     : return L"DXGI_FORMAT_R32_SINT";
    case DXGI_FORMAT_R8G8_UNORM                   : return L"DXGI_FORMAT_R8G8_UNORM";
    case DXGI_FORMAT_R8G8_UINT                    : return L"DXGI_FORMAT_R8G8_UINT";
    case DXGI_FORMAT_R8G8_SNORM                   : return L"DXGI_FORMAT_R8G8_SNORM";
    case DXGI_FORMAT_R8G8_SINT                    : return L"DXGI_FORMAT_R8G8_SINT";
    case DXGI_FORMAT_R16_FLOAT                    : return L"DXGI_FORMAT_R16_FLOAT";
    case DXGI_FORMAT_R16_UNORM                    : return L"DXGI_FORMAT_R16_UNORM";
    case DXGI_FORMAT_R16_UINT                     : return L"DXGI_FORMAT_R16_UINT";
    case DXGI_FORMAT_R16_SNORM                    : return L"DXGI_FORMAT_R16_SNORM";
    case DXGI_FORMAT_R16_SINT                     : return L"DXGI_FORMAT_R16_SINT";
    case DXGI_FORMAT_R8_UNORM                     : return L"DXGI_FORMAT_R8_UNORM";
    case DXGI_FORMAT_R8_UINT                      : return L"DXGI_FORMAT_R8_UINT";
    case DXGI_FORMAT_R8_SNORM                     : return L"DXGI_FORMAT_R8_SNORM";
    case DXGI_FORMAT_R8_SINT                      : return L"DXGI_FORMAT_R8_SINT";
    case DXGI_FORMAT_A8_UNORM                     : return L"DXGI_FORMAT_A8_UNORM";
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP           : return L"DXGI_FORMAT_R9G9B9E5_SHAREDEXP";
    case DXGI_FORMAT_BC1_UNORM                    : return L"DXGI_FORMAT_BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB               : return L"DXGI_FORMAT_BC1_UNORM_SRGB";
    case DXGI_FORMAT_BC2_UNORM                    : return L"DXGI_FORMAT_BC2_UNORM";
    case DXGI_FORMAT_BC2_UNORM_SRGB               : return L"DXGI_FORMAT_BC2_UNORM_SRGB";
    case DXGI_FORMAT_BC3_UNORM                    : return L"DXGI_FORMAT_BC3_UNORM";
    case DXGI_FORMAT_BC3_UNORM_SRGB               : return L"DXGI_FORMAT_BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_UNORM                    : return L"DXGI_FORMAT_BC4_UNORM";
    case DXGI_FORMAT_BC4_SNORM                    : return L"DXGI_FORMAT_BC4_SNORM";
    case DXGI_FORMAT_BC5_UNORM                    : return L"DXGI_FORMAT_BC5_UNORM";
    case DXGI_FORMAT_BC5_SNORM                    : return L"DXGI_FORMAT_BC5_SNORM";
    case DXGI_FORMAT_B5G6R5_UNORM                 : return L"DXGI_FORMAT_B5G6R5_UNORM";
    case DXGI_FORMAT_B5G5R5A1_UNORM               : return L"DXGI_FORMAT_B5G5R5A1_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM               : return L"DXGI_FORMAT_B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8X8_UNORM               : return L"DXGI_FORMAT_B8G8R8X8_UNORM";
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM   : return L"DXGI_FORMAT_R10G10B10_XR_BIAS_A2_NORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB          : return L"DXGI_FORMAT_B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB          : return L"DXGI_FORMAT_B8G8R8X8_UNORM_SRGB";
    case DXGI_FORMAT_BC6H_UF16                    : return L"DXGI_FORMAT_BC6H_UF16";
    case DXGI_FORMAT_BC6H_SF16                    : return L"DXGI_FORMAT_BC6H_SF16";
    case DXGI_FORMAT_BC7_UNORM                    : return L"DXGI_FORMAT_BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB               : return L"DXGI_FORMAT_BC7_UNORM_SRGB";
    }
}

size_t GetElementSize(DXGI_FORMAT Format)
{
    switch (Format)
    {
    default:
    case DXGI_FORMAT_UNKNOWN                      : return 0;
    case DXGI_FORMAT_R32G32B32A32_FLOAT           :
    case DXGI_FORMAT_R32G32B32A32_UINT            :
    case DXGI_FORMAT_R32G32B32A32_SINT            : return 16;
    case DXGI_FORMAT_R32G32B32_FLOAT              : 
    case DXGI_FORMAT_R32G32B32_UINT               : 
    case DXGI_FORMAT_R32G32B32_SINT               : return 12;
    case DXGI_FORMAT_R16G16B16A16_FLOAT           :
    case DXGI_FORMAT_R16G16B16A16_UNORM           :
    case DXGI_FORMAT_R16G16B16A16_UINT            :
    case DXGI_FORMAT_R16G16B16A16_SNORM           :
    case DXGI_FORMAT_R16G16B16A16_SINT            :
    case DXGI_FORMAT_R32G32_FLOAT                 :
    case DXGI_FORMAT_R32G32_UINT                  :
    case DXGI_FORMAT_R32G32_SINT                  : return 8;
    case DXGI_FORMAT_R10G10B10A2_UNORM            : 
    case DXGI_FORMAT_R10G10B10A2_UINT             : 
    case DXGI_FORMAT_R11G11B10_FLOAT              : 
    case DXGI_FORMAT_R8G8B8A8_UNORM               : 
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB          : 
    case DXGI_FORMAT_R8G8B8A8_UINT                : 
    case DXGI_FORMAT_R8G8B8A8_SNORM               : 
    case DXGI_FORMAT_R8G8B8A8_SINT                : 
    case DXGI_FORMAT_R16G16_FLOAT                 : 
    case DXGI_FORMAT_R16G16_UNORM                 : 
    case DXGI_FORMAT_R16G16_UINT                  : 
    case DXGI_FORMAT_R16G16_SNORM                 : 
    case DXGI_FORMAT_R16G16_SINT                  : 
    case DXGI_FORMAT_R32_FLOAT                    : 
    case DXGI_FORMAT_R32_UINT                     : 
    case DXGI_FORMAT_R32_SINT                     : return 4;
    case DXGI_FORMAT_R8G8_UNORM                   :
    case DXGI_FORMAT_R8G8_UINT                    :
    case DXGI_FORMAT_R8G8_SNORM                   :
    case DXGI_FORMAT_R8G8_SINT                    :
    case DXGI_FORMAT_R16_FLOAT                    :
    case DXGI_FORMAT_R16_UNORM                    :
    case DXGI_FORMAT_R16_UINT                     :
    case DXGI_FORMAT_R16_SNORM                    :
    case DXGI_FORMAT_R16_SINT                     : return 2;
    case DXGI_FORMAT_R8_UNORM                     :
    case DXGI_FORMAT_R8_UINT                      :
    case DXGI_FORMAT_R8_SNORM                     :
    case DXGI_FORMAT_R8_SINT                      :
    case DXGI_FORMAT_A8_UNORM                     : return 1;
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP           : return 4;
    case DXGI_FORMAT_BC1_UNORM                    : 
    case DXGI_FORMAT_BC1_UNORM_SRGB               : return 8; 
    case DXGI_FORMAT_BC2_UNORM                    : 
    case DXGI_FORMAT_BC2_UNORM_SRGB               : 
    case DXGI_FORMAT_BC3_UNORM                    : 
    case DXGI_FORMAT_BC3_UNORM_SRGB               : return 16;
    case DXGI_FORMAT_BC4_UNORM                    : 
    case DXGI_FORMAT_BC4_SNORM                    : return 8;
    case DXGI_FORMAT_BC5_UNORM                    : 
    case DXGI_FORMAT_BC5_SNORM                    : return 16;
    case DXGI_FORMAT_B5G6R5_UNORM                 :
    case DXGI_FORMAT_B5G5R5A1_UNORM               : return 2;
    case DXGI_FORMAT_B8G8R8A8_UNORM               :
    case DXGI_FORMAT_B8G8R8X8_UNORM               :
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM   :
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB          :
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB          : return 4;
    case DXGI_FORMAT_BC6H_UF16                    :
    case DXGI_FORMAT_BC6H_SF16                    :
    case DXGI_FORMAT_BC7_UNORM                    :
    case DXGI_FORMAT_BC7_UNORM_SRGB               : return 16;
    }
}

size_t GetChannelCount(DXGI_FORMAT Format)
{
    switch (Format)
    {
    default:
    case DXGI_FORMAT_UNKNOWN                      : return 0;
    case DXGI_FORMAT_R32G32B32A32_FLOAT           :
    case DXGI_FORMAT_R32G32B32A32_UINT            :
    case DXGI_FORMAT_R32G32B32A32_SINT            : return 4;
    case DXGI_FORMAT_R32G32B32_FLOAT              : 
    case DXGI_FORMAT_R32G32B32_UINT               : 
    case DXGI_FORMAT_R32G32B32_SINT               : return 3;
    case DXGI_FORMAT_R16G16B16A16_FLOAT           :
    case DXGI_FORMAT_R16G16B16A16_UNORM           :
    case DXGI_FORMAT_R16G16B16A16_UINT            :
    case DXGI_FORMAT_R16G16B16A16_SNORM           :
    case DXGI_FORMAT_R16G16B16A16_SINT            : return 4;
    case DXGI_FORMAT_R32G32_FLOAT                 :
    case DXGI_FORMAT_R32G32_UINT                  :
    case DXGI_FORMAT_R32G32_SINT                  : return 2;
    case DXGI_FORMAT_R10G10B10A2_UNORM            :
    case DXGI_FORMAT_R10G10B10A2_UINT             : return 4; 
    case DXGI_FORMAT_R11G11B10_FLOAT              : return 3;
    case DXGI_FORMAT_R8G8B8A8_UNORM               : 
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB          : 
    case DXGI_FORMAT_R8G8B8A8_UINT                : 
    case DXGI_FORMAT_R8G8B8A8_SNORM               : 
    case DXGI_FORMAT_R8G8B8A8_SINT                : return 4;
    case DXGI_FORMAT_R16G16_FLOAT                 : 
    case DXGI_FORMAT_R16G16_UNORM                 : 
    case DXGI_FORMAT_R16G16_UINT                  : 
    case DXGI_FORMAT_R16G16_SNORM                 : 
    case DXGI_FORMAT_R16G16_SINT                  : return 2;
    case DXGI_FORMAT_R32_FLOAT                    : 
    case DXGI_FORMAT_R32_UINT                     : 
    case DXGI_FORMAT_R32_SINT                     : return 1;
    case DXGI_FORMAT_R8G8_UNORM                   :
    case DXGI_FORMAT_R8G8_UINT                    :
    case DXGI_FORMAT_R8G8_SNORM                   :
    case DXGI_FORMAT_R8G8_SINT                    : return 2;
    case DXGI_FORMAT_R16_FLOAT                    :
    case DXGI_FORMAT_R16_UNORM                    :
    case DXGI_FORMAT_R16_UINT                     :
    case DXGI_FORMAT_R16_SNORM                    :
    case DXGI_FORMAT_R16_SINT                     :
    case DXGI_FORMAT_R8_UNORM                     :
    case DXGI_FORMAT_R8_UINT                      :
    case DXGI_FORMAT_R8_SNORM                     :
    case DXGI_FORMAT_R8_SINT                      :
    case DXGI_FORMAT_A8_UNORM                     : return 1;
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP           :
    case DXGI_FORMAT_BC1_UNORM                    : 
    case DXGI_FORMAT_BC1_UNORM_SRGB               : return 3; 
    case DXGI_FORMAT_BC2_UNORM                    : 
    case DXGI_FORMAT_BC2_UNORM_SRGB               : 
    case DXGI_FORMAT_BC3_UNORM                    : 
    case DXGI_FORMAT_BC3_UNORM_SRGB               : return 4;
    case DXGI_FORMAT_BC4_UNORM                    : 
    case DXGI_FORMAT_BC4_SNORM                    : return 1;
    case DXGI_FORMAT_BC5_UNORM                    : 
    case DXGI_FORMAT_BC5_SNORM                    : return 2;
    case DXGI_FORMAT_B5G6R5_UNORM                 : return 3;
    case DXGI_FORMAT_B5G5R5A1_UNORM               :
    case DXGI_FORMAT_B8G8R8A8_UNORM               :
    case DXGI_FORMAT_B8G8R8X8_UNORM               :
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM   :
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB          :
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB          : return 4;
    case DXGI_FORMAT_BC6H_UF16                    :
    case DXGI_FORMAT_BC6H_SF16                    : return 3;
    case DXGI_FORMAT_BC7_UNORM                    :
    case DXGI_FORMAT_BC7_UNORM_SRGB               : return 4;
    }
}

bool IsFloatFormat(DXGI_FORMAT Format)
{
    switch (Format)
    {
    default:
        return false;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC6H_UF16:
        return true;
    }
}

bool IsSnormFormat(DXGI_FORMAT Format)
{
    switch (Format)
    {
    default:
        return false;
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R8_SNORM:
        return true;
    }
}

bool IsIntegerFormat(DXGI_FORMAT Format)
{
    switch (Format)
    {
    default:
        return false;
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SINT:
        return true;
    }
}



DXGI_FORMAT EnableInteger(DXGI_FORMAT Format)
{
    switch (Format)
    {
    default                                 : return Format;
    case DXGI_FORMAT_R32G32B32A32_FLOAT     : return DXGI_FORMAT_R32G32B32A32_UINT;
    case DXGI_FORMAT_R32G32B32_FLOAT        : return DXGI_FORMAT_R32G32B32_UINT;
    case DXGI_FORMAT_R16G16B16A16_UNORM     : return DXGI_FORMAT_R16G16B16A16_UINT;
    case DXGI_FORMAT_R16G16B16A16_SNORM     : return DXGI_FORMAT_R16G16B16A16_SINT;
    case DXGI_FORMAT_R32G32_FLOAT           : return DXGI_FORMAT_R32G32_UINT;
    case DXGI_FORMAT_R10G10B10A2_UNORM      : return DXGI_FORMAT_R10G10B10A2_UINT;
    case DXGI_FORMAT_R8G8B8A8_UNORM         :
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB    : return DXGI_FORMAT_R8G8B8A8_UINT;
    case DXGI_FORMAT_R8G8B8A8_SNORM         : return DXGI_FORMAT_R8G8B8A8_SINT;
    case DXGI_FORMAT_R16G16_UNORM           : return DXGI_FORMAT_R16G16_UINT;
    case DXGI_FORMAT_R16G16_SNORM           : return DXGI_FORMAT_R16G16_SINT;
    case DXGI_FORMAT_R32_FLOAT              : return DXGI_FORMAT_R32_UINT;
    case DXGI_FORMAT_R8G8_UNORM             : return DXGI_FORMAT_R8G8_UINT;
    case DXGI_FORMAT_R8G8_SNORM             : return DXGI_FORMAT_R8G8_SINT;
    case DXGI_FORMAT_R16_UNORM              : return DXGI_FORMAT_R16_UINT;
    case DXGI_FORMAT_R16_SNORM              : return DXGI_FORMAT_R16_SINT;
    case DXGI_FORMAT_R8_UNORM               : return DXGI_FORMAT_R8_UINT;
    case DXGI_FORMAT_R8_SNORM               : return DXGI_FORMAT_R8_SINT;
    }
}

DXGI_FORMAT DisableInteger(DXGI_FORMAT Format)
{
    switch (Format)
    {
    default                                 : return Format;
    case  DXGI_FORMAT_R32G32B32A32_UINT     : return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case  DXGI_FORMAT_R32G32B32_UINT        : return DXGI_FORMAT_R32G32B32_FLOAT;
    case  DXGI_FORMAT_R16G16B16A16_UINT     : return DXGI_FORMAT_R16G16B16A16_UNORM;
    case  DXGI_FORMAT_R16G16B16A16_SINT     : return DXGI_FORMAT_R16G16B16A16_SNORM;
    case  DXGI_FORMAT_R32G32_UINT           : return DXGI_FORMAT_R32G32_FLOAT;
    case  DXGI_FORMAT_R10G10B10A2_UINT      : return DXGI_FORMAT_R10G10B10A2_UNORM;
    case  DXGI_FORMAT_R8G8B8A8_UINT         : return DXGI_FORMAT_R8G8B8A8_UNORM;
    case  DXGI_FORMAT_R8G8B8A8_SINT         : return DXGI_FORMAT_R8G8B8A8_SNORM;
    case  DXGI_FORMAT_R16G16_UINT           : return DXGI_FORMAT_R16G16_UNORM;
    case  DXGI_FORMAT_R16G16_SINT           : return DXGI_FORMAT_R16G16_SNORM;
    case  DXGI_FORMAT_R32_UINT              : return DXGI_FORMAT_R32_FLOAT;
    case  DXGI_FORMAT_R8G8_UINT             : return DXGI_FORMAT_R8G8_UNORM;
    case  DXGI_FORMAT_R8G8_SINT             : return DXGI_FORMAT_R8G8_SNORM;
    case  DXGI_FORMAT_R16_UINT              : return DXGI_FORMAT_R16_UNORM;
    case  DXGI_FORMAT_R16_SINT              : return DXGI_FORMAT_R16_SNORM;
    case  DXGI_FORMAT_R8_UINT               : return DXGI_FORMAT_R8_UNORM;
    case  DXGI_FORMAT_R8_SINT               : return DXGI_FORMAT_R8_SNORM;
    }
}