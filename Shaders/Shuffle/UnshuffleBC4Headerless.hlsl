//--------------------------------------------------------------------------------------
// UnshuffleBC4Headerless.hlsl
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------
// BC4 headerless build of UnshuffleBCnEx.hlsl: BC4_116 (0x03), 8-byte blocks (BC4_EX: UnshuffleBC4Ex.hlsl). The host
// dispatches 4 blocks per thread, plus one thread per straggler block, in groups of 256 threads.
//--------------------------------------------------------------------------------------
#define FIXED_TRANSFORM_ID 0x0Du
#define HEADERLESS_ONLY

#include "UnshuffleBCnEx.hlsl"
