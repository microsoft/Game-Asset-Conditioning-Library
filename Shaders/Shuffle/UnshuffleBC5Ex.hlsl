//--------------------------------------------------------------------------------------
// UnshuffleBC5Ex.hlsl
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------
// BC5 EX build of UnshuffleBCnEx.hlsl: BC5_EX (0x0E), 16-byte blocks (BC5_116116 and BC5_2266:
// UnshuffleBC5Headerless.hlsl). The host dispatches 4 blocks per thread, plus one thread per straggler block, in groups
// of 128 threads.
//--------------------------------------------------------------------------------------
#define FIXED_TRANSFORM_ID 0x0Eu
#define EX_ONLY
#define THREAD_GROUP_SIZE 128

#include "UnshuffleBCnEx.hlsl"
