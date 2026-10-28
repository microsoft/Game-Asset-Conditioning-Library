// ml_rdo_tests\main.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#if GACL_INCLUDE_CLER

#include <gtest/gtest.h>
#include "../ContentInput.h"
#include <exception>
#include <iostream>
#include <fstream>
#include <set>
#define ORT_API_MANUAL_INIT
#include "../../Lib/RDO_ML/LossMetrics.h"

namespace loss_metrics_tests
{
    Ort::Session* LoadLPIPSModel();
    Ort::Session* LoadVGGModel();
}
namespace
{
    void EnsureMLModelsAndCoverage(const std::filesystem::path& binaries)
    {
        const auto libraryModels = binaries.parent_path().parent_path().parent_path() / "ThirdParty/models";
        for (const auto* name : { L"lpips_model.onnx", L"vgg_model.onnx" })
        {
            const auto a = binaries / name, b = libraryModels / name;
            if (!std::filesystem::is_regular_file(a) || !std::filesystem::is_regular_file(b) || !std::filesystem::file_size(a))
                throw std::runtime_error("Missing/empty ML regression model");
            std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
            if (!left || !right) throw std::runtime_error("Cannot read ML regression models");
            if (!std::equal(std::istreambuf_iterator<char>(left), std::istreambuf_iterator<char>(),
                std::istreambuf_iterator<char>(right), std::istreambuf_iterator<char>()))
                throw std::runtime_error("CLER and loss-metric model copies differ");
        }
        std::set<std::string> groups;
        const auto* tests = testing::UnitTest::GetInstance();
        for (int i = 0; i < tests->total_test_suite_count(); ++i) groups.insert(tests->GetTestSuite(i)->name());
        for (const auto* group : { "ClerAPI", "LossMetrics", "CLERTest" })
            if (!groups.count(group)) throw std::runtime_error(std::string("Required ML coverage missing: ") + group);
        if (!loss_metrics_tests::LoadLPIPSModel() || !loss_metrics_tests::LoadVGGModel())
            throw std::runtime_error("ML model loading failed; refusing perceptual fallback");
    }
}

void RunMLContentInput(const ContentInput::File& file);

int __cdecl main(int argc, char** argv)
{
    try
    {
        const auto directory = ContentInput::ParseDirectory(argc, argv);
        if (!directory.empty())
        {
            ContentInput::Register(directory, RunMLContentInput);
            testing::GTEST_FLAG(filter) = "ContentInput.*";
        }
        testing::InitGoogleTest(&argc, argv);
        testing::GTEST_FLAG(catch_exceptions) = false;
        if (!testing::GTEST_FLAG(list_tests))
        {
            // Fixed regressions own their fixture staging; the launcher knows no filenames.
            // An explicitly narrowed ContentInput filter must not require regression fixtures.
            const auto filter = testing::GTEST_FLAG(filter);
            const auto positive = filter.substr(0, filter.find('-'));
            bool contentOnly = !directory.empty() && !positive.empty();
            for (size_t begin = 0; begin < positive.size();)
            {
                const auto end = positive.find(':', begin);
                contentOnly &= positive.compare(begin, 13, "ContentInput.") == 0;
                if (end == std::string::npos) break;
                begin = end + 1;
            }
            if (!contentOnly)
            {
                wchar_t executable[32768]{};
                if (!GetModuleFileNameW(nullptr, executable, 32768)) throw std::runtime_error("Cannot locate ML test executable");
                const auto binaryDirectory = std::filesystem::path(executable).parent_path();
                EnsureMLModelsAndCoverage(binaryDirectory);
                const auto assets = binaryDirectory / "test_assets";
                if (!std::filesystem::is_directory(assets)) throw std::runtime_error("ML regression fixtures missing");
                const auto workingAssets = std::filesystem::current_path() / "test_assets";
                // CI may start this suite beside its EXE, where the fixtures are already in place.
                // Only copy when the working directory needs its own fixture tree.
                if (!std::filesystem::exists(workingAssets) || !std::filesystem::equivalent(assets, workingAssets))
                    std::filesystem::copy(assets, workingAssets,
                        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
            }
        }
        return ContentInput::RunTests(!directory.empty());
    }
    catch (const std::exception& error)
    {
        std::cerr << "ML/RDO content input: " << error.what() << '\n';
        return 2;
    }
}
#endif
