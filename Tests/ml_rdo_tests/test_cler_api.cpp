//-------------------------------------------------------------------------------------
// test_cler_api.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#if GACL_INCLUDE_CLER

#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <memory>
#include <DirectXTex.h>

#include <gacl.h>
#include <RDO_ML/ML_RDO.h>

//tests for the CLER API directly
namespace cler_api_tests {

    struct DummyImage
    {
        uint32_t width;
        uint32_t height;
        uint32_t bcElementSize;

        std::vector<uint8_t> bc1Data;
        std::vector<uint8_t> rgbaData;
    };

    DummyImage CreateDummyImage(uint32_t w, uint32_t h)
    {
        DummyImage img;
        img.width = w;
        img.height = h;
        img.bcElementSize = 8;

        const uint32_t blocksX = (w + 3) / 4;
        const uint32_t blocksY = (h + 3) / 4;
        const uint32_t numBlocks = blocksX * blocksY;
        const uint32_t bc1Size = numBlocks * 8;

        img.bc1Data.resize(bc1Size, 0xAA);
        img.rgbaData.resize(w * h * 4, 128);

        return img;
    }

    //sets default parameters for CLER for dummy images
    RDOOptions DefaultOptions()
    {
        RDOOptions opt;
        opt.minClusters = 2;
        opt.maxClusters = 256;
        opt.iterations = 4;
        opt.numThreads = 4;
        opt.usePlusPlus = true;
        opt.metric = RDOLossMetric::MSE;
        return opt;
    }

    bool RDO_IsSuccess(RDO_ErrorCode code)
    {
        if (code == RDO_ErrorCode::OK)
        {
            GTEST_LOG_(INFO) << "[INFO] Advanced RDO was used (ONNX perceptual model).";
        }
        else if (code == RDO_ErrorCode::OK_NoAdvancedRDO)
        {
            GTEST_LOG_(INFO) << "[INFO] Basic RDO used (fallback: no perceptual model).";
        }

        return (code == RDO_ErrorCode::OK || code == RDO_ErrorCode::OK_NoAdvancedRDO);
    }

    //creates dummy image with given dimensions and runs CLER on it
    bool RunCLER(uint32_t w, uint32_t h)
    {
        DummyImage img = CreateDummyImage(w, h);

        RDOOptions opt = DefaultOptions();
        opt.maxClusters = 128;

        RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
            img.bcElementSize,
            img.bc1Data.data(),
            img.rgbaData.data(),
            w,
            h,
            DXGI_FORMAT_BC1_UNORM,
            opt
        );

        return RDO_IsSuccess(code);
    }
    //ensures it fails when null
    TEST(ClerAPI, NullEncodedData)
    {
        RDOOptions opt = DefaultOptions();
        std::vector<uint8_t> ref(16 * 16 * 4);

        RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(8, nullptr, ref.data(), 16, 16, DXGI_FORMAT_BC1_UNORM, opt);
        EXPECT_EQ(code, RDO_ErrorCode::NullEncodedData);
    }
    //ensures it fails when given incorrect bc element size
    TEST(ClerAPI, IncorrectBCElementSize)
    {
        std::vector<uint8_t> fakeBC(16);
        std::vector<uint8_t> fakeRGBA(16 * 16 * 4);
        RDOOptions opt = DefaultOptions();

        RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(16, fakeBC.data(), fakeRGBA.data(), 16, 16, DXGI_FORMAT_BC1_UNORM, opt);
        EXPECT_EQ(code, RDO_ErrorCode::InvalidBCElementSize);
    }
    //ensures it fails when zero dimensions
    TEST(ClerAPI, ZeroDimensions)
    {RDOOptions opt = DefaultOptions();
        std::vector<uint8_t> fakeBC(16);
        std::vector<uint8_t> fakeRGBA(16 * 16 * 4);

        RDO_ErrorCode code1 = GACL_RDO_ComponentLevelEntropyReduce(8, fakeBC.data(), fakeRGBA.data(), 0, 16, DXGI_FORMAT_BC1_UNORM, opt);
        EXPECT_EQ(code1, RDO_ErrorCode::InvalidImageDimensions);

        RDO_ErrorCode code2 = GACL_RDO_ComponentLevelEntropyReduce(8, fakeBC.data(), fakeRGBA.data(), 16, 0, DXGI_FORMAT_BC1_UNORM, opt);
        EXPECT_EQ(code2, RDO_ErrorCode::InvalidImageDimensions);
    }

    //runs CLER on dummy images of different sizes
    TEST(ClerAPI, Process256x256)
    {
        EXPECT_TRUE(RunCLER(256, 256));
    }

    TEST(ClerAPI, Process512x512)
    {
        EXPECT_TRUE(RunCLER(512, 512));
    }

    TEST(ClerAPI, Process1024x1024)
    {
        EXPECT_TRUE(RunCLER(1024, 1024));
    }

    TEST(ClerAPI, Process2048x2048)
    {
        EXPECT_TRUE(RunCLER(2048, 2048));
    }

    TEST(ClerAPI, Process4096x4096)
    {
        EXPECT_TRUE(RunCLER(4096, 4096));
    }

    //tests a dummy image with various thread counts 
    TEST(ClerAPI, ThreadCounts)
    {
        for (int t : {-1, 0, 1, 4, 16})
        {
            RDOOptions opt = DefaultOptions();
            opt.numThreads = t;

            DummyImage img = CreateDummyImage(256, 256);
            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(img.bcElementSize, img.bc1Data.data(), img.rgbaData.data(), 256, 256, DXGI_FORMAT_BC1_UNORM, opt);

            EXPECT_TRUE(RDO_IsSuccess(code));
        }
    }

    //tests a dummy image with various max cluster counts
    TEST(ClerAPI, maxClusterCounts)
    {
        // should succeed
        for (int k : {-1, 32, 128, 256, 300, 1000000000})
        {
            RDOOptions opt = DefaultOptions();
            opt.maxClusters = k;
            opt.minClusters = 2;

            DummyImage img = CreateDummyImage(256, 256);

            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
                img.bcElementSize,
                img.bc1Data.data(),
                img.rgbaData.data(),
                256, 
                256,
                DXGI_FORMAT_BC1_UNORM,
                opt
            );

            EXPECT_TRUE(RDO_IsSuccess(code));
        }

        // should fail
        for (int k : {0, 1})
        {
            RDOOptions opt = DefaultOptions();
            opt.maxClusters = k;
            opt.minClusters = 2;

            DummyImage img = CreateDummyImage(256, 256);

            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
                img.bcElementSize,
                img.bc1Data.data(),
                img.rgbaData.data(),
                256, 
                256,
                DXGI_FORMAT_BC1_UNORM,
                opt
            );

            EXPECT_EQ(code, RDO_ErrorCode::InvalidClusterRange);
        }
    }

    //tests a dummy image with various min cluster counts
    TEST(ClerAPI, minClusterCounts)
    {
        // should succeed
        for (int k : {-1, 4, 32, 128, 256})
        {
            RDOOptions opt = DefaultOptions();
            opt.maxClusters = 256;
            opt.minClusters = k;

            DummyImage img = CreateDummyImage(256, 256);

            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
                img.bcElementSize,
                img.bc1Data.data(),
                img.rgbaData.data(),
                256, 
                256,
                DXGI_FORMAT_BC1_UNORM,
                opt
            );

            EXPECT_TRUE(RDO_IsSuccess(code));
        }

        // should fail
        for (int k : {300, 1000000000})
        {
            RDOOptions opt = DefaultOptions();
            opt.maxClusters = 256;
            opt.minClusters = k;
            DummyImage img = CreateDummyImage(256, 256);

            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
                img.bcElementSize,
                img.bc1Data.data(),
                img.rgbaData.data(),
                256, 
                256,
                DXGI_FORMAT_BC1_UNORM,
                opt
            );

            EXPECT_EQ(code, RDO_ErrorCode::InvalidClusterRange);
        }
    }

    //tests a dummy image with all loss metrics
    TEST(ClerAPI, lossMetrics)
    {
        for (RDOLossMetric metric : {RDOLossMetric::LPIPS, RDOLossMetric::MSE, RDOLossMetric::RMSE, RDOLossMetric::VGG})
        {
            RDOOptions opt = DefaultOptions();
            opt.metric = metric;
            DummyImage img = CreateDummyImage(256, 256);
            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(img.bcElementSize, img.bc1Data.data(), img.rgbaData.data(), 256, 256, DXGI_FORMAT_BC1_UNORM, opt);

            EXPECT_TRUE(RDO_IsSuccess(code));
        }
    }

    //tests a dummy image with various iterations
    TEST(ClerAPI, iterations)
    {
        for (int iterations_count : {-1, 0, 1, 10})
        {
            RDOOptions opt = DefaultOptions();
            opt.iterations = iterations_count;
            DummyImage img = CreateDummyImage(256, 256);
            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(img.bcElementSize, img.bc1Data.data(), img.rgbaData.data(), 256, 256, DXGI_FORMAT_BC1_UNORM, opt);

            EXPECT_TRUE(code == RDO_ErrorCode::OK || code == RDO_ErrorCode::OK_NoAdvancedRDO);
        }
    }

    //tests a dummy image with and without plusplus init
    TEST(ClerAPI, plusplus)
    {
        DummyImage img = CreateDummyImage(256, 256);
        RDOOptions opt = DefaultOptions();

        opt.usePlusPlus = true;
        RDO_ErrorCode code1 = GACL_RDO_ComponentLevelEntropyReduce(img.bcElementSize, img.bc1Data.data(), img.rgbaData.data(), 256, 256, DXGI_FORMAT_BC1_UNORM, opt);
        EXPECT_TRUE(code1 == RDO_ErrorCode::OK || code1 == RDO_ErrorCode::OK_NoAdvancedRDO);

        opt.usePlusPlus = false;
        RDO_ErrorCode code2 = GACL_RDO_ComponentLevelEntropyReduce(img.bcElementSize, img.bc1Data.data(), img.rgbaData.data(), 256, 256, DXGI_FORMAT_BC1_UNORM, opt);
        EXPECT_TRUE(code2 == RDO_ErrorCode::OK || code2 == RDO_ErrorCode::OK_NoAdvancedRDO);
    }

    // Tests the three k-means++ sampling ratio code paths based on loss bounds:
    // - Default range (>=0.05): 12% sampling
    // - Medium range (>=0.02, <0.05): 25% sampling
    // - Tight range (<0.02): 50% sampling
    // Also verifies sampling works correctly across all loss metrics.
    TEST(ClerAPI, plusplusSamplingRatios)
    {
        DummyImage img = CreateDummyImage(512, 512);
        RDOOptions opt = DefaultOptions();
        opt.usePlusPlus = true;

        // Test all metrics to verify sampling thresholds work regardless of metric scale
        RDOLossMetric metrics[] = { RDOLossMetric::MSE, RDOLossMetric::RMSE, RDOLossMetric::LPIPS, RDOLossMetric::VGG };
        const char* metricNames[] = { "MSE", "RMSE", "LPIPS", "VGG" };

        for (int m = 0; m < 4; m++)
        {
            opt.metric = metrics[m];

            // Default range (0.05): should use 12% sampling
            {
                opt.lossMin = 0.05f;
                opt.lossMax = 0.10f;  // range = 0.05 >= 0.05
                RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
                    img.bcElementSize, img.bc1Data.data(), img.rgbaData.data(), 512, 512, DXGI_FORMAT_BC1_UNORM, opt);
                EXPECT_TRUE(RDO_IsSuccess(code)) << metricNames[m] << ": Default range (12% sampling) failed";
            }

            // Medium range (0.03): should use 25% sampling
            {
                opt.lossMin = 0.05f;
                opt.lossMax = 0.08f;  // range = 0.03, >= 0.02 and < 0.05
                RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
                    img.bcElementSize, img.bc1Data.data(),img.rgbaData.data(), 512, 512, DXGI_FORMAT_BC1_UNORM, opt);
                EXPECT_TRUE(RDO_IsSuccess(code)) << metricNames[m] << ": Medium range (25% sampling) failed";
            }

            // Tight range (0.01): should use 50% sampling
            {
                opt.lossMin = 0.05f;
                opt.lossMax = 0.06f;  // range = 0.01 < 0.02
                RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
                    img.bcElementSize, img.bc1Data.data(), img.rgbaData.data(), 512, 512, DXGI_FORMAT_BC1_UNORM, opt);
                EXPECT_TRUE(RDO_IsSuccess(code)) << metricNames[m] << ": Tight range (50% sampling) failed";
            }
        }
    }

    //tests a dummy image with various max loss values
    TEST(ClerAPI, maxLossValues)
    {
        DummyImage img = CreateDummyImage(256, 256);

        // should succeed
        const float maxLossValues[] = { 0.0f, 0.2f, 1.0f };
        for (float maxLoss : maxLossValues)
        {
            RDOOptions opt = DefaultOptions();
            opt.lossMax = maxLoss;
            opt.lossMin = 0.0f;

            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
				img.bcElementSize,
                img.bc1Data.data(),
                img.rgbaData.data(),
                256, 
                256,
                DXGI_FORMAT_BC1_UNORM,
                opt
            );

            EXPECT_TRUE(RDO_IsSuccess(code));
        }

        // should fail
        {
            RDOOptions opt = DefaultOptions();
            opt.lossMax = -1.0f;  // invalid
            opt.lossMin = 0.0f;

            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
                img.bcElementSize, 
                img.bc1Data.data(),
                img.rgbaData.data(),
                256, 
                256,
                DXGI_FORMAT_BC1_UNORM,
                opt
            );

            EXPECT_EQ(code, RDO_ErrorCode::InvalidLossRange);
        }
    }

    //tests a dummy image with various max loss values
    TEST(ClerAPI, minLossValues)
    {
        DummyImage img = CreateDummyImage(256, 256);

        // should succeed
        const float minLossValues[] = { 0.05f, 0.5f, 1.0f };
        for (float minLoss : minLossValues)
        {
            RDOOptions opt = DefaultOptions();
            opt.lossMax = 1.0f;
            opt.lossMin = minLoss;

            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
                img.bcElementSize, 
                img.bc1Data.data(),
                img.rgbaData.data(),
                256,
                256,
                DXGI_FORMAT_BC1_UNORM,
                opt
            );

            EXPECT_TRUE(RDO_IsSuccess(code));
        }

        // should fail: minLoss > maxLoss
        {
            RDOOptions opt = DefaultOptions();
            opt.lossMax = 0.5f;
            opt.lossMin = 1.0f;

            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
                img.bcElementSize, 
                img.bc1Data.data(),
                img.rgbaData.data(),
                256, 
                256,
                DXGI_FORMAT_BC1_UNORM,
                opt
            );

            EXPECT_EQ(code, RDO_ErrorCode::InvalidLossRange);
        }

        // should fail: minLoss < 0
        {
            RDOOptions opt = DefaultOptions();
            opt.lossMax = 0.5f;
            opt.lossMin = -1.0f;

            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
                img.bcElementSize, 
                img.bc1Data.data(),
                img.rgbaData.data(),
                256,
                256,
                DXGI_FORMAT_BC1_UNORM,
                opt
            );

            EXPECT_EQ(code, RDO_ErrorCode::InvalidLossRange);
        }
    }

    //verifies CLER produces valid output correlated with input, not random gibberish
    TEST(ClerAPI, VerifyLossWithRandomImage)
    {
        HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hrCom) && hrCom != RPC_E_CHANGED_MODE && hrCom != S_FALSE)
        {
            GTEST_LOG_(WARNING) << "Failed to initialize COM: 0x" << std::hex << hrCom;
        }

        const std::vector<std::pair<uint32_t, uint32_t>> resolutions = {
            {256, 256},
            {512, 512},
            {1024, 1024},
            {2048, 2048}
        };

        for (const auto& [width, height] : resolutions)
        {
            GTEST_LOG_(INFO) << "Testing resolution: " << width << "x" << height;

            std::vector<uint8_t> originalRGBA(width * height * 4);
            srand(static_cast<unsigned>(width * height));
            for (size_t i = 0; i < originalRGBA.size(); ++i)
            {
                originalRGBA[i] = static_cast<uint8_t>(rand() % 256);
            }

            DirectX::Image srcImage;
            srcImage.width = width;
            srcImage.height = height;
            srcImage.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srcImage.rowPitch = width * 4;
            srcImage.slicePitch = width * height * 4;
            srcImage.pixels = originalRGBA.data();

            DirectX::ScratchImage compressedBC1;
            HRESULT hr = DirectX::Compress(srcImage, DXGI_FORMAT_BC1_UNORM,
                DirectX::TEX_COMPRESS_DEFAULT,
                DirectX::TEX_THRESHOLD_DEFAULT,
                compressedBC1);
            ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to compress to BC1 for " << width << "x" << height;

            const DirectX::Image* bc1Image = compressedBC1.GetImage(0, 0, 0);
            ASSERT_NE(bc1Image, nullptr) << "BC1 image is null";

            std::vector<uint8_t> bc1Data(bc1Image->slicePitch);
            memcpy(bc1Data.data(), bc1Image->pixels, bc1Image->slicePitch);

            RDOOptions opt = DefaultOptions();
            opt.maxClusters = 128;
            opt.metric = RDOLossMetric::MSE;
            opt.lossMax = 0.2f; 

            RDO_ErrorCode code = GACL_RDO_ComponentLevelEntropyReduce(
				8, // BC1 block size
                bc1Data.data(),
                originalRGBA.data(),
                width,
                height,
                DXGI_FORMAT_BC1_UNORM,
                opt
            );

            ASSERT_TRUE(RDO_IsSuccess(code)) << "CLER failed with error code "
                << static_cast<int>(code)
                << " for " << width << "x" << height;

            const float expectedMaxLoss = 0.3f;

            DirectX::Image clerBC1Image;
            clerBC1Image.width = width;
            clerBC1Image.height = height;
            clerBC1Image.format = DXGI_FORMAT_BC1_UNORM;
            clerBC1Image.rowPitch = ((width + 3) / 4) * 8;
            clerBC1Image.slicePitch = bc1Data.size();
            clerBC1Image.pixels = bc1Data.data();

            DirectX::ScratchImage decompressed;
            hr = DirectX::Decompress(clerBC1Image, DXGI_FORMAT_R8G8B8A8_UNORM, decompressed);
            ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to decompress CLER output for "
                << width << "x" << height;

            const DirectX::Image* decImg = decompressed.GetImage(0, 0, 0);
            ASSERT_NE(decImg, nullptr) << "Decompressed image is null";

            const size_t totalPixels = width * height * 3;
            double sumSquaredError = 0.0;

            for (size_t h = 0; h < height; ++h)
            {
                for (size_t w = 0; w < width; ++w)
                {
                    const size_t idx = (h * width + w) * 4;

                    for (size_t c = 0; c < 3; ++c)
                    {
                        float original = originalRGBA[idx + c] / 255.0f;
                        float decomp = decImg->pixels[idx + c] / 255.0f;
                        double diff = static_cast<double>(decomp) - static_cast<double>(original);
                        sumSquaredError += diff * diff;
                    }
                }
            }

            float mseLoss = static_cast<float>(sumSquaredError / totalPixels);

            GTEST_LOG_(INFO) << "Resolution: " << width << "x" << height
                << ", MSE Loss: " << mseLoss
                << ", Expected Max: " << expectedMaxLoss;

            EXPECT_LE(mseLoss, expectedMaxLoss)
                << "MSE loss " << mseLoss << " exceeds max " << expectedMaxLoss
                << " for " << width << "x" << height
                << " (RDO mode: " << (code == RDO_ErrorCode::OK ? "Advanced" : "Basic") << ")";

            EXPECT_GE(mseLoss, 0.0f) << "MSE loss should be non-negative";
        }

        if (hrCom != RPC_E_CHANGED_MODE && hrCom != S_FALSE)
        {
            CoUninitialize();
        }
    }
}//namespace cler_api_tests
#endif