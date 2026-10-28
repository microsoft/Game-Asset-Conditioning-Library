// Shared support for running the existing GoogleTest suites on a content folder.
// Each suite supplies its own validation; this file finds inputs and checks coverage.
#pragma once
#include <gtest/gtest.h>
#include <Windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include <filesystem>
#include <algorithm>
#include <cwctype>
#include <cstdint>
#include <set>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ContentInput
{
    namespace fs = std::filesystem;
    // A validator throws this when a file does not apply to that particular suite.
    class Unsupported : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // Keep the original Windows path readable in GoogleTest output and properties.
    inline std::string Utf8(const std::wstring& value)
    {
        if (value.empty()) return {};
        // First find the output size, then convert into a buffer of that size.
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (!size) throw std::runtime_error("Invalid Unicode input path");
        std::string result(size, '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr))
            throw std::runtime_error("Input path conversion failed");
        return result;
    }

    // Compare extensions without treating .DDS and .dds as different formats.
    inline std::wstring Extension(const fs::path& path)
    {
        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) {
            return static_cast<wchar_t>(std::towlower(c));
        });
        return extension;
    }

    // Absolute path for loading, relative path for display, and index for unique outputs.
    struct File
    {
        fs::path path;
        std::string relative;
        size_t index;
    };

    // Adapt one file and its suite-specific validator into an ordinary GoogleTest case.
    class Case : public testing::Test
    {
        File file;
        std::function<void(const File&)> validate;
        void TestBody() override
        {
            // Preserve the original path even though the test name is sanitized.
            RecordProperty("input_file", file.relative);
            // An inapplicable format is a skip; an actual processing error is a failure.
            try
            {
                validate(file);
            }
            catch (const Unsupported& reason)
            {
                GTEST_SKIP() << "[UNSUPPORTED] " << reason.what();
            }
            catch (const std::exception& error)
            {
                FAIL() << file.relative << ": " << error.what();
            }
        }
    public:
        Case(File input, std::function<void(const File&)> action)
            : file(std::move(input)), validate(std::move(action)) {}
    };

    // Let GoogleTest print results. This listener only checks that useful tests ran;
    // an empty selection or a folder of entirely unsupported files must not pass.
    class CoverageListener : public testing::EmptyTestEventListener
    {
        bool contentMode;
        int unsupportedTests = 0;
        int unexpectedSkips = 0;
        int passedContentTests = 0;
    public:
        explicit CoverageListener(bool content) : contentMode(content) {}
        void OnTestEnd(const testing::TestInfo& test) override
        {
            // Check the completed case, not each assertion, so we count it only once.
            const auto& result = *test.result();
            const bool isContent = std::string(test.test_suite_name()) == "ContentInput";
            bool unsupported = result.Skipped();
            bool foundSkip = false;
            // A skipped case is expected only when every skip explains that the file is unsupported.
            for (int i = 0; i < result.total_part_count(); ++i)
            {
                const auto& part = result.GetTestPartResult(i);
                if (!part.skipped()) continue;
                foundSkip = true;
                unsupported &= std::string(part.message()).find("[UNSUPPORTED]") != std::string::npos;
            }
            // Fixed regressions cannot use the content-mode exception for unsupported files.
            unsupported &= foundSkip && isContent && contentMode;
            if (result.Skipped())
            {
                if (unsupported) ++unsupportedTests;
                else ++unexpectedSkips;
            }
            // Remember that at least one actual content validation succeeded.
            if (isContent && result.Passed()) ++passedContentTests;
        }
        bool HasCoverageFailure(const testing::UnitTest& tests) const
        {
            // GoogleTest can exit successfully after selecting no cases. Reject that here.
            if (!tests.test_to_run_count())
            {
                std::cerr << "[ COVERAGE FAIL ] No enabled tests selected\n";
                return true;
            }

            // Skipped files are not evidence that any content was successfully validated.
            if (!tests.successful_test_count() || (contentMode && !passedContentTests))
            {
                std::cerr << "[ COVERAGE FAIL ] No applicable tests passed\n";
                return true;
            }

            // Any other skip means requested coverage was not completed.
            if (unexpectedSkips)
            {
                std::cerr << "[ COVERAGE FAIL ] Unexpected skipped tests: " << unexpectedSkips << '\n';
                return true;
            }
            if (unsupportedTests) std::cout << "[ UNSUPPORTED FILES ] " << unsupportedTests << std::endl;
            return false;
        }
    };

    // Use this after registering cases and reading GoogleTest options.
    inline int RunTests(bool contentMode)
    {
        // Listing is intentionally allowed without executing or passing any cases.
        if (testing::GTEST_FLAG(list_tests)) return RUN_ALL_TESTS();

        // Keep the normal printer and add only the extra coverage check.
        auto* coverage = new CoverageListener(contentMode);
        testing::UnitTest::GetInstance()->listeners().Append(coverage); // GoogleTest owns the listener.
        const int exitCode = RUN_ALL_TESTS();
        // Either failed assertions or incomplete coverage makes the program fail.
        const bool coverageFailed = coverage->HasCoverageFailure(*testing::UnitTest::GetInstance());
        return exitCode || coverageFailed ? 1 : 0;
    }

    inline std::string TestNameForPath(const std::string& relative)
    {
        // Build a readable name from the relative path and hash the original bytes.
        // The hash distinguishes paths such as a-b.dds and a_b.dds, which sanitize alike.
        // Always include it so adding another file never renames an existing test.
        uint64_t hash = UINT64_C(14695981039346656037);
        std::string name;
        for (const unsigned char character : relative)
        {
            // Use the original byte for the stable FNV-1a hash, but only letters/digits in the name.
            hash = (hash ^ character) * UINT64_C(1099511628211);
            const bool alphanumeric = (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
            name += alphanumeric ? static_cast<char>(character) : '_';
        }
        // Avoid a leading digit, and do not let a DISABLED_* filename disable its test.
        if (name.empty() || (name.front() >= '0' && name.front() <= '9') || name.compare(0, 9, "DISABLED_") == 0)
            name.insert(name.begin(), '_');
        // The suffix is only a stable identifier, not a security check.
        std::ostringstream result;
        result << name << '_' << std::hex << std::setw(16) << std::setfill('0') << hash;
        return result.str();
    }

    // Read the input path from the Unicode command line, preserving non-English names.
    // Remove our option before letting GoogleTest parse its own arguments.
    inline fs::path ParseDirectory(int& argc, char** argv)
    {
        int count = 0;
        auto** wide = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!wide) throw std::runtime_error("Cannot read command line");
        fs::path input;
        // Accept at most one input folder and release Windows' argument buffer on error.
        try
        {
            for (int i = 1; i < count; ++i)
            {
                if (std::wstring(wide[i]) == L"--input-directory")
                {
                    if (!input.empty() || i + 1 == count || !*wide[i + 1])
                        throw std::runtime_error("--input-directory requires one directory");
                    input = wide[++i];
                }
            }
        }
        catch (...)
        {
            LocalFree(wide);
            throw;
        }
        LocalFree(wide);

        // Keep every argument except our input-folder option and its value.
        int out = 1;
        for (int i = 1; i < argc; ++i)
        {
            if (std::string(argv[i]) == "--input-directory")
            {
                ++i;
                continue;
            }
            argv[out++] = argv[i];
        }
        argc = out;
        argv[out] = nullptr;

        // No input means fixed regressions. Otherwise require a real directory.
        if (input.empty()) return {};
        input = fs::canonical(input);
        if (!fs::is_directory(input)) throw std::runtime_error("Input is not a directory");

        // Direct suite runs must also keep generated files outside the input tree.
        const auto working = fs::canonical(fs::current_path());
        const auto relative = working.lexically_relative(input);
        if (working == input || (!relative.empty() && *relative.begin() != L".."))
            throw std::runtime_error("Run from a working directory outside the input tree");
        return input;
    }

    // Find the files once and register one case per file in the existing suite.
    inline void Register(const fs::path& input, std::function<void(const File&)> validate)
    {
        if (input.empty()) return;
        std::vector<fs::path> paths;
        // Refuse links and junctions rather than leaving the selected content tree.
        // Tests read these original files; generated output belongs in the work folder.
        for (fs::recursive_directory_iterator it(input), end; it != end; ++it)
        {
            const auto attributes = GetFileAttributesW(it->path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("Cannot inspect input entry");
            if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            {
                if (it->is_directory()) it.disable_recursion_pending();
                throw std::runtime_error("Input contains a link/junction: " + Utf8(it->path().wstring()));
            }
            if (it->is_regular_file()) paths.push_back(it->path());
        }

        // Sort for a predictable display order; names themselves do not depend on order.
        std::sort(paths.begin(), paths.end());
        if (paths.empty()) throw std::runtime_error("Input directory contains no files");

        // Keep the original path for display and reject any duplicate final test name.
        std::set<std::string> registeredNames;
        for (size_t index = 0; index < paths.size(); ++index)
        {
            File file{ paths[index], Utf8(paths[index].lexically_relative(input).generic_wstring()), index };
            const auto name = TestNameForPath(file.relative);
            if (!registeredNames.insert(name).second)
                throw std::runtime_error("Duplicate content test name: " + name);
            // GoogleTest owns the case; the validator and file live until the case runs.
            testing::RegisterTest("ContentInput", name.c_str(), nullptr, file.relative.c_str(), __FILE__, __LINE__,
                [file, validate]() -> Case* { return new Case(file, validate); });
        }
    }
}
