//-------------------------------------------------------------------------------------
// test_gacl_exe.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <memory>
#include <DirectXTex.h>

//tests just pertaining to the gacl exe call for CLER
namespace gacl_exe_tests_with_real_images {

    struct ClerTestParam
    {
        const int width;
        const int height;
        const std::wstring filename;

        friend std::ostream& operator<<(std::ostream& os, const ClerTestParam& param)
        {
            return os << param.width << "x" << param.height;
        }
    };

    //helper functions for testing api
    class CLERTest : public ::testing::TestWithParam<ClerTestParam>
    {
    protected:

        static std::filesystem::path GetAssetsDirectory()
        {
            return std::filesystem::current_path() / "test_assets";
        }

        static std::filesystem::path GetOutputDirectory()
        {
            auto outputDir = std::filesystem::current_path() / "test_output";
            if (!std::filesystem::exists(outputDir))
            {
                std::filesystem::create_directories(outputDir);
            }
            return outputDir;
        }

        static std::filesystem::path GetGaclExePath()
        {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
            return exeDir / "gacl.exe";
        }

        //run given command -- for running gacl CLI
        static int RunCommand(const std::wstring& command)
        {
            STARTUPINFOW si = {};
            PROCESS_INFORMATION pi = {};
            si.cb = sizeof(si);

            std::vector<wchar_t> cmdLine(command.begin(), command.end());
            cmdLine.push_back(L'\0');

            BOOL result = CreateProcessW(
                NULL, cmdLine.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi
            );

            if (!result)
            {
                std::wcerr << L"CreateProcess failed: " << GetLastError() << std::endl;
                return -1;
            }

            WaitForSingleObject(pi.hProcess, INFINITE);

            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            return static_cast<int>(exitCode);
        }
    };

    //test image exists
    TEST_P(CLERTest, TestImageExists)
    {
        //test the images exist
        const auto& p = GetParam();
        auto assetsDir = GetAssetsDirectory();
        auto ddsPath = assetsDir / (p.filename + L".dds");
        auto pngPath = assetsDir / (p.filename + L".png");

        ASSERT_TRUE(std::filesystem::exists(ddsPath)) << "DDS file not found: " << ddsPath.string();
        ASSERT_TRUE(std::filesystem::exists(pngPath)) << "PNG file not found: " << pngPath.string();
        SUCCEED();
    }

    //test gacl exe runs on given images and produces verified output
    TEST_P(CLERTest, RunCLERProcessing)
    {
        const auto& p = GetParam();
        auto assetsDir = GetAssetsDirectory();
        auto outputDir = GetOutputDirectory();
        auto gaclExe = GetGaclExePath();

        auto inputDdsPath = assetsDir / (p.filename + L".dds");
        auto inputPngPath = assetsDir / (p.filename + L".png");
        auto outputDdsPath = outputDir / (p.filename + L"_cler.dds");

        ASSERT_TRUE(std::filesystem::exists(gaclExe)) << "gacl.exe not found at: " << gaclExe.string();
        ASSERT_TRUE(std::filesystem::exists(inputDdsPath)) << "Input DDS file not found: " << inputDdsPath.string();
        ASSERT_TRUE(std::filesystem::exists(inputPngPath)) << "Input PNG file not found: " << inputPngPath.string();

        std::wstring command = L"\"" + gaclExe.wstring() + L"\" \"" + inputDdsPath.wstring() + L"\" " + L"-cler " + L"--cmaxclusters " + L"128 " + L"-o \"" + outputDdsPath.wstring() + L"\"";

		//delete output file if it exists already
        if (std::filesystem::exists(outputDdsPath))
        {
            std::filesystem::remove(outputDdsPath);
        }

        //verify output exists and is populated
        int exitCode = RunCommand(command);
        ASSERT_EQ(exitCode, 0) << "gacl.exe failed with exit code: " << exitCode;
        ASSERT_TRUE(std::filesystem::exists(outputDdsPath)) << "Output DDS file not created: " << outputDdsPath.string();
        auto fileSize = std::filesystem::file_size(outputDdsPath);
        ASSERT_GT(fileSize, 0) << "Output file is empty";

        //verify output is a valid dds file
        DirectX::ScratchImage image;
        HRESULT hr = DirectX::LoadFromDDSFile(outputDdsPath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
        ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to load DDS file: " << outputDdsPath.string();

        //verify number of mips
		DirectX::ScratchImage originalImage;
        hr = DirectX::LoadFromDDSFile(inputDdsPath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, originalImage);
        ASSERT_EQ(originalImage.GetMetadata().mipLevels, image.GetMetadata().mipLevels) << "Output DDS file has wrong number of mip levels";

        //verify output is the same format
        ASSERT_EQ(image.GetMetadata().format, originalImage.GetMetadata().format) << "Output DDS file has wrong format";

        //verify output is the same size
        ASSERT_EQ(image.GetMetadata().width, originalImage.GetMetadata().width) << "Output DDS file has wrong width";
		ASSERT_EQ(image.GetMetadata().height, originalImage.GetMetadata().height) << "Output DDS file has wrong height";

    }

    //run gacl exe on different image sizes
    INSTANTIATE_TEST_CASE_P(
        CLER,
        CLERTest,
        ::testing::Values(
            ClerTestParam{ 256, 256,  L"256x256" },
            ClerTestParam{ 512, 512,  L"512x512" },
            ClerTestParam{ 1024, 1024, L"1024x1024" },
            ClerTestParam{ 2048, 2048, L"2048x2048" },
            ClerTestParam{ 4096, 4096, L"4096x4096" }
        )
    );

    //test gacl exe run on bad metric
    TEST_F(CLERTest, RunCLERProcessingBadMetric)
    {
        auto assetsDir = GetAssetsDirectory();
        auto outputDir = GetOutputDirectory();
        auto gaclExe = GetGaclExePath();

        auto inputDdsPath = assetsDir / L"256x256.dds";
        auto inputPngPath = assetsDir / L"256x256.png";
        auto outputDdsPath = outputDir / L"256x256_cler.dds";

        ASSERT_TRUE(std::filesystem::exists(gaclExe)) << "gacl.exe not found at: " << gaclExe.string();
        ASSERT_TRUE(std::filesystem::exists(inputDdsPath)) << "Input DDS file not found: " << inputDdsPath.string();
        ASSERT_TRUE(std::filesystem::exists(inputPngPath)) << "Input PNG file not found: " << inputPngPath.string();

        std::wstring command = L"\"" + gaclExe.wstring() + L"\" \"" + inputDdsPath.wstring() + L"\" " + L"-cler " + L"--cmetric " + L"bad_metric " + L"-o \"" + outputDdsPath.wstring() + L"\"";

        //delete output file if it exists already
        if (std::filesystem::exists(outputDdsPath))
        {
            std::filesystem::remove(outputDdsPath);
        }

        //verify output fails
        int exitCode = RunCommand(command);
        ASSERT_NE(exitCode, 0) << ", " << exitCode;
        ASSERT_FALSE(std::filesystem::exists(outputDdsPath));
    }

    //test gacl exe run on a bc 7 file
    TEST_F(CLERTest, RunCLERProcessingBC7)
    {
        auto assetsDir = GetAssetsDirectory();
        auto outputDir = GetOutputDirectory();
        auto gaclExe = GetGaclExePath();

        auto inputDdsPath = assetsDir / L"bc7.DDS";
        auto inputPngPath = assetsDir / L"bc7.png";
        auto outputDdsPath = outputDir / L"bc7_cler.dds";

        ASSERT_TRUE(std::filesystem::exists(gaclExe)) << "gacl.exe not found at: " << gaclExe.string();
        ASSERT_TRUE(std::filesystem::exists(inputDdsPath)) << "Input DDS file not found: " << inputDdsPath.string();
        ASSERT_TRUE(std::filesystem::exists(inputPngPath)) << "Input PNG file not found: " << inputPngPath.string();

        std::wstring command = L"\"" + gaclExe.wstring() + L"\" \"" + inputDdsPath.wstring() + L"\" " + L"-cler " + L"-o \"" + outputDdsPath.wstring() + L"\"";

        //delete output file if it exists already
        if (std::filesystem::exists(outputDdsPath))
        {
            std::filesystem::remove(outputDdsPath);
        }

        //verify output fails
        int exitCode = RunCommand(command);
        ASSERT_NE(exitCode, 0) << ", " << exitCode;
        ASSERT_FALSE(std::filesystem::exists(outputDdsPath));
    }

    TEST_F(CLERTest, RunCLERProcessingBadCref)
    {
        auto assetsDir = GetAssetsDirectory();
        auto outputDir = GetOutputDirectory();
        auto gaclExe = GetGaclExePath();

        auto inputDdsPath = assetsDir / L"256x256.dds";
        auto inputPngPath = assetsDir / L"256x256.dds";
        auto outputDdsPath = outputDir / L"256x256.dds";

        ASSERT_TRUE(std::filesystem::exists(gaclExe)) << "gacl.exe not found at: " << gaclExe.string();
        ASSERT_TRUE(std::filesystem::exists(inputDdsPath)) << "Input DDS file not found: " << inputDdsPath.string();
        ASSERT_TRUE(std::filesystem::exists(inputPngPath)) << "Input CREF file not found: " << inputPngPath.string();

        std::wstring command = L"\"" + gaclExe.wstring() + L"\" \"" + inputDdsPath.wstring() + L"\" " + L"-cler " + L"-cref \"" + inputPngPath.wstring() + L"\" -o \"" + outputDdsPath.wstring() + L"\"";

        //delete output file if it exists already
        if (std::filesystem::exists(outputDdsPath))
        {
            std::filesystem::remove(outputDdsPath);
        }

        //verify output does not fail
        int exitCode = RunCommand(command);
        ASSERT_EQ(exitCode, 0) << ", " << exitCode;
        ASSERT_TRUE(std::filesystem::exists(outputDdsPath));
    }

    //Test CLER + Forward Space Curve
    TEST_P(CLERTest, RunCLERWithForwardSpaceCurve)
    {
        const auto& p = GetParam();
        auto assetsDir = GetAssetsDirectory();
        auto outputDir = GetOutputDirectory();
        auto gaclExe = GetGaclExePath();

        auto inputDdsPath = assetsDir / (p.filename + L".dds");
        auto outputDdsPath = outputDir / (p.filename + L"_cler_fsc.dds");

        ASSERT_TRUE(std::filesystem::exists(gaclExe)) << "gacl.exe not found";
        ASSERT_TRUE(std::filesystem::exists(inputDdsPath)) << "Input DDS not found";

        std::wstring command = L"\"" + gaclExe.wstring() + L"\" \"" + inputDdsPath.wstring() +
            L"\" -cler --cmaxclusters 128 -fsc -o \"" + outputDdsPath.wstring() + L"\"";

        if (std::filesystem::exists(outputDdsPath))
        {
            std::filesystem::remove(outputDdsPath);
        }

        int exitCode = RunCommand(command);
        ASSERT_EQ(exitCode, 0) << "gacl.exe failed with exit code: " << exitCode;
        ASSERT_TRUE(std::filesystem::exists(outputDdsPath)) << "Output DDS file not created";

        DirectX::ScratchImage image;
        HRESULT hr = DirectX::LoadFromDDSFile(outputDdsPath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
        ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to load output DDS file";

        DirectX::ScratchImage originalImage;
        hr = DirectX::LoadFromDDSFile(inputDdsPath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, originalImage);
        ASSERT_EQ(originalImage.GetMetadata().mipLevels, image.GetMetadata().mipLevels) << "Mip level mismatch";
        ASSERT_EQ(image.GetMetadata().format, originalImage.GetMetadata().format) << "Format mismatch";
    }

    // Test CLER + Disable Space Curve
    TEST_P(CLERTest, RunCLERWithDisableSpaceCurve)
    {
        const auto& p = GetParam();
        auto assetsDir = GetAssetsDirectory();
        auto outputDir = GetOutputDirectory();
        auto gaclExe = GetGaclExePath();

        auto inputDdsPath = assetsDir / (p.filename + L".dds");
        auto outputDdsPath = outputDir / (p.filename + L"_cler_dsc.dds");

        ASSERT_TRUE(std::filesystem::exists(gaclExe)) << "gacl.exe not found";
        ASSERT_TRUE(std::filesystem::exists(inputDdsPath)) << "Input DDS not found";

        std::wstring command = L"\"" + gaclExe.wstring() + L"\" \"" + inputDdsPath.wstring() +
            L"\" -cler --cmaxclusters 128 -dsc -o \"" + outputDdsPath.wstring() + L"\"";

        if (std::filesystem::exists(outputDdsPath))
        {
            std::filesystem::remove(outputDdsPath);
        }

        int exitCode = RunCommand(command);
        ASSERT_EQ(exitCode, 0) << "gacl.exe failed with exit code: " << exitCode;
        ASSERT_TRUE(std::filesystem::exists(outputDdsPath)) << "Output file not created";

        DirectX::ScratchImage image;
        HRESULT hr = DirectX::LoadFromDDSFile(outputDdsPath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
        ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to load output DDS file";
    }
}  // namespace gacl_exe_tests_with_real_images
