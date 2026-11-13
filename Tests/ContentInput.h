// Discover content files and register them with the existing GoogleTest suites.
// Shared test plumbing amongst GACL test suites

// Flow: ParseDirectory -> RegisterDirectoryContents(directory, validator) -> RunTests
#pragma once
#include <gtest/gtest.h>
#include <Windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ContentInput
{
    namespace fs = std::filesystem;

    // A distinct exception type for "this validator doesn't support this input."
    class Unsupported : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // Converts Windows wide strings into UTF-8 for GoogleTest names and diagnostics.
    inline std::string WideToUtf8(const std::wstring& value)
    {
        if (value.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (!size) throw std::runtime_error("Invalid Unicode input path");
        std::string result(size, '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr))
            throw std::runtime_error("Input path conversion failed");
        return result;
    }

    // Helper that takes a file and returns a lowercase extension (including the dot).
    // This ensures files such as .DDS and .dds are treated identically
    inline std::wstring Extension(const fs::path& path)
    {
        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) {
            return static_cast<wchar_t>(std::towlower(c));
        });
        return extension;
    }

    struct File
    {
        fs::path path;
        std::string relative;
    };

    class Case : public testing::Test
    {
        File file;
        std::function<void(const File&)> validate;
        void TestBody() override
        {
            RecordProperty("input_file", file.relative);
            try { validate(file); }
            catch (const Unsupported& reason) { GTEST_SKIP() << "[UNSUPPORTED] " << reason.what(); }
            catch (const std::exception& error) { FAIL() << file.relative << ": " << error.what(); }
        }
    public:
        Case(File input, std::function<void(const File&)> action)
            : file(std::move(input)), validate(std::move(action)) {}
    };

    // Runs GoogleTest, then prevents empty/ineffective runs from appearing successful
    inline int RunTests(bool contentMode)
    {
        const int result = RUN_ALL_TESTS();
        if (testing::GTEST_FLAG(list_tests)) return result;
        const auto& tests = *testing::UnitTest::GetInstance();
        bool passedContent = !contentMode;
        for (int i = 0; i < tests.total_test_suite_count(); ++i)
        {
            const auto& suite = *tests.GetTestSuite(i);
            if (std::string(suite.name()) == "ContentInput" && suite.successful_test_count())
                passedContent = true;
        }
        if (!tests.successful_test_count() || !passedContent)
        {
            std::cerr << "[ COVERAGE FAIL ] No applicable tests passed\n";
            return 1;
        }
        return result;
    }

    // Reads and resolves an input directory
    inline fs::path ParseDirectory(int& argc, char** argv)
    {
        int count = 0;
        std::unique_ptr<wchar_t*, decltype(&LocalFree)> wide(CommandLineToArgvW(GetCommandLineW(), &count), &LocalFree);
        if (!wide) throw std::runtime_error("Cannot read command line");
        fs::path input;
        for (int i = 1; i < count; ++i)
        {
            if (std::wstring(wide.get()[i]) != L"--input-directory") continue;
            if (!input.empty() || ++i == count || !*wide.get()[i])
                throw std::runtime_error("--input-directory requires one directory");
            input = wide.get()[i];
        }
        int out = 1;
        for (int i = 1; i < argc; ++i)
        {
            if (std::string(argv[i]) == "--input-directory") ++i;
            else argv[out++] = argv[i];
        }
        argc = out;
        argv[out] = nullptr;
        if (input.empty()) return {};
        input = fs::canonical(input);
        if (!fs::is_directory(input)) throw std::runtime_error("Input is not a directory");
        // Suite validators generate files in CWD, never in the source content tree.
        const auto relative = fs::canonical(fs::current_path()).lexically_relative(input);
        if (relative == L"." || (!relative.empty() && *relative.begin() != L".."))
            throw std::runtime_error("Run from a working directory outside the input tree");
        return input;
    }

    // Discovers files recursively, registers one google test case per file
    inline void RegisterDirectoryContents(const fs::path& input, std::function<void(const File&)> validate)
    {
        if (input.empty()) return;
        std::vector<fs::path> paths;
        for (const auto& entry : fs::recursive_directory_iterator(input))
            if (entry.is_regular_file()) paths.push_back(entry.path());
        std::sort(paths.begin(), paths.end());
        if (paths.empty()) throw std::runtime_error("Input directory contains no files");
        for (size_t index = 0; index < paths.size(); ++index)
        {
            File file{ paths[index], WideToUtf8(paths[index].lexically_relative(input).generic_wstring()) };
            std::string name = "File_" + std::to_string(index) + "_" + file.relative;
            for (char& character : name)
            {
                const bool alphanumeric = (character >= 'a' && character <= 'z') ||
                    (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
                if (!alphanumeric) character = '_';
            }
            testing::RegisterTest("ContentInput", name.c_str(), nullptr, file.relative.c_str(), __FILE__, __LINE__,
                [file, validate]() -> Case* { return new Case(file, validate); });
        }
    }
}
