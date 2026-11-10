// Launch the existing GoogleTest suites; validation remains in each suite.
#define NOMINMAX
#include <Windows.h>
#include <wrl\wrappers\corewrappers.h>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    using Win32Handle =
        Microsoft::WRL::Wrappers::HandleT<Microsoft::WRL::Wrappers::HandleTraits::HANDLENullTraits>;
    using Microsoft::WRL::Wrappers::FileHandle;

    const std::vector<std::wstring> Suites{
        L"gacl_lib_tests", L"gacl_exe_tests", L"ml_rdo_tests", L"shader_tests" };

    // Turns a failed Windows API operation into a C++ exception.
    void ThrowIfWin32Failed(bool success, const char* operation)
    {
        if (!success) throw std::runtime_error(std::string(operation) +
            " (Windows error " + std::to_string(GetLastError()) + ")");
    }

    // Converts a string into a correctly quoted Windows command-line argument.
    std::wstring QuoteCommandLineArgument(const std::wstring& value)
    {
        std::wstring result = L"\"";
        size_t slashes = 0;
        for (const wchar_t character : value)
        {
            if (character == L'\\') { ++slashes; continue; }
            result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            result += character;
            slashes = 0;
        }
        // Trailing backslashes must not escape the closing quote.
        result.append(slashes * 2, L'\\');
        return result + L'"';
    }

    // Launch one test executable, wait for it, clean up its process tree.
    // Returns the exe's exit code, or throws
    DWORD RunProcess(const fs::path& executable, const std::vector<std::wstring>& arguments,
        const fs::path& working, ULONGLONG deadline)
    {
        // Create a Windows job object
        Win32Handle job(CreateJobObjectW(nullptr, nullptr));
        ThrowIfWin32Failed(job.IsValid(), "Create process job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ThrowIfWin32Failed(SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
            "Set job limits");

        // Connect input/output
        SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
        FileHandle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, 0, nullptr));
        ThrowIfWin32Failed(input.IsValid(), "Open null standard input");
        Win32Handle output, error;
        ThrowIfWin32Failed(DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_OUTPUT_HANDLE), GetCurrentProcess(),
            output.GetAddressOf(), 0, TRUE, DUPLICATE_SAME_ACCESS), "Inherit standard output");
        ThrowIfWin32Failed(DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(),
            error.GetAddressOf(), 0, TRUE, DUPLICATE_SAME_ACCESS), "Inherit standard error");
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input.Get();
        startup.hStdOutput = output.Get();
        startup.hStdError = error.Get();

        // Build the command line
        std::wstring command = QuoteCommandLineArgument(executable.wstring());
        for (const auto& argument : arguments) command += L" " + QuoteCommandLineArgument(argument);
        PROCESS_INFORMATION process{};

        // Run the command
        ThrowIfWin32Failed(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, working.c_str(), &startup, &process),
            "Launch test process");
        Win32Handle processHandle(process.hProcess), threadHandle(process.hThread);
        // Assign before resuming so descendants cannot escape timeout cleanup.
        if (!AssignProcessToJobObject(job.Get(), process.hProcess))
        {
            const DWORD errorCode = GetLastError();
            TerminateProcess(process.hProcess, 126);
            WaitForSingleObject(process.hProcess, 10000);
            SetLastError(errorCode);
            ThrowIfWin32Failed(false, "Assign test process to job");
        }
        ThrowIfWin32Failed(ResumeThread(process.hThread) != static_cast<DWORD>(-1), "Resume test process");
        const ULONGLONG now = GetTickCount64();
        const DWORD waited = WaitForSingleObject(process.hProcess,
            now < deadline ? static_cast<DWORD>(deadline - now) : 0);
        ThrowIfWin32Failed(waited == WAIT_OBJECT_0 || waited == WAIT_TIMEOUT, "Wait for test process");
        DWORD exitCode = 0;
        ThrowIfWin32Failed(GetExitCodeProcess(process.hProcess, &exitCode), "Get test exit code");
        ThrowIfWin32Failed(TerminateJobObject(job.Get(), 124), "Stop remaining test processes");
        ThrowIfWin32Failed(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0, "Wait for stopped test process");

        // Wait and finish
        if (waited == WAIT_TIMEOUT) throw std::runtime_error("Overall run timeout reached");
        return exitCode;
    }

    struct Options
    {
        fs::path binaryDirectory, inputDirectory;
        std::vector<std::wstring> suites;
        std::wstring filter;
        int timeout = 3600;
        bool help = false;
    };

    // Convert command-line arguments into settings
    Options ParseOptions(int argc, wchar_t** argv)
    {
        enum class Option { Help, BinaryDirectory, InputDirectory, Suite, GtestFilter, TimeoutSeconds };
        const std::map<std::wstring, Option> optionNames{
            { L"--help", Option::Help }, { L"-h", Option::Help },
            { L"--binary-directory", Option::BinaryDirectory },
            { L"--input-directory", Option::InputDirectory },
            { L"--suite", Option::Suite },
            { L"--gtest_filter", Option::GtestFilter },
            { L"--timeout-seconds", Option::TimeoutSeconds }
        };
        wchar_t executable[32768];
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        ThrowIfWin32Failed(length != 0 && length < 32768, "Locate runner");
        Options options;
        options.binaryDirectory = fs::path(executable).parent_path();
        // For each argument
        for (int i = 1; i < argc; ++i)
        {
            // Convert to wstring
            const std::wstring option = argv[i];
            auto value = [&]() -> std::wstring {
                if (++i == argc || !*argv[i]) throw std::runtime_error("Missing option value");
                return argv[i];
            };
            const auto entry = optionNames.find(option.rfind(L"--gtest_filter=", 0) == 0 ? L"--gtest_filter" : option);
            if (entry == optionNames.end()) throw std::runtime_error("Unknown option");

            // Match with options
            switch (entry->second)
            {
                case Option::Help: options.help = true; break;
                case Option::BinaryDirectory: options.binaryDirectory = value(); break;
                case Option::InputDirectory: options.inputDirectory = value(); break;
                case Option::Suite:
                {
                    const auto suite = value();
                    if (std::find(Suites.begin(), Suites.end(), suite) == Suites.end())
                        throw std::runtime_error("Unknown suite");
                    if (std::find(options.suites.begin(), options.suites.end(), suite) == options.suites.end())
                        options.suites.push_back(suite);
                    break;
                }
                case Option::GtestFilter:
                    options.filter = option == L"--gtest_filter" ? value() : option.substr(15);
                    if (options.filter.empty()) throw std::runtime_error("Filter must not be empty");
                    break;
                case Option::TimeoutSeconds:
                {
                    const auto text = value();
                    size_t used = 0;
                    options.timeout = std::stoi(text, &used);
                    if (used != text.size() || options.timeout <= 0 || options.timeout > 86400)
                        throw std::runtime_error("Timeout must be between 1 and 86400 seconds");
                    break;
                }
            }
        }
        if (options.help) return options;
        options.binaryDirectory = fs::canonical(options.binaryDirectory);
        if (!fs::is_directory(options.binaryDirectory)) throw std::runtime_error("Binary directory is not a directory");
        if (!options.inputDirectory.empty())
        {
            options.inputDirectory = fs::canonical(options.inputDirectory);
            if (!fs::is_directory(options.inputDirectory)) throw std::runtime_error("Input is not a directory");
        }
        if (options.suites.empty()) options.suites = Suites;
        if (options.filter.empty()) options.filter = options.inputDirectory.empty() ? L"*" : L"ContentInput.*";
        return options;
    }

    // Manages the temporary filesystem workspace.
    struct WorkingDirectory
    {
        fs::path path;

        // constructor creates a directory under the system temp folder, named using the process ID and tick count
        WorkingDirectory()
        {
            path = fs::temp_directory_path() /
                (L"gacl-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
            if (!fs::create_directory(path)) throw std::runtime_error("Cannot create test working directory");
        }

        // destructor removes the workspace, retrying up to ten times with 100 ms pauses for temporary file locks.
        ~WorkingDirectory()
        {
            std::error_code error;
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                fs::remove_all(path, error);
                if (!error) return;
                Sleep(100);
            }
            std::cerr << "[ CLEANUP FAIL ] " << error.message() << '\n';
        }
    };

    // Coordinates the complete run, sequentially, one suite at a time.
    int RunTestSuites(const Options& options)
    {
        const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(options.timeout) * 1000;
        WorkingDirectory workingDir;
        size_t failed = 0;
        std::vector<std::wstring> summary;
        for (const auto& suite : options.suites)
        {
            const ULONGLONG started = GetTickCount64();
            bool suiteFailed = true;
            std::wostringstream result;
            try
            {
                if (GetTickCount64() >= deadline)
                    throw std::runtime_error("Not run: overall timeout exhausted");
                std::wcout << L"[ SUITE RUN ] " << suite << std::endl;
                const auto directory = workingDir.path / suite;
                fs::create_directory(directory);
                std::vector<std::wstring> arguments{ L"--gtest_color=no", L"--gtest_repeat=1",
                    L"--gtest_filter=" + options.filter };
                if (!options.inputDirectory.empty())
                {
                    arguments.push_back(L"--input-directory");
                    arguments.push_back(options.inputDirectory.wstring());
                }
                const DWORD exitCode = RunProcess(options.binaryDirectory / (suite + L".exe"),
                    arguments, directory, deadline);
                suiteFailed = exitCode != 0;
                result << L"exit " << exitCode;
                if (suiteFailed)
                {
                    result << L", 0x" << std::hex << std::uppercase << std::setfill(L'0')
                        << std::setw(8) << exitCode;
                    if (exitCode == 0xC0000005) result << L" (access violation)";
                    if (exitCode == 0xC0000409) result << L" (fail-fast)";
                }
            }
            catch (const std::exception& error)
            {
                result << error.what();
            }
            const auto line = suite + L": " + (suiteFailed ? L"FAIL" : L"PASS") + L" (" +
                result.str() + L", " + std::to_wstring(GetTickCount64() - started) + L" ms)";
            std::wcout << (suiteFailed ? L"[ SUITE FAIL ] " : L"[ SUITE OK ] ") << line << std::endl;
            summary.push_back(line);
            failed += suiteFailed;
        }
        std::wcout << L"\n[ RUN SUMMARY ] " << summary.size() - failed << L" suites passed, "
            << failed << L" failed" << std::endl;
        for (const auto& line : summary) std::wcout << L"  " << line << std::endl;
        return failed ? 1 : 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        // First parse options, then run tests
        const auto options = ParseOptions(argc, argv);
        if (options.help)
        {
            std::wcout << L"Usage: gacl_test_runner [options]\n"
                L"  --binary-directory <dir>  Suite EXEs (default: beside runner)\n"
                L"  --input-directory <dir>   Content files; omit for fixed regressions\n"
                L"  --suite <name>            Select a suite; repeat to select several\n"
                L"  --gtest_filter <pattern>  Default: ContentInput.* with input, otherwise *\n"
                L"  --timeout-seconds <n>     Overall run timeout (default: 3600)\n"
                L"Suites: gacl_lib_tests, gacl_exe_tests, ml_rdo_tests, shader_tests\n";
            return 0;
        }
        return RunTestSuites(options);
    }
    catch (const std::exception& error)
    {
        std::cerr << "gacl_test_runner: " << error.what() << std::endl;
        return 2;
    }
}
