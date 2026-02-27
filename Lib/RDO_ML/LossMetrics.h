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

    // Calculate loss between bc and reference image
    static float CalculateLoss(
        const Ort::Value& bcTensor,
        const Ort::Value& referenceTensor,
        Metric metric,
		Ort::Session* onnxModelPtr = nullptr);

    static std::wstring ToString(Metric metric);
        
private:
    // Internal loss calculation methods
    static float CalculateMSE(
        const Ort::Value& bcTensor,
        const Ort::Value& referenceTensor);
    
    static float CalculateRMSE(
        const Ort::Value& bcTensor,
		const Ort::Value& referenceTensor);

    static float CalculateUsingModel(
    //can be used for LPIPS or VGG
        const Ort::Value& bcTensor,
        const Ort::Value& referenceTensor,
        Ort::Session* onnxModel);
};

#endif

