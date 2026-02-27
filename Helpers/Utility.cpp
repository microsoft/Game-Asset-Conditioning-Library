//-------------------------------------------------------------------------------------
// Utlity.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "gacl.h"
#include "Utility.h"
#include <string>
#include <locale>
#include <cassert>

#include <system_error>

// required for MultiByteToWideChar\WideCharToMultiByte
#include <Windows.h>

void Utility::DefaultLoggingRoutine(GACL_Logging_Priority msgPri, const wchar_t* msg)
{
    UNREFERENCED_PARAMETER(msgPri);
    OutputDebugString(msg);
}

namespace 
{
    PGACL_LOGGING_ROUTINE LoggingRoutine = &Utility::DefaultLoggingRoutine;
}

void GACL_Logging_SetCallback(
    _In_ PGACL_LOGGING_ROUTINE callback
)
{
    LoggingRoutine = callback;
}

void Utility::Print(GACL_Logging_Priority msgPri, const wchar_t* msg)
{
    LoggingRoutine(msgPri, msg);
}

void Utility::Print(const wchar_t* msg)
{
    LoggingRoutine(GACL_Logging_Priority_Low, msg);
}

DXGI_FORMAT Utility::GetBaseFormat(DXGI_FORMAT Format)
{
    switch (Format)
    {
        // 32:32:32:32
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        return DXGI_FORMAT_R32G32B32A32_TYPELESS;

        // 32:32:32
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
    case DXGI_FORMAT_R32G32B32_TYPELESS:
        return DXGI_FORMAT_R32G32B32_TYPELESS;

        // 16:16:16:16
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_TYPELESS;

        // 32:32
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:
        return DXGI_FORMAT_R32G32_TYPELESS;

        // 10:10:10:2
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_TYPELESS;

        // 8:8:8:8
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;

        // 16:16
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS:
        return DXGI_FORMAT_R16G16_TYPELESS;

        // 32
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R32_TYPELESS:
        return DXGI_FORMAT_R32_TYPELESS;

        // 8:8
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R8G8_TYPELESS:
        return DXGI_FORMAT_R8G8_TYPELESS;

        // 16
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
        return DXGI_FORMAT_R16_TYPELESS;

        // 8
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_R8_TYPELESS:
        return DXGI_FORMAT_R8_TYPELESS;

        // BCn
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC1_TYPELESS:
        return DXGI_FORMAT_BC1_TYPELESS;

    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS:
        return DXGI_FORMAT_BC2_TYPELESS;

    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
        return DXGI_FORMAT_BC3_TYPELESS;

    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC4_TYPELESS:
        return DXGI_FORMAT_BC4_TYPELESS;

    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC5_TYPELESS:
        return DXGI_FORMAT_BC5_TYPELESS;

    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC6H_TYPELESS:
        return DXGI_FORMAT_BC6H_TYPELESS;

    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
    case DXGI_FORMAT_BC7_TYPELESS:
        return DXGI_FORMAT_BC7_TYPELESS;

        // BGRA
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;

        // BGRX
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return DXGI_FORMAT_B8G8R8X8_TYPELESS;

    default:
        return Format;
    }
}

Utility::Exception::Exception(HRESULT hr)
{
    switch (hr)
    {
    case DXGI_ERROR_DEVICE_REMOVED:
        m_what = "DXGI_ERROR_DEVICE_REMOVED";
        break;

    case DXGI_ERROR_INVALID_CALL:
        m_what = "DXGI_ERROR_INVALID_CALL";
        break;

    case E_INVALIDARG:
        m_what = "E_INVALIDARG";
        break;

    default:
        char hrcode[32];
        sprintf_s(hrcode, "Failed HRESULT: 0x%08X", hr);
        m_what = hrcode;
        break;
    }
}

// A faster version of memcopy that uses SSE instructions.  TODO:  Write an ARM variant if necessary.
void Utility::SIMDMemCopy( void* __restrict _Dest, const void* __restrict _Source, size_t NumQuadwords )
{
    assert(IsAligned(_Dest, 16));
    assert(IsAligned(_Source, 16));

    __m128i* __restrict Dest = (__m128i* __restrict)_Dest;
    const __m128i* __restrict Source = (const __m128i* __restrict)_Source;

    // Discover how many quadwords precede a cache line boundary.  Copy them separately.
    size_t InitialQuadwordCount = (4 - ((size_t)Source >> 4) & 3) & 3;
    if (InitialQuadwordCount > NumQuadwords)
        InitialQuadwordCount = NumQuadwords;

    switch (InitialQuadwordCount)
    {
    case 3: _mm_stream_si128(Dest + 2, _mm_load_si128(Source + 2)); [[fallthrough]];
    case 2: _mm_stream_si128(Dest + 1, _mm_load_si128(Source + 1)); [[fallthrough]];
    case 1: _mm_stream_si128(Dest + 0, _mm_load_si128(Source + 0)); [[fallthrough]];
    default:
        break;
    }

    if (NumQuadwords == InitialQuadwordCount)
        return;

    Dest += InitialQuadwordCount;
    Source += InitialQuadwordCount;
    NumQuadwords -= InitialQuadwordCount;

    size_t CacheLines = NumQuadwords >> 2;

    switch (CacheLines)
    {
    default:
    case 10: _mm_prefetch((char*)(Source + 36), _MM_HINT_NTA); [[fallthrough]];
    case 9:  _mm_prefetch((char*)(Source + 32), _MM_HINT_NTA); [[fallthrough]];
    case 8:  _mm_prefetch((char*)(Source + 28), _MM_HINT_NTA); [[fallthrough]];
    case 7:  _mm_prefetch((char*)(Source + 24), _MM_HINT_NTA); [[fallthrough]];
    case 6:  _mm_prefetch((char*)(Source + 20), _MM_HINT_NTA); [[fallthrough]];
    case 5:  _mm_prefetch((char*)(Source + 16), _MM_HINT_NTA); [[fallthrough]];
    case 4:  _mm_prefetch((char*)(Source + 12), _MM_HINT_NTA); [[fallthrough]];
    case 3:  _mm_prefetch((char*)(Source + 8 ), _MM_HINT_NTA); [[fallthrough]];
    case 2:  _mm_prefetch((char*)(Source + 4 ), _MM_HINT_NTA); [[fallthrough]];
    case 1:  _mm_prefetch((char*)(Source + 0 ), _MM_HINT_NTA);

        // Do four quadwords per loop to minimize stalls.
        for (size_t i = CacheLines; i > 0; --i)
        {
            // If this is a large copy, start prefetching future cache lines.  This also prefetches the
            // trailing quadwords that are not part of a whole cache line.
            if (i >= 10)
                _mm_prefetch((char*)(Source + 40), _MM_HINT_NTA);

            _mm_stream_si128(Dest + 0, _mm_load_si128(Source + 0));
            _mm_stream_si128(Dest + 1, _mm_load_si128(Source + 1));
            _mm_stream_si128(Dest + 2, _mm_load_si128(Source + 2));
            _mm_stream_si128(Dest + 3, _mm_load_si128(Source + 3));

            Dest += 4;
            Source += 4;
        }
        [[fallthrough]];
    case 0:	// No whole cache lines to read
        break;
    }

    // Copy the remaining quadwords
    switch (NumQuadwords & 3)
    {
    case 3: _mm_stream_si128(Dest + 2, _mm_load_si128(Source + 2));	[[fallthrough]];
    case 2: _mm_stream_si128(Dest + 1, _mm_load_si128(Source + 1));	[[fallthrough]];
    case 1: _mm_stream_si128(Dest + 0, _mm_load_si128(Source + 0));	[[fallthrough]];
    default:
        break;
    }

    _mm_sfence();
}

void Utility::SIMDMemFill( void* __restrict _Dest, __m128 FillVector, size_t NumQuadwords )
{
    assert(IsAligned(_Dest, 16));

    const __m128i Source = _mm_castps_si128(FillVector);
    __m128i* __restrict Dest = (__m128i* __restrict)_Dest;

    switch (((size_t)Dest >> 4) & 3)
    {
    case 1: _mm_stream_si128(Dest++, Source); --NumQuadwords; [[fallthrough]];
    case 2: _mm_stream_si128(Dest++, Source); --NumQuadwords; [[fallthrough]];
    case 3: _mm_stream_si128(Dest++, Source); --NumQuadwords; [[fallthrough]];
    default:
        break;
    }

    size_t WholeCacheLines = NumQuadwords >> 2;

    // Do four quadwords per loop to minimize stalls.
    while (WholeCacheLines--)
    {
        _mm_stream_si128(Dest++, Source);
        _mm_stream_si128(Dest++, Source);
        _mm_stream_si128(Dest++, Source);
        _mm_stream_si128(Dest++, Source);
    }

    // Copy the remaining quadwords
    switch (NumQuadwords & 3)
    {
    case 3: _mm_stream_si128(Dest++, Source); [[fallthrough]];
    case 2: _mm_stream_si128(Dest++, Source); [[fallthrough]];
    case 1: _mm_stream_si128(Dest++, Source); [[fallthrough]];
    default:
        break;
    }

    _mm_sfence();
}

std::wstring Utility::UTF8ToWideString( const std::string& str )
{
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring wstr(size_t(size), 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size);
    return wstr;
}

std::string Utility::WideStringToUTF8( const std::wstring& wstr )
{
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string str(size_t(size), 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size, nullptr, nullptr);
    return str;
}

std::string Utility::ToLower(const std::string& str)
{
    std::string lower_case = str;
    std::locale loc;
    for (char& s : lower_case)
        s = std::tolower(s, loc);
    return lower_case;
}

std::wstring Utility::ToLower(const std::wstring& str)
{
    std::wstring lower_case = str;
    std::locale loc;
    for (wchar_t& s : lower_case)
        s = std::tolower(s, loc);
    return lower_case;
}

std::string Utility::ToUpper(const std::string& str)
{
    std::string upper_case = str;
    std::locale loc;
    for (char& s : upper_case)
        s = std::toupper(s, loc);
    return upper_case;
}

std::wstring Utility::ToUpper(const std::wstring& str)
{
    std::wstring upper_case = str;
    std::locale loc;
    for (wchar_t& s : upper_case)
        s = std::toupper(s, loc);
    return upper_case;
}

std::string Utility::GetBasePath(const std::string& filePath)
{
    size_t lastSlash;
    if ((lastSlash = filePath.rfind('/')) != std::string::npos)
        return filePath.substr(0, lastSlash + 1);
    else if ((lastSlash = filePath.rfind('\\')) != std::string::npos)
        return filePath.substr(0, lastSlash + 1);
    else
        return "./";
}

std::wstring Utility::GetBasePath(const std::wstring& filePath)
{
    size_t lastSlash;
    if ((lastSlash = filePath.rfind(L'/')) != std::wstring::npos)
        return filePath.substr(0, lastSlash + 1);
    else if ((lastSlash = filePath.rfind(L'\\')) != std::wstring::npos)
        return filePath.substr(0, lastSlash + 1);
    else
        return L"./";
}

std::string Utility::RemoveBasePath(const std::string& filePath)
{
    size_t lastSlash;
    if ((lastSlash = filePath.rfind('/')) != std::string::npos)
        return filePath.substr(lastSlash + 1, std::string::npos);
    else if ((lastSlash = filePath.rfind('\\')) != std::string::npos)
        return filePath.substr(lastSlash + 1, std::string::npos);
    else
        return filePath;
}

std::wstring Utility::RemoveBasePath(const std::wstring& filePath)
{
    size_t lastSlash;
    if ((lastSlash = filePath.rfind(L'/')) != std::string::npos)
        return filePath.substr(lastSlash + 1, std::string::npos);
    else if ((lastSlash = filePath.rfind(L'\\')) != std::string::npos)
        return filePath.substr(lastSlash + 1, std::string::npos);
    else
        return filePath;
}

std::string Utility::GetFileExtension(const std::string& filePath)
{
    size_t extOffset = filePath.rfind('.');
    if (extOffset == std::wstring::npos)
        return "";

    return filePath.substr(extOffset + 1);
}

std::wstring Utility::GetFileExtension(const std::wstring& filePath)
{
    size_t extOffset = filePath.rfind(L'.');
    if (extOffset == std::wstring::npos)
        return L"";

    return filePath.substr(extOffset + 1);
}

std::string Utility::RemoveExtension(const std::string& filePath)
{
    return filePath.substr(0, filePath.rfind("."));
}

std::wstring Utility::RemoveExtension(const std::wstring& filePath)
{
    return filePath.substr(0, filePath.rfind(L"."));
}
