// gacl_exe_tests\test_gacl.cpp
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
#include <DirectXTex.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "../ContentInput.h"

namespace
{
    namespace fs = std::filesystem;

    // Never rely on PATH or the caller's current directory to select the CLI.
    fs::path GaclExecutable()
    {
        std::vector<wchar_t> name(32768);
        const DWORD length = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
        if (!length || length >= name.size())
            throw std::runtime_error("Cannot resolve the test executable directory");
        const auto executable = fs::path(std::wstring(name.data(), length)).parent_path() / L"gacl.exe";
        if (!fs::is_regular_file(executable))
            throw std::runtime_error("gacl.exe is required beside gacl_exe_tests.exe");
        return executable;
    }

    bool WithinDirectory(const fs::path& path, const fs::path& directory)
    {
        const auto normalized = fs::weakly_canonical(path);
        const auto root = fs::weakly_canonical(directory);
        auto item = normalized.begin();
        for (const auto& component : root)
        {
            if (item == normalized.end() || _wcsicmp(item->c_str(), component.c_str()) != 0)
                return false;
            ++item;
        }
        return true;
    }

    class WorkDirectory
    {
    public:
        WorkDirectory(const fs::path& inputDirectory)
        {
            static std::atomic<unsigned> sequence{ 0 };
            // The launcher owns CWD's tree even when forced termination skips this destructor.
            const auto root = fs::canonical(fs::current_path());
            if (WithinDirectory(root, inputDirectory))
                throw std::runtime_error("Run from a working directory outside the input tree");
            for (unsigned attempt = 0; attempt < 100; ++attempt)
            {
                const auto candidate = root / (L"gacl_exe_content_" + std::to_wstring(GetCurrentProcessId())
                    + L"_" + std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(sequence++));
                if (fs::create_directory(candidate))
                {
                    path = candidate;
                    return;
                }
            }
            throw std::runtime_error("Cannot create an isolated work directory beneath the current directory");
        }

        ~WorkDirectory()
        {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }

        WorkDirectory(const WorkDirectory&) = delete;
        WorkDirectory& operator=(const WorkDirectory&) = delete;
        fs::path path;
    };

    std::wstring QuoteArgument(const std::wstring& value)
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
            result.append(character == L'"' ? 2 * slashes + 1 : slashes, L'\\');
            result += character;
            slashes = 0;
        }
        result.append(2 * slashes, L'\\');
        return result + L"\"";
    }

    DWORD RunGacl(const fs::path& executable, const fs::path& input, const fs::path& work)
    {
        // --export writes opaque per-mip/slice streams. --output also saves a DDS
        // whose header and full pixel payload can be checked with DirectXTex.
        std::wstring command = QuoteArgument(executable.wstring()) + L" " + QuoteArgument(input.wstring())
            + L" --shufflecompress --export " + QuoteArgument((work / L"exports" / L"texture").wstring())
            + L" --output " + QuoteArgument((work / L"processed.dds").wstring());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
            nullptr, work.c_str(), &startup, &process))
            throw std::runtime_error("CreateProcessW(gacl.exe) failed: " + std::to_string(GetLastError()));

        CloseHandle(process.hThread);
        constexpr DWORD timeoutMilliseconds = 120000;
        const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMilliseconds);
        if (wait != WAIT_OBJECT_0)
        {
            // Do not leave a child using the work directory after this test ends.
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, INFINITE);
            CloseHandle(process.hProcess);
            throw std::runtime_error(wait == WAIT_TIMEOUT ? "gacl.exe timed out after 120 seconds"
                : "Waiting for gacl.exe failed");
        }
        DWORD exitCode = 0;
        const BOOL queried = GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hProcess);
        if (!queried)
            throw std::runtime_error("GetExitCodeProcess(gacl.exe) failed");
        return exitCode;
    }

    bool SameFileBytes(const fs::path& left, const fs::path& right)
    {
        if (fs::file_size(left) != fs::file_size(right))
            return false;
        std::ifstream a(left, std::ios::binary);
        std::ifstream b(right, std::ios::binary);
        if (!a || !b)
            throw std::runtime_error("Cannot open input files for byte-preservation check");
        std::array<char, 65536> aBytes{}, bBytes{};
        while (a && b)
        {
            a.read(aBytes.data(), static_cast<std::streamsize>(aBytes.size()));
            b.read(bBytes.data(), static_cast<std::streamsize>(bBytes.size()));
            if (a.gcount() != b.gcount() || !std::equal(aBytes.begin(), aBytes.begin() + a.gcount(), bBytes.begin()))
                return false;
        }
        if (a.bad() || b.bad())
            throw std::runtime_error("Cannot read input files for byte-preservation check");
        return a.eof() && b.eof();
    }

    bool SupportsShuffleExport(DXGI_FORMAT format)
    {
        // Match the actual export branch in Api/gaclexe/Processing.cpp, not the
        // wider set of formats accepted for other CLI operations (e.g. BC2/6H).
        switch (format)
        {
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return true;
        default:
            return false;
        }
    }

    void ValidateGaclFile(const fs::path& file, const fs::path& inputDirectory)
    {
        if (ContentInput::Extension(file) != L".dds")
            throw ContentInput::Unsupported("gacl CLI content validation supports DDS only");

        const auto executable = GaclExecutable();
        WorkDirectory work(inputDirectory);
        const auto copiedInput = work.path / L"input.dds";
        const auto originalWriteTime = fs::last_write_time(file);
        fs::copy_file(file, copiedInput);
        ASSERT_TRUE(SameFileBytes(file, copiedInput)) << "Input copy must match the original";
        fs::create_directory(work.path / L"exports");

        DirectX::TexMetadata metadata{};
        DirectX::ScratchImage image;
        const HRESULT loaded = DirectX::LoadFromDDSFile(copiedInput.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
        if (FAILED(loaded))
        {
            const DWORD exitCode = RunGacl(executable, copiedInput, work.path);
            EXPECT_NE(exitCode, 0u) << "gacl.exe accepted a malformed/unreadable DDS";
            EXPECT_TRUE(SameFileBytes(file, copiedInput)) << "Original DDS or isolated copy changed";
            EXPECT_EQ(originalWriteTime, fs::last_write_time(file)) << "Original input timestamp changed";
            FAIL() << "Malformed/unreadable DDS (DirectXTex HRESULT " << static_cast<unsigned long>(loaded)
                << "); gacl.exe exit code " << exitCode;
        }

        if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || !SupportsShuffleExport(metadata.format))
        {
            throw ContentInput::Unsupported("CLI shuffle/compress validation supports 2D BC1/3/4/5/7 DDS only; DXGI format "
                + std::to_string(static_cast<unsigned>(metadata.format)) + ", dimension "
                + std::to_string(static_cast<unsigned>(metadata.dimension)));
        }

        const DWORD exitCode = RunGacl(executable, copiedInput, work.path);
        EXPECT_TRUE(SameFileBytes(file, copiedInput)) << "Original DDS or isolated copy changed";
        EXPECT_EQ(originalWriteTime, fs::last_write_time(file)) << "Original input timestamp changed";
        ASSERT_EQ(exitCode, 0u) << "gacl.exe shuffle/compress failed";

        size_t exportCount = 0;
        for (const auto& entry : fs::directory_iterator(work.path / L"exports"))
        {
            ASSERT_TRUE(entry.is_regular_file()) << "Unexpected non-file export";
            EXPECT_GT(entry.file_size(), 0u) << "Empty shuffle/compress export: " << entry.path().u8string();
            ++exportCount;
        }
        // The CLI may emit an .uncompressed fallback for incompressible content.
        // Both that fallback and a compressed stream must contain actual data.
        ASSERT_GT(exportCount, 0u) << "gacl.exe produced no shuffle/compress exports";
        const auto output = work.path / L"processed.dds";
        ASSERT_TRUE(fs::is_regular_file(output)) << "gacl.exe produced no output DDS";
        ASSERT_GT(fs::file_size(output), 0u) << "gacl.exe produced an empty DDS";

        DirectX::TexMetadata outputMetadata{};
        DirectX::ScratchImage outputImage;
        ASSERT_TRUE(SUCCEEDED(DirectX::LoadFromDDSFile(output.c_str(), DirectX::DDS_FLAGS_NONE,
            &outputMetadata, outputImage))) << "Output DDS header/payload is invalid";
        EXPECT_EQ(metadata.width, outputMetadata.width);
        EXPECT_EQ(metadata.height, outputMetadata.height);
        EXPECT_EQ(metadata.depth, outputMetadata.depth);
        EXPECT_EQ(metadata.arraySize, outputMetadata.arraySize);
        EXPECT_EQ(metadata.mipLevels, outputMetadata.mipLevels);
        EXPECT_EQ(metadata.format, outputMetadata.format);
        EXPECT_EQ(metadata.dimension, outputMetadata.dimension);
        EXPECT_EQ(metadata.IsCubemap(), outputMetadata.IsCubemap());
        ASSERT_EQ(image.GetPixelsSize(), outputImage.GetPixelsSize());
        EXPECT_EQ(std::memcmp(image.GetPixels(), outputImage.GetPixels(), image.GetPixelsSize()), 0)
            << "Shuffle/compress-only processing changed the DDS pixel payload";
    }
}

void RegisterGaclExeContentTests(const std::filesystem::path& input)
{
    ContentInput::RegisterDirectoryContents(input, [input](const ContentInput::File& file)
    {
        ValidateGaclFile(file.path, input);
    });
}

// Basic sanity test
TEST(HelloWorld, BasicEquality) {
    EXPECT_EQ(42, 40 + 2);
}
