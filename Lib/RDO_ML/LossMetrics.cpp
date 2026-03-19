//-------------------------------------------------------------------------------------
// LossMetrics.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#if GACL_INCLUDE_CLER

#define ORT_API_MANUAL_INIT

#include "LossMetrics.h"
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>
#include <iostream>

bool LossMetrics::requiresOnnx(Metric metric)
{
    return metric != Metric::MSE && metric != Metric::RMSE;
}

float LossMetrics::CalculateLoss(
    const Ort::Value& bcTensor,
    const Ort::Value& referenceTensor,
    Metric metric,
    Ort::Session* onnxModelPtr)
{
    if (!requiresOnnx(metric))
    {
        auto bcShape = bcTensor.GetTensorTypeAndShapeInfo().GetShape();
        auto refShape = referenceTensor.GetTensorTypeAndShapeInfo().GetShape();
        if (bcShape != refShape) {
            throw std::runtime_error("Input tensors must have the same shape.");
        }
        size_t numel = 1;
        for (auto d : bcShape) numel *= static_cast<size_t>(d);
        return CalculateLoss(
            bcTensor.GetTensorData<float>(),
            referenceTensor.GetTensorData<float>(),
            numel,
            metric);
    }
    else
    {
        return CalculateUsingModel(bcTensor, referenceTensor, onnxModelPtr);
    }
}

float LossMetrics::CalculateLoss(
    const float* decodedData,
    const float* referenceData,
    size_t numel,
    Metric metric)
{
    switch (metric) {
        case Metric::RMSE:
            return CalculateRMSE(decodedData, referenceData, numel);
        case Metric::MSE:
        default:
            return CalculateMSE(decodedData, referenceData, numel);
    }
}

std::wstring LossMetrics::ToString(Metric metric) {
    switch (metric) {
    case Metric::MSE:
        return L"MSE";
    case Metric::RMSE:
        return L"RMSE";
    case Metric::VGG:
        return L"VGG";
    case Metric::LPIPS:
        return L"LPIPS";
    default:
        return L"Unknown";
    }
}

float LossMetrics::CalculateUsingModel(
    const Ort::Value& bcTensorOnDevice,
    const Ort::Value& referenceTensorOnDevice,
    Ort::Session* onnxModel)
{
    //Can be used for LPIPS or VGG

    if (!onnxModel)
    {
        throw std::runtime_error("Model pointer is null!");
    }

    //lambda function for normalizing function
    auto normalizeOnnxTensor = [](const Ort::Value& originalTensor) -> Ort::Value {

        // Get shape and type info
        if (originalTensor.GetTensorMemoryInfo().GetDeviceType() != OrtMemoryInfoDeviceType_CPU) {
            throw std::runtime_error("normalizeOnnxTensor expects CPU-backed tensors");
        }

        auto shape = originalTensor.GetTensorTypeAndShapeInfo().GetShape();
        auto type_info = originalTensor.GetTensorTypeAndShapeInfo();
        if (type_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            throw std::runtime_error("Input tensor must be of type float.");
        }

        //get numel
        size_t numel = 1;
        for (auto d : shape) numel *= static_cast<size_t>(d);
        const float* input_data = originalTensor.GetTensorData<float>();
        if (!input_data) {
            throw std::runtime_error("input_data is nullptr");
        }

        // Create a new tensor using ONNX allocator 
        Ort::AllocatorWithDefaultOptions allocator;
        Ort::Value normalizedOnnxTensor = Ort::Value::CreateTensor<float>(
            allocator, shape.data(), shape.size()
        );

        //normalize
        float* norm_data = normalizedOnnxTensor.GetTensorMutableData<float>();
        for (size_t i = 0; i < numel; ++i) {
            norm_data[i] = input_data[i] * 2.0f - 1.0f;
        }

        return normalizedOnnxTensor;
    };

    //normalize the two input tensors
    Ort::Value bcTensorNorm = normalizeOnnxTensor(bcTensorOnDevice);
    Ort::Value referenceTensorNorm = normalizeOnnxTensor(referenceTensorOnDevice);

    //create a ONNX Runtime allocator for memory management
    Ort::AllocatorWithDefaultOptions allocator;

    // set "input" and "target" input names
    std::array<Ort::Value, 2> input_tensors = { std::move(bcTensorNorm), std::move(referenceTensorNorm) };
    std::array<const char*, 2> input_names = { "input", "target" };

    //set output name "loss"
    std::array<const char*, 1> output_names = { "loss" };

    //run model
    auto output_tensors = onnxModel->Run(
        Ort::RunOptions{ nullptr },
        input_names.data(),
        input_tensors.data(),
        input_tensors.size(),
        output_names.data(),
        output_names.size()
    );

    if (output_tensors.empty()) {
        throw std::runtime_error("ONNX model run failed: no outputs returned.");
    }
    float* output_data = output_tensors[0].GetTensorMutableData<float>();
    if (!output_data) {
        throw std::runtime_error("ONNX model run failed: output data is null.");
    }
    return output_data[0];
}

float LossMetrics::CalculateMSE(
    const float* dataA,
    const float* dataB,
    size_t numel)
{
    double sum = 0.0;
    for (size_t i = 0; i < numel; ++i) {
        double error = static_cast<double>(dataA[i]) - static_cast<double>(dataB[i]);
        sum += error * error;
    }
    return static_cast<float>(sum / numel);
}

float LossMetrics::CalculateRMSE(
    const float* dataA,
    const float* dataB,
    size_t numel)
{
    return std::sqrt(CalculateMSE(dataA, dataB, numel));
}
#endif