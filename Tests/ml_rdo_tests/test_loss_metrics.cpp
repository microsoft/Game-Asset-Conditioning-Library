//-------------------------------------------------------------------------------------
// test_loss_metrics.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#if GACL_INCLUDE_CLER

#define ORT_API_MANUAL_INIT

#include <gtest/gtest.h>
#include <onnxruntime_c_api.h>
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <cmath>
#include <limits>
#include <filesystem>
#include <windows.h>

#include "../../lib/RDO_ML/LossMetrics.h"
#include <gacl.h>

namespace loss_metrics_tests {

    bool initializedORT = initORT();

    // Helper to get executable directory
    std::filesystem::path GetExeDirectory()
    {
        wchar_t exePathBuf[MAX_PATH];
        GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
        return std::filesystem::path(exePathBuf).parent_path();
    }

    // Load LPIPS model
    Ort::Session* LoadLPIPSModel()
    {
		if (!initORT()) {
			return nullptr;
		}

        static std::unique_ptr<Ort::Session> s_lpipsModel;
        static std::unique_ptr<Ort::Env> s_ortEnv;

        if (s_lpipsModel) {
            return s_lpipsModel.get();
        }

        if (!s_ortEnv) {
            s_ortEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "LossMetricsTests");
        }

        auto exeDir = GetExeDirectory();
        auto modelPath = exeDir / L"lpips_model.onnx";

        if (!std::filesystem::exists(modelPath)) {
            return nullptr;
        }

        try {
            Ort::SessionOptions sessionOptions;
            sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            // Try CUDA, fallback to CPU
            try {
                OrtCUDAProviderOptions cuda_options;
                cuda_options.device_id = 0;
                sessionOptions.AppendExecutionProvider_CUDA(cuda_options);
            }
            catch (...) {
                // CUDA not available, use CPU
            }

            s_lpipsModel = std::make_unique<Ort::Session>(*s_ortEnv, modelPath.c_str(), sessionOptions);
            return s_lpipsModel.get();
        }
        catch (const Ort::Exception& e) {
            GTEST_LOG_(WARNING) << "Failed to load LPIPS model: " << e.what();
            return nullptr;
        }
    }

    // Load VGG model 
    Ort::Session* LoadVGGModel()
    {
        if (!initORT()) {
            return nullptr;
        }

        static std::unique_ptr<Ort::Session> s_vggModel;
        static std::unique_ptr<Ort::Env> s_ortEnv;

        if (s_vggModel) {
            return s_vggModel.get();
        }

        if (!s_ortEnv) {
            s_ortEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "LossMetricsTests");
        }

        auto exeDir = GetExeDirectory();
        auto modelPath = exeDir / L"vgg_model.onnx";

        if (!std::filesystem::exists(modelPath)) {
            return nullptr;
        }

        try {
            Ort::SessionOptions sessionOptions;
            sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            // Try CUDA, fallback to CPU
            try {
                OrtCUDAProviderOptions cuda_options;
                cuda_options.device_id = 0;
                sessionOptions.AppendExecutionProvider_CUDA(cuda_options);
            }
            catch (...) {
                // CUDA not available, use CPU
            }

            s_vggModel = std::make_unique<Ort::Session>(*s_ortEnv, modelPath.c_str(), sessionOptions);
            return s_vggModel.get();
        }
        catch (const Ort::Exception& e) {
            GTEST_LOG_(WARNING) << "Failed to load VGG model: " << e.what();
            return nullptr;
        }
    }

    // Helper function to create test tensors with specific values
    Ort::Value CreateTestTensor(const std::vector<float>& data, const std::vector<int64_t>& shape)
    {
        if (!initORT()) {
            return nullptr;
        }

        Ort::AllocatorWithDefaultOptions allocator;

        // Calculate total elements
        size_t numel = 1;
        for (auto dim : shape) {
            numel *= static_cast<size_t>(dim);
        }

        // Verify data size matches shape
        if (data.size() != numel) {
            throw std::runtime_error("Data size doesn't match shape");
        }

        // Create tensor
        Ort::Value tensor = Ort::Value::CreateTensor<float>(
            allocator,
            shape.data(),
            shape.size()
        );

        // Copy data
        float* tensor_data = tensor.GetTensorMutableData<float>();
        std::memcpy(tensor_data, data.data(), data.size() * sizeof(float));

        return tensor;
    }

    //---------------------------------------------------------------------
    // CalculateMSE Tests (via CalculateLoss with Metric::MSE)
    //---------------------------------------------------------------------

    TEST(LossMetrics, CalculateMSE_IdenticalTensors)
    {
        // Two identical tensors should have MSE = 0
        std::vector<float> data = {0.5f, 0.6f, 0.7f, 0.8f};
        std::vector<int64_t> shape = {1, 1, 2, 2}; // 1x1x2x2 tensor (NCHW format)

        Ort::Value tensor1 = CreateTestTensor(data, shape);
        Ort::Value tensor2 = CreateTestTensor(data, shape);

        float mse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);

        EXPECT_FLOAT_EQ(mse, 0.0f);
    }

    TEST(LossMetrics, CalculateMSE_KnownDifference_SinglePixel)
    {
        // Test with a known difference
        // Tensor1: [0.5, 0.5, 0.5, 0.5]
        // Tensor2: [0.6, 0.5, 0.5, 0.5]
        // Difference: [0.1, 0.0, 0.0, 0.0]
        // Squared: [0.01, 0.0, 0.0, 0.0]
        // MSE = 0.01 / 4 = 0.0025

        std::vector<float> data1 = {0.5f, 0.5f, 0.5f, 0.5f};
        std::vector<float> data2 = {0.6f, 0.5f, 0.5f, 0.5f};
        std::vector<int64_t> shape = {1, 1, 2, 2};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float mse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);

        EXPECT_NEAR(mse, 0.0025f, 1e-6f);
    }

    TEST(LossMetrics, CalculateMSE_KnownDifference_AllPixels)
    {
        // All pixels differ by 0.1
        // Error = 0.1, Squared = 0.01
        // MSE = 0.01 (same for all pixels)

        std::vector<float> data1 = {0.0f, 0.1f, 0.2f, 0.3f};
        std::vector<float> data2 = {0.1f, 0.2f, 0.3f, 0.4f};
        std::vector<int64_t> shape = {1, 1, 2, 2};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float mse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);

        EXPECT_NEAR(mse, 0.01f, 1e-6f);
    }

    TEST(LossMetrics, CalculateMSE_AllZeros)
    {
        // Both tensors all zeros
        std::vector<float> data(16, 0.0f);
        std::vector<int64_t> shape = {1, 1, 4, 4};

        Ort::Value tensor1 = CreateTestTensor(data, shape);
        Ort::Value tensor2 = CreateTestTensor(data, shape);

        float mse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);

        EXPECT_FLOAT_EQ(mse, 0.0f);
    }

    TEST(LossMetrics, CalculateMSE_MaxDifference)
    {
        // One tensor all 0.0, other all 1.0
        // Difference = 1.0, Squared = 1.0
        // MSE = 1.0

        std::vector<float> data1(16, 0.0f);
        std::vector<float> data2(16, 1.0f);
        std::vector<int64_t> shape = {1, 1, 4, 4};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float mse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);

        EXPECT_NEAR(mse, 1.0f, 1e-6f);
    }

    TEST(LossMetrics, CalculateMSE_NegativeValues)
    {
        // Test with negative values (can occur after normalization)
        // [-1.0, -0.5] vs [1.0, 0.5]
        // Differences: [-2.0, -1.0]
        // Squared: [4.0, 1.0]
        // MSE = 5.0 / 2 = 2.5

        std::vector<float> data1 = {-1.0f, -0.5f};
        std::vector<float> data2 = {1.0f, 0.5f};
        std::vector<int64_t> shape = {1, 1, 1, 2};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float mse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);

        EXPECT_NEAR(mse, 2.5f, 1e-6f);
    }

    TEST(LossMetrics, CalculateMSE_LargerImage)
    {
        // Test with 8x8 image (64 pixels)
        // All pixels in tensor1 = 0.5
        // All pixels in tensor2 = 0.7
        // Difference = 0.2, Squared = 0.04
        // MSE = 0.04

        std::vector<float> data1(64, 0.5f);
        std::vector<float> data2(64, 0.7f);
        std::vector<int64_t> shape = {1, 1, 8, 8};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float mse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);

        EXPECT_NEAR(mse, 0.04f, 1e-6f);
    }

    TEST(LossMetrics, CalculateMSE_MultiChannel)
    {
        // Test with RGB image (3 channels, 2x2 pixels = 12 values)
        // Channel layout: NCHW (batch, channels, height, width)

        std::vector<float> data1 = {
            // Channel 0 (R)
            0.0f, 0.1f,
            0.2f, 0.3f,
            // Channel 1 (G)
            0.4f, 0.5f,
            0.6f, 0.7f,
            // Channel 2 (B)
            0.8f, 0.9f,
            1.0f, 1.0f
        };

        std::vector<float> data2 = {
            // Channel 0 (R)
            0.1f, 0.2f,
            0.3f, 0.4f,
            // Channel 1 (G)
            0.5f, 0.6f,
            0.7f, 0.8f,
            // Channel 2 (B)
            0.9f, 1.0f,
            1.0f, 1.0f  // Last pixel same
        };

        std::vector<int64_t> shape = {1, 3, 2, 2};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        // Channel differences:
        // R (4 pixels): all differ by 0.1
        // G (4 pixels): all differ by 0.1
        // B (4 pixels): 2 differ by 0.1, 2 are same
        // Total: 10 pixels with diff 0.1 (squared = 0.01), 2 pixels same (squared = 0.0)
        // MSE = (10 * 0.01 + 2 * 0.0) / 12 = 0.1 / 12 ≈ 0.00833333

        float mse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);

        EXPECT_NEAR(mse, 0.00833333f, 1e-6f);
    }

    TEST(LossMetrics, CalculateMSE_DifferentShapes_ThrowsException)
    {
        // Mismatched shapes should throw exception
        std::vector<float> data1(4, 0.5f);
        std::vector<float> data2(9, 0.5f);
        std::vector<int64_t> shape1 = {1, 1, 2, 2};
        std::vector<int64_t> shape2 = {1, 1, 3, 3};

        Ort::Value tensor1 = CreateTestTensor(data1, shape1);
        Ort::Value tensor2 = CreateTestTensor(data2, shape2);

        EXPECT_THROW({
            LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);
        }, std::runtime_error);
    }

    TEST(LossMetrics, CalculateMSE_SymmetricProperty)
    {
        // MSE(A, B) should equal MSE(B, A)
        std::vector<float> data1 = {0.2f, 0.4f, 0.6f, 0.8f};
        std::vector<float> data2 = {0.3f, 0.5f, 0.7f, 0.9f};
        std::vector<int64_t> shape = {1, 1, 2, 2};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float mse1 = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);
        float mse2 = LossMetrics::CalculateLoss(tensor2, tensor1, LossMetrics::Metric::MSE);

        EXPECT_FLOAT_EQ(mse1, mse2);
    }

    TEST(LossMetrics, CalculateMSE_SmallValues)
    {
        // Test with very small values to check numerical stability
        std::vector<float> data1 = {0.001f, 0.002f, 0.003f, 0.004f};
        std::vector<float> data2 = {0.001f, 0.002f, 0.003f, 0.005f};
        std::vector<int64_t> shape = {1, 1, 2, 2};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        // Only last pixel differs: 0.001
        // Squared: 0.000001
        // MSE = 0.000001 / 4 = 0.00000025

        float mse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);

        EXPECT_NEAR(mse, 0.00000025f, 1e-9f);
    }

    //---------------------------------------------------------------------
    // CalculateRMSE Tests (via CalculateLoss with Metric::RMSE)
    //---------------------------------------------------------------------

    TEST(LossMetrics, CalculateRMSE_IdenticalTensors)
    {
        std::vector<float> data = {0.5f, 0.6f, 0.7f, 0.8f};
        std::vector<int64_t> shape = {1, 1, 2, 2};

        Ort::Value tensor1 = CreateTestTensor(data, shape);
        Ort::Value tensor2 = CreateTestTensor(data, shape);

        float rmse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::RMSE);

        EXPECT_FLOAT_EQ(rmse, 0.0f);
    }

    TEST(LossMetrics, CalculateRMSE_KnownDifference)
    {
        // MSE = 0.04 (as tested above)
        // RMSE = sqrt(0.04) = 0.2

        std::vector<float> data1(64, 0.5f);
        std::vector<float> data2(64, 0.7f);
        std::vector<int64_t> shape = {1, 1, 8, 8};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float rmse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::RMSE);

        EXPECT_NEAR(rmse, 0.2f, 1e-6f);
    }

    TEST(LossMetrics, CalculateRMSE_MaxDifference)
    {
        // MSE = 1.0
        // RMSE = sqrt(1.0) = 1.0

        std::vector<float> data1(16, 0.0f);
        std::vector<float> data2(16, 1.0f);
        std::vector<int64_t> shape = {1, 1, 4, 4};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float rmse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::RMSE);

        EXPECT_NEAR(rmse, 1.0f, 1e-6f);
    }

    TEST(LossMetrics, CalculateRMSE_VerifyAgainstMSE)
    {
        // Verify that RMSE = sqrt(MSE)
        std::vector<float> data1 = {0.1f, 0.2f, 0.3f, 0.4f};
        std::vector<float> data2 = {0.15f, 0.25f, 0.35f, 0.45f};
        std::vector<int64_t> shape = {1, 1, 2, 2};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float mse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::MSE);
        float rmse = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::RMSE);

        EXPECT_NEAR(rmse, std::sqrt(mse), 1e-6f);
    }

    TEST(LossMetrics, CalculateRMSE_SymmetricProperty)
    {
        // RMSE(A, B) should equal RMSE(B, A)
        std::vector<float> data1 = {0.2f, 0.4f, 0.6f, 0.8f};
        std::vector<float> data2 = {0.3f, 0.5f, 0.7f, 0.9f};
        std::vector<int64_t> shape = {1, 1, 2, 2};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float rmse1 = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::RMSE);
        float rmse2 = LossMetrics::CalculateLoss(tensor2, tensor1, LossMetrics::Metric::RMSE);

        EXPECT_FLOAT_EQ(rmse1, rmse2);
    }

    //---------------------------------------------------------------------
    // VGG and LPIPS Tests
    // Note: These tests will gracefully handle missing ONNX models
    //---------------------------------------------------------------------

    TEST(LossMetrics, VGG_IdenticalTensors)
    {

        Ort::Session* model = LoadVGGModel();
        if (!model) {
            GTEST_LOG_(INFO) << "VGG model not available (test skipped)";
            return;
        }

        // Use 256x256 images to ensure big enough for model requirements
        std::vector<float> data(3 * 256 * 256, 0.5f); // 1x3x256x256 RGB image
        std::vector<int64_t> shape = {1, 3, 256, 256};

        Ort::Value tensor1 = CreateTestTensor(data, shape);
        Ort::Value tensor2 = CreateTestTensor(data, shape);

        float loss = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::VGG, model);

        // Identical images should have loss of approximately 0
        EXPECT_NEAR(loss, 0.0f, 1e-6f);
    }

    TEST(LossMetrics, VGG_DifferentImages)
    {
        Ort::Session* model = LoadVGGModel();
        if (!model) {
            GTEST_LOG_(INFO) << "VGG model not available (test skipped)";
            return;
        }

        // Use 256x256 images to ensure big enough for model requirements
        std::vector<float> data1(3 * 256 * 256, 0.3f); // Dark image
        std::vector<float> data2(3 * 256 * 256, 0.7f); // Bright image
        std::vector<int64_t> shape = {1, 3, 256, 256};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float loss = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::VGG, model);

        // Different images should have non-zero loss
        EXPECT_GT(loss, 0.0f);
    }

    TEST(LossMetrics, VGG_SymmetricProperty)
    {
        Ort::Session* model = LoadVGGModel();
        if (!model) {
            GTEST_LOG_(INFO) << "VGG model not available (test skipped)";
            return;
        }

        // Use 256x256 images to ensure big enough for model requirements
        std::vector<float> data1(3 * 256 * 256, 0.3f);
        std::vector<float> data2(3 * 256 * 256, 0.7f);
        std::vector<int64_t> shape = {1, 3, 256, 256};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float loss1 = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::VGG, model);
        float loss2 = LossMetrics::CalculateLoss(tensor2, tensor1, LossMetrics::Metric::VGG, model);

        // Should be symmetric (within neural network floating point tolerance)
        // Neural networks can have small numerical variations due to optimization
        EXPECT_NEAR(loss1, loss2, 1e-4f);
    }

    TEST(LossMetrics, VGG_LargerImage)
    {
        Ort::Session* model = LoadVGGModel();
        if (!model) {
            GTEST_LOG_(INFO) << "VGG model not available (test skipped)";
            return;
        }

        // Use 512x512 for a larger test image
        std::vector<float> data1(3 * 512 * 512, 0.4f); // 1x3x512x512
        std::vector<float> data2(3 * 512 * 512, 0.6f);
        std::vector<int64_t> shape = {1, 3, 512, 512};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float loss = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::VGG, model);

        // Should produce valid loss value
        EXPECT_GE(loss, 0.0f);
        EXPECT_LT(loss, 10.0f); // Reasonable upper bound
    }

    TEST(LossMetrics, LPIPS_IdenticalTensors)
    {
        Ort::Session* model = LoadLPIPSModel();
        if (!model) {
            GTEST_LOG_(INFO) << "LPIPS model not available (test skipped)";
            return;
        }

        // Use 256x256 images to ensure big enough for model requirements
        std::vector<float> data(3 * 256 * 256, 0.5f); // 1x3x256x256 RGB image
        std::vector<int64_t> shape = {1, 3, 256, 256};

        Ort::Value tensor1 = CreateTestTensor(data, shape);
        Ort::Value tensor2 = CreateTestTensor(data, shape);

        float loss = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::LPIPS, model);

        // Identical images should have loss of approximately 0
        EXPECT_NEAR(loss, 0.0f, 1e-6f);
    }

    TEST(LossMetrics, LPIPS_DifferentImages)
    {
        Ort::Session* model = LoadLPIPSModel();
        if (!model) {
            GTEST_LOG_(INFO) << "LPIPS model not available (test skipped)";
            return;
        }

        // Use 256x256 images to ensure big enough for model requirements
        std::vector<float> data1(3 * 256 * 256, 0.3f); // Dark image
        std::vector<float> data2(3 * 256 * 256, 0.7f); // Bright image
        std::vector<int64_t> shape = {1, 3, 256, 256};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float loss = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::LPIPS, model);

        // Different images should have non-zero loss
        EXPECT_GT(loss, 0.0f);
    }

    TEST(LossMetrics, LPIPS_SymmetricProperty)
    {
        Ort::Session* model = LoadLPIPSModel();
        if (!model) {
            GTEST_LOG_(INFO) << "LPIPS model not available (test skipped)";
            return;
        }

        // Use 256x256 images to ensure big enough for model requirements
        std::vector<float> data1(3 * 256 * 256, 0.3f);
        std::vector<float> data2(3 * 256 * 256, 0.7f);
        std::vector<int64_t> shape = {1, 3, 256, 256};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float loss1 = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::LPIPS, model);
        float loss2 = LossMetrics::CalculateLoss(tensor2, tensor1, LossMetrics::Metric::LPIPS, model);

        // Should be symmetric (within neural network floating point tolerance)
        // Neural networks can have small numerical variations due to optimization
        EXPECT_NEAR(loss1, loss2, 1e-4f);
    }

    TEST(LossMetrics, LPIPS_LargerImage)
    {
        Ort::Session* model = LoadLPIPSModel();
        if (!model) {
            GTEST_LOG_(INFO) << "LPIPS model not available (test skipped)";
            return;
        }

        // Use 512x512 for a larger test image
        std::vector<float> data1(3 * 512 * 512, 0.4f); // 1x3x512x512
        std::vector<float> data2(3 * 512 * 512, 0.6f);
        std::vector<int64_t> shape = {1, 3, 512, 512};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float loss = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::LPIPS, model);

        // Should produce valid loss value
        EXPECT_GE(loss, 0.0f);
        EXPECT_LT(loss, 10.0f); // Reasonable upper bound
    }

    TEST(LossMetrics, LPIPS_GradualDifference)
    {
        Ort::Session* model = LoadLPIPSModel();
        if (!model) {
            GTEST_LOG_(INFO) << "LPIPS model not available (test skipped)";
            return;
        }

        // Use 256x256 images to ensure big enough for model requirements
        std::vector<int64_t> shape = {1, 3, 256, 256};
        std::vector<float> reference(3 * 256 * 256, 0.5f);

        Ort::Value ref_tensor = CreateTestTensor(reference, shape);

        // Small difference
        std::vector<float> small_diff(3 * 256 * 256, 0.55f);
        Ort::Value small_tensor = CreateTestTensor(small_diff, shape);
        float small_loss = LossMetrics::CalculateLoss(ref_tensor, small_tensor, LossMetrics::Metric::LPIPS, model);

        // Large difference
        std::vector<float> large_diff(3 * 256 * 256, 0.8f);
        Ort::Value large_tensor = CreateTestTensor(large_diff, shape);
        float large_loss = LossMetrics::CalculateLoss(ref_tensor, large_tensor, LossMetrics::Metric::LPIPS, model);

        // Larger difference should have larger loss
        EXPECT_GT(large_loss, small_loss);
    }

    TEST(LossMetrics, VGG_GradualDifference)
    {
        Ort::Session* model = LoadVGGModel();
        if (!model) {
            GTEST_LOG_(INFO) << "VGG model not available (test skipped)";
            return;
        }

        // Use 256x256 images to ensure big enough for model requirements
        std::vector<int64_t> shape = {1, 3, 256, 256};
        std::vector<float> reference(3 * 256 * 256, 0.5f);

        Ort::Value ref_tensor = CreateTestTensor(reference, shape);

        // Small difference
        std::vector<float> small_diff(3 * 256 * 256, 0.55f);
        Ort::Value small_tensor = CreateTestTensor(small_diff, shape);
        float small_loss = LossMetrics::CalculateLoss(ref_tensor, small_tensor, LossMetrics::Metric::VGG, model);

        // Large difference
        std::vector<float> large_diff(3 * 256 * 256, 0.8f);
        Ort::Value large_tensor = CreateTestTensor(large_diff, shape);
        float large_loss = LossMetrics::CalculateLoss(ref_tensor, large_tensor, LossMetrics::Metric::VGG, model);

        // Larger difference should have larger loss
        EXPECT_GT(large_loss, small_loss);
    }

    TEST(LossMetrics, VGG_KnownValue)
    {
        Ort::Session* model = LoadVGGModel();
        if (!model) {
            GTEST_LOG_(INFO) << "VGG model not available (test skipped)";
            return;
        }

        // Test with known input values and verify against expected output
        // Uniform tensors: 0.3 vs 0.7, 256x256 RGB
        // This catches regressions like non-contiguous data, model loading issues, etc.
        std::vector<float> data1(3 * 256 * 256, 0.3f);
        std::vector<float> data2(3 * 256 * 256, 0.7f);
        std::vector<int64_t> shape = {1, 3, 256, 256};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float loss = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::VGG, model);
        float expected = 0.1066f;  // generated from trial run

        GTEST_LOG_(INFO) << "VGG loss for (0.3 vs 0.7): " << loss;
        EXPECT_NEAR(loss, expected, 1e-4f);
    }

    TEST(LossMetrics, LPIPS_KnownValue)
    {
        Ort::Session* model = LoadLPIPSModel();
        if (!model) {
            GTEST_LOG_(INFO) << "LPIPS model not available (test skipped)";
            return;
        }

        // Test with known input values and verify against expected output
        // Uniform tensors: 0.3 vs 0.7, 256x256 RGB
        // This catches regressions like non-contiguous data, model loading issues, etc.
        std::vector<float> data1(3 * 256 * 256, 0.3f);
        std::vector<float> data2(3 * 256 * 256, 0.7f);
        std::vector<int64_t> shape = {1, 3, 256, 256};

        Ort::Value tensor1 = CreateTestTensor(data1, shape);
        Ort::Value tensor2 = CreateTestTensor(data2, shape);

        float loss = LossMetrics::CalculateLoss(tensor1, tensor2, LossMetrics::Metric::LPIPS, model);
        float expected = 0.2369f;  // generated from trial run

        GTEST_LOG_(INFO) << "LPIPS loss for (0.3 vs 0.7): " << loss;
        EXPECT_NEAR(loss, expected, 1e-4f);
    }

    //---------------------------------------------------------------------
    // ToString Tests
    //---------------------------------------------------------------------

    TEST(LossMetrics, ToString_AllMetrics)
    {
        EXPECT_EQ(LossMetrics::ToString(LossMetrics::Metric::MSE), L"MSE");
        EXPECT_EQ(LossMetrics::ToString(LossMetrics::Metric::RMSE), L"RMSE");
        EXPECT_EQ(LossMetrics::ToString(LossMetrics::Metric::VGG), L"VGG");
        EXPECT_EQ(LossMetrics::ToString(LossMetrics::Metric::LPIPS), L"LPIPS");
    }

} // namespace loss_metrics_tests

#endif // GACL_INCLUDE_CLER
