// Read the command, run each selected test program, and return whether any failed.
// Test programs print their own results. This launcher does not create reports.
#define NOMINMAX
#include "process.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace
{
    // Convert Windows text so paths and errors print correctly in the console.
    std::string ToUtf8(const std::wstring& value)
    {
        if (value.empty()) return {};
        // Ask how much space is needed before converting the text.
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (!size) throw std::runtime_error("Invalid Unicode string");
        // Allocate the output and perform the conversion.
        std::string result(size, '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr))
            throw std::runtime_error("Unicode conversion failed");
        return result;
    }

    // One set of settings is shared by every selected test program.
    struct RunnerOptions
    {
        fs::path binaryDirectory;
        fs::path inputDirectory; // Empty selects fixed regressions.
        fs::path workDirectory;  // Optional: retain generated files in a new folder.
        std::vector<std::wstring> suiteNames;
        std::wstring testFilter = L"*";
        int timeoutSeconds = 1500;
        bool listTests = false;
    };

    // These are the four existing test programs; fuzzing is run separately.
    const std::vector<std::wstring> KnownSuiteNames = {
        L"gacl_lib_tests", L"gacl_exe_tests", L"ml_rdo_tests", L"shader_tests"
    };

    // Read our options here. Only supported GoogleTest options are passed through.
    RunnerOptions ParseOptions(int argc, wchar_t** argv)
    {
        RunnerOptions options;
        std::set<std::wstring> seenOptions;
        for (int i = 1; i < argc; ++i)
        {
            // Help and list-tests are switches; other options need a value.
            std::wstring option = argv[i];
            if (option == L"--help" || option == L"-h")
            {
                std::cout << "gacl_test_runner --binary-directory <drop>\n"
                    "  [--input-directory <content folder>] [--suite <name>] (repeatable)\n"
                    "  [--gtest_filter=<pattern>] [--gtest_list_tests] [--timeout-seconds 1500]\n"
                    "  [--filter <pattern>] (alias for --gtest_filter)\n"
                    "  [--work-directory <new folder to retain generated files>]\n"
                    "Default suites: gacl_lib_tests, gacl_exe_tests, ml_rdo_tests, shader_tests\n";
                std::exit(0);
            }
            // Accept --gtest_filter=value as well as --gtest_filter value.
            // Count the older --filter spelling as the same option.
            if (option.compare(0, 15, L"--gtest_filter=") == 0)
            {
                if (!seenOptions.insert(L"--gtest_filter").second) throw std::runtime_error("Repeated test filter");
                options.testFilter = option.substr(15);
                if (options.testFilter.empty()) throw std::runtime_error("Empty test filter");
                continue;
            }
            if (option == L"--filter") option = L"--gtest_filter";
            // Listing asks the suites for names without running the test cases.
            if (option == L"--gtest_list_tests")
            {
                if (!seenOptions.insert(option).second) throw std::runtime_error("Repeated option: " + ToUtf8(option));
                options.listTests = true;
                continue;
            }
            // Allow several --suite options, but reject repeated settings.
            if (option != L"--suite" && !seenOptions.insert(option).second)
                throw std::runtime_error("Repeated option: " + ToUtf8(option));
            if (++i == argc) throw std::runtime_error("Missing option value: " + ToUtf8(option));
            const std::wstring value = argv[i];
            if (value.empty()) throw std::runtime_error("Empty value: " + ToUtf8(option));

            // Store directory and filter values; validate their meaning below.
            if (option == L"--binary-directory") options.binaryDirectory = value;
            else if (option == L"--input-directory") options.inputDirectory = value;
            else if (option == L"--work-directory") options.workDirectory = value;
            else if (option == L"--gtest_filter") options.testFilter = value;
            else if (option == L"--timeout-seconds")
            {
                // Require a positive timeout, capped at one day per suite.
                if (value.find_first_not_of(L"0123456789") != std::wstring::npos)
                    throw std::runtime_error("Timeout must be an integer");
                const auto seconds = std::stoul(value);
                if (!seconds || seconds > 86400) throw std::runtime_error("Timeout outside 1-86400 seconds");
                options.timeoutSeconds = static_cast<int>(seconds);
            }
            else if (option == L"--suite")
            {
                // Catch misspelled suite names and accidental duplicate selections.
                if (std::find(KnownSuiteNames.begin(), KnownSuiteNames.end(), value) == KnownSuiteNames.end() ||
                    std::find(options.suiteNames.begin(), options.suiteNames.end(), value) != options.suiteNames.end())
                    throw std::runtime_error("Unknown or duplicate suite: " + ToUtf8(value));
                options.suiteNames.push_back(value);
            }
            else throw std::runtime_error("Unknown option: " + ToUtf8(option));
        }

        // Resolve existing directories now, before creating any output folders.
        if (options.binaryDirectory.empty()) throw std::runtime_error("--binary-directory is required; see --help");
        options.binaryDirectory = fs::canonical(options.binaryDirectory);
        if (!fs::is_directory(options.binaryDirectory)) throw std::runtime_error("Binary directory is not a directory");
        if (!options.inputDirectory.empty())
        {
            options.inputDirectory = fs::canonical(options.inputDirectory);
            if (!fs::is_directory(options.inputDirectory)) throw std::runtime_error("Input is not a directory");
            // Folder mode selects per-file cases unless the user supplied a filter.
            if (!seenOptions.count(L"--gtest_filter")) options.testFilter = L"ContentInput.*";
        }

        // With no explicit selection, run all four suites.
        if (options.suiteNames.empty()) options.suiteNames = KnownSuiteNames;
        return options;
    }

    // Give generated files a separate folder so tests cannot overwrite input files.
    // Delete automatic folders when finished; keep folders requested by the user.
    struct WorkingDirectory
    {
        fs::path path;
        bool keep;
        WorkingDirectory(const RunnerOptions& options) : keep(!options.workDirectory.empty())
        {
            // Use the requested folder, or choose a temporary name for this run.
            path = keep ? options.workDirectory : fs::temp_directory_path() /
                (L"gacl-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
            path = fs::weakly_canonical(fs::absolute(path));
            if (!options.inputDirectory.empty())
            {
                // Check each folder name without treating letter case as different.
                // Reject a work folder that is inside the input folder.
                auto child = path.begin();
                auto parent = options.inputDirectory.begin();
                while (child != path.end() && parent != options.inputDirectory.end() &&
                    CompareStringOrdinal(child->c_str(), -1, parent->c_str(), -1, TRUE) == CSTR_EQUAL)
                {
                    ++child;
                    ++parent;
                }
                if (parent == options.inputDirectory.end()) throw std::runtime_error("Work directory must be outside the input directory");
            }
            // Refuse an existing work folder instead of mixing two runs together.
            if (keep) fs::create_directories(path.parent_path());
            if (!fs::create_directory(path)) throw std::runtime_error("Work directory exists; choose a fresh directory");
            if (keep) std::cout << "[ WORK     ] " << ToUtf8(path.wstring()) << std::endl;
        }
        // Leaving this scope also cleans up after a failed test or timeout.
        ~WorkingDirectory()
        {
            if (!keep)
            {
                std::error_code error;
                // Windows may briefly hold a file open after a test is stopped.
                // Try cleanup for about a second, then explain any remaining error.
                for (int attempt = 0; attempt < 10; ++attempt)
                {
                    fs::remove_all(path, error);
                    if (!error) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (error) std::cerr << "Cannot remove temporary work directory " << ToUtf8(path.wstring()) << ": " << error.message() << '\n';
            }
        }
    };

    // Quote text so the printed command can be pasted back into PowerShell.
    std::string PowerShellLiteral(const std::wstring& value)
    {
        std::string quoted = "'";
        for (const char character : ToUtf8(value))
        {
            quoted += character;
            // Inside a single-quoted PowerShell string, write an apostrophe twice.
            if (character == '\'') quoted += '\'';
        }
        return quoted + "'";
    }

    // Run suites one at a time. A failed suite must not hide results from later ones.
    int RunSuites(const RunnerOptions& options)
    {
        WorkingDirectory work(options);
        int exitCode = 0;
        for (const auto& suiteName : options.suiteNames)
        {
            const auto name = ToUtf8(suiteName);
            std::cout << "\n[ SUITE    ] " << name << std::endl;
            try
            {
                // Find this test program and give it its own generated-file folder.
                const auto executable = options.binaryDirectory / (suiteName + L".exe");
                if (!fs::is_regular_file(executable)) throw std::runtime_error("Required executable missing");
                const auto directory = work.path / suiteName;
                fs::create_directory(directory);

                // Run once with the same filter and input folder the user selected.
                std::vector<std::wstring> arguments = { L"--gtest_filter=" + options.testFilter,
                    L"--gtest_color=no", L"--gtest_repeat=1" };
                if (!options.inputDirectory.empty())
                {
                    arguments.push_back(L"--input-directory");
                    arguments.push_back(options.inputDirectory.wstring());
                }
                if (options.listTests) arguments.push_back(L"--gtest_list_tests");
                // Print commands that reproduce this run. Use --work-directory
                // if you want the generated-file folder to remain for a retry.
                std::cout << "[ TEST CWD ] Set-Location -LiteralPath " << PowerShellLiteral(directory.wstring()) << '\n';
                std::cout << "[ TEST CMD ] & " << PowerShellLiteral(executable.wstring());
                for (const auto& argument : arguments) std::cout << ' ' << PowerShellLiteral(argument);
                std::cout << std::endl;

                // The process helper streams output, waits, and stops timed-out children.
                const auto process = RunProcess(executable, arguments, directory, options.timeoutSeconds);
                if (process.timedOut) throw std::runtime_error("Timed out; test process tree terminated");
                if (process.remainingChildren) throw std::runtime_error("Suite left child processes running; process tree terminated");
                if (process.exitCode) throw std::runtime_error("Test executable exited " + std::to_string(process.exitCode));
                // A successful listing is not the same thing as passing tests.
                std::cout << (options.listTests ? "[ SUITE LISTED ] " : "[ SUITE OK ] ") << name << std::endl;
            }
            catch (const std::exception& error)
            {
                // Remember the failure, print why it happened, and continue.
                exitCode = 1;
                std::cerr << "[ SUITE FAIL ] " << name << " | " << error.what() << std::endl;
            }
        }
        return exitCode;
    }
}

int wmain(int argc, wchar_t** argv)
{
    // Avoid error dialogs that could stall an unattended run.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    // Test failures return 1. Errors setting up the overall run return 2.
    try
    {
        return RunSuites(ParseOptions(argc, argv));
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
