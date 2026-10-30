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
    BC7Join,    // BC7 Join streams (headerless GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN, or ..._BC7_EX with a Join header), and
                // curve-only BC7 streams (..._BC7_EX)
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
            bool useHeader;     // the format's EX stream (GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_EX - _BC5_EX: header, duplicate
                                // removal, curve parameter)
            uint8_t predictionOff;  // EX: BCNEX_PREDICT_* bits removed from the format's default index prediction
                                    // (header parameter 2 when nonzero)
            bool coded;             // EX (BC5): no duplicate blocks, so the stream header must code blocks to make room for itself
            bool longCoded;         // EX: a header written by the test, coding the costliest block the format's code allows
                                    // (EncodeBCnExLongCoded); the CPU decode is the expected output
            bool overrun;           // with longCoded: the header's last bit is the first past its k blocks, in a stream of one
                                    // decoder thread group (which decodes every body): the decoders reject it, the output stays zero
            bool manyCoded;         // with longCoded: two removed duplicates sourced above coded blocks, and at least 40 coded
                                    // blocks (more positions than a 32-lane wave reads at once)
            bool curveOnly;         // EX: a curve-only stream (header parameter 2 BCNEX_UNSHUFFLED: the curved blocks,
                                    // unshuffled), making room for its header by removing duplicates or, with coded, coding blocks
            bool unshuffled;        // EX: BCNEX_UNSHUFFLED added to the index prediction mask (the kept blocks unshuffled,
                                    // still predicted), which the encoder only writes for curve-only streams
            bool farDuplicates;     // EX: every block unique but two pairs far apart and far from block 0 (AddFarDuplicates):
                                    // long codes, which for 8-byte blocks fill a two-block header past FastParseHeader's window
        } BC1345;

        struct {
            uint32_t ChunkSize;
            uint8_t endpointOrderStrategy;
            uint32_t patternIndex;
            bool useIndexPrediction;
            uint8_t sectionLayout;
        } BC7;

        struct {
            uint8_t masks;          // 0 = none, 1 = one statics mask for all modes, 2 = per-mode masks, 3 = per-mode with Mode 5 / 6 alpha flags,
                                    // 4 = per-mode with only the alpha flags of Modes 4-7, 5 = every flag for all modes
            bool coded;             // every block unique, so the stream header must code blocks to make room for itself
            uint8_t truncation;     // 0 = none, 1 = the stream loses its last block, 2 = its last byte (the decoders reject it)
            bool remap;             // the blocks are byte-remapped (header parameter 4)
            bool mode5;             // most blocks Mode 5 (as in RDO content), for whole waves of Mode 5 blocks after the decoder's mode sort
            bool spareHeaderBlock;  // the header rewritten into two blocks, removing a second duplicate, though it fits in one: valid
                                    // (the decoders don't require the fewest header blocks), though not the encoder's choice
            bool farDuplicate;      // every block unique but one pair far apart and far from block 0: long codes in a one-block
                                    // header, crossing its 32-bit words
            bool curveOnly;         // not Join: a curve-only BC7 stream (BC7_EX, the blocks carried as BC5_116116 blocks), with coded as
                                    // for BC1345 (the other fields unused)
            uint32_t transformId;   // set by the test: GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN or ..._BC7_EX
            uint32_t streamSize;    // set by the test: the (possibly truncated) stream size given to the shader
        } BC7Join;
    };

#ifdef NEWBC1345
    uint32_t transformOverride; // 0, or an EX stream's id given to the shader in place of its own (and dispatched for, as
                                // DirectStorage would): another format's EX id, under which the decoders reject a header
                                // naming the stream's format (the output stays zero), but for BC5_116116 headers of
                                // parameter 2 exactly BCNEX_UNSHUFFLED (curve-only BC7 streams), which BC5_EX and BC7_EX
                                // both take. The CPU decoder (BCnEx_Decode) gives the expected output.
    bool escape;                // an EX stream with its header's escape bit (GACL_STREAMHEADER_ESCAPE) set, as a future
                                // format extension would set it: the decoders reject it (the output stays zero), as the
                                // CPU decoder does
#endif
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

#ifdef NEWBC1345
    bool RunBC7JoinTest(GTestParameters const& params);
#endif

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
    bool                                            m_useUnifiedShader; // -unified: UnshuffleBCnEx.cso for BC1\3\4\5\7 Join

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

    /*               BC1\3\4\5 and BC7 Join unshuffle (Game.cpp gShaderInfo: an EX shader per GBaseFormat below BC7, then a
                     headerless shader per BC1\3\4\5) */
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     m_bcnCSRootSignature[9];
    Microsoft::WRL::ComPtr<ID3D12PipelineState>     m_bcnCSPipelineState[9];

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