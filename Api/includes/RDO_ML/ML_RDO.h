//-------------------------------------------------------------------------------------
// ML_RDO.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#if GACL_INCLUDE_CLER

#if defined (__cplusplus)
extern "C" {
#endif

enum class RDOLossMetric { MSE, RMSE, VGG, LPIPS };

enum class RDO_ErrorCode : int
{
    // Success codes
    OK = 0,
    OK_NoAdvancedRDO = 1,

    NullEncodedData = 10,
    UnsupportedEncodedFormat = 11,
    InvalidImageDimensions = 12,
    InvalidBCElementSize = 13,
    UnsupportedBaseFormat = 14,
    InvalidClusterRange = 20,
    InvalidLossRange = 21,
    ClusteredSizeMismatch = 30,
    ModesSizeMismatch = 31,
    UnsupportedFormatNotImplemented = 32,
    InternalException = 40,
    UnknownError = 50
};

struct RDOOptions
{
    int maxClusters = -1;
    int minClusters = 1;
    int iterations = 7;
    RDOLossMetric metric = RDOLossMetric::LPIPS;
    float lossMin = 0.05f;
    float lossMax = 0.1f;
    int numThreads = 0;
    bool usePlusPlus = true;
    bool useClusterRDO = true;
    bool isGammaFormat = false;

    void* onnxModelPtr = nullptr;
};

bool initORT();

// EncodedData and ReferenceR8G8B8A8 buffer expectations
//
// encodedData:
//  - Modifiable buffer containing raw BC texture data only representing 
//    contigious, row-order BC blocks as they are stored in a dds file
//  - The expected size of encodedData in bytes is:
//      encodedDataSizeBytes = ((imageWidth  + 3) / 4) * ((imageHeight + 3) / 4) * bcElementSizeBytes
//  - The BC format requires full 4x4 blocks, even if image height or 
//     width is not a multiple of 4, and the encodedData buffer also 
//     must include full blocks 
//
// referenceR8G8B8A8:
//   - Uncompressed (or decompressed BC) reference image corresponding 
//     to the same image represented by encodedData
//   - Pixel format is R8G8B8A8_UNORM
//   - Pixels are tightly packed in row-major order, 1 byte per channel
//   - No per-row padding or stride is supported
//   - The expected size of referenceR8G8B8A8 in bytes is:
//      referenceSizeBytes = imageWidth * imageHeight * 4
//   - If referenceR8G8B8A8 is nullptr, only basic k-means will be applied


/// <summary>
/// Reduces the number of unique endpoints in a block compressed texture by clustering color endpoints together
/// using k-means clustering guided by loss metric evaluation
/// 
/// 
/// </summary>
/// <param name="bcElementSizeBytes">Size of one block compressed element in bytes (e.g. 8 for BC1, 16 for BC3)</param>
/// <param name="encodedData">Encoded texture data</param>
/// <param name="referenceR8G8B8A8">Reference image in R8G8B8A8 format</param>
/// <param name="imageWidth">Width of image in pixels</param>
/// <param name="imageHeight">Height of image in pixels</param>
/// <param name="format">DXGI format of the texture</param>
/// <param name="options">RDO options, refer to struct definition for more</param>
/// <returns>Returns an RDO_ErrorCode value, refer to the RDO_ErrorCode enum definition and README for specific error codes.</returns>

GACL_API RDO_ErrorCode GACL_RDO_ComponentLevelEntropyReduce(
    uint32_t bcElementSizeBytes,
    _Inout_updates_bytes_(bcElementSizeBytes * (((imageWidth + 3) >> 2)* ((imageHeight + 3) >> 2))) void* encodedData,
    const _In_reads_bytes_opt_(4 * imageWidth * imageHeight) void* referenceR8G8B8A8,
    const uint32_t imageWidth,
    const uint32_t imageHeight,
    const DXGI_FORMAT format,
    RDOOptions& options);

#if defined (__cplusplus)
}
#endif

#endif