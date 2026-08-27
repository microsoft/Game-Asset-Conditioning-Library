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
// for the GACL_SHUFFLE_TRANSFORM enum used to drive BC1\3\4\5 unshuffle shader selection
#include "gacl.h"
// for Root descriptor versus heap descriptor...
#include "../../Shaders/Shuffle/Shared.hlsli"

// Selects the newer BC1\3\4\5 shaders (with the "x" suffix) added for this release over the
// original preview shaders. The legacy preview shaders are still built, but the default is the new set.
#define NEWBC1345

enum GBaseFormat
{
    BC1,
    BC3,
    BC4,
    BC5,
    CurveOnly,
    BC7,
};

enum GPU_TIMER_OPERATIONS 
{
    UNSHUFFLE_PREPASS = 0,
    UNSHUFFLE_PREFIX_SUM,
    UNSHUFFLE_BINNING,
    UNSHUFFLE_FINAL,
    COPY_TO_TEXTURE,
    COPY_TO_READBACK,
    RENDER,
    GPU_TIMER_OP_COUNT
};

enum CPU_TIMER_OPERATIONS
{
    FULL_FRAME_TIME = 0,
    UNSHUFFLE_CL_SUBMIT_TO_FENCE_SIGNAL,
    CPU_TIMER_OP_COUNT
};

// A basic game implementation that creates a D3D12 device and provides a render loop.
class Game final : public DX::IDeviceNotify
{
public:

    Game() noexcept(false);
    ~Game() = default;

    Game(Game&&) = default;
    Game& operator= (Game&&) = default;

    Game(Game const&) = delete;
    Game& operator= (Game const&) = delete;

    // Initialization and management
    void Initialize(HWND window, LPWSTR lpCmdLine);

    // Basic render loop
    void Tick();

    // Messages
    void OnSuspending();
    void OnResuming();
    void OnWindowSizeChanged(int width, int height);
    void OnWindowMoved();
    void OnConstrained() {}
    void OnUnConstrained() {}

    void GetDefaultSize(int& width, int& height) const;

    // Scroll support
    void SetScrollOffset(int x, int y) noexcept { m_scrollX = static_cast<uint32_t>(x); m_scrollY = static_cast<uint32_t>(y); }
    int  GetTextureWidth()  const noexcept { return static_cast<int>(m_bcTextureWidthInPixels); }
    int  GetTextureHeight() const noexcept { return static_cast<int>(m_bcTextureHeightInPixels); }

private:
    void Update(DX::StepTimer const& timer);
    void Render();
    void Unshuffle();    
    void RenderUI();

    void OnDeviceLost() override;
    void OnDeviceRestored() override;

    void Clear();

    void CreateBC7UnshufflePassD3D12Resources(std::vector<uint8_t>& shuffledBuffer);

    void CreateBC1345UnshufflePassD3D12Resources(std::vector<uint8_t>& shuffledBuffer, size_t sizeInBytes);

    bool CompareAgainstOriginalBCnData();

    void LoadImageAndShuffle(const std::wstring& rootTestDirectory);

    void GetInputAndIntermediateResourceSizes(size_t const bcnSizeInBytes, uint32_t& scratchBufferSizeBytes, uint32_t& indirectArgBufferSize, uint32_t& headerSizeBytes, uint32_t& histogramBufferSizeInBytes);

    void ResetD3D12Resources();

    void CreateRenderPassResources();

    std::unique_ptr<DX::DeviceResources>            m_deviceResources;

    uint64_t                                        m_frame;
    DX::StepTimer                                   m_timer;

    std::unique_ptr<DX::CPUTimer>                   m_CPUTimer;
    double                                          m_frameDeltaTime;

    std::unique_ptr<DX::GPUTimer>                   m_GPUTimer;
    double                                          m_passDurations[GPU_TIMER_OP_COUNT];
    double                                          m_clSubmissionToFenceTimeMs;

    uint32_t                                        m_offsetIntoHistogram;
    uint32_t                                        m_offsetIntoBinning;
    uint32_t                                        m_offsetIntoHeader;

    std::vector<uint8_t>                            m_BCnOriginalData;
    std::vector<uint8_t>                            m_BCnUnshuffledData;

    size_t                                          m_bcnSizeInBytes;
    size_t                                          m_shuffledBufferSizeBytes;
    size_t                                          m_bcTextureWidthInPixels;
    size_t                                          m_bcTextureHeightInPixels;
    size_t                                          m_bcTextureSubresourceCount;
    DXGI_FORMAT                                     m_bcTextureFormat;

    // BC1\3\4\5 unshuffle state
    GBaseFormat                                     m_baseFormat;
    uint32_t                                        m_elementSize;
    uint32_t                                        m_microShuffleId;
    bool                                            m_useSpaceCurveEffective;

    // Transform id selected by GACL_ShuffleCompress_BCn; passed to the BC1\3\4\5 unshuffle shader as a constant.
    GACL_SHUFFLE_TRANSFORM                          m_bcnTransformId;

    uint32_t                                        m_minHWSupportedWaveSize;
    uint32_t                                        m_maxHWSupportedWaveSize;
    bool                                            m_forceWarp;

    // Resource to copy bc7 buffer into and render in screen
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_bcnRenderResource;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     m_renderPassRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>     m_renderPassPipelineState;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    m_renderPassDescHeap;

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

    // Output resource and its heap
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_outBCnBuffer; 

    // BC1\3\4\5 single-pass unshuffle
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     m_bcnCSRootSignature[5];
    Microsoft::WRL::ComPtr<ID3D12PipelineState>     m_bcnCSPipelineState[5];

    // Shader visible descriptor heap for the BC1\3\4\5 output UAV (descriptor-table binding path)
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    m_uavSrvCbvHeap;
    uint32_t                                        m_DHIncrementSize;
    D3D12_GPU_DESCRIPTOR_HANDLE                     m_UAVOutputBufferGpuHandle;

    // Resources the sample needs
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_shuffledBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_shuffledBufferUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_BCnReadback;

    std::wstring                                    m_inputFileName;
    std::wstring                                    m_inputPath;

    // Scroll offsets (in pixels) for the texture viewport
    uint32_t                                        m_scrollX;
    uint32_t                                        m_scrollY;

    // Text rendering
    struct ResourceDescriptors
    {
        enum : uint32_t
        {
            Font,
            Count
        };
    };

    bool                                            m_validationPassed = false;
    std::unique_ptr<DirectX::GraphicsMemory>        m_graphicsMemory;
    std::unique_ptr<DirectX::DescriptorHeap>        m_resourceDescriptorHeap;
    std::unique_ptr<DirectX::SpriteBatch>           m_spriteBatch;
    std::unique_ptr<DirectX::SpriteFont>            m_font;
};