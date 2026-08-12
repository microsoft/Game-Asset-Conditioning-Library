//--------------------------------------------------------------------------------------
// Game.cpp
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "Game.h"
#include "Processing.h"
#include "SharedDefinitions.h"
#include "TestParameters.h"
#include <filesystem>

extern void ExitGame() noexcept;

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// BC7 unshuffle shaders are mode-specific.
static std::wstring s_perModeUnshufflePSONames[9] =
{
    L"UnshuffleCSMode0.cso",
    L"UnshuffleCSMode1.cso",
    L"UnshuffleCSMode2.cso",
    L"UnshuffleCSMode3.cso",
    L"UnshuffleCSMode4.cso",
    L"UnshuffleCSMode5.cso",
    L"UnshuffleCSMode6.cso",
    L"UnshuffleCSMode7.cso",
    L"UnshuffleCSMode8.cso"
};

struct ShuffleShaderInfo
{
    std::wstring fileName;
    std::wstring signatureName;
    std::wstring pixName;
    uint32_t blockSizes;
    uint32_t blocksPerThread;
};



#ifdef NEWBC1345
// Final BC1\3\4\5 shaders handle multiple transform IDs based on the microShuffleId and useSpaceCurve parameters. The transform ID is passed in as a shader constant.
constexpr uint32_t gNumTransformsPerShader[4] = { 4, 4, 2, 2 };
constexpr uint32_t gaclTransformIDs[4][4]
{
    {   // BC1
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224_SC,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44_SC
    },
    {   // BC3
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224_SC,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664_SC
    },
    {   // BC4
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116_SC
    },
    {   // BC5
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116,
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116_SC
    },
};

static const ShuffleShaderInfo gShaderInfo[] = {                               // data processed per 256-thread group 
    {L"UnshuffleBC1x.cso", L"BC1xCSRootSignature", L"BC1x Unshuffle", 8,  2},  // 4KB
    {L"UnshuffleBC3x.cso", L"BC3xCSRootSignature", L"BC3x Unshuffle", 16, 4},  // 16KB
    {L"UnshuffleBC4x.cso", L"BC4xCSRootSignature", L"BC4x Unshuffle", 8,  4},  // 8KB
    {L"UnshuffleBC5x.cso", L"BC5xCSRootSignature", L"BC5x Unshuffle", 16, 4},  // 16KB
    {L"UnshuffleCurveOnly.cso", L"BCnCurveOnlyRootSignature", L"Uncurve only", 16, 32},  // 16KB, but with a thread group size of 32, so 512 bytes per thread
};
#else // Preview #1 interim shaders
static const ShuffleShaderInfo gShaderInfo[] = {
    {L"UnshuffleBC1.cso", L"BC1CSRootSignature", L"BC1 Unshuffle", 8,  2},
    {L"UnshuffleBC3.cso", L"BC3CSRootSignature", L"BC3 Unshuffle", 16, 4},
    {L"UnshuffleBC4.cso", L"BC4CSRootSignature", L"BC4 Unshuffle", 8,  4},
    {L"UnshuffleBC5.cso", L"BC5CSRootSignature", L"BC5 Unshuffle", 16, 4},
#endif


Game::Game() noexcept(false) : 
    m_uavHeapOffsetInBytes(0),
    m_allTestsPassed(false),
    m_minHWSupportedWaveSize(0),
    m_maxHWSupportedWaveSize(0),
    m_forceWarp(false),
    m_DHIncrementSize(0),
    m_UAVOutputBufferGpuHandle{},
    m_offsetIntoHistogram(0u),
    m_offsetIntoBinning(0u),
    m_offsetIntoHeader(0u)
    #if RUN_TEST_FROM_TEXTURE_IN_DISK
    , m_inputPath(L"")
    #endif  
{
    m_deviceResources = std::make_unique<DX::DeviceResources>();
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(LPWSTR lpCmdLine)
{
    int numArgs = 0;
    LPWSTR* argList = CommandLineToArgvW(lpCmdLine, &numArgs);

#ifdef CAPTURE_PIX
    PIXLoadLatestWinPixGpuCapturerLibrary();
#endif
    
    #if RUN_TEST_FROM_TEXTURE_IN_DISK
    if (!lpCmdLine || lpCmdLine[0] == L'\0' || !argList || numArgs == 0)
    {
        throw std::runtime_error("Did not provide any argument for the sample.");
    }
    #endif

    for (int i = 0; i < numArgs;)
    {
        LPWSTR arg = argList[i];

        #if RUN_TEST_FROM_TEXTURE_IN_DISK
        if (wcscmp(arg, L"/path") == 0 || wcscmp(arg, L"-path") == 0)
        {
            m_inputPath = argList[i + 1];
            i += 2;
        }
        else
        #endif
        if (wcscmp(arg, L"/warp") == 0 || wcscmp(arg, L"-warp") == 0)
        {
            m_forceWarp = true;
            i += 1;
        }
        else 
        {
            i += 1; // skip unrecognized argument (e.g. exe name, GTest flags)
        }
    }
    LocalFree(argList);

    m_deviceResources->CreateDeviceResources(m_forceWarp);

    auto device = m_deviceResources->GetD3DDevice();
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
    DX::ThrowIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1)));
    m_minHWSupportedWaveSize = options1.WaveLaneCountMin;
    m_maxHWSupportedWaveSize = options1.WaveLaneCountMax;
}

// Deterministic per-block, per-byte value. Uses a multiplicative hash so
// every block has varied, reproducible non-zero bit patterns without relying
// on a random seed. Same blockIdx+byteIdx always produces the same byte.
static uint8_t BlockByteHash(uint32_t blockIdx, uint32_t byteIdx)
{
    uint32_t x = blockIdx * 2654435761u ^ byteIdx * 1234567891u ^ 1u;
    x ^= x >> 16;
    x *= 0x45d9f3bu;
    x ^= x >> 16;
    return static_cast<uint8_t>(x);
}

void Game::CreateBC7RawFromMetadata(uint32_t widthPixels, uint32_t heightPixels, std::vector<uint8_t>& rawBC7)
{
    uint32_t widthInBlocks  = (widthPixels  + 3) / 4;
    uint32_t heightInBlocks = (heightPixels + 3) / 4;
    uint32_t totalBlocks    = widthInBlocks * heightInBlocks;

    rawBC7.resize(static_cast<size_t>(totalBlocks) * BC7_BYTES_PER_BLOCK);

    for (uint32_t blockIdx = 0; blockIdx < totalBlocks; ++blockIdx)
    {
        uint32_t mode  = blockIdx % (BC7_MODES_COUNT);
        uint8_t* block = rawBC7.data() + blockIdx * BC7_BYTES_PER_BLOCK;

        if (mode == 8)
        {
            for (uint32_t b = 0; b < BC7_BYTES_PER_BLOCK; ++b)  block[b] = 0u;
            continue;
        }

        // Fill all 16 bytes with varied, reproducible values so that
        // endpoints, indices, P-bits and rotation fields are non-zero.
        for (uint32_t b = 0; b < BC7_BYTES_PER_BLOCK; ++b)
            block[b] = BlockByteHash(blockIdx, b);

        // Fix byte 0: the mode indicator occupies bits [0..mode] of byte 0
        // (bit `mode` = 1, bits below it = 0). Clear those bits from the
        // hash value and OR in the correct mode indicator.
        uint32_t modeMask = (mode < 7u) ? ((2u << mode) - 1u) : 0xFFu;
        block[0] = static_cast<uint8_t>((block[0] & ~modeMask) | (1u << mode));
    }
}

void Game::CreateBCnRawFromMetadata(uint32_t widthPixels, uint32_t heightPixels, uint32_t elementSize, std::vector<uint8_t>& rawBCn)
{
    uint32_t widthInBlocks = (widthPixels + 3) / 4;
    uint32_t heightInBlocks = (heightPixels + 3) / 4;
    uint32_t totalBlocks = widthInBlocks * heightInBlocks;

    rawBCn.resize(static_cast<size_t>(totalBlocks) * elementSize);

    for (uint32_t blockIdx = 0; blockIdx < totalBlocks; ++blockIdx)
    {
        uint8_t* block = rawBCn.data() + blockIdx * elementSize;

        // Fill all 8/16 bytes with varied, reproducible values so that
        // endpoints, fields are non-zero.
        for (uint32_t b = 0; b < elementSize; ++b)
            block[b] = BlockByteHash(blockIdx, b);
    }
}

HRESULT(*Shuffle_BC1345[])(uint8_t* dest, const uint8_t* src, size_t size, size_t version) {
    &Shuffle_BC1,
    &Shuffle_BC3,
    &Shuffle_BC4,
    &Shuffle_BC5
};

#if RUN_TEST_FROM_TEXTURE_IN_DISK
extern const char* ToString(DXGI_FORMAT Format);
extern DXGI_FORMAT GetBaseFormat(DXGI_FORMAT Format);

bool Game::RunAllPermutationsForLoadedTexture()
{
    // Load dds texture contents from m_inputPath and fills out the out params widthPixels, heightPixels, format, bcTextureRaw
    std::vector<uint8_t> bcTextureRaw;
    uint32_t widthPixels, heightPixels;
    DXGI_FORMAT format;
    LoadTextureFromDisk(widthPixels, heightPixels, format, bcTextureRaw);

    size_t bcSizeInBytes = bcTextureRaw.size();
    DXGI_FORMAT baseFormat = GetBaseFormat(format);

    std::wcout << L"\n-- Loaded BC (raw) " << ToString(format) << " texture of resolution " << widthPixels << L"x" << heightPixels << std::endl;

    bool allPermutationPassed = true;

    if (baseFormat == DXGI_FORMAT_BC7_TYPELESS)
    {
        // Assume this scope is a for loop, going over every permutation
        for (uint32_t chunkIndex = 0; chunkIndex < std::size(g_testChunkSizes); ++chunkIndex)
        {
            for (uint32_t stratIndex = 0; stratIndex < std::size(g_endpointOrderStrategies); ++stratIndex)
            {
                for (uint32_t scIndex = 0; scIndex < std::size(g_useSpaceCurve); ++scIndex)
                {
                    for (uint32_t patternSetIndex = 0; patternSetIndex < std::size(g_patternSets); ++patternSetIndex)
                    {
                        BC7ModeSplitShufflePattern Patterns[8];
                        memcpy(Patterns, &g_patternSets[patternSetIndex][0], sizeof(Patterns));

                        // GACL Shuffle parameters for this run
                        ShuffleParameters shuffleParams = {};
                        shuffleParams.useSpaceCurve = static_cast<bool>(g_useSpaceCurve[scIndex]);
                        shuffleParams.endpointOrderStrategy = g_endpointOrderStrategies[stratIndex];
                        shuffleParams.ChunkSize = g_testChunkSizes[chunkIndex];
                        shuffleParams.Patterns = Patterns;


                        // Will hold the data for the buffer shuffled in ProcessTexture
                        std::vector<uint8_t> shuffledBuffer;

                        std::stringstream testName;
                        testName << "Res" << std::to_string(widthPixels) << "x" << std::to_string(heightPixels) <<
                            "_ChunkSize" << gs_chunkSizesName[chunkIndex] <<
                            "_Strat" << std::to_string(shuffleParams.endpointOrderStrategy) <<
                            "_SC" << (shuffleParams.useSpaceCurve ? "True" : "False") <<
                            "_PatternSet" << std::to_string(patternSetIndex);

                        // Call that interfaces with gaclLib to shuffle the texture.
                        ShuffleBC7Texture(
                            shuffledBuffer,
                            bcTextureRaw.data(),
                            bcTextureRaw.size(),
                            widthPixels,
                            shuffleParams);

#if DUMP_SHUFFLED_BUFFER_TO_DISK
                        std::stringstream dumpFileName;
                        dumpFileName << testName.str() << ".bin";
                        std::ofstream(dumpFileName.str(), std::ios::binary).write(reinterpret_cast<const char*>(shuffledBuffer.data()), shuffledBuffer.size());
#endif

                        // Create all d3d12 resources for this test
                        CreateBC7UnshufflePassD3D12Resources(shuffledBuffer, bcTextureRaw.size());

                        UNSHUFFLE_PER_TEST_STATISTICS testStats = {};
                        testStats.testName = testName.str();
                        testStats.textureWidthPixels = static_cast<uint32_t>(widthPixels);

                        GTestParameters params = {
                            GBaseFormat::BC7,
                            testName.str(),
                            widthPixels,
                            heightPixels,
                            shuffleParams.useSpaceCurve,
                        };
                        params.BC7.ChunkSize = shuffleParams.ChunkSize;
                        params.BC7.endpointOrderStrategy = shuffleParams.endpointOrderStrategy;
                        params.BC7.patternIndex = patternSetIndex;


                        TestGpuUnshuffling(params, bcTextureRaw, testStats, bcSizeInBytes);

                        std::cout << "-- Finished test " << testName.str() << " -- Passed: " <<
                            (testStats.testPassed ? "true" : "false") << " - ShuffledSize: " << shuffledBuffer.size() << std::endl;

                        allPermutationPassed = allPermutationPassed && testStats.testPassed;
                    }
                }
            }
        }
    }
    else  // BC1, BC3, BC4, BC5
    {
        uint32_t transforms = 0;
        GBaseFormat baseFormatEnum;
        switch (baseFormat) {
            case DXGI_FORMAT_BC1_TYPELESS: baseFormatEnum = GBaseFormat::BC1; break;
            case DXGI_FORMAT_BC3_TYPELESS: baseFormatEnum = GBaseFormat::BC3; break;
            case DXGI_FORMAT_BC4_TYPELESS: baseFormatEnum = GBaseFormat::BC4; break;
            case DXGI_FORMAT_BC5_TYPELESS: baseFormatEnum = GBaseFormat::BC5; break;
            default:
                throw std::runtime_error("RunAllPermutationsForLoadedTexture: unsupported BC format.");
        }
        transforms = gNumTransformsPerShader[static_cast<uint32_t>(baseFormatEnum)];

        for (uint32_t transform = 0; transform < transforms; ++transform)
        {
            bool useSpaceCurve = (transform % 2 == 1);  // Half of the transforms are for useSpaceCurve=false, half for useSpaceCurve=true
            uint32_t microShuffleId = 1 + transform / 2;

            std::vector<uint8_t> curvedData;
            if (useSpaceCurve)
            {
                if (GACL_Shuffle_ApplySpaceCurve(nullptr, nullptr, bcSizeInBytes, gShaderInfo[baseFormatEnum].blockSizes, widthPixels, true))
                {
                    curvedData.resize(bcSizeInBytes);
                    GACL_Shuffle_ApplySpaceCurve(curvedData.data(), bcTextureRaw.data(), bcSizeInBytes, gShaderInfo[baseFormatEnum].blockSizes, widthPixels, true);
                }
                else
                {
                    continue;
                }
            }

            // Will hold the data for the buffer shuffled in ProcessTexture
            std::vector<uint8_t> shuffledBuffer(bcSizeInBytes);

            Shuffle_BC1345[static_cast<uint32_t>(baseFormatEnum)](shuffledBuffer.data(), useSpaceCurve ? curvedData.data() : bcTextureRaw.data(), bcSizeInBytes, microShuffleId);

            std::stringstream testName;
            testName << "Res" << std::to_string(widthPixels) << "x" << std::to_string(heightPixels) <<
                "_SC" << (useSpaceCurve ? "True" : "False") <<
                "_MicroPattern" << std::to_string(microShuffleId);

#if DUMP_SHUFFLED_BUFFER_TO_DISK
            std::stringstream dumpFileName;
            dumpFileName << testName.str() << ".bin";
            std::ofstream(dumpFileName.str(), std::ios::binary).write(reinterpret_cast<const char*>(shuffledBuffer.data()), shuffledBuffer.size());
#endif

            // Create all d3d12 resources for this test
            CreateBC1345UnshufflePassD3D12Resources(shuffledBuffer, bcTextureRaw.size());

            UNSHUFFLE_PER_TEST_STATISTICS testStats = {};
            testStats.testName = testName.str();
            testStats.textureWidthPixels = static_cast<uint32_t>(widthPixels);

            GTestParameters params = {
                baseFormatEnum,
                testName.str(),
                widthPixels,
                heightPixels,
                useSpaceCurve,
                {microShuffleId}
            };


            TestGpuUnshuffling(params, bcTextureRaw, testStats, bcSizeInBytes);

            std::cout << "-- Finished test " << testName.str() << " -- Passed: " <<
                (testStats.testPassed ? "true" : "false") << " - ShuffledSize: " << shuffledBuffer.size() << std::endl;

            allPermutationPassed &&= testStats.testPassed;
        }
    }
    std::wcout << L"\n-- All test passed: " << (allPermutationPassed ? L"true" : L"false") << std::endl;

    return allPermutationPassed;
}

// Note: Loading only Mip 0 of the texture pointed by m_inputPath
void Game::LoadTextureFromDisk(uint32_t& widthPixels, uint32_t& heightPixels, DXGI_FORMAT& format, std::vector<uint8_t>& bcTextureRaw)
{
    DirectX::TexMetadata metadata = {};
    DirectX::ScratchImage scratchImage;

    HRESULT hr = DirectX::LoadFromDDSFile(m_inputPath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, scratchImage);
    if (FAILED(hr))
    {
        throw std::runtime_error("LoadTextureFromDisk: failed to load DDS file.");
    }

    DXGI_FORMAT baseFormat = GetBaseFormat(metadata.format);

    if (baseFormat != DXGI_FORMAT_BC1_TYPELESS &&
        baseFormat != DXGI_FORMAT_BC3_TYPELESS && 
        baseFormat != DXGI_FORMAT_BC4_TYPELESS && 
        baseFormat != DXGI_FORMAT_BC5_TYPELESS && 
        baseFormat != DXGI_FORMAT_BC7_TYPELESS)
    {
        throw std::runtime_error("LoadTextureFromDisk: texture is not BC1\\3\\4\\5\\7 format.");
    }

    // Use the top mip level (mip=0, item=0, slice=0)
    const DirectX::Image* img = scratchImage.GetImage(0, 0, 0);
    if (!img)
    {
        throw std::runtime_error("LoadTextureFromDisk: failed to retrieve image data.");
    }

    widthPixels  = static_cast<uint32_t>(img->width);
    heightPixels = static_cast<uint32_t>(img->height);
    format = img->format;

    bcTextureRaw.resize(img->slicePitch);
    memcpy(bcTextureRaw.data(), img->pixels, img->slicePitch);
}
#endif



bool Game::RunTestWithParams(GTestParameters const& params)
{
    if (params.baseFormat == GBaseFormat::BC7)
    {
        // Method that creates the raw contents of a bc7 texture of widthPixels x heightPixels.
        // The texture will be created block by block. And it will cycle through each bc7 mode foreach new block.
        // So if the texture has 6 blocks, the first will be mode 0, second mode 1, then mode 2, etc.
        // If the texture has more than 9 blocks, it wraps around and starts again from mode 0. this way the texture is representative of every mode.
        // The color contents of the texture do not matter. All that matter is that each block is constructed conforming to the bc7 specification.
        std::vector<uint8_t> bc7TextureRaw;
        CreateBC7RawFromMetadata(params.width, params.height, bc7TextureRaw);

        BC7ModeSplitShufflePattern Patterns[8];
        memcpy(Patterns, &g_patternSets[params.BC7.patternIndex][0], sizeof(Patterns));

        ShuffleParameters shuffleParams = {};
        shuffleParams.useSpaceCurve = static_cast<bool>(params.useSpaceCurve);
        shuffleParams.endpointOrderStrategy = params.BC7.endpointOrderStrategy;
        shuffleParams.ChunkSize = params.BC7.ChunkSize;
        shuffleParams.Patterns = Patterns;

        size_t bc7SizeInBytes = bc7TextureRaw.size();

        // Will hold the data for the buffer shuffled in ProcessTexture
        std::vector<uint8_t> shuffledBuffer;

        // Call that interfaces with gaclLib to shuffle the texture.
        ShuffleBC7Texture(
            shuffledBuffer,
            bc7TextureRaw.data(),
            bc7TextureRaw.size(),
            params.width,
            shuffleParams);

#if DUMP_SHUFFLED_BUFFER_TO_DISK
        std::stringstream dumpFileName;
        dumpFileName << params.testName << ".bin";
        std::ofstream(dumpFileName.str(), std::ios::binary).write(reinterpret_cast<const char*>(shuffledBuffer.data()), shuffledBuffer.size());
#endif

        // Create all d3d12 resources for this test
        CreateBC7UnshufflePassD3D12Resources(shuffledBuffer, bc7SizeInBytes);

        UNSHUFFLE_PER_TEST_STATISTICS testStats = {};
        testStats.testName = params.testName;
        testStats.textureWidthPixels = static_cast<uint32_t>(params.width);

        TestGpuUnshuffling(params, bc7TextureRaw, testStats, bc7SizeInBytes);

        return testStats.testPassed;
    }
    else
    {
        uint32_t elementSize = gShaderInfo[params.baseFormat].blockSizes;
        std::vector<uint8_t> bcnTextureRaw;
        CreateBCnRawFromMetadata(params.width, params.height, elementSize, bcnTextureRaw);

        size_t sizeInBytes = bcnTextureRaw.size();

        std::vector<uint8_t> curvedData(bcnTextureRaw.size());
        std::vector<uint8_t> shuffledBuffer(bcnTextureRaw.size());
        const uint8_t* dataToShuffle = bcnTextureRaw.data();

        if (params.useSpaceCurve)
        {
            GACL_Shuffle_ApplySpaceCurve(curvedData.data(), bcnTextureRaw.data(), sizeInBytes, elementSize, params.width, true);
            dataToShuffle = curvedData.data();
        }

        if (params.baseFormat == GBaseFormat::CurveOnly)
        {
            shuffledBuffer.swap(curvedData);
        }
        else
        {
            Shuffle_BC1345[params.baseFormat](shuffledBuffer.data(), dataToShuffle, sizeInBytes, params.BC1345.microShuffleId);
        }

#if DUMP_SHUFFLED_BUFFER_TO_DISK
        std::stringstream dumpFileName;
        dumpFileName << params.testName << ".bin";
        std::ofstream(dumpFileName.str(), std::ios::binary).write(reinterpret_cast<const char*>(shuffledBuffer.data()), shuffledBuffer.size());
#endif

        CreateBC1345UnshufflePassD3D12Resources(shuffledBuffer, sizeInBytes);

        UNSHUFFLE_PER_TEST_STATISTICS testStats = {};
        testStats.testName = params.testName;
        testStats.textureWidthPixels = static_cast<uint32_t>(params.width);

        TestGpuUnshuffling(params, bcnTextureRaw, testStats, sizeInBytes);

        return testStats.testPassed;
    }
}

void Game::TestGpuUnshuffling(GTestParameters params, std::vector<uint8_t> const& BCnOriginalData, UNSHUFFLE_PER_TEST_STATISTICS& testStats, size_t sizeInBytes)
{
#ifdef CAPTURE_PIX
    PIXCaptureParameters pixCaptureParams = {};
    wchar_t captureName[200];
    const wchar_t* formatNames[] = {L"BC1", L"BC3", L"BC4", L"BC5", L"SCO", L"BC7"};
    if (params.baseFormat == GBaseFormat::BC7)
    {
        swprintf_s(captureName, _countof(captureName), L"FCap_%s_%ux%u_sc%d_p%d_cs%d_eos%d.wpix", 
            formatNames[params.baseFormat], params.width, params.height, params.useSpaceCurve, 
            params.BC7.patternIndex, params.BC7.ChunkSize, params.BC7.endpointOrderStrategy);
    }
    else
    {
        swprintf_s(captureName, _countof(captureName), L"FCap_%s_%ux%u_sc%d_p%d.wpix", 
            formatNames[params.baseFormat], params.width, params.height, params.useSpaceCurve, 
            params.BC1345.microShuffleId);
    }
    pixCaptureParams.GpuCaptureParameters.FileName = captureName;
    CAPTURE_SELECT
    PIXBeginCapture(PIX_CAPTURE_GPU, &pixCaptureParams);
    #endif

    // Add unshuffling shader commands to the command list
    Unshuffle(params, sizeInBytes);

    #ifdef CAPTURE_PIX
    CAPTURE_SELECT
    {
    PIXEndCapture(false /*Discard param not supported on Windows*/);
    }
    #endif

    // Copy unshuffled data back to CPU from GPU
    std::vector<uint8_t> unshuffledData(sizeInBytes);
    UnshuffleCPUReadback(unshuffledData, sizeInBytes);

    // Compare the unshuffled data with the expected data
    if (params.baseFormat == GBaseFormat::BC7)
    {
        CompareAgainstOriginalBC7Data(BCnOriginalData, unshuffledData, testStats);
    }
    else
    {
        testStats.testPassed = 
            (BCnOriginalData.size() == unshuffledData.size()) &&
            (0 == memcmp(BCnOriginalData.data(), unshuffledData.data(), sizeInBytes));
    }

    // Reset d3d12 resources before next test
    ResetD3D12Resources();
}

void Game::Unshuffle(GTestParameters params, size_t sizeInBytes)
{
    m_deviceResources->ResetCommandList();
    auto commandList = m_deviceResources->GetCommandList();

    if (params.baseFormat == GBaseFormat::BC7)
    {
        uint32_t numBlocks = static_cast<uint32_t>(sizeInBytes) / BC7_BYTES_PER_BLOCK;
        uint32_t histogramCount = (numBlocks + TG_THREAD_COUNT - 1) / TG_THREAD_COUNT;

        D3D12_RESOURCE_BARRIER barriers[3] = {};

        // BC7 Prepass
        {
            PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"BC7 Prepass");

            commandList->SetPipelineState(m_prepassCSPipelineState.Get());

            commandList->SetComputeRootSignature(m_prepassCSRootSignature.Get());

            commandList->SetComputeRoot32BitConstant(0, numBlocks, 0);
            commandList->SetComputeRoot32BitConstant(0, m_offsetIntoHistogram, 1);

            commandList->SetComputeRootShaderResourceView(1, m_shuffledBuffer->GetGPUVirtualAddress());

            commandList->SetComputeRootUnorderedAccessView(2, m_scratchBuffer->GetGPUVirtualAddress());

            commandList->Dispatch(histogramCount, 1, 1);

            PIXEndEvent(commandList);
        }

        barriers[0] = CD3DX12_RESOURCE_BARRIER::UAV(m_scratchBuffer.Get());
        commandList->ResourceBarrier(1, barriers);

        // BC7 Compute Prefix-Sum
        {
            PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"BC7 Prefix-Sum");

            commandList->SetPipelineState(m_prefixSumCSPipelineState.Get());

            commandList->SetComputeRootSignature(m_prefixSumCSRootSignature.Get());

            commandList->SetComputeRoot32BitConstant(0, histogramCount, 0);
            commandList->SetComputeRoot32BitConstant(0, m_offsetIntoHistogram, 1);

            commandList->SetComputeRootUnorderedAccessView(1, m_scratchBuffer->GetGPUVirtualAddress());

            commandList->Dispatch(1, BC7_MODES_COUNT, 1);

            PIXEndEvent(commandList);
        }

        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_unshufflePassIndirectBuffer.Get(),
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barriers[1] = CD3DX12_RESOURCE_BARRIER::UAV(m_scratchBuffer.Get());
        commandList->ResourceBarrier(2, barriers);

        // BC7 Mode Binning Pass
        {
            PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"BC7 ModeBinning");

            commandList->SetPipelineState(m_modeBinningCSPipelineState.Get());

            commandList->SetComputeRootSignature(m_modeBinningCSRootSignature.Get());

            commandList->SetComputeRoot32BitConstant(0, histogramCount, 0);
            commandList->SetComputeRoot32BitConstant(0, numBlocks, 1);
            commandList->SetComputeRoot32BitConstant(0, static_cast<uint32_t>(params.width), 2);
            commandList->SetComputeRoot32BitConstant(0, params.useSpaceCurve, 3);
            commandList->SetComputeRoot32BitConstant(0, m_offsetIntoHistogram, 4);
            commandList->SetComputeRoot32BitConstant(0, m_offsetIntoBinning, 5);

            commandList->SetComputeRootShaderResourceView(1, m_shuffledBuffer->GetGPUVirtualAddress());
            commandList->SetComputeRootUnorderedAccessView(2, m_scratchBuffer->GetGPUVirtualAddress());
            commandList->SetComputeRootUnorderedAccessView(3, m_outBCnBuffer->GetGPUVirtualAddress());
            commandList->SetComputeRootUnorderedAccessView(4, m_unshufflePassIndirectBuffer->GetGPUVirtualAddress());

            commandList->Dispatch(histogramCount, 1, 1);

            PIXEndEvent(commandList);
        }

        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_unshufflePassIndirectBuffer.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        barriers[1] = CD3DX12_RESOURCE_BARRIER::UAV(m_scratchBuffer.Get());
        barriers[2] = CD3DX12_RESOURCE_BARRIER::UAV(m_outBCnBuffer.Get());
        commandList->ResourceBarrier(3, barriers);

        // BC7 Compute Unshuffle
        {
            PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"BC7 Unshuffle");

            commandList->SetComputeRootSignature(m_unshuffleCSRootSignature.Get());

            commandList->SetComputeRoot32BitConstant(0, m_offsetIntoBinning, 0);

            commandList->SetComputeRootShaderResourceView(1, m_shuffledBuffer->GetGPUVirtualAddress());
            commandList->SetComputeRootUnorderedAccessView(2, m_scratchBuffer->GetGPUVirtualAddress());
            commandList->SetComputeRootUnorderedAccessView(3, m_outBCnBuffer->GetGPUVirtualAddress());

            for (uint32_t i = 0; i < BC7_MODES_COUNT; ++i)
            {
                commandList->SetPipelineState(m_unshuffleBinnedCSPSO[i].Get());

                commandList->ExecuteIndirect(m_unshufflePassCmdSignature.Get(), 1,
                    m_unshufflePassIndirectBuffer.Get(), i * sizeof(UnshufflePassIndirectBuffer), nullptr, 0);
            }
        }
    }
    else
    {
        PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, gShaderInfo[params.baseFormat].pixName.data());

        uint32_t totalBlocks = uint32_t(sizeInBytes) / gShaderInfo[params.baseFormat].blockSizes;
        uint32_t numPairsOrQuads = totalBlocks / gShaderInfo[params.baseFormat].blocksPerThread;
        uint32_t numStragglers = totalBlocks % gShaderInfo[params.baseFormat].blocksPerThread;
        uint32_t numThreadsRequired = numPairsOrQuads + numStragglers;

        uint32_t numThreadGroupsX = params.baseFormat == GBaseFormat::CurveOnly ?
            (numThreadsRequired + (31u)) / 32u :
            (numThreadsRequired + (TG_THREAD_COUNT - 1)) / TG_THREAD_COUNT;


#ifdef NEWBC1345
        uint32_t tID = (params.baseFormat == GBaseFormat::CurveOnly) ? GACL_SHUFFLE_TRANSFORM_ZSTD_SC :
            gaclTransformIDs[params.baseFormat][2 * (params.BC1345.microShuffleId - 1) + (params.useSpaceCurve ? 1 : 0)];
        uint32_t shaderConstants[4] = { uint32_t(sizeInBytes), 0, tID,  params.width};
#else
        uint32_t shaderConstants[2] = { uint32_t(sizeInBytes), 0 };
#endif

        commandList->SetPipelineState(m_bcnCSPipelineState[params.baseFormat].Get());

        commandList->SetComputeRootSignature(m_bcnCSRootSignature[params.baseFormat].Get());

        commandList->SetComputeRootShaderResourceView(1, m_shuffledBuffer->GetGPUVirtualAddress());
#ifdef USE_ROOT_DESCRIPTORS
        commandList->SetComputeRootUnorderedAccessView(2, m_outBCnBuffer->GetGPUVirtualAddress());
#else
        ID3D12DescriptorHeap* dheaps[] = { m_uavSrvCbvHeap.Get() };
        commandList->SetDescriptorHeaps(1, dheaps);
        commandList->SetComputeRootDescriptorTable(2, m_UAVOutputBufferGpuHandle);
#endif
#ifdef NEWBC1345
        commandList->SetComputeRoot32BitConstants(0, 4, &shaderConstants, 0);
#else
        commandList->SetComputeRoot32BitConstants(0, 2, &shaderConstants, 0);
#endif
        commandList->Dispatch(numThreadGroupsX, 1, 1);

        PIXEndEvent(commandList);
    }

    // Send commands to the GPU
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"ECL Submission");
    auto cmdQueue = m_deviceResources->GetCommandQueue();
    commandList->Close();
    ID3D12CommandList* cmdLists[] = { commandList };
    cmdQueue->ExecuteCommandLists(1, cmdLists);
    PIXEndEvent();
}

void Game::UnshuffleCPUReadback(std::vector<uint8_t>& BCnUnshuffledData, size_t bcSizeInBytes)
{
    m_deviceResources->ResetCommandListToReadbackAllocator();
    auto commandList = m_deviceResources->GetCommandList();
    
    D3D12_RESOURCE_BARRIER barriers[1];
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_outBCnBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(1, barriers);
    
    // Copy from output to readback
    commandList->CopyBufferRegion(m_BCnReadback.Get(), 0ull, m_outBCnBuffer.Get(), m_uavHeapOffsetInBytes, bcSizeInBytes);
    
    // Send commands to the GPU
    auto cmdQueue = m_deviceResources->GetCommandQueue();
    commandList->Close();
    ID3D12CommandList* cmdLists[] = { commandList };
    cmdQueue->ExecuteCommandLists(1, cmdLists);
    
    // Signal the readback queue and then wait for it on the CPU
    m_deviceResources->WaitForReadbackFence();
    
    // Get data from readback
    uint8_t* m_pReadbackBufferData = nullptr;
    DX::ThrowIfFailed(m_BCnReadback->Map(0, nullptr, reinterpret_cast<void**>(&m_pReadbackBufferData)));
    memcpy(&BCnUnshuffledData[0], m_pReadbackBufferData, bcSizeInBytes);
    m_BCnReadback->Unmap(0, nullptr);
}

void Game::CreateBC1345UnshufflePassD3D12Resources(std::vector<uint8_t>& shuffledBuffer, size_t sizeInBytes)
{
    auto device = m_deviceResources->GetD3DDevice();

    // Create shader RootSignature and PSO

    static bool oneTime = true;
    if (oneTime)
    {
        oneTime = false;

        for (size_t i = 0; i < _countof(gShaderInfo); i++)
        {
            auto csBlob = DX::ReadData(gShaderInfo[i].fileName.data());
            DX::ThrowIfFailed(device->CreateRootSignature(0, csBlob.data(), csBlob.size(), IID_PPV_ARGS(m_bcnCSRootSignature[i].ReleaseAndGetAddressOf())));
            m_bcnCSRootSignature[i]->SetName(gShaderInfo[i].signatureName.data());

            D3D12_COMPUTE_PIPELINE_STATE_DESC computePSODesc = {};
            computePSODesc.CS = { csBlob.data(), csBlob.size() };
            computePSODesc.pRootSignature = m_bcnCSRootSignature[i].Get();
            DX::ThrowIfFailed(device->CreateComputePipelineState(&computePSODesc, IID_PPV_ARGS(m_bcnCSPipelineState[i].ReleaseAndGetAddressOf())));
        }
    }


#ifndef USE_ROOT_DESCRIPTORS
    // Create DescriptorHeap
    D3D12_DESCRIPTOR_HEAP_DESC dHeapDesc = {};
    dHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    dHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    dHeapDesc.NumDescriptors = 1; // output buffer UAV only.
    DX::ThrowIfFailed(device->CreateDescriptorHeap(&dHeapDesc, IID_PPV_ARGS(m_uavSrvCbvHeap.ReleaseAndGetAddressOf())));
    m_DHIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
#endif


    D3D12_HEAP_PROPERTIES defaultHeapProperties = {};
    defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    uint32_t shuffledBufferSizeBytes = uint32_t(shuffledBuffer.size() + 3) & ~0b11ul; // round up to DWORD

    // OUTPUT RESOURCE
    {
        D3D12_RESOURCE_DESC outBCnBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        D3D12_RESOURCE_ALLOCATION_INFO heapAllocInfo = device->GetResourceAllocationInfo(0, 1, &outBCnBufferDesc);

        D3D12_HEAP_DESC outputHeapDesc = {};
        outputHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        outputHeapDesc.Properties = defaultHeapProperties;
        outputHeapDesc.SizeInBytes = heapAllocInfo.SizeInBytes;

        DX::ThrowIfFailed(device->CreateHeap(&outputHeapDesc, IID_PPV_ARGS(m_outputHeap.ReleaseAndGetAddressOf())));

        DX::ThrowIfFailed(device->CreatePlacedResource(
            m_outputHeap.Get(),
            0,
            &outBCnBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_outBCnBuffer.ReleaseAndGetAddressOf())));
        m_outBCnBuffer->SetName(L"OutBCnBuffer");
    }

    // SAMPLE-ONLY RESOURCES
    {
        D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        // Shuffled buffer related resources (input)
        D3D12_RESOURCE_DESC shuffledBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(shuffledBufferSizeBytes);

        // Intermediate resource with buffer data
        DX::ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &shuffledBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(m_shuffledBufferUpload.ReleaseAndGetAddressOf())));
        m_shuffledBufferUpload->SetName(L"ShuffledBufferUpload");

        // Resource with buffer data on GPU readable memory
        DX::ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &shuffledBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_shuffledBuffer.ReleaseAndGetAddressOf())));
        m_shuffledBuffer->SetName(L"ShuffledBuffer");

        // Readback Resource (for reading and comparing output data after unshuffling
        CD3DX12_HEAP_PROPERTIES readbackHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        D3D12_RESOURCE_DESC readbackBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);

        DX::ThrowIfFailed(device->CreateCommittedResource(
            &readbackHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &readbackBufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(m_BCnReadback.ReleaseAndGetAddressOf())));
        m_BCnReadback->SetName(L"BCnReadback");
    }


    // Create Descriptors
#ifndef USE_ROOT_DESCRIPTORS
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle;
        //D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;

        auto uavSrvCbvDescHeapStart = m_uavSrvCbvHeap->GetCPUDescriptorHandleForHeapStart();

        // Unshuffled output buffer UAV
        uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        uavDesc.Buffer.FirstElement = static_cast<uint32_t>(m_uavHeapOffsetInBytes) / sizeof(uint32_t);
        uavDesc.Buffer.NumElements = static_cast<uint32_t>(sizeInBytes) / sizeof(uint32_t);
        uavDesc.Buffer.StructureByteStride = 0;
        handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(uavSrvCbvDescHeapStart, UAV_OUTPUT_BUFFER, m_DHIncrementSize);
        device->CreateUnorderedAccessView(m_outBCnBuffer.Get(), nullptr, &uavDesc, handle);

        m_UAVOutputBufferGpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_uavSrvCbvHeap->GetGPUDescriptorHandleForHeapStart(), UAV_OUTPUT_BUFFER, m_DHIncrementSize);
    }
#endif

    // Upload shuffled data to GPU
    {
        m_deviceResources->ResetCommandList();
        auto commandList = m_deviceResources->GetCommandList();

        // Here we would need to upload the data we got from the shuffled buffer to the gpu
        uint8_t* data = nullptr;
        DX::ThrowIfFailed(m_shuffledBufferUpload->Map(0, nullptr, reinterpret_cast<void**>(&data)));
        memcpy(data, shuffledBuffer.data(), shuffledBuffer.size());
        m_shuffledBufferUpload->Unmap(0, nullptr);

        // Once uploaded, transition resources to correct states and perform the copy
        {
            D3D12_RESOURCE_BARRIER barriers[1] = {};
            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_shuffledBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, barriers);

            commandList->CopyResource(m_shuffledBuffer.Get(), m_shuffledBufferUpload.Get());

            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_shuffledBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            commandList->ResourceBarrier(1, barriers);
        }

        commandList->Close();
        ID3D12CommandList* cmdLists[] = { commandList };
        auto commandQueue = m_deviceResources->GetCommandQueue();
        commandQueue->ExecuteCommandLists(1, cmdLists);

        m_deviceResources->WaitForGpu();
    }
}

void Game::CreateBC7UnshufflePassD3D12Resources(std::vector<uint8_t>& shuffledBuffer, size_t bc7SizeInBytes)
{
    auto device = m_deviceResources->GetD3DDevice();

    // Create RootSignature and PSO
    {
        // PREPASS
        auto csBlobPrepass = DX::ReadData(L"PrepassB.cso");
        DX::ThrowIfFailed(
            device->CreateRootSignature(0, csBlobPrepass.data(), csBlobPrepass.size(), IID_PPV_ARGS(m_prepassCSRootSignature.ReleaseAndGetAddressOf())));
        m_prepassCSRootSignature->SetName(L"PrepassCSRootSignature");

        D3D12_COMPUTE_PIPELINE_STATE_DESC prepassComputePSODesc = {};
        prepassComputePSODesc.CS = { csBlobPrepass.data(), csBlobPrepass.size() };
        prepassComputePSODesc.pRootSignature = m_prepassCSRootSignature.Get();

        DX::ThrowIfFailed(device->CreateComputePipelineState(&prepassComputePSODesc, IID_PPV_ARGS(m_prepassCSPipelineState.ReleaseAndGetAddressOf())));
        
        // Mode Binning
        std::vector<uint8_t> csModeBinningBlob;
        if (m_minHWSupportedWaveSize == 4)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave4.cso");
        }
        else if (m_minHWSupportedWaveSize == 8)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave8.cso");
        }
        else if (m_minHWSupportedWaveSize == 16)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave16.cso");
        }
        else if (m_minHWSupportedWaveSize == 32)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave32.cso");
        }
        else if (m_minHWSupportedWaveSize == 64)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave64.cso");
        }
        else if (m_minHWSupportedWaveSize == 128)
        {
            csModeBinningBlob = DX::ReadData(L"BinningPassCS_MinWave128.cso");
        }
        else 
        {
            throw std::runtime_error("Unsupported wave size.");
        }

        DX::ThrowIfFailed(
            device->CreateRootSignature(0, csModeBinningBlob.data(), csModeBinningBlob.size(), IID_PPV_ARGS(m_modeBinningCSRootSignature.ReleaseAndGetAddressOf())));
        m_modeBinningCSRootSignature->SetName(L"ModeBinningCSRootSignature");

        D3D12_COMPUTE_PIPELINE_STATE_DESC modeBinningPSODesc = {};
        modeBinningPSODesc.CS = { csModeBinningBlob.data(), csModeBinningBlob.size() };
        modeBinningPSODesc.pRootSignature = m_modeBinningCSRootSignature.Get();

        DX::ThrowIfFailed(device->CreateComputePipelineState(&modeBinningPSODesc, IID_PPV_ARGS(m_modeBinningCSPipelineState.ReleaseAndGetAddressOf())));
       
        // PREFIX-SUM PASS
        std::vector<uint8_t> csPrefixSumBlob;
        if (m_minHWSupportedWaveSize == 4)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave4.cso");
        }
        else if (m_minHWSupportedWaveSize == 8)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave8.cso");
        }
        else if (m_minHWSupportedWaveSize == 16)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave16.cso");
        }
        else if (m_minHWSupportedWaveSize == 32)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave32.cso");
        }
        else if (m_minHWSupportedWaveSize == 64)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave64.cso");
        }
        else if (m_minHWSupportedWaveSize == 128)
        {
            csPrefixSumBlob = DX::ReadData(L"PrefixSumPassCS_MinWave128.cso");
        }
        else
        {
            assert(false); // Size not supported
        }
        DX::ThrowIfFailed(
            device->CreateRootSignature(0, csPrefixSumBlob.data(), csPrefixSumBlob.size(), IID_PPV_ARGS(m_prefixSumCSRootSignature.ReleaseAndGetAddressOf())));
        m_prefixSumCSRootSignature->SetName(L"PrefixSumCSRootSignature");

        D3D12_COMPUTE_PIPELINE_STATE_DESC prefixsumPSODesc = {};
        prefixsumPSODesc.CS = { csPrefixSumBlob.data(), csPrefixSumBlob.size() };
        prefixsumPSODesc.pRootSignature = m_prefixSumCSRootSignature.Get();

        DX::ThrowIfFailed(device->CreateComputePipelineState(&prefixsumPSODesc, IID_PPV_ARGS(m_prefixSumCSPipelineState.ReleaseAndGetAddressOf())));

        // UNSHUFFLE PASS
        auto csBlob = DX::ReadData(s_perModeUnshufflePSONames[0].c_str());
        DX::ThrowIfFailed(
            device->CreateRootSignature(0, csBlob.data(), csBlob.size(), IID_PPV_ARGS(m_unshuffleCSRootSignature.ReleaseAndGetAddressOf())));
        m_unshuffleCSRootSignature->SetName(L"UnshuffleCSRootSignature");

        D3D12_COMPUTE_PIPELINE_STATE_DESC computePSODesc = {};
        computePSODesc.pRootSignature = m_unshuffleCSRootSignature.Get();

        for (uint32_t i = 0; i < BC7_MODES_COUNT; ++i)
        {
            csBlob = DX::ReadData(s_perModeUnshufflePSONames[i].c_str());
            computePSODesc.CS = { csBlob.data(), csBlob.size() };
            DX::ThrowIfFailed(device->CreateComputePipelineState(&computePSODesc, IID_PPV_ARGS(m_unshuffleBinnedCSPSO[i].ReleaseAndGetAddressOf())));
        }
    }

    // Shuffled buffer data. Since this is going to be accessed as a raw buffer which needs to be 
    // dword aligned, size is aligned to the next dword boundary
    uint32_t shuffledBufferSizeBytes = static_cast<uint32_t>(shuffledBuffer.size());
    uint32_t rem = shuffledBufferSizeBytes % 4;
    if (rem != 0) shuffledBufferSizeBytes += (4 - rem);

    uint32_t blockCount = static_cast<uint32_t>(bc7SizeInBytes) / BC7_BYTES_PER_BLOCK;

    uint32_t modeBinningBufferSizeInBytes = blockCount * sizeof(uint32_t);

    uint32_t histogramCount = (blockCount + TG_THREAD_COUNT - 1) / TG_THREAD_COUNT;
    uint32_t histogramEntrySize = BC7_MODES_COUNT * sizeof(uint32_t);
    uint32_t histogramBufferSizeInBytes = histogramCount * histogramEntrySize;
    
    uint32_t headerSizeBytes = sizeof(CommonHeader);
    rem = headerSizeBytes % 4;
    if (rem != 0) headerSizeBytes += (4 - rem);

    // These offsets are passed to shaders in order to fetch from the correct memory boundary
    // Right now there is no alignment between these other than dword alignment
    m_offsetIntoHeader = 0;
    m_offsetIntoHistogram = headerSizeBytes;
    m_offsetIntoBinning = headerSizeBytes + histogramBufferSizeInBytes;

    // This is the size of the buffer holding the intermediate resources (minus the indirect arguments)
    uint32_t scratchBufferSizeBytes = histogramBufferSizeInBytes + modeBinningBufferSizeInBytes + headerSizeBytes;

    uint32_t indirectArgBufferSize = BC7_MODES_COUNT * sizeof(UnshufflePassIndirectBuffer);

    // Resource Description struct for all intermediates
    D3D12_RESOURCE_DESC scratchBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(scratchBufferSizeBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_RESOURCE_DESC indirectBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indirectArgBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    std::vector<D3D12_RESOURCE_ALLOCATION_INFO> allocInfoList;
    allocInfoList.push_back(device->GetResourceAllocationInfo(0, 1, &scratchBufferDesc));
    allocInfoList.push_back(device->GetResourceAllocationInfo(0, 1, &indirectBufferDesc));

    uint64_t intermediateResourcesOffsetsIntoHeap[4];
    uint64_t heapSizeBytes = 0u;
    for (size_t i = 0; i < allocInfoList.size(); ++i)
    {
        intermediateResourcesOffsetsIntoHeap[i] = heapSizeBytes;
        heapSizeBytes += allocInfoList[i].SizeInBytes;
    }

    D3D12_HEAP_PROPERTIES defaultHeapProperties = {};
    defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_HEAP_DESC heapDesc = {};
    heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heapDesc.Flags = D3D12_HEAP_FLAG_NONE;
    heapDesc.Properties = defaultHeapProperties;
    heapDesc.SizeInBytes = heapSizeBytes;

    DX::ThrowIfFailed(device->CreateHeap(&heapDesc, IID_PPV_ARGS(m_intermediateHeap.ReleaseAndGetAddressOf())));
    
    m_uavHeapOffsetInBytes = 0;

    // OUTPUT RESOURCE
    {
        size_t outputHeapSize = bc7SizeInBytes + m_uavHeapOffsetInBytes;

        D3D12_RESOURCE_DESC outBC7BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(outputHeapSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        
        D3D12_RESOURCE_ALLOCATION_INFO heapAllocInfo = device->GetResourceAllocationInfo(0, 1, &outBC7BufferDesc);
        
        D3D12_HEAP_DESC outputHeapDesc = {};
        outputHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        outputHeapDesc.Properties = defaultHeapProperties;
        outputHeapDesc.SizeInBytes = heapAllocInfo.SizeInBytes;

        DX::ThrowIfFailed(device->CreateHeap(&outputHeapDesc, IID_PPV_ARGS(m_outputHeap.ReleaseAndGetAddressOf())));

        DX::ThrowIfFailed(device->CreatePlacedResource(
            m_outputHeap.Get(),
            0,
            &outBC7BufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_outBCnBuffer.ReleaseAndGetAddressOf())));
        m_outBCnBuffer->SetName(L"OutBC7Buffer");
    }
    
    // SAMPLE-ONLY RESOURCES
    {
        D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        // Shuffled buffer related resources (input)
        D3D12_RESOURCE_DESC shuffledBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(shuffledBufferSizeBytes);

        // Intermediate resource with buffer data
        DX::ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &shuffledBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(m_shuffledBufferUpload.ReleaseAndGetAddressOf())));
        m_shuffledBufferUpload->SetName(L"ShuffledBufferUpload");

        // Resource with buffer data on GPU readable memory
        DX::ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &shuffledBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_shuffledBuffer.ReleaseAndGetAddressOf())));
        m_shuffledBuffer->SetName(L"ShuffledBuffer");

        // Readback Resource (for reading and comparing BC7 output data after unshuffling
        CD3DX12_HEAP_PROPERTIES readbackHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        D3D12_RESOURCE_DESC readbackBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bc7SizeInBytes);

        DX::ThrowIfFailed(device->CreateCommittedResource(
            &readbackHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &readbackBufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(m_BCnReadback.ReleaseAndGetAddressOf())));
        m_BCnReadback->SetName(L"BC7Readback");
    }

    // INTERMEDIATE RESOURCES
    {
        // Buffer that contains header, binning buffer, and histogram buffer
        DX::ThrowIfFailed(device->CreatePlacedResource(
            m_intermediateHeap.Get(),
            intermediateResourcesOffsetsIntoHeap[0],
            &scratchBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_scratchBuffer.ReleaseAndGetAddressOf())));
        m_scratchBuffer->SetName(L"ScratchBuffer");

        DX::ThrowIfFailed(device->CreatePlacedResource(
            m_intermediateHeap.Get(),
            intermediateResourcesOffsetsIntoHeap[1],
            &indirectBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_unshufflePassIndirectBuffer.ReleaseAndGetAddressOf())));
    }

    // Execute Indirect command signature
    {
        D3D12_INDIRECT_ARGUMENT_DESC arguments[1] = {};
        arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC cmdSigDesc = {};
        cmdSigDesc.NumArgumentDescs = std::size(arguments);
        cmdSigDesc.pArgumentDescs = arguments;
        cmdSigDesc.ByteStride = sizeof(UnshufflePassIndirectBuffer);

        DX::ThrowIfFailed(device->CreateCommandSignature(&cmdSigDesc, nullptr, IID_PPV_ARGS(m_unshufflePassCmdSignature.ReleaseAndGetAddressOf())));
    }

    // Upload shuffled data to GPU
    {
        m_deviceResources->ResetCommandList();
        auto commandList = m_deviceResources->GetCommandList();

        // Here we would need to upload the data we got from the shuffled buffer to the gpu
        uint8_t* data = nullptr;
        DX::ThrowIfFailed(m_shuffledBufferUpload->Map(0, nullptr, reinterpret_cast<void**>(&data)));
        memcpy(data, shuffledBuffer.data(), shuffledBuffer.size());
        m_shuffledBufferUpload->Unmap(0, nullptr);

        // Once uploaded, transition resources to correct states and perform the copy
        {
            D3D12_RESOURCE_BARRIER barriers[1] = {};
            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_shuffledBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, barriers);
            
            commandList->CopyResource(m_shuffledBuffer.Get(), m_shuffledBufferUpload.Get());

            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_shuffledBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            commandList->ResourceBarrier(1, barriers);
        }

        commandList->Close();
        ID3D12CommandList* cmdLists[] = { commandList };
        auto commandQueue = m_deviceResources->GetCommandQueue();
        commandQueue->ExecuteCommandLists(1, cmdLists);

        m_deviceResources->WaitForGpu();
    }
}

void Game::CompareAgainstOriginalBC7Data(std::vector<uint8_t> const& BC7OriginalData, 
    std::vector<uint8_t> const& BC7UnshuffledData, UNSHUFFLE_PER_TEST_STATISTICS& stats)
{
    // Compare m_OriginalBC7Buffer vs m_BC7UnshuffledData block per block. Gather stats and log at the end

    uint32_t blockIndex = 0;

    uint32_t modeCounts[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint32_t modePassCounts[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint32_t modeFailCounts[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    do
    {
        uint32_t blockBaseAddress = blockIndex * 16;
        uint8_t modeByte = BC7OriginalData[blockBaseAddress];

        DWORD mode;
        if (!_BitScanForward(&mode, modeByte))
        {
            mode = 8;
        }

        bool pass = true;
        for (uint8_t byte = 0; byte < 16 && pass; ++byte)
        {
            if (BC7OriginalData[blockBaseAddress + byte] != BC7UnshuffledData[blockBaseAddress + byte])
            {
                pass = false;
            }
        }

        modeCounts[mode]++;
        if (pass) 
        {
            modePassCounts[mode]++;
        }
        else 
        {
            modeFailCounts[mode]++;
        }

        blockIndex++;
    } 
    while (blockIndex * 16 < BC7OriginalData.size());

    uint32_t modesEvaluated = 0;
    for (uint8_t i = 0; i < 9; ++i)  modesEvaluated += modeCounts[i];
    assert(modesEvaluated == blockIndex);

    stats.blockCount = modesEvaluated;

    bool passed = true;
    for (size_t i = 0; i < 9; ++i) 
    {
        passed = passed && (modeFailCounts[i] == 0);

        stats.perModeFailCount[i] = modeFailCounts[i];
        stats.perModePassCount[i] = modePassCounts[i];
    }

    stats.testPassed = passed;
}

void Game::ResetD3D12Resources() 
{
    // Prepass
    m_prepassCSRootSignature.Reset();
    m_prepassCSPipelineState.Reset();

    // PrefixSum
    m_prefixSumCSRootSignature.Reset();
    m_prefixSumCSPipelineState.Reset();

    // Mode Binning
    m_modeBinningCSRootSignature.Reset();
    m_modeBinningCSPipelineState.Reset();

    // Unshuffle
    m_unshuffleCSRootSignature.Reset();
    for (uint32_t i = 0; i < BC7_MODES_COUNT; ++i) 
    {
        m_unshuffleBinnedCSPSO[i].Reset();
    }
    m_unshufflePassCmdSignature.Reset();

    // Resources that need allocation
    m_outBCnBuffer.Reset();
    m_scratchBuffer.Reset();
    m_unshufflePassIndirectBuffer.Reset();

    // And their heaps
    m_intermediateHeap.Reset();
    m_outputHeap.Reset();

    // Inputs and Aux
    m_shuffledBuffer.Reset();
    m_shuffledBufferUpload.Reset();
    m_BCnReadback.Reset();
}