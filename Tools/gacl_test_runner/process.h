// Windows subprocess ownership for the test runner.
#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>

// Keep process failures separate from the test executable's own exit code.
struct ProcessResult
{
    DWORD exitCode = 0; // The exit code reported by the test executable.
    bool timedOut = false; // The launcher stopped a run that took too long.
    bool remainingChildren = false; // The main test exited but left children running.
    double seconds = 0; // Total time spent starting, waiting, and cleaning up.
};

// Run in the supplied work folder, with output sent directly to the caller.
ProcessResult RunProcess(const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments, const std::filesystem::path& working,
    int timeout);
