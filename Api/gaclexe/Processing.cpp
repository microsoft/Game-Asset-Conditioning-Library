//-------------------------------------------------------------------------------------
// Processing.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#define NOMINMAX

#include "gacl.h"
#include "DirectXTex.h"
#include "Processing.h"
#include "../helpers/Utility.h"
#include "../helpers/FileUtility.h"
#include "../helpers/FormatHelper.h"
#include "../ThirdParty/zstd/lib/zstd.h"

#include <chrono>
#include <thread>
#include <fstream>

using namespace DirectX;
using namespace std;

static void CountBC7Modes(const DirectX::ScratchImage& si, bool debugOutput, size_t* countsOut)
{
    const uint32_t* data = (const uint32_t*)si.GetPixels();
    const size_t block_count = si.GetPixelsSize() / 16;

    size_t counts[9] = {};

    for (size_t i = 0; i < block_count; ++i, data += 4)
    {
        // Look for first set bit in [7:0].  If none set, return 8 (invalid).
        unsigned long mode;
        _BitScanForward(&mode, *data | 0x100);
        counts[mode]++;
    }
    
    if (debugOutput)
    {
        static const wchar_t* descriptors[9] =
        {
            L"6x 444   idx3   6p ",
            L"4x 666   idx3   2p ",
            L"6x 555   idx2      ",
            L"4x 777   idx2   4p ",
            L"2x 5556  idx2+3 rot",
            L"2x 7778  idx2+2 rot",
            L"2x 7777  idx4   2p ",
            L"4x 5555  idx2   4p ",
            L"INVALID BC7 BLOCKS "
        };

        const int numDescriptors = counts[8] > 0 ? 9 : 8;

        Utility::Printf(GACL_Logging_Priority_Medium, L"BC7 mode counts:\n");
        for (int i = 0; i < numDescriptors; ++i)
        {
            Utility::Printf(GACL_Logging_Priority_Medium, L" Mode %i (%s): %5.2f%%  %zd\n", i, descriptors[i], counts[i] * 100.0f / block_count, counts[i]);
        }
        Utility::Printf(GACL_Logging_Priority_Medium, L"\n");
    }

    if (countsOut)
    {
        for (int i = 0; i < 9; ++i)
        {
            countsOut[i] = counts[i];
        }
    }
}

DXGI_FORMAT gacl::IdentifyBCEncodeFormat(const std::string& ident)
{
    const std::string format = Utility::ToUpper(ident);

    DXGI_FORMAT ret = DXGI_FORMAT_UNKNOWN;

    if (format.find("BC1") != std::string::npos)
        ret = DXGI_FORMAT_BC1_UNORM;
    else if (format.find("BC2") != std::string::npos)
        ret = DXGI_FORMAT_BC2_UNORM;
    else if (format.find("BC3") != std::string::npos)
        ret = DXGI_FORMAT_BC3_UNORM;
    else if (format.find("BC4") != std::string::npos)
        ret = DXGI_FORMAT_BC4_UNORM;
    else if (format.find("BC5") != std::string::npos)
        ret = DXGI_FORMAT_BC5_UNORM;
    else if (format.find("BC6") != std::string::npos)
        ret = DXGI_FORMAT_BC6H_UF16;
    else if (format.find("BC7") != std::string::npos)
        ret = DXGI_FORMAT_BC7_UNORM;
    else
        return ret;

    // Check for SRGB, SNORM, and SF16 variants
    if (format.find("_S") != std::string::npos)
        ret = (DXGI_FORMAT)(ret + 1);

    return ret;
}

bool gacl::ProcessTexture(
    const std::wstring& inputBlockCompressedFileName,
    const std::wstring& inputOriginalArtFileName,
    const std::wstring& outputFileName,
    const std::wstring& exportBaseName,
    DXGI_FORMAT format,
    size_t width,
    ProcessingOptions& options,
    Verbosity verbosity)
{
    ScratchImage si;
    TexMetadata siMeta;

    HRESULT hr = S_OK;

    if (Utility::ToLower(Utility::GetFileExtension(inputBlockCompressedFileName)) == L"dds")
    {
        hr = DirectX::LoadFromDDSFile(inputBlockCompressedFileName.c_str(), DDS_FLAGS::DDS_FLAGS_NONE, &siMeta, si);
        
        if (!FAILED(hr))
        {
            if (!IsBlockCompressed(siMeta.format))
            {
                Utility::Printf(GACL_Logging_Priority_High, L"ERROR: File \"%ws\" is not in a block compressed texture.\n", inputBlockCompressedFileName.c_str());
                return false;
            }
        }
    }
    else if (format != DXGI_FORMAT_UNKNOWN && width != 0)
    {
        Utility::ByteArray ba = Utility::ReadFileSync(inputBlockCompressedFileName.c_str());
        
        const size_t widthInElements = width / 4;
        const size_t elementSize = GetElementSize(format);
        const size_t rowPitchInBytes = widthInElements * elementSize;
        const size_t heightInElements = ba->size() / rowPitchInBytes;
        const size_t height = heightInElements * 4;
        
        si.Initialize2D(format, width, height, 1, 1);
        assert(ba->size() == si.GetPixelsSize());
        memcpy(si.GetPixels(), ba->data(), ba->size());
        siMeta = si.GetMetadata();
    }
    else
    {
        Utility::Printf(GACL_Logging_Priority_High, L"ERROR: Failed to load \"%ws\", it is not a dds, nor was a raw DXGI and pitch width specified\n", inputBlockCompressedFileName.c_str());
        return false;
    }

    if (FAILED(hr))
    {
        Utility::Printf(GACL_Logging_Priority_High, L"ERROR: Failed to load \"%ws\" with HRESULT(0x%08X)\n", inputBlockCompressedFileName.c_str(), hr);
        return false;
    }

    const DXGI_FORMAT baseFormat = GetBaseFormat(siMeta.format);
    const uint32_t elementSize = uint32_t(GetElementSize(siMeta.format));

    if (verbosity >= Verbosity::eVerbose)
    {
        CountBC7Modes(si, true, nullptr);
    }

    if (options.CurveOptions.ReverseSpaceCurve || options.CurveOptions.ForwardSpaceCurve)
    {
        DirectX::ScratchImage outImage;
        hr = outImage.Initialize2D(siMeta.format, siMeta.width, siMeta.height, siMeta.arraySize, siMeta.mipLevels);
        if (FAILED(hr)) {
            Utility::Printf(GACL_Logging_Priority_High, L"Failed to initialize output image.\n");
            return false;
        }

        for (size_t item = 0; item < siMeta.arraySize; ++item)
        {
            for (size_t mip = 0; mip < siMeta.mipLevels; ++mip)
            {
                const DirectX::Image* dstConst = outImage.GetImage(mip, item, 0);
                DirectX::Image* dst = const_cast<DirectX::Image*>(dstConst);

                const DirectX::Image* src = si.GetImage(mip, item, 0);

                size_t mipWidth = std::max<size_t>(1, siMeta.width >> mip);
                size_t mipHeight = std::max<size_t>(1, siMeta.height >> mip);
                size_t blocksX = (mipWidth + 3) / 4;
                size_t blocksY = (mipHeight + 3) / 4;
                size_t numBlocks = blocksX * blocksY;

                std::vector<uint8_t> curved(numBlocks * elementSize);
                if (options.CurveOptions.ForwardSpaceCurve)
                {
                    GACL_Shuffle_ApplySpaceCurve(curved.data(), src->pixels, numBlocks * elementSize, elementSize, blocksX*4, true);
#if _DEBUG
                    std::vector<uint8_t> decurved(numBlocks * elementSize);
                    GACL_Shuffle_ApplySpaceCurve(decurved.data(), curved.data(), numBlocks * elementSize, elementSize, blocksX * 4, false);
                    if (memcmp(src->pixels, decurved.data(), numBlocks * elementSize)) __debugbreak();
#endif
                }
                else
                {
                    GACL_Shuffle_ApplySpaceCurve(curved.data(), src->pixels, numBlocks * elementSize, 16, blocksX*4, false);
                }
                memcpy(dst->pixels, curved.data(), numBlocks * elementSize);
            }
        }


        if (outputFileName.empty())
        {
            Utility::Printf(GACL_Logging_Priority_Medium, L"Output file not specified, modified file discarded\n");
        }
        else
        {
            hr = DirectX::SaveToDDSFile(outImage.GetImages(), outImage.GetImageCount(),
                outImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, outputFileName.c_str());
            if (FAILED(hr))
            {
                Utility::Printf(GACL_Logging_Priority_High, L"Failed to save DDS file.\n");
                return false;
            }
            Utility::Printf(GACL_Logging_Priority_Medium, L"Modified file written to \"%ws\"\n", outputFileName.c_str());
        }
        return true;
    }
    
#if GACL_INCLUDE_CLER || GACL_INCLUDE_BLER
    const bool isGammaFormat = IsGammaFormat(siMeta.format);
#endif


#if GACL_INCLUDE_CLER
    if (options.ClerOptions.Enabled)
    {
        Utility::Printf(GACL_Logging_Priority_Medium, L"\n=== Component-Level Entropy Reduction (ML RDO) ===\n");
        Utility::Printf(GACL_Logging_Priority_Medium, L"Format: %ws\n", (baseFormat == DXGI_FORMAT_BC1_TYPELESS) ? L"BC1" : (baseFormat == DXGI_FORMAT_BC7_TYPELESS) ? L"BC7" : L"Unknown");
        Utility::Printf(GACL_Logging_Priority_Medium, L"Image size: %zux%zu\n", siMeta.width, siMeta.height);
        Utility::Printf(GACL_Logging_Priority_Medium, L"Mip levels: %zu\n", siMeta.mipLevels);
        if (baseFormat != DXGI_FORMAT_BC1_TYPELESS)
        {
            Utility::Printf(GACL_Logging_Priority_High, L"ERROR: Component-Level Entropy Reduction only supports BC1 format.\n");
            return false;
        }

        TexMetadata refInfo{};
        std::unique_ptr<ScratchImage> referenceImage;
        bool comInitialized = false;
        if (!inputOriginalArtFileName.empty())
        {
            HRESULT coInitHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            comInitialized = SUCCEEDED(coInitHr) || coInitHr == RPC_E_CHANGED_MODE;
            if (!comInitialized)
            {
                Utility::Printf(GACL_Logging_Priority_High, L"WARNING: CoInitializeEx failed (HRESULT=0x%08X)\n", coInitHr);
            }
            else
            {
                referenceImage = std::make_unique<ScratchImage>();
                coInitHr = LoadFromWICFile(inputOriginalArtFileName.c_str(), WIC_FLAGS_NONE, &refInfo, *referenceImage);
                if (FAILED(coInitHr))
                {
                    if (coInitHr == static_cast<HRESULT>(0xc00d5212) /* MF_E_TOPO_CODEC_NOT_FOUND */)
                    {
                        wstring ext = Utility::GetFileExtension(inputOriginalArtFileName);
                        if (_wcsicmp(ext.c_str(), L".heic") == 0 || _wcsicmp(ext.c_str(), L".heif") == 0)
                        {
                            Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: This format requires installing the HEIF Image Extensions - https://aka.ms/heif\n");
                        }
                        else if (_wcsicmp(ext.c_str(), L".webp") == 0)
                        {
                            Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: This format requires installing the WEBP Image Extensions - https://apps.microsoft.com/detail/9PG2DK419DRG\n");
                        }
                    }
                    Utility::Printf(GACL_Logging_Priority_High, L"\nWarning: Failed to load reference image \"%ws\" HRESULT(0x%08X), using decoded BC image instead\n", inputOriginalArtFileName.c_str(), static_cast<unsigned int>(coInitHr));
                    referenceImage.reset();
                    refInfo = TexMetadata{};
                }
                else
                {
                    DXGI_FORMAT originalRefFormat = refInfo.format;

                    // Check channel count - filter out channel counts not equal to 3 or 4
                    size_t bitsPerPixel = DirectX::BitsPerPixel(originalRefFormat);
                    size_t bitsPerColor = DirectX::BitsPerColor(originalRefFormat);
                    size_t channelCount = (bitsPerColor > 0) ? (bitsPerPixel / bitsPerColor) : 0;

                    if (channelCount > 0 && (channelCount < 3 || channelCount > 4))
                    {
                        Utility::Printf(GACL_Logging_Priority_Medium,
                            L"Warning: Reference image has %zu channel(s). Using decoded BC image as reference instead.\n",
                            channelCount);
                        if (bitsPerColor > 6) {
                            Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: Use of decoded BC will result in loss of precision from %zu-bit bit per channel to R5G6B5.\n", bitsPerColor);
                        }
                        referenceImage.reset();
                        refInfo = TexMetadata{};
                    }
                    else if (bitsPerColor > 8)
                    {
                        Utility::Printf(GACL_Logging_Priority_Medium,
                            L"Warning: Reference image precision reduced from %zu-bit to 8-bit per channel.\n",
                            bitsPerColor);
                    }
                    else 
                    {
                        // Convert reference image to R8G8B8A8_UNORM to match the expected format
                        // WIC can load images in various formats (BGRA, RGB24, etc.) depending on source
                        DXGI_FORMAT targetRefFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
                        if (refInfo.format != targetRefFormat)
                        {
                            DirectX::ScratchImage converted;
                            HRESULT convertHr = DirectX::Convert(
                                referenceImage->GetImages(),
                                referenceImage->GetImageCount(),
                                referenceImage->GetMetadata(),
                                targetRefFormat,
                                DirectX::TEX_FILTER_DEFAULT,
                                DirectX::TEX_THRESHOLD_DEFAULT,
                                converted);

                            if (SUCCEEDED(convertHr))
                            {
                                *referenceImage = std::move(converted);
                                refInfo = referenceImage->GetMetadata();
                                Utility::Printf(GACL_Logging_Priority_Medium, L"Converted reference image to R8G8B8A8_UNORM\n");
                            }
                            else
                            {
                                Utility::Printf(GACL_Logging_Priority_High, L"Warning: Failed to convert reference image format (HRESULT=0x%08X), using decoded BC as reference instead.\n", convertHr);
                                referenceImage.reset();
                                refInfo = TexMetadata{};
                            }
                        }
                    }
                }
            }
        }

        RDOOptions rdoOptions;
        rdoOptions.maxClusters = options.ClerOptions.MaxK;
        rdoOptions.minClusters = options.ClerOptions.MinK;

        rdoOptions.iterations = options.ClerOptions.InitialIterations;
        if (rdoOptions.iterations <= 0) 
        {
            rdoOptions.iterations = 10;
        }

        switch (options.ClerOptions.lossMetric)
        {
            case LossMetrics::MSE:
                rdoOptions.metric = RDOLossMetric::MSE;
                break;

            case LossMetrics::RMSE:
                rdoOptions.metric = RDOLossMetric::RMSE;
                break;

            case LossMetrics::LPIPS:
                rdoOptions.metric = RDOLossMetric::LPIPS;
                break;

            case LossMetrics::VGG:
                rdoOptions.metric = RDOLossMetric::VGG;
                break;

            default:
                rdoOptions.metric = RDOLossMetric::LPIPS;
                break;
        }

        rdoOptions.lossMin = options.ClerOptions.LowerLossBound;
        rdoOptions.lossMax = options.ClerOptions.UpperLossBound;
        rdoOptions.usePlusPlus = options.ClerOptions.PlusPlus;

        using namespace std::chrono;
        auto start_time = high_resolution_clock::now();

        for (size_t item = 0; item < siMeta.arraySize; ++item)
        {
            if (siMeta.arraySize > 1)
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"=== Processing array item %zu/%zu ===\n", item + 1, siMeta.arraySize);
            }

            for (size_t mip = 0; mip < siMeta.mipLevels; ++mip)
            {
                const DirectX::Image* src = si.GetImage(mip, item, 0);

                size_t c_width = std::max<size_t>(1, siMeta.width >> mip);
                size_t c_height = std::max<size_t>(1, siMeta.height >> mip);

                if (siMeta.mipLevels > 1)
                {
                    Utility::Printf(GACL_Logging_Priority_Medium, L"\nMip level %zu/%zu (%zux%zu)\n", mip, siMeta.mipLevels - 1, c_width, c_height);
                }

                if (mip >= options.ClerOptions.Mips) {
                    Utility::Printf(GACL_Logging_Priority_Medium, L"Skipping - mip level %zu is beyond the last mip level to process %zu. To change this, use '-cmips' argument.\n", mip, options.ClerOptions.Mips - 1);
					continue;
                }

                if (c_width < 24 || c_height < 24)
                {
                    Utility::Printf(GACL_Logging_Priority_Medium, L"Skipping - too small for RDO (< 24x24)\n");
                    continue;
                }

                // Decode BC data to RGBA for reference
                DirectX::TexMetadata mipMetadata = siMeta;
                mipMetadata.width = c_width;
                mipMetadata.height = c_height;
                mipMetadata.mipLevels = 1;
                mipMetadata.arraySize = 1;
                mipMetadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

                DirectX::ScratchImage decoded;
                hr = DirectX::Decompress(src, 1, mipMetadata, isGammaFormat ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, decoded);
                if (FAILED(hr)) 
                {
                    Utility::Printf(GACL_Logging_Priority_High, L"ERROR: Failed to decode mip %zu array item %zu\n", mip, item);
                    return false;
                }

                const DirectX::Image* rgbaImg = decoded.GetImage(0, 0, 0);

                if (options.ClerOptions.MaxK <= 0)
                {
                    rdoOptions.maxClusters = static_cast<int>(((c_width > c_height) ? 0.125 * c_width : 0.125 * c_height));
                    if (rdoOptions.maxClusters < rdoOptions.minClusters) 
                    {
                        rdoOptions.maxClusters = rdoOptions.minClusters;
                    }
                }

                const void* referenceRGBA = nullptr;
                DirectX::ScratchImage resizedRef;

                if (referenceImage && refInfo.width > 0 && refInfo.height > 0)
                {
                    const DirectX::Image* refMip = nullptr;
                    if (mip < refInfo.mipLevels)
                    {
                        refMip = referenceImage->GetImage(mip, 0, 0);
                    }
                    else
                    {
                        refMip = referenceImage->GetImage(0, 0, 0);
                    }

                    const DirectX::Image* refToUse = refMip;

                    bool refMatchesMipDims = refToUse && refToUse->width == c_width && refToUse->height == c_height;

                    if (!refMatchesMipDims)
                    {
                        if (comInitialized && refToUse)
                        {
                            HRESULT rhr = DirectX::Resize(
                                referenceImage->GetImages(),
                                referenceImage->GetImageCount(),
                                referenceImage->GetMetadata(),
                                (size_t)c_width,
                                (size_t)c_height,
                                DirectX::TEX_FILTER_FANT,
                                resizedRef);

                            if (SUCCEEDED(rhr))
                            {
                                refToUse = resizedRef.GetImage(0, 0, 0);
                                refMatchesMipDims = refToUse && refToUse->width == c_width && refToUse->height == c_height;
                                Utility::Printf(GACL_Logging_Priority_Medium, L"Downscaled reference to %zux%zu for mip %zu\n", c_width, c_height, mip);
                            }
                            else
                            {
                                Utility::Printf(GACL_Logging_Priority_Medium, L"WARNING: Failed to downscale reference, using decoded image as fallback\n");
                                refToUse = nullptr;
                            }
                        }
                        else
                        {
                            refToUse = nullptr;
                        }
                    }

                    if (refToUse && refMatchesMipDims) {
                        referenceRGBA = refToUse->pixels;
                        Utility::Printf(GACL_Logging_Priority_Medium, L"Using provided reference image\n");
                    }
                }

                if (!referenceRGBA)
                {
                    referenceRGBA = rgbaImg->pixels;
                    Utility::Printf(GACL_Logging_Priority_Medium, L"No external reference; using decoded image as reference\n");
                }              

                RDO_ErrorCode rdoResult = GACL_RDO_ComponentLevelEntropyReduce(
                    (uint32_t)GetElementSize(siMeta.format),
                    const_cast<uint8_t*>(src->pixels),
                    const_cast<void*>(referenceRGBA),
                    (uint32_t)c_width,
                    (uint32_t)c_height,
                    siMeta.format,
                    rdoOptions
                );

                if (rdoResult != RDO_ErrorCode::OK &&
                    rdoResult != RDO_ErrorCode::OK_NoAdvancedRDO)
                {
                    Utility::Printf(GACL_Logging_Priority_High, L"ERROR: RDO failed on mip %zu array item %zu (error=%d)\n", mip, item, (int)rdoResult);
                    return false;
                }
            }

            if (siMeta.arraySize > 1)
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"\nArray item %zu complete\n", item + 1);
            }
        }

        if (comInitialized)
        {
            CoUninitialize();
        }
        auto end_time = high_resolution_clock::now();
        auto duration_ms = duration_cast<milliseconds>(end_time - start_time).count();
        Utility::Printf(GACL_Logging_Priority_Medium, L"RDO completed successfully in %.3f seconds (%.0f ms)\n", duration_ms / 1000.0, (double)duration_ms);

        Utility::Printf(GACL_Logging_Priority_Medium, L"\n=== RDO Processing Complete ===\n");
        Utility::Printf(GACL_Logging_Priority_Medium, L"All %zu array items and %zu mip levels processed\n", siMeta.arraySize, siMeta.mipLevels);
        Utility::Printf(GACL_Logging_Priority_Medium, L"=================================================\n\n");
    }
#endif

    DirectX::ScratchImage blerLinearSpace;
    DirectX::ScratchImage blerScreenSpace;

#if GACL_INCLUDE_BLER
    bool genBlerScreenSpace = 
        options.BlerOptions.Enabled &&
        (baseFormat == DXGI_FORMAT_BC1_TYPELESS || baseFormat == DXGI_FORMAT_BC3_TYPELESS || baseFormat == DXGI_FORMAT_BC4_TYPELESS || baseFormat == DXGI_FORMAT_BC5_TYPELESS || baseFormat == DXGI_FORMAT_BC7_TYPELESS) &&
        (options.ShuffleOptions.Enabled || !options.CurveOptions.DisableSpaceCurve);

    bool genBlerLinearSpace = 
        options.BlerOptions.Enabled &&
        (baseFormat != DXGI_FORMAT_BC7_TYPELESS || (options.ShuffleOptions.Enabled || options.CurveOptions.DisableSpaceCurve));

    if (options.BlerOptions.Enabled)
    {
        if (baseFormat == DXGI_FORMAT_BC1_TYPELESS ||
            baseFormat == DXGI_FORMAT_BC2_TYPELESS ||
            baseFormat == DXGI_FORMAT_BC3_TYPELESS ||
            baseFormat == DXGI_FORMAT_BC4_TYPELESS ||
            baseFormat == DXGI_FORMAT_BC5_TYPELESS ||
            baseFormat == DXGI_FORMAT_BC7_TYPELESS)
        {
            
            // perform Block-Level Entropy reduction in both screen-sapce and linear space by default...
            DirectX::ScratchImage* blerImages[] = 
            {
                genBlerLinearSpace ? &blerLinearSpace : nullptr,
                genBlerScreenSpace ? &blerScreenSpace : nullptr
            };
            
            for (DirectX::ScratchImage* blerImage : blerImages)
            {
                if (!blerImage) continue;

                hr = blerImage->Initialize2D(siMeta.format, siMeta.width, siMeta.height, siMeta.arraySize, siMeta.mipLevels);
                if (FAILED(hr)) {
                    Utility::Printf(GACL_Logging_Priority_High, L"Failed to initialize output image.\n");
                    return false;
                }

                for (size_t item = 0; item < siMeta.arraySize; ++item)
                {
                    for (size_t mip = 0; mip < siMeta.mipLevels; ++mip)
                    {
                        const DirectX::Image* dstConst = blerImage->GetImage(mip, item, 0);
                        DirectX::Image* dst = const_cast<DirectX::Image*>(dstConst);

                        // Copy source BC data
                        const DirectX::Image* src = si.GetImage(mip, item, 0);

                        size_t mipWidth = std::max<size_t>(1, siMeta.width >> mip);
                        size_t mipHeight = std::max<size_t>(1, siMeta.height >> mip);
                        size_t blocksX = (mipWidth + 3) / 4;
                        size_t blocksY = (mipHeight + 3) / 4;
                        size_t numBlocks = blocksX * blocksY;

                        uint8_t* encodedBlocks = reinterpret_cast<uint8_t*>(_aligned_malloc(numBlocks * elementSize, 32));

                        if (blerImage == &blerScreenSpace)
                        {
                            std::vector<uint8_t> curvedData(numBlocks* elementSize);
                            if (!GACL_Shuffle_ApplySpaceCurve(curvedData.data(), src->pixels, numBlocks * elementSize, elementSize, blocksX * 4, true))
                            {
                                // If a given mip isn't eligable for curved transforms, smaller mips won't be either
                                // Short circuit here, and skip all further mips.  But, later we need to copy across the data from the texture that had linear RDO.
                                break;  
                            }
                            memcpy(encodedBlocks, curvedData.data(), numBlocks * elementSize);

#if _DEBUG
                            std::vector<uint8_t> uncurvedData(numBlocks* elementSize);
                            GACL_Shuffle_ApplySpaceCurve(uncurvedData.data(), curvedData.data(), numBlocks * elementSize, elementSize, blocksX*4, false);
                            if (memcmp(uncurvedData.data(), src->pixels, numBlocks * elementSize)) __debugbreak();
#endif
                        }
                        else
                        {
                            memcpy(encodedBlocks, src->pixels, numBlocks * elementSize);
                        }
                        DirectX::Image curvedSrc = *src;
                        curvedSrc.pixels = encodedBlocks;

                        // Decode raw RGBA (developer might instead pull this in from elsewhere
                        DirectX::TexMetadata mipMetadata = siMeta;
                        mipMetadata.width = mipWidth;
                        mipMetadata.height = mipHeight;
                        mipMetadata.mipLevels = 1;
                        mipMetadata.arraySize = 1;
                        mipMetadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
                        DirectX::ScratchImage decoded;
                        hr = DirectX::Decompress(&curvedSrc, 1, mipMetadata, isGammaFormat ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, decoded);
                        if (FAILED(hr)) {
                            Utility::Printf(GACL_Logging_Priority_High, L"Failed to decode mip %ull array item %ull\n", mip, item);
                            _mm_free(encodedBlocks);
                            return false;
                        }

                        const DirectX::Image* rgbaImg = decoded.GetImage(0, 0, 0);
                        uint8_t* decodedBlocks = reinterpret_cast<uint8_t*>(_aligned_malloc(numBlocks * 16 * 4, 32));
                        GACL_RDO_R8G8B8A8LinearToBlockGrouped(decodedBlocks, rgbaImg->pixels, rgbaImg->rowPitch, mipWidth, mipHeight);



                        assert(numBlocks >> 32 == 0);
                        GACL_RDO_BlockLevelEntropyReduce(uint32_t(numBlocks), encodedBlocks, (uint32_t)elementSize, decodedBlocks, options.BlerOptions.TargetUniqueBlockReduction);

                        if (blerImage == &blerScreenSpace)
                        {
                            // if we applied a space curve, uncurve the data
                            std::vector<uint8_t> decurvedData(numBlocks* elementSize);
                            GACL_Shuffle_ApplySpaceCurve(decurvedData.data(), encodedBlocks, numBlocks * elementSize, elementSize, blocksX * 4, false);
                            memcpy(dst->pixels, decurvedData.data(), numBlocks * elementSize);
                        }
                        else
                        {
                            // Replace original image with clustered image
                            memcpy(dst->pixels, encodedBlocks, numBlocks * elementSize);
                        }

                        _aligned_free(encodedBlocks);
                        _aligned_free(decodedBlocks);
                    }
                }
            }
        }
        else
        {
            Utility::Printf(GACL_Logging_Priority_High, L"ERROR: Block-Level Entropy Reduction not implemented for texture format.\n");
            return false;
        }
    }
#else
    bool genBlerScreenSpace = false;
#endif  // GACL_INCLUDE_BLER

    DirectX::ScratchImage* outImage = nullptr;
    if (options.BlerOptions.Enabled)
    {
        outImage = genBlerScreenSpace ? &blerScreenSpace : &blerLinearSpace;
    }
#if GACL_INCLUDE_CLER
    else if (options.ClerOptions.Enabled)
    {
        outImage = &si;
    }
#endif


    if (options.ShuffleOptions.Enabled == true)
    {
        DirectX::ScratchImage& srcForCurvedTransforms = blerScreenSpace;
        DirectX::ScratchImage& srcForUncurvedTransforms = options.BlerOptions.Enabled ? blerLinearSpace : si;
        outImage = (genBlerScreenSpace ? &srcForCurvedTransforms : &srcForUncurvedTransforms);

        // Experimental transforms include curved (16KB micro-tile) patterns.
        // Those transforms allow for the application of RDO in curved space, which can yield 10-20% additional compressed size savings
        // Warn if experimental is enabled, and Bler is not
        if (!options.BlerOptions.Enabled && options.ShuffleOptions.Transform == GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL)
        {
            Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: Experimental\\curved Shuffle+Compress specified independently of entropy reduction.\n");
            Utility::Printf(GACL_Logging_Priority_Medium, L"         When Entropy reduction is specified in conjunction with Shuffle+Compress, the gacl.exe frontend is able to\n");
            Utility::Printf(GACL_Logging_Priority_Medium, L"         apply the transform in linear space for the non-shuffled (zstd only) fallback, and in screen space for shuffled\n");
            Utility::Printf(GACL_Logging_Priority_Medium, L"         data.  This yields improved quality and compression ratio for the non-shuffled fallback.\n");
            Utility::Printf(GACL_Logging_Priority_Medium, L"\n");
            Utility::Printf(GACL_Logging_Priority_Medium, L"         When Shuffle+Compress is applied independently of entropy reduction, ensure the texture was previously\n");
            Utility::Printf(GACL_Logging_Priority_Medium, L"         exported from gacl.exe with Entropy Reduction and Shuffle+Compress enabled, if entropy reduction is desired.\n");
            Utility::Printf(GACL_Logging_Priority_Medium, L"\n");
        }

        if (baseFormat == DXGI_FORMAT_BC1_TYPELESS ||
            baseFormat == DXGI_FORMAT_BC3_TYPELESS ||
            baseFormat == DXGI_FORMAT_BC4_TYPELESS ||
            baseFormat == DXGI_FORMAT_BC5_TYPELESS ||
            baseFormat == DXGI_FORMAT_BC7_TYPELESS)
        {
            for (size_t item = 0; item < siMeta.arraySize; ++item)
            {
                size_t remainingSize = srcForUncurvedTransforms.GetPixelsSize() / siMeta.arraySize;

                for (size_t mip = 0; remainingSize; mip++)
                {
                    
                    const DirectX::Image* imgNoCurve = srcForUncurvedTransforms.GetImage(mip, item, 0);

                    const size_t blocksX = (imgNoCurve->width + 3) / 4;
                    const size_t blocksY = (imgNoCurve->height + 3) / 4;

                    const size_t mipSize = elementSize * blocksX * blocksY;

                    const bool mipIsCurvedTransformEligible = GACL_Shuffle_ApplySpaceCurve(nullptr, nullptr, mipSize, elementSize, blocksX * 4, true);
                    const bool mipHasCurvedRdoData = genBlerScreenSpace && mipIsCurvedTransformEligible;
                    const DirectX::Image* imgForCurve = genBlerScreenSpace ? srcForCurvedTransforms.GetImage(mip, item, 0) : nullptr;

                    uint8_t zstdCompressionLevel = options.CompressOptions.Level;
                    int tbs = options.CompressOptions.TargetBlockSize;
                    if (zstdCompressionLevel == 0xff)
                    {
                        if (mipSize > 256 * 1024ull)
                        {
                            zstdCompressionLevel = 18;
                        }
                        else if (mipSize > 16 * 1024ull)
                        {
                            zstdCompressionLevel = 14;
                        }
                        else
                        {
                            zstdCompressionLevel = 12;
                        }
                    }

                    GACL_SHUFFLE_TRANSFORM transformID = options.ShuffleOptions.Transform;
                    vector<uint8_t> compressed;
                    size_t compressedBytes = 0;

                    wstring outputDerivedFileName;

                    HRESULT status = S_OK;
                    bool sizeReduced;
                    size_t bytesProcessed = 0;
                    if (mipSize >= (64*1024ull) || (siMeta.mipLevels == 1 && siMeta.arraySize == 1))  // TODO decide on "tail" size
                    {
                        compressed.resize(mipSize);

                        // if we have mip data that's had RDO applied in screen space, use that.
                        // else let the API make a curved copy of the data that would be used for non-curve transforms, so long as the mip is eligible
                        const uint8_t* curvedRdoTextureData = mipHasCurvedRdoData ? imgForCurve->pixels : (mipIsCurvedTransformEligible ? imgNoCurve->pixels : nullptr);
                        SHUFFLE_COMPRESS_PARAMETERS params = { mipSize, siMeta.format, imgNoCurve->pixels, {zstdCompressionLevel, tbs}, {4 * blocksX, curvedRdoTextureData} };


                        status = GACL_ShuffleCompress_BCn(
                            compressed.data(), 
                            &transformID, 
                            &compressedBytes, 
                            params
                            );

                        sizeReduced = SUCCEEDED(status);
                        const wchar_t* ext = sizeReduced ? GACL_ShuffleCompress_GetFileExtensionForTransform(transformID) : L"uncompressed";

                        bytesProcessed = mipSize;
                        if (!exportBaseName.empty())
                        {
                            wchar_t fileNamePostfix[20];
                            if (siMeta.mipLevels > 1 && siMeta.arraySize > 1)
                            {
                                swprintf_s(fileNamePostfix, _countof(fileNamePostfix), L".item%I64u.mip%I64u.%ws", item, mip, ext);
                                outputDerivedFileName = exportBaseName + fileNamePostfix;
                            }
                            else if (siMeta.mipLevels > 1)
                            {
                                swprintf_s(fileNamePostfix, _countof(fileNamePostfix), L".mip%I64u.%ws", mip, ext);
                                outputDerivedFileName = exportBaseName + fileNamePostfix;
                            }
                            else if (siMeta.arraySize > 1)
                            {
                                swprintf_s(fileNamePostfix, _countof(fileNamePostfix), L".item%I64u.%ws", item, ext);
                                outputDerivedFileName = exportBaseName + fileNamePostfix;
                            }
                            else
                            {
                                outputDerivedFileName = exportBaseName + L"." + ext;
                            }
                        }
                    }
                    else
                    {
                        compressed.resize(remainingSize);

                        SHUFFLE_COMPRESS_PARAMETERS params = { mipSize, siMeta.format, imgNoCurve->pixels, {zstdCompressionLevel, tbs}};

                        status = GACL_ShuffleCompress_BCn(compressed.data(), &transformID, &compressedBytes, params);
                        sizeReduced = SUCCEEDED(status);
                        const wchar_t* ext = sizeReduced ? GACL_ShuffleCompress_GetFileExtensionForTransform(transformID) : L"uncompressed";
                        outputDerivedFileName = exportBaseName + L".tail." + ext;
                        bytesProcessed = remainingSize;
                    }
                    

                    // If we're processing a texture that's had RDO applied both in linear and screen space, but for a mip size where screen space transform isn't possible,
                    // then we skipped the RDO step earlier, and the screen space mip is blank.  Copy across the linear space RDO data
                    if (genBlerScreenSpace && !mipHasCurvedRdoData)
                    {
                        DirectX::Image* dst = const_cast<DirectX::Image*>(imgForCurve);
                        memcpy(dst->pixels, imgNoCurve->pixels, bytesProcessed);
                    }
                    remainingSize -= bytesProcessed;

                    if (!exportBaseName.empty())
                    {
                        // TODO change the unique file extensions for exports to instead have transform IDs?
                        std::ofstream outFile(outputDerivedFileName, std::ios_base::out | std::ios::binary);
                        if (!outFile)
                        {
                            Utility::Printf(GACL_Logging_Priority_High, L"ERROR: Failed open output file \"%ws\"\n", outputDerivedFileName.c_str());
                            return false;
                        }
                        if (sizeReduced)
                        {
                            outFile.write(reinterpret_cast<const char*>(compressed.data()), compressedBytes);
                            Utility::Printf(GACL_Logging_Priority_Medium, L"%u bytes written to \"%ws\"\n", compressedBytes, outputDerivedFileName.c_str());
                        }
                        else
                        {
                            outFile.write(reinterpret_cast<const char*>(imgNoCurve->pixels), bytesProcessed);
                            Utility::Printf(GACL_Logging_Priority_Medium, L"%u bytes written to \"%ws\"\n", bytesProcessed, outputDerivedFileName.c_str());
                        }
                        outFile.close();
                    }
                }
            }
        }
    }


    if (outImage && !outputFileName.empty())
    {
        hr = DirectX::SaveToDDSFile(outImage->GetImages(), outImage->GetImageCount(),
            outImage->GetMetadata(), DirectX::DDS_FLAGS_NONE, outputFileName.c_str());
        if (FAILED(hr)) {
            Utility::Printf(GACL_Logging_Priority_High, L"Failed to save DDS file.\n");
            return false;
        }
        Utility::Printf(GACL_Logging_Priority_Medium, L"Modified file written to \"%ws\"\n", outputFileName.c_str());
    }

    return true;

}
