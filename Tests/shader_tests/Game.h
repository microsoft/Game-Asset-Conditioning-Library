//--------------------------------------------------------------------------------------
// Game.h
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "DeviceResources.h"
#include "StepTimer.h"
#include <vector>
// for Root descriptor versus heap descriptor...
#include "../../Shaders/Shuffle/Shared.hlsli"

// Auxiliary define for when we want to run the set of tests on a 
// particular texture loaded from disk - Off by default.
#define RUN_TEST_FROM_TEXTURE_IN_DISK   0
#define DUMP_SHUFFLED_BUFFER_TO_DISK    0

// Uncomment if a pix capture of the Unshuffle passes is needed
//#define CAPTURE_PIX
//#define CAPTURE_SELECT     if (params.baseFormat == GBaseFormat::BC1 && params.BC1345.microShuffleId == 1 && sizeInBytes >8000000)


#define NEWBC1345

enum GBaseFormat
{
    BC1,
    BC3,
    BC4,
    BC5,
#ifdef NEWBC1345
    CurveOnly,
#endif
    BC7,
};

struct GTestParameters 
{
    GBaseFormat baseFormat;
    std::string testName;
    uint32_t width;
    uint32_t height;
    bool useSpaceCurve;

    union {
        struct
        {
            uint32_t microShuffleId;
        } BC1345;

        struct {
            uint32_t ChunkSize;
            uint8_t endpointOrderStrategy;
            uint32_t patternIndex;
        } BC7;
    };
};

struct UNSHUFFLE_PER_TEST_STATISTICS
{
    // Time the test took
    float gpuTimeMiliseconds;
    float prepassMiliseconds;
    float prefixsumMiliseconds;
    float binningMiliseconds;
    float unshuffleMiliseconds;

    // Info about the texture
    std::string testName;
    uint32_t textureWidthPixels;
    uint32_t blockCount;

    // Info about passing-stats per mode
    bool testPassed;
    uint32_t perModePassCount[9];
    uint32_t perModeFailCount[9];
};

// A basic game implementation that creates a D3D12 device and
// provides a render loop.
class Game
{
public:

    Game() noexcept(false);
    ~Game() = default;

    Game(Game&&) = default;
    Game& operator= (Game&&) = default;

    Game(Game const&) = delete;
    Game& operator= (Game const&) = delete;

    // Initialization and management
    void Initialize(LPWSTR lpCmdLine);

    // Runs the full shuffle→unshuffle→validate pipeline for a loaded BCn texture. 
    // Returns true if all permutations pass.
    bool RunTestWithParams(GTestParameters const& params);
    
    #if RUN_TEST_FROM_TEXTURE_IN_DISK
    // Runs a single test for a given set of parameters
    bool RunAllPermutationsForLoadedTexture();

    // Loads a BCn from disk and stores raw data, width and height
    void LoadTextureFromDisk(uint32_t& widthPixels, uint32_t& heightPixels, DXGI_FORMAT& format, std::vector<uint8_t>& bcTextureRaw);
    #endif

private:

    void Unshuffle(GTestParameters params, size_t sizeInBytes);

    void UnshuffleCPUReadback(std::vector<uint8_t>& BCnUnshuffledData, size_t bcSizeInBytes);

    void Clear();

    void CreateBC1345UnshufflePassD3D12Resources(std::vector<uint8_t>& shuffledBuffer, size_t bcSizeInBytes);
    void CreateBC7UnshufflePassD3D12Resources(std::vector<uint8_t>& shuffledBuffer, size_t bcSizeInBytes);

    void CompareAgainstOriginalBC7Data(std::vector<uint8_t> const& BC7OriginalData,
        std::vector<uint8_t> const& BC7UnshuffledData, UNSHUFFLE_PER_TEST_STATISTICS& stats);

    void TestGpuUnshuffling(GTestParameters params, std::vector<uint8_t> const& BCnOriginalData, UNSHUFFLE_PER_TEST_STATISTICS& testStats, size_t sizeInBytes);

    void ResetD3D12Resources();

    void CreateBC7RawFromMetadata(uint32_t widthPixels, uint32_t heightPixels, std::vector<uint8_t>& rawBC7);
    void CreateBCnRawFromMetadata(uint32_t widthPixels, uint32_t heightPixels, uint32_t elementSize, std::vector<uint8_t>& rawBCn);


    // Device resources.
    std::unique_ptr<DX::DeviceResources>            m_deviceResources;

    // Rendering loop timer.
    DX::StepTimer                                   m_timer;
    
    // Relevant if uav heap is bigger than output unshuffle resource. 
    size_t                                          m_uavHeapOffsetInBytes;

    bool                                            m_allTestsPassed;
    uint32_t                                        m_minHWSupportedWaveSize;
    uint32_t                                        m_maxHWSupportedWaveSize;
    bool                                            m_forceWarp;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    m_uavSrvCbvHeap;
    uint32_t                                        m_DHIncrementSize;
    D3D12_GPU_DESCRIPTOR_HANDLE                     m_UAVOutputBufferGpuHandle;
    uint32_t                                        m_offsetIntoHistogram;
    uint32_t                                        m_offsetIntoBinning;
    uint32_t                                        m_offsetIntoHeader;

    /*               BC7 unshuffle                  */
    // Prepass
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     m_prepassCSRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>     m_prepassCSPipelineState;

    // PrefixSum
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     m_prefixSumCSRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>     m_prefixSumCSPipelineState;

    // Mode Binning
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     m_modeBinningCSRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>     m_modeBinningCSPipelineState;

    // Unshuffle
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     m_unshuffleCSRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>     m_unshuffleBinnedCSPSO[9];
    Microsoft::WRL::ComPtr<ID3D12CommandSignature>  m_unshufflePassCmdSignature;

    // Intermediate resources and their heap
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_scratchBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_unshufflePassIndirectBuffer;
    Microsoft::WRL::ComPtr<ID3D12Heap>              m_intermediateHeap;

    /*               BC1\3\4\5 unshuffle           */
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     m_bcnCSRootSignature[5];
    Microsoft::WRL::ComPtr<ID3D12PipelineState>     m_bcnCSPipelineState[5];

    // Output resource and its heap
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_outBCnBuffer; 
    Microsoft::WRL::ComPtr<ID3D12Heap>              m_outputHeap;

    // Resources the sample needs
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_shuffledBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_shuffledBufferUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_BCnReadback;
    
    #if RUN_TEST_FROM_TEXTURE_IN_DISK
    std::wstring                                    m_inputPath;
    #endif  
};