//--------------------------------------------------------------------------------------
// UnshuffleBC1Headerless.hlsl
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------
// BC1 headerless build of UnshuffleBCnEx.hlsl: BC1_224 (0x01) and BC1_44 (0x08), 8-byte blocks (BC1_EX:
// UnshuffleBC1Ex.hlsl). The host dispatches 2 blocks per thread, plus one thread per straggler block, in groups of 128
// threads.
//--------------------------------------------------------------------------------------
#define FIXED_TRANSFORM_ID 0x0Bu
#define HEADERLESS_ONLY
#define THREAD_GROUP_SIZE 128

#include "UnshuffleBCnEx.hlsl"
