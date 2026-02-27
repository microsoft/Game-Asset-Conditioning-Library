//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//

//
// pch.h
// Header for standard system include files.
//

#pragma once

#include <winsdkver.h>
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <sdkddkver.h>

// Use the C++ standard templated min/max
#define NOMINMAX

// DirectX apps don't need GDI
#define NODRAWTEXT
#define NOGDI
#define NOBITMAP

// Include <mcx.h> if you need this
#define NOMCX

// Include <winsvc.h> if you need this
#define NOSERVICE

// WinHelp is deprecated
#define NOHELP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <shellapi.h>

#include <wrl/client.h>
#include <wrl/event.h>

#ifdef USING_DIRECTX_HEADERS
#include <directx/dxgiformat.h>
#include <directx/d3d12.h>
#include <directx/d3dx12.h>
#include <dxguids/dxguids.h>
#else
#include <d3d12.h>

#include "d3dx12.h"
#endif

#include <dstorage.h>

#include <dxgi1_6.h>
#include <dxcapi.h>

#include <DirectXMath.h>
#include <DirectXColors.h>
#include <DirectXTex.h>

#include "GraphicsMemory.h"
#include "DescriptorHeap.h"
#include "BufferHelpers.h"
#include "ResourceUploadBatch.h"
#include "SimpleMath.h"
#include "SpriteFont.h"
#include "SpriteBatch.h"
#include "DirectXHelpers.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <exception>
#include <iterator>
#include <iostream>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <tuple>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

namespace DX
{
    template<size_t bufferSize = 2048>
    void debugPrint(std::wstring_view format, ...)
    {
        assert(
            format.size() < bufferSize &&
            "format string is too large, split up the string or increase the buffer size");

        wchar_t buffer[bufferSize] = L"";

        va_list args;
        va_start(args, format);
        vswprintf_s(buffer, format.data(), args);
        va_end(args);

        OutputDebugStringW(buffer);

        std::wcout << buffer;
    }

    // Helper class for COM exceptions
    class com_exception : public std::exception
    {
    public:
        com_exception(HRESULT hr) noexcept : result(hr) {}

        const char* what() const noexcept override
        {
            static char s_str[64] = {};
            sprintf_s(s_str, "Failure with HRESULT of %08X", static_cast<unsigned int>(result));
            return s_str;
        }

    private:
        HRESULT result;
    };

    // Helper utility converts D3D API failures into exceptions.
    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            debugPrint(L"Fatal error %08X", hr);
            // Set a breakpoint on this line to catch DirectX API errors
            throw com_exception(hr);
        }
    }

    inline void ThrowIfFalse(bool condition)
    {
        if (!condition)
        {
            debugPrint(L"Fatal error: condition is false");
            throw std::exception();
        }
    }
}
