//-------------------------------------------------------------------------------------
// shuffle.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#include "dxgiformat.h"
#include "winerror.h"

#if defined (__cplusplus)
extern "C" {
#endif

enum GACL_SHUFFLE_TRANSFORM
{
    GACL_SHUFFLE_TRANSFORM_NONE = 0,
   
    GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224          = 1,     // 3 streams (1:1:2):         color_0, color_1, index bit stream   (DSTORAGE_GACL_SHUFFLE_TRANSFORM_BC1)
    GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224_SC       = 17,    // 3 streams (1:1:2):         color_0, color_1, index bit stream, space curve

    GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224       = 2,     // 6 streams (1:1:6:2:2:4)    alpha_0, alpha_1, Alpha index, color_0, color_1, color index stream (DSTORAGE_GACL_SHUFFLE_TRANSFORM_BC3)
    GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224_SC    = 18,    // 6 streams (1:1:6:2:2:4)    alpha_0, alpha_1, Alpha index, color_0, color_1, color index stream, space curve

    GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116          = 3,     // 3 streams (1:1:6)          red_0, red_1, red index stream (DSTORAGE_GACL_SHUFFLE_TRANSFORM_BC4)
    GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116_SC       = 19,    // 3 streams (1:1:6)          red_0, red_1, red index stream, space curve

    GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116       = 4,     // 6 streams (1:1:6:1:1:6)    red_0, red_1, red index stream, green_0, green_1, green index stream (DSTORAGE_GACL_SHUFFLE_TRANSFORM_BC5)
    GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116_SC    = 20,    // 6 streams (1:1:6:1:1:6)    red_0, red_1, red index stream, green_0, green_1, green index stream, space curve

    GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT        = 5,	 // mode-specific streams and transforms, control bytes within compressed stream	(better compression, slower reverse transform)
    GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT_SC     = 21,    // as above, but data is curved, can only be decompressed into a target with dimensionality that supports reverse mapping 	

    GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN         = 6,     // mode-specific transforms, control bytes outside of compressed stream (less compression, trivial+fast reverse transform)
    GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN_SC      = 22,    // as above, but data is curved, can only be decompressed into a target with dimensionality that supports reverse mapping 	

    GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY             = 7,
    GACL_SHUFFLE_TRANSFORM_ZSTD_SC               = 23,


    // v2 (experimental) shuffle patterns
    GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44           = 32,	  // 2 streams (1:1):            interleaved color_0 + color_1, index bit stream 
    GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44_SC,	              // 2 streams (1:1):            As above, with curve

    GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664,		          // 3 streams (3:3:2)           alpha_0 + alpha_1 + color_0 + color_1, Alpha index, color index stream
    GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664_SC,		          // 3 streams (3:3:2)           alpha_0 + alpha_1 + color_0 + color_1, Alpha index, color index stream


    // masks for API callers
    GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED    = 0x8004,    // This define will change value over time
    GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL = 0xff00,    // Allow experimental transforms that were not compiled out at build time
};

/// <summary>
/// This function re-orders texture memory to be screen-adjacent.
/// 
/// The space curve is defined as a 16KB region.  
/// For 16 byte elements, this means a 32x32 element square (128x128 pixels)
/// For 8 byte elements, this means a 64x32 element horizontal rectangle (256x128 pixels)
/// For other size elements, the texture is not eligible for curved transforms currently.
/// 
/// These 16KB micro-tiles are then z-ordered across the texture, which means it can only be applied to 
/// textures that are a power of 2 micro-tiles in each direction.  The below would the micro tile ordering
/// for a BC7 texture that was 1024x512 pixels:
/// 
///      0   1   4   5  16  17  20  21
///      
///      2   3   6   7  18  19  22  23
///  
///      8   9  12  13  24  25  28  29
/// 
///     10  11  14  15  26  27  30  31
/// 
/// The GACL internally uses this function when curved shuffle patterns are selected.  Distance between matches
/// is reduced, and this reduces the encoding cost of those matches for zstd, typically yielding 1-2% compression.
/// 
/// More importantly, when allowing curved transforms, then any RDO applied to the texture should be done after 
/// converting to space curve layout.  Block-Level Entropy Reduction at higher setting applied in this way typically
/// results in 10-20% additional savings due to screen adjacency yielding a higher number of similar (mergeable) 
/// block before hitting any specified error\deviation limits.
/// 
/// Note: Curved transforms are not elligable for all DirectStorage request destinations.  Only a destination 
/// type with known dimensionality that follows the same rules as above (power of 2 micro-tiles in each dimension)
/// are elligable.
/// 
/// DirectStorage read requests to read data that include a curved transform will fail for the following destination
/// types that have no inherent dimensionality: 
/// 
///   DSTORAGE_REQUEST_DESTINATION_MEMORY, DSTORAGE_REQUEST_DESTINATION_BUFFER
/// 
/// DirectStorage read requests to read data that include a curved transform work for the following destination types
/// if and only if the details of the request implies a single sub-resource is being loaded, and that subresource meets
/// the dimensionality restrictions:
/// 
///   DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES, DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES_RANGE
/// 
/// DirectStorage read requests to read data that include a curved transform work for the following destination types
/// if the updated region meets the dimensionality requirements, though this is generally true for most implementations
/// of partially resident textures:
///
///   DSTORAGE_REQUEST_DESTINATION_TEXTURE_REGION, DSTORAGE_REQUEST_DESTINATION_TILES
/// 
/// The function can be called without source and destination to test if the stream is elligable for space curve transforms.
/// 
/// </summary>
/// <param name="dest">Destination buffer, optional, filled with curved or uncurved data on exit</param>
/// <param name="src">Source buffer, optional</param>
/// <param name="size">Size of source and destination buffers</param>
/// <param name="elementSizeBytes">Block compressed element size</param>
/// <param name="widthInPixels">Texture width in pixels, used to determine eligibility</param>
/// <param name="forward">Specifies when to transform "forward" into curved memory layout, or "reverse" back to initial memory layout</param>
/// <returns>True if the texture meets space curve dimentionality requirements, else false</returns>

_Success_(dest!=nullptr && src != nullptr)
GACL_API bool GACL_Shuffle_ApplySpaceCurve(
    _Out_writes_bytes_opt_(size) uint8_t* dest,
    _In_reads_opt_(size) const uint8_t* src,
    size_t size, 
    size_t elementSizeBytes, 
    size_t widthInPixels, 
    bool forward
);


GACL_API const wchar_t* GACL_ShuffleCompress_GetFileExtensionForTransform(GACL_SHUFFLE_TRANSFORM transformId);

/// <summary>
///  This structure is passed into GACL_ShuffleCompress_BCn(), guiding the shuffle+compress operations.
/// </summary>

struct SHUFFLE_COMPRESS_PARAMETERS
{
    /*  Required parameters for any call, though technically TextureData can be null to disallow uncurved transforms */
    size_t SizeInBytes;                                                 // Size of input texture stream(s), as well as the destination buffer size
    DXGI_FORMAT Format;                                                 // BCn format of the input textures stream(s)
    const uint8_t* TextureData;                                         // Texture data in linear (row-major) memory layout

    /*  Optional parameters that guide compression */
    union COMPRESS_SETTINGS{
        struct COMPRESS_SETTINGS_DEFAULT                                // parameters for the default zstd compression handler
        {
            int ZstdCompressionLevel;                                   // requested compression level.  0 indicates gacl should use default settings to ensure btopt zstd strategy and 3 byte match
            int TargetBlockSize;                                        // zstd Target block size, use to ensure block-level parallelism
        } Default;
        void* Ptr;                                                      // Used for custom compression overrides, if a developer needs more than >64 bits of data
        uint64_t Inline;                                                // Used for custom compression overrides, if a developer needs more than <=64 bits of data
    } CompressSettings;

    /*  Optional parameters relating to curved transforms  */
    struct CURVED_TRANSFORM_INPUTS
    {
        size_t WidthInPixels;                                           // Texture width, required for correct applicaiton of 16KB micro-tile space curve
        const uint8_t* TextureData;                                     // Texture data that has RDO applied in curved space (Data may be curved or linear)
        bool DataIsCurved;                                              // specifies whether texture data is _already_ in 16KB micro-tile curved memory layout
    } CurvedTransforms;
};


typedef HRESULT(*PGACL_COMPRESSION_INITROUTINE) (_Out_ void** ccContext, _Out_ size_t* destBytesRequired, _In_ const SHUFFLE_COMPRESS_PARAMETERS* params);
typedef HRESULT(*PGACL_COMPRESSION_COMPRESSROUTINE) (_In_ void* context, _Out_writes_(*destBytes) void* dest, _Inout_ size_t* destBytes, _In_reads_(srcBytes) const void* src, size_t srcBytes);
typedef HRESULT(*PGACL_COMPRESSION_CLEANUPROUTINE) (_In_ void* pContext);

GACL_API extern PGACL_COMPRESSION_INITROUTINE GACL_Compression_InitRoutine;
GACL_API extern PGACL_COMPRESSION_COMPRESSROUTINE GACL_Compression_CompressRoutine;
GACL_API extern PGACL_COMPRESSION_CLEANUPROUTINE GACL_Compression_CleanupRoutine;


/// <summary>
/// Default compression init routine.
/// 
/// This function will initialize a zstd compression context that will enforce the following settings 
/// to maintain compression ratios while enabling future cpu offload implementations of decompression
/// to achieve higher throughput.
/// 256KB max window size(enforced via the zstd_p variant lib)
/// Strategy >= btopt(to allow for 3 byte matching)
/// Min match size = 3
/// Target block size(smaller blocks allow for more parallelism)
/// </summary>
/// <param name="ccContext">Returns a zstd compression context to the GACL</param>
/// <param name="destBytesRequired">Returns the required size for the compressed destination buffer for a given input size</param>
/// <param name="params">Shuffle+Compress parameters, passed in when calling GACL_ShuffleCompress_BCn()</param>
/// <returns>HRESULT indicating success or failure</returns>

GACL_API HRESULT GACL_Compression_DefaultInitRoutine(
    _Out_ void** ccContext,
    _Out_ size_t* destBytesRequired,
    _In_ const SHUFFLE_COMPRESS_PARAMETERS* params
);


/// <summary>
/// Default compress routine
/// 
/// Performs zstd compression on the input stream with the context provided
/// </summary>
/// <param name="context">Compression context previously returned by the Init routine</param>
/// <param name="dest">Destination buffer for compressed stream</param>
/// <param name="destBytes">Bytes written to destination</param>
/// <param name="src">Souce buffer of uncompressed data</param>
/// <param name="srcBytes">Byte count of source buffer</param>
/// <returns>HRESULT indicating success or failure</returns>

GACL_API HRESULT GACL_Compression_DefaultCompressRoutine(
    _In_ void* context,
    _Out_writes_(*destBytes) void* dest,
    _Inout_ size_t* destBytes,
    _In_reads_(srcBytes) const void* src,
    size_t srcBytes
);


/// <summary>
/// Default compression cleanup routine.
/// 
/// Frees the zstd compression context.
/// </summary>
/// <param name="pContext">Pointer to previously allocated compression context.</param>
/// <returns>HRESULT indicating success or failure</returns>

GACL_API HRESULT GACL_Compression_DefaultCleanupRoutine(
    _In_ void* pContext
);




/// <summary>
/// Transforms a given Block Compressed (BCn) input data stream to improve compressibility, followed by compression.
///
/// Each BCn mode may have multiple transforms that are tried, with the smallest compressed result returned to
/// the caller, along with the transform ID that is used to reverse the operation at load\runtime.
///
/// Transforms for which there is no current GPU-based reverse transform within DirectStorage are considered
/// experimental or proposed for future support.
/// 
/// Default compression is zstd, with some specific setting intended to balance compression ratio and
/// throughput on anticipated future cpu offload implementations of zstd decompression.
/// </summary>
/// <param name="dest">Destination buffer for compressed data. It's size must be equal to the input data stream.</param>
/// <param name="destTransformId">On entry, either a functional support level like GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED, or a specific transform ID.  On exit, indicated the transform selected.</param>
/// <param name="destBytesWritten">On exit, the number of bytes written to the destination.</param>
/// <param name="inParams">Input parameters for the shuffle and compression stages, see SHUFFLE_COMPRESS_PARAMETERS.</param>
/// <returns>HRESULT indicating success or failute.  S_FALSE indicates uncompressible content, with no compressed data copied to the destination.</returns>
GACL_API HRESULT GACL_ShuffleCompress_BCn(
    _Out_writes_(params.SizeInBytes) uint8_t* dest,
    _Inout_ GACL_SHUFFLE_TRANSFORM* destTransformId,
    _Out_ size_t* destBytesWritten,
    _In_ SHUFFLE_COMPRESS_PARAMETERS& params
);

#if defined (__cplusplus)
}
#endif

