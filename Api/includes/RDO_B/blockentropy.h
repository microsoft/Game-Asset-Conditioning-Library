//-------------------------------------------------------------------------------------
// blockentropy.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#if defined (__cplusplus)
extern "C" {
#endif

struct Vec64u8;

/// <summary>
/// Reduces the number of unique block compressed elements within a texture by finding 4x4 groups of pixels that are
/// nearly identical, and replacing the BC-encoded and RGB[A] data in one of the two regions so that the two elements
/// have the same encoding.  Near-matches are only unified within a sliding window consistent with the zstd compression
/// window size used for CPU offload compatibility.
/// 
/// Performance:  3-5 second per 4K*4K texture, measured on a Threadripper pro 3975WX 
/// 
/// </summary>
/// <param name="numBlocks">number of 4x4 BC elements in the texture</param>
/// <param name="encodedData">BC1\2\3\4\5\7 encoded texture data</param>
/// <param name="bcElementSizeBytes">BC element size (8\16)</param>
/// <param name="decodedR8G8B8A8">Decoded RGBA pixel data, in identical memory layout to BC data, in that 4x4 pixel groups are packed together</param>
/// <param name="uniqueBlockReduce">Percentage of unique block elements to target for removal, unifying them with similar blocks</param>
/// <param name="maxDistSq">Maximum\limit total distance across the 64 color channel vectors comparing two 16-pixel groups</param>
/// <param name="avgDistSq">Limit of 64 color channel distance averaged across all 16 pixel blocks</param>
/// <returns></returns>

GACL_API void GACL_RDO_BlockLevelEntropyReduce(
	uint32_t numBlocks,
	_Inout_updates_all_(bcElementSizeBytes * numBlocks)  void* encodedData,
	uint32_t bcElementSizeBytes,
	_Inout_updates_all_(64 * numBlocks) void* decodedR8G8B8A8,
	float uniqueBlockReduce,
	float maxDistSq = 64.0f * 4.0f,
	float avgDistSq = 64.0f * 0.5f
);

/// <summary>
/// Takes linear texture data, and groups it into 4x4 screen adjacent blocks.  In this way, the decoded memory for each BC element within
/// an encoded texture is memory adjacent for quick comparison and updating by the BLER algorithm.  This helper is left separate in case
/// there are scenarios where the Linear->BlockGrouped conversion will happen once for multiple calls to BLER with different parameters.
/// </summary>
/// <param name="blockGroupedData">Output buffer, with each 4x4 pixel group's 64B of memory colocated</param>
/// <param name="linearR8G8B8A8Data">Input linear (row major) texture data</param>
/// <param name="bcRowPitch">Texture row pitch</param>
/// <param name="width">Texture width</param>
/// <param name="height">Texture height</param>
/// <returns></returns>

GACL_API void GACL_RDO_R8G8B8A8LinearToBlockGrouped(
	_Out_writes_all_(64 * ((width + 3) >> 2) * ((height + 3) >> 2)) uint8_t* blockGroupedData,
	_In_reads_(rowPitch* ((height + 3) >> 2)) const uint8_t* linearR8G8B8A8Data,
	size_t rowPitch,
	size_t width, 
	size_t height
);

#if defined (__cplusplus)
}
#endif