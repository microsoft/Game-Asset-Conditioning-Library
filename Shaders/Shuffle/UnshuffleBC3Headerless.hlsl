//--------------------------------------------------------------------------------------
// UnshuffleBC3Headerless.hlsl
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------
// BC3 headerless build of UnshuffleBCnEx.hlsl: BC3_116224 (0x02) and BC3_664 (0x09), 16-byte blocks (BC3_EX:
// UnshuffleBC3Ex.hlsl). The host dispatches 4 blocks per thread, plus one thread per straggler block, in groups of 256
// threads.
//--------------------------------------------------------------------------------------
#define FIXED_TRANSFORM_ID 0x0Cu
#define HEADERLESS_ONLY

#include "UnshuffleBCnEx.hlsl"
