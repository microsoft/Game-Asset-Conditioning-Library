//-------------------------------------------------------------------------------------
// KmeansRDO.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#if GACL_INCLUDE_CLER

#include <cstdint>
#include <d3d11.h>

#include <vector>
#include "LossMetrics.h"

namespace KMeansRDO
{
    std::vector<std::vector<uint8_t>> InitializeCentroids(
        const std::vector<std::vector<uint8_t>>& data,
        int k,
        bool plusplus,
        float lowerLossBound,
        float upperLossBound);

    std::vector<std::vector<uint8_t>> ClusterEventing(
        const std::vector<std::vector<uint8_t>>& endpoints,
        int k,
        int iterations,
        int numThreads,
        std::vector<std::vector<uint8_t>> centroids);

    std::vector<int> AssignClustersParallelized(
        const std::vector<std::vector<uint8_t>>& data,
        const std::vector<std::vector<uint8_t>>& centroids,
        int numThreads = 8);

    std::vector<std::vector<uint8_t>> ClusterRDOWithLoss(
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
        Ort::Session* onnxModelPtr);

    void ApplyBC1Endpoints(
        uint8_t* encodedData,
        uint32_t numBlocks,
        const std::vector<std::vector<uint8_t>>& endpoints,
        const std::vector<int>& modes);

    std::vector<std::vector<uint8_t>> ExtractBC1Endpoints(
        const uint8_t* encodedData,
        uint32_t numBlocks,
        std::vector<int>& outModes);

};
#endif