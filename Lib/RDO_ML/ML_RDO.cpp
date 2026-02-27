//-------------------------------------------------------------------------------------
// ML_RDO.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#if GACL_INCLUDE_CLER

#define ORT_API_MANUAL_INIT
#define NOMINMAX

#include "gacl.h"
#include "LossMetrics.h"
#include "../helpers/FormatHelper.h"
#include "../helpers/Utility.h"
#include "KMeansRDO.h"

#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdint>
#include <limits>
#include <thread>
#include <mutex>

#include <filesystem>
#include <direct.h>

#include <onnxruntime_c_api.h>
#include <onnxruntime_cxx_api.h>

#include <iostream>
#include <memory>

using namespace std::filesystem;
using namespace std;

static const OrtApi* g_ort = nullptr;
static std::once_flag g_ortInitFlag;

bool initORT()
{
    std::call_once(g_ortInitFlag, []()
        {
            const OrtApiBase* base = OrtGetApiBase();
            if (!base)
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: Failed to get ONNX Runtime API base for ORT initialization.\n");
                return;
            }

            g_ort = base->GetApi(ORT_API_VERSION);
            if (!g_ort)
            {
                const char* ver = base->GetVersionString();
                Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: Failed to get ONNX Runtime API for ORT initialization. Runtime version: %hs\n", ver ? ver : "unknown");
                return;
            }
            Ort::InitApi(g_ort);
            const char* ortVersion = base->GetVersionString();
            Utility::Printf(GACL_Logging_Priority_Medium, L"ONNX Runtime initialized successfully.\n");
            Utility::Printf(GACL_Logging_Priority_Medium, L"Version: %hs\n", ortVersion ? ortVersion : "unknown");
        }
    );
	return g_ort != nullptr;
}

static void LoadPerceptualModel(RDOOptions& rdoOptions, LossMetrics::Metric metric)
{
    rdoOptions.onnxModelPtr = nullptr;

    wchar_t exePathBuf[MAX_PATH];
    GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
    path exeDir = path(exePathBuf).parent_path();
    path repoDir = exeDir.parent_path().parent_path().parent_path();

    std::filesystem::path lpipsPath = repoDir / L"ThirdParty" / L"models" / L"lpips_model.onnx";
    std::filesystem::path vggPath = repoDir / L"ThirdParty" / L"models" / L"vgg_model.onnx";
    
    try
    {
        static Ort::Env ortEnv(ORT_LOGGING_LEVEL_WARNING, "ML_RDO");

        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(static_cast<int>(rdoOptions.numThreads));
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        if (metric == LossMetrics::Metric::LPIPS)
        {
            try
            {
                static std::unique_ptr<Ort::Session> s_lpipsOnnxModel;
                if (!s_lpipsOnnxModel)
                {
                    Utility::Printf(GACL_Logging_Priority_Medium, L"Loading ONNX LPIPS model from: %ws\n", lpipsPath.c_str());
                    s_lpipsOnnxModel = std::make_unique<Ort::Session>(ortEnv, lpipsPath.c_str(), sessionOptions);
                    Utility::Printf(GACL_Logging_Priority_Medium, L"Loaded ONNX LPIPS model successfully.\n");
                }
                rdoOptions.onnxModelPtr = s_lpipsOnnxModel.get();
            }
            catch (const Ort::Exception& e)
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: Failed to load ONNX LPIPS model: %hs\n", e.what());
                rdoOptions.onnxModelPtr = nullptr;
            }
        }

        else if (metric == LossMetrics::Metric::VGG)
        {
            try
            {
                static std::unique_ptr<Ort::Session> s_vggOnnxModel;
                if (!s_vggOnnxModel)
                {
                    Utility::Printf(GACL_Logging_Priority_Medium, L"Loading ONNX VGG model from: %ws\n", vggPath.c_str());
                    s_vggOnnxModel = std::make_unique<Ort::Session>(ortEnv, vggPath.c_str(), sessionOptions);
                    Utility::Printf(GACL_Logging_Priority_Medium, L"Loaded ONNX VGG model successfully.\n");
                }
                rdoOptions.onnxModelPtr = s_vggOnnxModel.get();
            }
            catch (const Ort::Exception& e)
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: Failed to load ONNX VGG model: %hs\n", e.what());
                rdoOptions.onnxModelPtr = nullptr;
            }
        }

        else
        {
            Utility::Printf(GACL_Logging_Priority_Medium, L"Metric %s does not require perceptual model.\n", LossMetrics::ToString(metric).c_str());
        }

        if (rdoOptions.onnxModelPtr)
        {
            Utility::Printf(GACL_Logging_Priority_Medium, L"Using ONNX perceptual model.\n");
        }
        else
        {
            Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: No perceptual model loaded; will fallback to MSE loss.\n");
        }
    }
    catch (const std::exception& e)
    {
        Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: Failed to initialize ONNX Runtime environment: %hs\n", e.what());
        Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: No perceptual model loaded; will fallback to MSE loss.\n");
    }
}

static LossMetrics::Metric ToLossMetric(RDOLossMetric metric)
{
    return static_cast<LossMetrics::Metric>(static_cast<int>(metric));
}

// Returns a reasonable number of threads to use for parallel work.
// Try std::thread::hardware_concurrency(), usually returns logical CPUs.
// If that fails (returns 0), fall back to Windows API and count physical cores.
// Always return at least 1 to avoid invalid thread counts.
static uint32_t GetOptimalThreadCount()
{
    unsigned int hwThreads = std::thread::hardware_concurrency();
    if (hwThreads > 0)
    {
        return hwThreads;
    }

    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return 1;
    }

    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    if (!GetLogicalProcessorInformation(buffer.data(), &len))
    {
        return 1;
    }

    int coreCount = 0;
    for (const auto& info : buffer)
    {
        if (info.Relationship == RelationProcessorCore)
        {
            coreCount++;
        }
    }

    return (coreCount > 0) ? coreCount : 1;
}

RDO_ErrorCode GACL_RDO_ComponentLevelEntropyReduce(
    uint32_t bcElementSizeBytes,
    _Inout_updates_bytes_(bcElementSizeBytes* (((imageWidth + 3) >> 2)* ((imageHeight + 3) >> 2))) void* encodedData,
    const _In_reads_bytes_opt_(4 * imageWidth * imageHeight) void* referenceR8G8B8A8,
    const uint32_t imageWidth,
    const uint32_t imageHeight,
    const DXGI_FORMAT format,
    RDOOptions& options)
{
    try 
    {
        // Calculate expected BC1 data size
        uint32_t widthInBlocks = (imageWidth + 3) / 4;
        uint32_t heightInBlocks = (imageHeight + 3) / 4;
        uint32_t numBlocks = widthInBlocks * heightInBlocks;

        if (!encodedData)
        {
            Utility::Printf(GACL_Logging_Priority_High, L"\nERROR: encodedData is null\n");
            return  RDO_ErrorCode::NullEncodedData;
        }

        // Validate encodedData size for BC1
        if (format != DXGI_FORMAT_BC1_TYPELESS && format != DXGI_FORMAT_BC1_UNORM && format != DXGI_FORMAT_BC1_UNORM_SRGB)
        {
            Utility::Printf(GACL_Logging_Priority_High, L"\nERROR: Only BC1 format is supported for encodedData validation\n");
            return  RDO_ErrorCode::UnsupportedEncodedFormat;
        }

        if (imageWidth == 0 || imageHeight == 0)
        {
            Utility::Printf(GACL_Logging_Priority_High, L"\nERROR: Invalid image dimensions: %ux%u\n", imageWidth, imageHeight);
            return  RDO_ErrorCode::InvalidImageDimensions;
        }

        DXGI_FORMAT baseFormat = GetBaseFormat(format);
        size_t bcElementSize = GetElementSize(format);

        if (bcElementSize == 0)
        {
            Utility::Printf(GACL_Logging_Priority_High, L"\nERROR: Unsupported or invalid format\n");
            return  RDO_ErrorCode::InvalidBCElementSize;
        }

        if (bcElementSizeBytes != static_cast<uint32_t>(bcElementSize))
        {
            Utility::Printf(GACL_Logging_Priority_High, L"\nERROR: bcElementSizeBytes parameter (%u) does not match format's element size (%zu)\n",
                bcElementSizeBytes, bcElementSize);
            return RDO_ErrorCode::InvalidBCElementSize;
        }

        if (baseFormat != DXGI_FORMAT_BC1_TYPELESS)
        {
            Utility::Printf(GACL_Logging_Priority_High, L"\nERROR: RDO currently only supports BC1 format\n");
            return RDO_ErrorCode::UnsupportedBaseFormat;
        }

        int maxK = options.maxClusters;
        if (maxK == -1)
        {
            maxK = static_cast<int>(std::max(imageWidth / 2, imageHeight / 2));
        }

        maxK = std::min(maxK, static_cast<int>(widthInBlocks * heightInBlocks * 2)); //capping at the max num of clusters realistically 

		int minK = std::max(options.minClusters, 1); //minK should be at least 1

        if (maxK < minK)
        {
            Utility::Printf(GACL_Logging_Priority_High, L"\nERROR: maxClusters (%d) < minClusters (%d)\n", maxK, minK);
            return RDO_ErrorCode::InvalidClusterRange;
        }

        Utility::Printf(GACL_Logging_Priority_Medium, L"\nRDO Configuration:\n");
        Utility::Printf(GACL_Logging_Priority_Medium, L"Max clusters: %d\n", maxK);
        Utility::Printf(GACL_Logging_Priority_Medium, L"Min clusters: %d\n", minK);
        Utility::Printf(GACL_Logging_Priority_Medium, L"Number of iterations: %d\n", options.iterations);


        if (options.lossMax < 0.0f || options.lossMax > 1.0f ||
            options.lossMin < 0.0f || options.lossMin > 1.0f ||
            options.lossMax < options.lossMin)
        {
            Utility::Printf(GACL_Logging_Priority_High, L"Invalid loss range : lossMin = % f, lossMax = % f(expected 0.0 <= lossMin <= lossMax <= 1.0)\n",
                options.lossMin,
                options.lossMax );

            return RDO_ErrorCode::InvalidLossRange;
        }

        Utility::Printf(GACL_Logging_Priority_Medium, L"Max loss: %f\n", options.lossMax);
        Utility::Printf(GACL_Logging_Priority_Medium, L"Min loss: %f\n", options.lossMin);

        int numThreads = options.numThreads;
        if (numThreads <= 0)
        {
            numThreads = static_cast<int>(GetOptimalThreadCount());
        }

        int iterations = std::max(1, options.iterations);
		iterations = std::min(iterations, 25);

        Utility::Printf(GACL_Logging_Priority_Medium, L"Processing %u blocks (%ux%u), k=[%d, %d], threads=%d\n\n",numBlocks, imageWidth, imageHeight, minK, maxK, numThreads);
        
        LossMetrics::Metric lossMetric = ToLossMetric(options.metric);

        if (baseFormat == DXGI_FORMAT_BC1_TYPELESS)
        {
            uint8_t* bcData = static_cast<uint8_t*>(encodedData);

            std::vector<int> modes;
            std::vector<std::vector<uint8_t>> endpoints = KMeansRDO::ExtractBC1Endpoints(bcData, numBlocks, modes);

            bool hasReference = (referenceR8G8B8A8 != nullptr);

            bool useAdvancedRDO = options.useClusterRDO && hasReference;

            std::vector<std::vector<uint8_t>> clustered;

            if (useAdvancedRDO)
            {
                if (lossMetric == LossMetrics::Metric::LPIPS || lossMetric == LossMetrics::Metric::VGG) {
                    bool ortInitialized = initORT();
                    if (!ortInitialized)
                    {
                        Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: ONNX Runtime initialization failed; will use MSE loss rather than perceptual loss.\n");
                        lossMetric = LossMetrics::Metric::MSE;
                    }
                }

                if (lossMetric == LossMetrics::Metric::LPIPS || lossMetric == LossMetrics::Metric::VGG) {
                    LoadPerceptualModel(options, lossMetric);
                }

                Ort::Session* onnxModel = nullptr;

                if(options.onnxModelPtr)
                {
                    onnxModel = static_cast<Ort::Session*>(options.onnxModelPtr);
                }
                try
                {
                    clustered = KMeansRDO::ClusterRDOWithLoss(
                        endpoints,
                        modes,
                        bcData,
                        numBlocks,
                        imageWidth,
                        imageHeight,
                        referenceR8G8B8A8,
                        format,
                        options.isGammaFormat,
                        maxK,
                        minK,
                        iterations,
                        lossMetric,
                        options.lossMax,
                        options.lossMin,
                        numThreads,
                        options.usePlusPlus,
                        onnxModel
                    );
                }
                catch (...)
                {
                    Utility::Printf(GACL_Logging_Priority_Medium, L"[INFO] Falling back to basic RDO.\n");
                    useAdvancedRDO = false;
                }
            }
            else if(!useAdvancedRDO)
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"  Using basic RDO with k-means clustering (k=%d)\n", maxK);

                std::vector<std::vector<uint8_t>> centroids = KMeansRDO::InitializeCentroids(
                    endpoints,
                    maxK,
                    options.usePlusPlus,
                    options.lossMin,
                    options.lossMax
                );

                clustered = KMeansRDO::ClusterEventing(
                    endpoints,
                    maxK,
                    iterations,
                    numThreads,
                    centroids
                );
            }

            if (clustered.size() < uint64_t(numBlocks * 2))
            {
                Utility::Printf(GACL_Logging_Priority_High, L"ERROR: clustered.size()=%zu but expected %u\n", clustered.size(), numBlocks * 2);
                return RDO_ErrorCode::ClusteredSizeMismatch;
            }
            if (modes.size() != numBlocks)
            {
                Utility::Printf(GACL_Logging_Priority_High, L"ERROR: modes.size()=%zu but expected %u\n", modes.size(), numBlocks);
                return RDO_ErrorCode::ModesSizeMismatch;
            }
            KMeansRDO::ApplyBC1Endpoints(bcData, numBlocks, clustered, modes);


            Utility::Printf(GACL_Logging_Priority_Medium, L"RDO complete - endpoints reduced from %zu to clustered values\n\n", endpoints.size());

            if (useAdvancedRDO)
            {
                return RDO_ErrorCode::OK;
            }
            else
            {
                return RDO_ErrorCode::OK_NoAdvancedRDO;
            }
        }

        return RDO_ErrorCode::UnknownError;
    }
    catch (const std::exception& e)
    {
        Utility::Printf(GACL_Logging_Priority_High, L"ERROR: RDO processing failed: %hs\n", e.what());
        return RDO_ErrorCode::InternalException;
    }
}
#endif