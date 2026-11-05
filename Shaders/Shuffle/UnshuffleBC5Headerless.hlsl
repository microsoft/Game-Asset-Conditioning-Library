//--------------------------------------------------------------------------------------
// UnshuffleBC5Headerless.hlsl
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------
// BC5 headerless build of UnshuffleBCnEx.hlsl: BC5_116116 (0x04) and BC5_2266 (0x0A), 16-byte blocks (BC5_EX:
// UnshuffleBC5Ex.hlsl). The host dispatches 4 blocks per thread, plus one thread per straggler block, in groups of 128
// threads.
//--------------------------------------------------------------------------------------
#define FIXED_TRANSFORM_ID 0x0Eu
#define HEADERLESS_ONLY
#define THREAD_GROUP_SIZE 128

#include "UnshuffleBCnEx.hlsl"
