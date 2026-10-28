// Start a Windows test process, stream its output, and stop its child processes.
// This file does not know about GACL formats, test cases, or reports.
#define NOMINMAX
#include "process.h"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace
{
    // Close each Windows handle automatically when it leaves scope.
    struct Handle
    {
        HANDLE value = nullptr;
        explicit Handle(HANDLE handle = nullptr) : value(handle) {}
        ~Handle()
        {
            if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
    };

    // Attach the Windows error number when a system call fails.
    void Check(bool success, const char* operation)
    {
        if (!success) throw std::runtime_error(std::string(operation) +
            " (Windows error " + std::to_string(GetLastError()) + ")");
    }

    // Quote one argument using Windows command-line rules.
    // Backslashes next to quotes need special handling to preserve the exact text.
    std::wstring Quote(const std::wstring& value)
    {
        std::wstring result = L"\"";
        size_t slashes = 0;
        for (const wchar_t character : value)
        {
            if (character == L'\\')
            {
                ++slashes;
                continue;
            }
            // Keep ordinary slashes; double them before a quote.
            result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            result += character;
            slashes = 0;
        }
        // Double trailing slashes so they do not escape our closing quote.
        result.append(slashes * 2, L'\\');
        return result + L'"';
    }

    // The job tracks the main process and any children it starts.
    DWORD ActiveProcesses(HANDLE job)
    {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
        Check(QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr),
            "Query process job");
        return info.ActiveProcesses;
    }
}

ProcessResult RunProcess(const fs::path& executable, const std::vector<std::wstring>& arguments,
    const fs::path& working, int timeout)
{
    // A job groups the whole process tree. Closing it stops anything still running.
    const auto started = Clock::now();
    Handle job(CreateJobObjectW(nullptr, nullptr));
    Check(job.value != nullptr, "Create process job");
    // Enable cleanup even if this function throws before its normal shutdown path.
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    Check(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)), "Set job limits");

    // Give tests an empty input stream so an unexpected prompt cannot wait forever.
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
    Check(input.value != INVALID_HANDLE_VALUE, "Open null standard input");
    // Send output straight to the caller, whether it is a console, file, or pipe.
    // Use duplicate handles so the child does not take ownership of ours.
    Handle output, error;
    Check(DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_OUTPUT_HANDLE), GetCurrentProcess(),
        &output.value, 0, TRUE, DUPLICATE_SAME_ACCESS), "Inherit standard output");
    Check(DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(),
        &error.value, 0, TRUE, DUPLICATE_SAME_ACCESS), "Inherit standard error");

    // Pass only standard input/output/error to the child, not unrelated open handles.
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    // Windows first tells us how much space its handle-list setup needs.
    std::vector<unsigned char> storage(attributeBytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    Check(InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes), "Initialize process attributes");
    // Release that setup automatically if a later step fails.
    struct AttributesCleanup
    {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        ~AttributesCleanup() { DeleteProcThreadAttributeList(list); }
    } attributeCleanup{ attributes };
    // Connect the three allowed handles to the child's startup settings.
    HANDLE handles[] = { input.value, output.value, error.value };
    Check(UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr),
        "Set inherited process handles");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    // These streams are the ones GoogleTest and GACL print to.
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = output.value;
    startup.StartupInfo.hStdError = error.value;
    startup.lpAttributeList = attributes;

    // Keep the caller's environment, except GoogleTest settings that could split,
    // repeat, filter, or redirect the requested run behind the launcher's back.
    const wchar_t* inherited = GetEnvironmentStringsW();
    Check(inherited != nullptr, "Read process environment");
    std::vector<wchar_t> environment;
    for (const wchar_t* entry = inherited; *entry; entry += wcslen(entry) + 1)
    {
        if (_wcsnicmp(entry, L"GTEST_", 6) != 0)
            environment.insert(environment.end(), entry, entry + wcslen(entry) + 1);
    }
    // Windows expects the copied environment to end with an extra zero character.
    FreeEnvironmentStringsW(const_cast<wchar_t*>(inherited));
    environment.push_back(L'\0');

    // Quote every argument and start paused, before any test code can run.
    std::wstring command = Quote(executable.wstring());
    for (const auto& argument : arguments) command += L" " + Quote(argument);
    PROCESS_INFORMATION process{};
    Check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
        environment.data(), working.c_str(), &startup.StartupInfo, &process), "Launch test process");
    // Keep both process handles owned here until cleanup is complete.
    Handle processHandle(process.hProcess);
    Handle threadHandle(process.hThread);
    // Add the paused process to the job before it can start any children.
    if (!AssignProcessToJobObject(job.value, process.hProcess))
    {
        // Assignment failed: stop the paused process instead of leaving it behind.
        const DWORD code = GetLastError();
        TerminateProcess(process.hProcess, 126);
        WaitForSingleObject(process.hProcess, 10000);
        SetLastError(code);
        Check(false, "Assign test process to job");
    }

    // Start the test and wait until it exits or reaches the time limit.
    Check(ResumeThread(process.hThread) != static_cast<DWORD>(-1), "Resume test process");
    const DWORD waited = WaitForSingleObject(process.hProcess, static_cast<DWORD>(timeout) * 1000);
    Check(waited == WAIT_OBJECT_0 || waited == WAIT_TIMEOUT, "Wait for test process");
    ProcessResult result;
    result.timedOut = waited == WAIT_TIMEOUT;
    // The main process may be finished before Windows updates the job count.
    // Allow a short delay before deciding that child processes were left running.
    auto cleanup = Clock::now();
    while (!result.timedOut && ActiveProcesses(job.value) && Clock::now() - cleanup < std::chrono::milliseconds(200))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // Stop the whole tree after a timeout or after the main process exits.
    result.remainingChildren = !result.timedOut && ActiveProcesses(job.value) != 0;
    Check(TerminateJobObject(job.value, result.timedOut ? 124 : 125), "Terminate remaining test processes");

    // Wait for stopped children to disappear before returning to the launcher.
    cleanup = Clock::now();
    while (ActiveProcesses(job.value) && Clock::now() - cleanup < std::chrono::seconds(10))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (ActiveProcesses(job.value)) throw std::runtime_error("Test descendants did not terminate");

    // Return the test's exit code and the reasons the launcher may reject this run.
    Check(GetExitCodeProcess(process.hProcess, &result.exitCode), "Get test exit code");
    result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    return result;
}
