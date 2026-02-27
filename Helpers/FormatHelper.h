//-------------------------------------------------------------------------------------
// FormatHelper.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#include <dxgiformat.h>

DXGI_FORMAT GetBaseFormat(DXGI_FORMAT Format);
DXGI_FORMAT GetUAVFormat(DXGI_FORMAT Format);
bool IsBlockCompressed(DXGI_FORMAT Format);
bool IsGammaFormat(DXGI_FORMAT Format);
bool HasGammaFormat(DXGI_FORMAT Format);
DXGI_FORMAT DisableSRGB(DXGI_FORMAT Format);
DXGI_FORMAT EnableSRGB(DXGI_FORMAT Format);
DXGI_FORMAT EnableInteger(DXGI_FORMAT Format);
DXGI_FORMAT DisableInteger(DXGI_FORMAT Format);
const char* ToString(DXGI_FORMAT Format);
const wchar_t* ToStringW(DXGI_FORMAT Format);
size_t GetElementSize(DXGI_FORMAT Format);
size_t GetChannelCount(DXGI_FORMAT Format);
bool IsFloatFormat(DXGI_FORMAT Format);
bool IsSnormFormat(DXGI_FORMAT Format);
bool IsIntegerFormat(DXGI_FORMAT Format);