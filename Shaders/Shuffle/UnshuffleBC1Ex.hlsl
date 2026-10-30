//--------------------------------------------------------------------------------------
// UnshuffleBC1Ex.hlsl
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------
// BC1 EX build of UnshuffleBCnEx.hlsl: BC1_EX (0x0B), 8-byte blocks (BC1_224 and BC1_44: UnshuffleBC1Headerless.hlsl).
// The host dispatches 2 blocks per thread, plus one thread per straggler block, in groups of 128 threads.
//--------------------------------------------------------------------------------------
#define FIXED_TRANSFORM_ID 0x0Bu
#define EX_ONLY
#define THREAD_GROUP_SIZE 128

#include "UnshuffleBCnEx.hlsl"
