//-------------------------------------------------------------------------------------
// LossMetrics.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#if GACL_INCLUDE_CLER

#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>

// LossMetrics.h - Implements various loss metrics
class LossMetrics {
public:
    enum class Metric {
        MSE,
        RMSE,
        VGG,
        LPIPS
    };

    // Returns true if the metric requires ONNX Runtime
    static bool requiresOnnx(Metric metric);

    // Calculate loss using ONNX tensors (required for perceptual metrics)
    static float CalculateLoss(
        const Ort::Value& bcTensor,
        const Ort::Value& refTensor,
        Metric metric,
		Ort::Session* onnxModelPtr = nullptr);

    // Calculate loss using raw float, no ORT needed
    static float CalculateLoss(
        const float* bcData,
        const float* refData,
        size_t numel,
        Metric metric);

    static std::wstring ToString(Metric metric);
        
private:
    static float CalculateMSE(const float* bcData, const float* refData, size_t numel);
    static float CalculateRMSE(const float* bcData, const float* refData, size_t numel);

    static float CalculateUsingModel(
    //can be used for LPIPS or VGG
        const Ort::Value& bcTensor,
        const Ort::Value& referenceTensor,
        Ort::Session* onnxModel);
};

#endif

