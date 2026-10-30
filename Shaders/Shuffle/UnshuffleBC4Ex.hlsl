//--------------------------------------------------------------------------------------
// UnshuffleBC4Ex.hlsl
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//--------------------------------------------------------------------------------------
// BC4 EX build of UnshuffleBCnEx.hlsl: BC4_EX (0x0D), 8-byte blocks (BC4_116: UnshuffleBC4Headerless.hlsl). The host
// dispatches 2 blocks per thread, plus one thread per straggler block, in groups of 256 threads. BC4_EX streams take
// the general header path, and those of 65536 blocks (512KB) or more whose header FastParseHeader doesn't take decode
// 4 blocks per thread instead (the dispatch's spare groups exit), so that a group's work covers the parse.
//--------------------------------------------------------------------------------------
#define FIXED_TRANSFORM_ID 0x0Du
#define EX_ONLY
#define EX_BLOCKS_PER_THREAD 2u
#define EX_GENERAL_DECODE
// Lib\shuffle\streamheader.cpp DecoderGroup mirrors it
#define EX_WIDE_STREAM_BLOCKS 65536u

#include "UnshuffleBCnEx.hlsl"
