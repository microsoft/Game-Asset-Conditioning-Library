//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
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

#include <wrl/client.h>
#include <wrl/event.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <regex>
#include <string>
#include <vector>
#include <DirectXTex.h>
#include <winrt/base.h>

#include "gacl.h"

namespace
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
} // namespace

// Enable off by default warnings to improve code conformance
#pragma warning(default : 4061 4062 4191 4263 4264 4265 4266 4289 4365 4746 4826 4841 4986 4987 5029 5038 5042)