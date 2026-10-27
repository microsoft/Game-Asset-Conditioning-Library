//--------------------------------------------------------------------------------------
// UnshuffleBC3Ex.hlsl
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------
// BC3 EX build of UnshuffleBCnEx.hlsl: BC3_EX (0x0C), 16-byte blocks (BC3_116224 and BC3_664:
// UnshuffleBC3Headerless.hlsl). The host dispatches 4 blocks per thread, plus one thread per straggler block, in groups
// of 256 threads.
//--------------------------------------------------------------------------------------
#define FIXED_TRANSFORM_ID 0x0Cu
#define EX_ONLY

#include "UnshuffleBCnEx.hlsl"
