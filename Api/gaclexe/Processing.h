//-------------------------------------------------------------------------------------
// Processing.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------
#pragma once

#include <gacl.h>

#include <string>

namespace gacl
{
    //-------------------------------------------------------------------------------------------------
    // Amount of output.

    enum class Verbosity
    {
        eSilent,        // No output except internal errors
        eDefault,       // Basic output including PSNR and compression ratios
        eVerbose,       // More detailed output
        eMaximum        // Full reporting and statistics. Useful for debugging or tuning.
    };

    DXGI_FORMAT IdentifyBCEncodeFormat(const std::string& ident);

    struct ShuffleTransformOptions
    {
        bool Enabled;
        GACL_SHUFFLE_TRANSFORM Transform;
    };

#if GACL_INCLUDE_CLER
    enum class LossMetrics
    {
        MSE,         // Mean Squared Error
        RMSE,        // Root Mean Squared Error
        VGG,         // Structural Similarity Index
        LPIPS,       // Learned Perceptual Image Patch Similarity
        SSIM         // VGG-based perceptual distance
    };

    struct ComponentLevelEntopyReductionOptions
    {
        bool Enabled;
        int MaxK;
        int MinK;
        int InitialIterations;
        LossMetrics lossMetric;
        float UpperLossBound;
        float LowerLossBound;
        bool PlusPlus;
        int Mips;
    };
#endif

    struct BlockLevelEntopyReductionOptions
    {
        bool Enabled;
        float TargetUniqueBlockReduction;
    };

    struct SpaceCurveOptions
    {
        bool ReverseSpaceCurve;
        bool ForwardSpaceCurve;
        bool DisableSpaceCurve;
    };

    struct ZstdCompressOptions
    {
        uint8_t Level;
        int TargetBlockSize;
    };

    struct ProcessingOptions
    {
        ShuffleTransformOptions ShuffleOptions;
        ZstdCompressOptions CompressOptions;
#if GACL_INCLUDE_CLER
        ComponentLevelEntopyReductionOptions ClerOptions;
#endif
        BlockLevelEntopyReductionOptions BlerOptions;
        SpaceCurveOptions CurveOptions;
    };

    bool ProcessTexture(
        const std::wstring& inputBlockCompressedFileName,
        const std::wstring& inputOriginalArtFileName,
        const std::wstring& outputFileName,
        const std::wstring& exportBaseName,
        DXGI_FORMAT format,
        size_t width,
        ProcessingOptions& options,
        Verbosity verbosity);
}