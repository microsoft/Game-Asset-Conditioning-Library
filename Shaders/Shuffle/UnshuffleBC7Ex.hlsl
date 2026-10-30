//--------------------------------------------------------------------------------------
// UnshuffleBC7Ex.hlsl
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------
// BC7 build of UnshuffleBCnEx.hlsl: BC7_EX (0x0F) and BC7 Join (0x06), 16-byte blocks. The host dispatches 1 block
// per thread in groups of 256 threads (curve-only BC7_EX streams decode 4 blocks per thread, their spare threads
// exiting).
//--------------------------------------------------------------------------------------
#define FIXED_TRANSFORM_ID 0x0Fu

#include "UnshuffleBCnEx.hlsl"
