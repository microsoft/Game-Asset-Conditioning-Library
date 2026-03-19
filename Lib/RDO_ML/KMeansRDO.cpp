//-------------------------------------------------------------------------------------
// KmeansRDO.cpp
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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "KMeansRDO.h"
#include "LossMetrics.h"
#include <DirectXTex.h>
#include <windows.h>
#include <random>
#include <limits>
#include <algorithm>
#include <set>
#include <cstdio>
#include <vector>
#include <cstring>
#include <cstdint>
#include "../helpers/Utility.h"

#define CLUSTER_EVENT_TIMEOUT_MS 10000

struct ClusterEventingThreadData {
    const std::vector<std::vector<uint8_t>>* endpoints;
    std::vector<std::vector<uint8_t>>* centroids;
    std::vector<std::vector<uint8_t>>* newCentroids;
    std::vector<int>* assignments;
    size_t endpointsStartIdx;
    size_t endpointsEndIdx;
    size_t centroidsStartIdx;
    size_t centroidsEndIdx;
    int k;
    std::vector<int>* counts;
    std::vector<std::vector<float>>* sums;
    volatile bool* stillClustering;
    int threadInd;
    std::vector<HANDLE> ghStartEvents;
    std::vector<HANDLE> ghCompleteEvents;
    int dimensions;
};

std::vector<std::vector<uint8_t>> KMeansRDO::ClusterRDOWithLoss(
    const std::vector<std::vector<uint8_t>>& endpoints,
    const std::vector<int>& modes,
    uint8_t* encodedData,
    uint32_t numBlocks,
    uint32_t imageWidth,
    uint32_t imageHeight,
    const void* referenceR8G8B8A8,
    DXGI_FORMAT format,
    bool isGammaFormat,
    int maxK,
    int minK,
    int iterations,
    LossMetrics::Metric lossMetric,
    float upperLossBound,
    float lowerLossBound,
    int numThreads,
    bool plusplus,
    Ort::Session* onnxModelPtr)
{
    Utility::Printf(GACL_Logging_Priority_Medium, L"\nEndpoints: %zu, k range: [%d, %d]\n", endpoints.size(), minK, maxK);
    Utility::Printf(GACL_Logging_Priority_Medium, L"Loss metric: %s, bounds: [%.4f, %.4f]\n", LossMetrics::ToString(lossMetric).c_str(), lowerLossBound, upperLossBound);

    if (endpoints.empty() || modes.size() != numBlocks)
    {
        Utility::Printf(GACL_Logging_Priority_High, L"ERROR: invalid inputs (endpoints=%zu, modes=%zu, blocks=%u).\n", endpoints.size(), modes.size(), numBlocks);
        return {};
    }

    const bool hasReference = (referenceR8G8B8A8 != nullptr);
    const bool needsModels = (lossMetric != LossMetrics::Metric::MSE &&
        lossMetric != LossMetrics::Metric::RMSE);
    const bool hasModels = (onnxModelPtr != nullptr);

    if (!hasReference || imageWidth < 24 || imageHeight < 24)
    {
        if (!hasReference)
        {
            Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: no reference provided; using basic k-means (k=%d).\n", maxK);
        }
        if (imageWidth < 24 || imageHeight < 24)
        {
            Utility::Printf(GACL_Logging_Priority_Medium, L"Small mip level detected (%ux%u) — using basic k-means (k=%d).\n", imageWidth, imageHeight, maxK);
        }

        auto centroids = InitializeCentroids(endpoints, maxK, plusplus, lowerLossBound, upperLossBound);
        auto clustered = ClusterEventing(endpoints, maxK, iterations, numThreads, centroids);
        Utility::Printf(GACL_Logging_Priority_Medium, L"Basic k-means fallback completed.\n\n");
        return clustered;
    }

    if (needsModels && !hasModels)
    {
        Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: loss metric %s requires models, but no models provided; using MSE loss instead.\n", LossMetrics::ToString(lossMetric).c_str());
        lossMetric = LossMetrics::Metric::MSE;
    }

    const bool useOrtTensors = LossMetrics::requiresOnnx(lossMetric);

    //create reference data
    std::vector<float> referenceTensorData(static_cast<size_t>(imageHeight) * imageWidth * 3);
    const uint8_t* src = static_cast<const uint8_t*>(referenceR8G8B8A8);
    const size_t H = imageHeight;
    const size_t W = imageWidth;
    const size_t HW = H * W;
    float* R = referenceTensorData.data() + 0 * HW;
    float* G = referenceTensorData.data() + 1 * HW;
    float* B = referenceTensorData.data() + 2 * HW;
    for (uint32_t h = 0; h < imageHeight; ++h) {
        for (uint32_t w = 0; w < imageWidth; ++w) {
            const size_t i = static_cast<size_t>(h) * W + w; 
            const size_t offset = i * 4;                     
            R[i] = src[offset + 0] / 255.0f;
            G[i] = src[offset + 1] / 255.0f; 
            B[i] = src[offset + 2] / 255.0f; 
        }
    }

	// ONNX tensor wrappers for perceptual losses
    Ort::Value referenceTensor(nullptr);
    if (useOrtTensors)
    {
        Ort::AllocatorWithDefaultOptions allocator;
        std::array<int64_t, 4> referenceShape{ 1, 3, (int64_t)imageHeight, (int64_t)imageWidth };
        referenceTensor = Ort::Value::CreateTensor<float>(
            allocator.GetInfo(),
            referenceTensorData.data(),
            referenceTensorData.size(),
            referenceShape.data(),
            referenceShape.size());
    }
    const uint32_t widthInBlocks = (imageWidth + 3) / 4;

    DirectX::TexMetadata meta{};
    meta.width = imageWidth;
    meta.height = imageHeight;
    meta.mipLevels = 1;
    meta.arraySize = 1;
    meta.format = format;
    meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
    meta.depth = 1;

    const DXGI_FORMAT targetFormat = isGammaFormat
        ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        : DXGI_FORMAT_R8G8B8A8_UNORM;

    DirectX::Blob bcBlob;
    DirectX::ScratchImage reloaded;
    DirectX::ScratchImage decompressed;

    std::vector<uint8_t> tempBC(numBlocks * 8);

    // Decompress clustered BC data to NCHW float buffer
    auto decompress_to_float_buffer =
        [&](const std::vector<std::vector<uint8_t>>& clustered) -> std::vector<float>
        {
            std::memcpy(tempBC.data(), encodedData, tempBC.size());
            ApplyBC1Endpoints(tempBC.data(), numBlocks, clustered, modes);

            DirectX::Image bcImg{};
            bcImg.width = imageWidth;
            bcImg.height = imageHeight;
            bcImg.format = format;
            bcImg.rowPitch = size_t(widthInBlocks) * 8u; // BC1 row: 8 bytes per block
            bcImg.slicePitch = size_t(numBlocks) * 8u;
            bcImg.pixels = tempBC.data();

            //save to memory
            bcBlob.Release();
            HRESULT hr = DirectX::SaveToDDSMemory(&bcImg, 1, meta, DirectX::DDS_FLAGS_NONE, bcBlob);
            if (FAILED(hr))
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: SaveToDDSMemory failed while attempting to decompress BC image for loss calculation (0x%08X)\n", hr);
                return {};
            }

            //read from mem buffer
            reloaded.Release();
            hr = DirectX::LoadFromDDSMemory(bcBlob.GetBufferPointer(), bcBlob.GetBufferSize(), DirectX::DDS_FLAGS_NONE, nullptr, reloaded);
            if (FAILED(hr))
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: LoadFromDDSMemory failed while attempting to decompress BC image for loss calculation (0x%08X)\n", hr);
                return {};
            }

            //decompress
            decompressed.Release();
            hr = DirectX::Decompress(reloaded.GetImages(), reloaded.GetImageCount(), reloaded.GetMetadata(), targetFormat, decompressed);
            if (FAILED(hr))
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: Decompress failed while attempting to decompress BC image for loss calculation (0x%08X)\n", hr);
                return {};
            }

            const DirectX::Image* img = decompressed.GetImage(0, 0, 0);
            if (!img || !img->pixels)
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: Invalid decoded image while attempting to decompress BC image for loss calculation\n");
                return {};
            }

            //conv decompressed RGBA to RGB, NCHW
            std::vector<float> decData(imageHeight * imageWidth * 3);
            const uint8_t* decSrc = static_cast<const uint8_t*>(img->pixels);
            const size_t H = imageHeight;
            const size_t W = imageWidth;
            const size_t HW = H * W;
            float* R = decData.data() + 0 * HW;
            float* G = decData.data() + 1 * HW;
            float* B = decData.data() + 2 * HW;
            for (uint32_t h = 0; h < imageHeight; ++h) {
                for (uint32_t w = 0; w < imageWidth; ++w) {
                    const size_t i = static_cast<size_t>(h) * W + w; 
                    const size_t offset = i * 4;                     
                    R[i] = decSrc[offset + 0] / 255.0f; 
                    G[i] = decSrc[offset + 1] / 255.0f; 
                    B[i] = decSrc[offset + 2] / 255.0f;
                }
            }

            return decData;
        };

    int kLow = std::max(1, minK);
    int kHigh = std::max(kLow, maxK);
    int k = (kLow + kHigh) / 2;
    bool foundAcceptable = false;
    float bestLoss = std::numeric_limits<float>::infinity();
    std::vector<std::vector<uint8_t>> bestClustered;
    auto cents = InitializeCentroids(endpoints, maxK, plusplus, lowerLossBound, upperLossBound);

    while (kLow <= kHigh && k > 0)
    {
        std::vector<std::vector<uint8_t>> curr_cents(cents.begin(), cents.begin() + k);
        auto clustered = ClusterEventing(endpoints, k, iterations, numThreads, curr_cents);

        if (clustered.size() != size_t(numBlocks) * 2 || (!clustered.empty() && clustered[0].size() < 3))
        {
            Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: clustered.size()=%zu (expected %u*2) or endpoint dim < 3\n", clustered.size(), numBlocks);
            kLow = k + 1; k = (kLow + kHigh) / 2;
            continue;
        }

        auto decodedData = decompress_to_float_buffer(clustered);
        if (decodedData.empty())
        {
            kLow = k + 1; k = (kLow + kHigh) / 2;
            continue;
        }

        float loss;
        if (useOrtTensors)
        {
            // Perceptual metrics require float buffers to be wrapped in Ort::Value tensors
            Ort::AllocatorWithDefaultOptions allocator;
            std::array<int64_t, 4> decShape{ 1, 3, (int64_t)imageHeight, (int64_t)imageWidth };
            Ort::Value decodedTensor = Ort::Value::CreateTensor<float>(
                allocator,
                decShape.data(),
                decShape.size());
            float* dst = decodedTensor.GetTensorMutableData<float>();
            std::memcpy(dst, decodedData.data(), decodedData.size() * sizeof(float));

            auto decTensorShape = decodedTensor.GetTensorTypeAndShapeInfo().GetShape();
            auto refTensorShape = referenceTensor.GetTensorTypeAndShapeInfo().GetShape();

            if (decTensorShape.size() != 4 || refTensorShape.size() != 4 ||
                decTensorShape[1] != 3 || refTensorShape[1] != 3)
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: tensor shapes not NCHW(3): dec=[");
                for (auto s : decTensorShape) Utility::Printf(GACL_Logging_Priority_Medium, L"%ld ", (long)s);
                Utility::Printf(GACL_Logging_Priority_Medium, L"] ref=[");
                for (auto s : refTensorShape) Utility::Printf(GACL_Logging_Priority_Medium, L"%ld ", (long)s);
                Utility::Printf(GACL_Logging_Priority_Medium, L"]\n");
                kLow = k + 1; k = (kLow + kHigh) / 2;
                continue;
            }

            loss = LossMetrics::CalculateLoss(decodedTensor, referenceTensor, lossMetric, onnxModelPtr);
        }
        else
        {
            // classic metrics can be computed with no ORT needed
            loss = LossMetrics::CalculateLoss(
                decodedData.data(),
                referenceTensorData.data(),
                decodedData.size(),
                lossMetric);
        }

        Utility::Printf(GACL_Logging_Priority_Medium, L"Loss: %.6f\n", loss);

        const float targetMid = 0.5f * (lowerLossBound + upperLossBound);
        const float curDist = std::abs(loss - targetMid);
        const float bestDist = std::abs(bestLoss - targetMid);

        if (curDist < bestDist)
        {
            bestLoss = loss;
            bestClustered = std::move(clustered);
        }

        if (loss <= upperLossBound && loss >= lowerLossBound)
        {
            Utility::Printf(GACL_Logging_Priority_Medium, L"Acceptable loss found at k=%d (%.6f)\n", k, loss);
            foundAcceptable = true;
            if (bestClustered.empty())
            {
                bestClustered = std::move(clustered);
            }
            break;
        }

        int prevK = k;
        if (loss < lowerLossBound)
        {
            kHigh = k - 1;
        }
        else
        {
            kLow = k + 1;
        }
        k = (kLow + kHigh) / 2;
        if (k == prevK)
        {
            k = (loss < lowerLossBound) ? kLow : kHigh;
        }
    }

    if (!foundAcceptable)
    {
        Utility::Printf(GACL_Logging_Priority_Medium, L"No exact solution; using best loss: %.6f\n\n", bestLoss);
    }
    else
    {
        Utility::Printf(GACL_Logging_Priority_Medium, L"Success: best loss=%.6f\n", bestLoss);
    }

    if (bestClustered.empty())
    {
        return ClusterEventing(endpoints, maxK, iterations, numThreads, cents);
    }
    return bestClustered;
}


static DWORD WINAPI ClusterEventThreadProc(LPVOID lpParam) {
    ClusterEventingThreadData* threadData = static_cast<ClusterEventingThreadData*>(lpParam);
    const auto& endpoints = *(threadData->endpoints);
    auto& centroids = *(threadData->centroids);
    auto& assignments = *(threadData->assignments);
    auto& newCentroids = *(threadData->newCentroids);
    auto& counts = *(threadData->counts);
    auto& sums = *(threadData->sums);
    auto& stillClustering = *(threadData->stillClustering);
    const int dimensions = threadData->dimensions;

    while (stillClustering)
    {
        DWORD dwResult = WaitForSingleObject(threadData->ghStartEvents[threadData->threadInd], INFINITE);
        if (!stillClustering) 
        {
            return 0;
        }

        if (dwResult == WAIT_OBJECT_0) 
        {
            for (size_t i = threadData->endpointsStartIdx; i < threadData->endpointsEndIdx; i++) 
            {
                int bestCluster = 0;
                double minDistance = std::numeric_limits<double>::infinity();

                for (size_t j = 0; j < threadData->k; j++) 
                {
                    double distance = 0;
                    for (int dim = 0; dim < dimensions; dim++) 
                    {
                        double diff = static_cast<double>(endpoints[i][dim]) - static_cast<double>(centroids[j][dim]);
                        distance += diff * diff;
                    }
                    if (distance < minDistance) 
                    {
                        minDistance = distance;
                        bestCluster = static_cast<int>(j);
                    }
                }

                assignments[i] = bestCluster;
            }
        }

        SetEvent(threadData->ghCompleteEvents[threadData->threadInd]);

        dwResult = WaitForSingleObject(threadData->ghStartEvents[threadData->threadInd], INFINITE);
        if (!stillClustering) 
        {
            return 0;
        }

        if (dwResult == WAIT_OBJECT_0) 
        {
            for (size_t i = threadData->centroidsStartIdx; i < threadData->centroidsEndIdx; i++) {
                if (counts[i] > 0) 
                {
                    for (int dim = 0; dim < dimensions; dim++) 
                    {
                        newCentroids[i][dim] = static_cast<uint8_t>(std::round(sums[i][dim] / counts[i]));
                    }
                }
                else {
                    // EMPTY CLUSTER: Keep previous centroid value
                    // This prevents [0,0,0] black holes
                    for (int dim = 0; dim < dimensions; dim++) {
                        newCentroids[i][dim] = centroids[i][dim];
                    }
                }
            }
        }

        SetEvent(threadData->ghCompleteEvents[threadData->threadInd]);
    }

    return 0;
}

std::vector<std::vector<uint8_t>> KMeansRDO::ClusterEventing(
    const std::vector<std::vector<uint8_t>>& endpoints,
    int k,
    int iterations,
    int numThreads,
    std::vector<std::vector<uint8_t>> centroids)
{
    Utility::Printf(GACL_Logging_Priority_Medium, L"Running k-means with input: %zu endpoints, k=%d, iterations=%d, numThreads=%d\n", endpoints.size(), k, iterations, numThreads);

    std::vector<std::vector<uint8_t>> result = endpoints;

    if (numThreads <= 0) 
    {
        numThreads = 1;
        Utility::Printf(GACL_Logging_Priority_Medium, L"WARNING: numThreads was %d, set to 1\n", numThreads);
    }

    int groupK = (std::min)(k, static_cast<int>(endpoints.size()));

    if (endpoints.empty()) 
    {
        Utility::Printf(GACL_Logging_Priority_High, L"ERROR: endpoints is empty!\n");
        return result;
    }

    volatile bool stillClustering = true;
    int dimensions = static_cast<int>(endpoints[0].size());

    if (centroids.size() > groupK) 
    {
        centroids.resize(groupK);
    }

    std::vector<std::vector<uint8_t>> newCentroids(groupK, std::vector<uint8_t>(dimensions, 0));
    std::vector<int> counts(groupK, 0);
    std::vector<std::vector<float>> sums(groupK, std::vector<float>(dimensions, 0.0f));
    std::vector<int> assignments(endpoints.size());

    std::vector<HANDLE> threadHandles(numThreads, nullptr);
    std::vector<ClusterEventingThreadData*> threadDataPtrs(numThreads, nullptr);
    std::vector<HANDLE> ghStartEvents(numThreads, nullptr);
    std::vector<HANDLE> ghCompleteEvents(numThreads, nullptr);

    int threadsCreated = 0;
    for (int i = 0; i < numThreads; i++) 
    {
        ghStartEvents[i] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!ghStartEvents[i]) 
        {
            Utility::Printf(GACL_Logging_Priority_High, L"ERROR: Failed to create start event for thread %d (Error: %lu)\n", i, GetLastError());
            continue;
        }

        ghCompleteEvents[i] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!ghCompleteEvents[i]) 
        {
            Utility::Printf(GACL_Logging_Priority_High, L"ERROR: Failed to create complete event for thread %d (Error: %lu)\n", i, GetLastError());
            CloseHandle(ghStartEvents[i]);
            ghStartEvents[i] = nullptr;
            continue;
        }

        int endpointThreads = (std::min)(numThreads, static_cast<int>(endpoints.size()));
        size_t endpointStartIdx = i * endpoints.size() / endpointThreads;
        size_t endpointEndIdx = ((endpointStartIdx + endpoints.size() / endpointThreads) < endpoints.size()) ?
            (endpointStartIdx + endpoints.size() / endpointThreads) : endpoints.size();

        int centroidThreads = (std::min)(numThreads, groupK);
        size_t centroidStartIdx = uint64_t(i) * groupK / centroidThreads;
        size_t centroidEndIdx = ((centroidStartIdx + groupK / centroidThreads) < groupK) ?
            (centroidStartIdx + groupK / centroidThreads) : groupK;

        if (endpointStartIdx >= endpointEndIdx) 
        {
            CloseHandle(ghStartEvents[i]);
            CloseHandle(ghCompleteEvents[i]);
            ghStartEvents[i] = nullptr;
            ghCompleteEvents[i] = nullptr;
            continue;
        }

        ClusterEventingThreadData* threadData = new ClusterEventingThreadData
        {
            &endpoints,
            &centroids,
            &newCentroids,
            &assignments,
            endpointStartIdx,
            endpointEndIdx,
            centroidStartIdx,
            centroidEndIdx,
            groupK,
            &counts,
            &sums,
            &stillClustering,
            i,
            ghStartEvents,
            ghCompleteEvents,
            dimensions
        };
        threadDataPtrs[i] = threadData;

        threadHandles[i] = CreateThread(nullptr, 0, ClusterEventThreadProc, threadData, 0, nullptr);
        if (!threadHandles[i]) 
        {
            Utility::Printf(GACL_Logging_Priority_High, L"ERROR: Failed to create thread %d (Error: %lu)\n", i, GetLastError());
            delete threadData;
            threadDataPtrs[i] = nullptr;
            CloseHandle(ghStartEvents[i]);
            CloseHandle(ghCompleteEvents[i]);
            ghStartEvents[i] = nullptr;
            ghCompleteEvents[i] = nullptr;
            continue;
        }

        threadsCreated++;
    }

    if (threadsCreated == 0) 
    {
        Utility::Printf(GACL_Logging_Priority_High, L"ERROR: No threads created! Cannot proceed.\n");
        return result;
    }

    for (int iter = 0; iter < iterations; iter++) 
    {
        for (int i = 0; i < numThreads; i++) 
        {
            if (ghStartEvents[i]) 
            {
                SetEvent(ghStartEvents[i]);
            }
        }

        std::vector<HANDLE> validHandles;
        for (HANDLE h : ghCompleteEvents) 
        {
            if (h) validHandles.push_back(h);
        }

        if (!validHandles.empty()) 
        {
            // 10 sec timeout
            DWORD waitResult = WaitForMultipleObjects(static_cast<DWORD>(validHandles.size()), validHandles.data(), TRUE, CLUSTER_EVENT_TIMEOUT_MS);
            if (waitResult == WAIT_TIMEOUT) 
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"WARNING: Timeout waiting for threads!\n");
            }
        }

        for (auto& centroid : newCentroids)
        {
            std::fill(centroid.begin(), centroid.end(), uint8_t{ 0 });
        }
        std::fill(counts.begin(), counts.end(), 0);
        for (auto& sum : sums)
        {
            std::fill(sum.begin(), sum.end(), 0.0f);
        }

        // Accumulate sums and counts (main thread)
        for (size_t i = 0; i < endpoints.size(); i++) 
        {
            int cluster = assignments[i];
            if (cluster < 0 || cluster >= groupK) 
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: Invalid assignment[%zu] = %d (should be 0-%d)\n", i, cluster, groupK - 1);
                continue;
            }
            counts[cluster]++;
            for (int dim = 0; dim < dimensions; dim++) 
            {
                sums[cluster][dim] += static_cast<float>(endpoints[i][dim]);
            }
        }

        // Signal threads to update centroids
        for (int i = 0; i < numThreads; i++) 
        {
            if (ghStartEvents[i]) 
            {
                SetEvent(ghStartEvents[i]);
            }
        }

        if (!validHandles.empty()) 
        {
            DWORD waitResult = WaitForMultipleObjects(static_cast<DWORD>(validHandles.size()), validHandles.data(), TRUE, CLUSTER_EVENT_TIMEOUT_MS);
            if (waitResult == WAIT_TIMEOUT) 
            {
                Utility::Printf(GACL_Logging_Priority_Medium, L"WARNING: Timeout waiting for threads!\n");
            }
        }

        // Check for convergence
        bool converged = true;
        for (size_t i = 0; i < groupK; i++) 
        {
            for (size_t j = 0; j < dimensions; j++) 
            {
                if (centroids[i][j] != newCentroids[i][j]) 
                {
                    converged = false;
                    break;
                }
            }
            if (!converged) break;
        }

        if (converged) 
        {
            break;
        }

        centroids = newCentroids;
    }

    stillClustering = false;
    for (int i = 0; i < numThreads; i++) 
    {
        if (ghStartEvents[i]) 
        {
            SetEvent(ghStartEvents[i]);
        }
    }

    std::vector<HANDLE> validThreadHandles;
    for (HANDLE h : threadHandles) {
        if (h) validThreadHandles.push_back(h);
    }
    if (!validThreadHandles.empty()) {
        WaitForMultipleObjects(static_cast<DWORD>(validThreadHandles.size()), validThreadHandles.data(), TRUE, CLUSTER_EVENT_TIMEOUT_MS);
    }

    // Cleanup
    for (int i = 0; i < numThreads; i++) 
    {
        if (threadHandles[i]) 
        {
            CloseHandle(threadHandles[i]);
        }
        if (ghStartEvents[i]) 
        {
            CloseHandle(ghStartEvents[i]);
        }
        if (ghCompleteEvents[i]) 
        {
            CloseHandle(ghCompleteEvents[i]);
        }
        if (threadDataPtrs[i]) 
        {
            delete threadDataPtrs[i];
        }
    }

    // Assignment
    std::vector<int> finalAssignments = AssignClustersParallelized(endpoints, centroids, numThreads);

    for (size_t i = 0; i < endpoints.size(); i++) 
    {
        result[i] = centroids[finalAssignments[i]];
    }

    return result;
}

std::vector<std::vector<uint8_t>> KMeansRDO::InitializeCentroids(
    const std::vector<std::vector<uint8_t>>& data,
    int k,
    bool plusplus,
    float lowerLossBound,
    float upperLossBound)
{
    Utility::Printf(GACL_Logging_Priority_Medium, L"\nInitializing centroids (k=%d, plusplus=%d).\n", k, plusplus);

    std::vector<std::vector<uint8_t>> centroids;

    if (data.empty() || k <= 0)
    {
        Utility::Printf(GACL_Logging_Priority_Medium, L"Warning: data.empty()=%d, k=%d\n", data.empty(), k);
        return centroids;
    }

    std::random_device rd;
    std::mt19937 gen(rd());

    if (plusplus) {
        // Sample a subset for k-means++ to reduce O(n*k) cost
        // Default is 12% sampling; tighter loss bounds = more samples
        const size_t MIN_SAMPLES = 10000;
        float range = upperLossBound - lowerLossBound;
        float samplePercent = (range < 0.02f) ? 0.50f : (range < 0.05f) ? 0.25f : 0.12f;
        const size_t sampleSize = (std::max)(MIN_SAMPLES, static_cast<size_t>(data.size() * samplePercent));
        const std::vector<std::vector<uint8_t>>* samplePtr;
        std::vector<std::vector<uint8_t>> sample;

        if (data.size() > sampleSize) {
            // Sample evenly from both endpoint types (even indices = endpoint0, odd indices = endpoint1)
            // This avoids artifacts from biased sampling
            size_t halfSample = sampleSize / 2;
            size_t numBlocks = data.size() / 2;
            std::uniform_int_distribution<size_t> blockDis(0, numBlocks - 1);
            sample.reserve(sampleSize);

            // Sample from first endpoints (even indices)
            for (size_t i = 0; i < halfSample; i++) {
                size_t blockIdx = blockDis(gen);
                sample.push_back(data[blockIdx * 2]);
            }
            // Sample from second endpoints (odd indices)
            for (size_t i = 0; i < halfSample; i++) {
                size_t blockIdx = blockDis(gen);
                sample.push_back(data[blockIdx * 2 + 1]);
            }

            samplePtr = &sample;
            Utility::Printf(GACL_Logging_Priority_Medium, L"Sampled %zu points (%.0f%%) from %zu for k-means++ centroid initialization.\n", sample.size(), samplePercent * 100.0f, data.size());
        } else {
            samplePtr = &data;
            Utility::Printf(GACL_Logging_Priority_Medium, L"Using all %zu points for k-means++ centroid initialization.\n", data.size());
        }

        const auto& sampleData = *samplePtr;

        // Pick first centroid randomly
        std::uniform_int_distribution<> dis(0, static_cast<int>(sampleData.size()) - 1);
        int firstIdx = dis(gen);
        centroids.push_back(sampleData[firstIdx]);

        // k-means++: weighted random selection based on distance
        std::vector<double> minDistances(sampleData.size(), std::numeric_limits<double>::infinity());

        for (int i = 1; i < k; i++)
        {
            const auto& newCentroid = centroids.back();

            // Update min distances to nearest centroid
            for (size_t j = 0; j < sampleData.size(); j++)
            {
                double dist = 0;
                const auto& point = sampleData[j];
                for (size_t dim = 0; dim < point.size(); dim++)
                {
                    double diff = static_cast<double>(point[dim]) - static_cast<double>(newCentroid[dim]);
                    dist += diff * diff;
                }
                minDistances[j] = (std::min)(minDistances[j], dist);
            }

            // Weighted random selection
            const double epsilon = 1e-10;
            std::vector<double> probabilities;
            probabilities.reserve(minDistances.size());
            for (double dist : minDistances)
            {
                probabilities.push_back((std::max)(dist, epsilon));
            }

            std::discrete_distribution<> weightedDis(probabilities.begin(), probabilities.end());
            int idx = weightedDis(gen);
            centroids.push_back(sampleData[idx]);
        }
    }
    else 
    {
        std::uniform_int_distribution<> dis(0, static_cast<int>(data.size()) - 1);
        centroids.reserve(k);
        for (int i = 0; i < k; i++) 
        {
            int idx = dis(gen);
            centroids.push_back(data[idx]);
        }
    }

    Utility::Printf(GACL_Logging_Priority_Medium, L"%zu centroids initialized.\n\n", centroids.size());
    return centroids;
}

struct AssignClustersThreadData {
    const std::vector<std::vector<uint8_t>>* data;
    const std::vector<std::vector<uint8_t>>* centroids;
    std::vector<int>* assignments;
    size_t startIdx;
    size_t endIdx;
    int threadInd;
};

static DWORD WINAPI AssignClustersThreadProc(LPVOID lpParam) 
{
    AssignClustersThreadData* threadData = static_cast<AssignClustersThreadData*>(lpParam);
    const auto& data = *(threadData->data);
    const auto& centroids = *(threadData->centroids);
    auto& assignments = *(threadData->assignments);

    for (size_t i = threadData->startIdx; i < threadData->endIdx; i++) 
    {
        int bestCluster = 0;
        double minDistance = std::numeric_limits<double>::infinity();

        for (size_t j = 0; j < centroids.size(); j++) 
        {
            double distance = 0;
            for (size_t dim = 0; dim < data[i].size(); dim++) 
            {
                double diff = static_cast<double>(data[i][dim]) - static_cast<double>(centroids[j][dim]);
                distance += diff * diff;
            }
            if (distance < minDistance) 
            {
                minDistance = distance;
                bestCluster = static_cast<int>(j);
            }
        }
        assignments[i] = bestCluster;
    }

    return 0;
}

std::vector<int> KMeansRDO::AssignClustersParallelized(
    const std::vector<std::vector<uint8_t>>& data,
    const std::vector<std::vector<uint8_t>>& centroids,
    int numThreads)
{
    if (numThreads <= 0) 
    { 
        numThreads = 1; 
    }
    size_t dataSize = data.size();
    std::vector<int> assignments(dataSize);

    if (dataSize == 0 || centroids.size() == 0) 
    {
        return assignments;
    }

    std::vector<HANDLE> threadHandles(numThreads);
    std::vector<AssignClustersThreadData*> threadDataPtrs(numThreads);

    size_t chunkSize = (dataSize + numThreads - 1) / numThreads;

    for (int i = 0; i < numThreads; i++) 
    {
        size_t startIdx = i * chunkSize;
        size_t endIdx = ((startIdx + chunkSize) < dataSize) ? (startIdx + chunkSize) : dataSize;

        if (startIdx >= endIdx) 
        {
            threadHandles[i] = nullptr;
            threadDataPtrs[i] = nullptr;
            continue;
        }

        AssignClustersThreadData* threadData = new AssignClustersThreadData
        {
            &data, &centroids, &assignments, startIdx, endIdx, i
        };

        threadDataPtrs[i] = threadData;
        threadHandles[i] = CreateThread(nullptr, 0, AssignClustersThreadProc, threadData, 0, nullptr);
    }

    std::vector<HANDLE> validHandles;
    for (HANDLE h : threadHandles) {
        if (h) validHandles.push_back(h);
    }
    if (!validHandles.empty()) {
        WaitForMultipleObjects(static_cast<DWORD>(validHandles.size()), validHandles.data(), TRUE, INFINITE);
    }

    for (int i = 0; i < numThreads; i++) {
        if (threadHandles[i]) {
            CloseHandle(threadHandles[i]);
        }
        if (threadDataPtrs[i]) {
            delete threadDataPtrs[i];
        }
    }

    return assignments;
}

std::vector<std::vector<uint8_t>> KMeansRDO::ExtractBC1Endpoints(
    const uint8_t* encodedData,
    uint32_t numBlocks,
    std::vector<int>& outModes)
{
    std::vector<std::vector<uint8_t>> endpoints;
    endpoints.reserve(size_t(numBlocks) * 2);
    outModes.resize(numBlocks);

    const size_t srcSize = size_t(numBlocks) * 8;
    std::vector<uint8_t> shuffled(srcSize);

    {
        const uint8_t* src = encodedData;
        uint8_t* d1 = shuffled.data();
        uint8_t* d2 = shuffled.data() + (srcSize / 4);
        uint8_t* d3 = shuffled.data() + (srcSize / 2);

        size_t size = srcSize;
        do {
            *d1++ = *src++; *d1++ = *src++;
            *d2++ = *src++; *d2++ = *src++;
            *d3++ = *src++; *d3++ = *src++;
            *d3++ = *src++; *d3++ = *src++;
        } while (size -= 8);
    }

    const uint8_t* e0Ptr = shuffled.data();
    const uint8_t* e1Ptr = shuffled.data() + (srcSize / 4);

    for (uint32_t i = 0; i < numBlocks; ++i)
    {
        uint16_t c0, c1;
        std::memcpy(&c0, e0Ptr, 2);
        std::memcpy(&c1, e1Ptr, 2);

        outModes[i] = (c0 > c1) ? 1 : (c0 < c1 ? 2 : 3);

        const uint8_t r0_5 = uint8_t((c0 >> 11) & 31);
        const uint8_t g0_6 = uint8_t((c0 >> 5) & 63);
        const uint8_t b0_5 = uint8_t((c0) & 31);

        const uint8_t r1_5 = uint8_t((c1 >> 11) & 31);
        const uint8_t g1_6 = uint8_t((c1 >> 5) & 63);
        const uint8_t b1_5 = uint8_t((c1) & 31);

        endpoints.push_back({ uint8_t((r0_5 * 255 + 15) / 31),
                                uint8_t((g0_6 * 255 + 31) / 63),
                                uint8_t((b0_5 * 255 + 15) / 31) });

        endpoints.push_back({ uint8_t((r1_5 * 255 + 15) / 31),
                                uint8_t((g1_6 * 255 + 31) / 63),
                                uint8_t((b1_5 * 255 + 15) / 31) });

        e0Ptr += 2;
        e1Ptr += 2;
    }

    return endpoints;
}

static inline uint32_t RemapIndicesMode1(uint32_t idx32)
{
    uint32_t out = 0;
    for (int p = 0; p < 16; ++p)
    {
        uint32_t v = (idx32 >> (p * 2)) & 3u;
        switch (v)
        {
        case 0:
            v = 1;
            break;
        case 1:
            v = 0;
            break;

        case 2:
            v = 3;
            break;

        case 3:
            v = 2;
            break;
        }

        out |= (v << (p * 2));
    }
    return out;
}

static inline uint32_t RemapIndicesMode2(uint32_t idx32)
{
    uint32_t out = 0;
    for (int p = 0; p < 16; ++p)
    {
        uint32_t v = (idx32 >> (p * 2)) & 3u;
        if (v == 0)
        {
            v = 1;
        }
        else if (v == 1)
        {
            v = 0;
        }
        // 2,3 unchanged ,3 stays transparent
        out |= (v << (p * 2));
    }
    return out;
}

void KMeansRDO::ApplyBC1Endpoints(
    uint8_t* encodedData,
    uint32_t numBlocks,
    const std::vector<std::vector<uint8_t>>& endpoints,
    const std::vector<int>& modes)
{
    const size_t srcSize = size_t(numBlocks) * 8;
    std::vector<uint8_t> shuffled(srcSize);


    {
        const uint8_t* src = encodedData;
        uint8_t* d1 = shuffled.data();
        uint8_t* d2 = shuffled.data() + (srcSize / 4);
        uint8_t* d3 = shuffled.data() + (srcSize / 2);

        size_t size = srcSize;
        do {
            *d1++ = *src++; *d1++ = *src++;
            *d2++ = *src++; *d2++ = *src++;
            *d3++ = *src++; *d3++ = *src++;
            *d3++ = *src++; *d3++ = *src++;
        } while (size -= 8);
    }

    uint8_t* e0Ptr = shuffled.data();
    uint8_t* e1Ptr = shuffled.data() + (srcSize / 4);
    uint8_t* indicesPtr = shuffled.data() + (srcSize / 2);

    for (uint32_t i = 0; i < numBlocks; ++i)
    {
        const auto& ep0 = endpoints[uint64_t(i) * 2 + 0];
        const auto& ep1 = endpoints[uint64_t(i) * 2 + 1];

        uint16_t c0 = uint16_t(((ep0[0] * 31 + 127) / 255) << 11
            | ((ep0[1] * 63 + 127) / 255) << 5
            | ((ep0[2] * 31 + 127) / 255));
        uint16_t c1 = uint16_t(((ep1[0] * 31 + 127) / 255) << 11
            | ((ep1[1] * 63 + 127) / 255) << 5
            | ((ep1[2] * 31 + 127) / 255));

        uint32_t idx; 
        std::memcpy(&idx, indicesPtr, 4);

        switch (modes[i])
        {
            case 1: 
            {

                if (c0 < c1)
                {
                    std::swap(c0, c1);
                    idx = RemapIndicesMode1(idx);
                }

                else if (c0 == c1)
                {
                    if (c0 == 0)
                    {
                        c0 = 1;
                    }
                    else
                    {
                        c1 = 0;
                    }
                    idx = 0x00000000u;
                }
            }
            break;

            case 2:
            {

                if (c0 > c1)
                {
                    std::swap(c0, c1);
                    idx = RemapIndicesMode2(idx);
                }

                else if (c0 == c1)
                {
                    if (c1 == 0)
                    {
                        c1 = 1;
                    }
                    else
                    {
                        c0 = 0;
                    }
                    //make all pixels use index 1
                    idx = 0x55555555u;
                }
            }
            break;

        case 3:
            c1 = c0;
            idx = 0x00000000u;

            break;
        }

        std::memcpy(e0Ptr, &c0, 2);
        std::memcpy(e1Ptr, &c1, 2);
        std::memcpy(indicesPtr, &idx, 4);

        e0Ptr += 2;
        e1Ptr += 2;
        indicesPtr += 4;
    }

    {
        const uint8_t* s1 = shuffled.data();
        const uint8_t* s2 = shuffled.data() + (srcSize / 4);
        const uint8_t* s3 = shuffled.data() + (srcSize / 2);
        uint8_t* dPtr = encodedData;

        size_t size = srcSize;
        do {
            *dPtr++ = *s1++; *dPtr++ = *s1++;
            *dPtr++ = *s2++; *dPtr++ = *s2++;
            *dPtr++ = *s3++; *dPtr++ = *s3++;
            *dPtr++ = *s3++; *dPtr++ = *s3++;
        } while (size -= 8);
    }
}

#endif