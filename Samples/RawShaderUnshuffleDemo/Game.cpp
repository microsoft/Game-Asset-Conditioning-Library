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

extern void ExitGame() noexcept;

using namespace DirectX;

using Microsoft::WRL::ComPtr;

static float g_bgColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

static ShuffleParameters g_shuffleParams = {};

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

// Per-shader description for the single-pass BC1\3\4\5 (and CurveOnly) unshuffle shaders. The order matches the
// GBaseFormat enum so an entry can be indexed directly by base format.
struct ShuffleShaderInfo
{
    std::wstring fileName;
    std::wstring signatureName;
    std::wstring pixName;
    uint32_t blockSizes;      // bytes per BCn block
    uint32_t blocksPerThread; // blocks each thread unshuffles
};

#ifdef NEWBC1345
// Newer BC1\3\4\5 shaders ("x" suffix). A single shader per format handles multiple transform ids; the specific
// transform id (as chosen by GACL_ShuffleCompress_BCn) is passed in as a shader constant.
static const ShuffleShaderInfo gShaderInfo[] = {
    {L"UnshuffleBC1x.cso", L"BC1xCSRootSignature", L"BC1x Unshuffle", 8,  2},
    {L"UnshuffleBC3x.cso", L"BC3xCSRootSignature", L"BC3x Unshuffle", 16, 4},
    {L"UnshuffleBC4x.cso", L"BC4xCSRootSignature", L"BC4x Unshuffle", 8,  4},
    {L"UnshuffleBC5x.cso", L"BC5xCSRootSignature", L"BC5x Unshuffle", 16, 4},
    {L"UnshuffleCurveOnly.cso", L"BCnCurveOnlyRootSignature", L"Uncurve only", 16, 32},
};

// With the newer shaders we allow the library to consider the experimental (v2) transforms too.
constexpr GACL_SHUFFLE_TRANSFORM kRequestedTransformGroup = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;
#else // Preview #1 interim shaders
static const ShuffleShaderInfo gShaderInfo[] = {
    {L"UnshuffleBC1.cso", L"BC1CSRootSignature", L"BC1 Unshuffle", 8,  2},
    {L"UnshuffleBC3.cso", L"BC3CSRootSignature", L"BC3 Unshuffle", 16, 4},
    {L"UnshuffleBC4.cso", L"BC4CSRootSignature", L"BC4 Unshuffle", 8,  4},
    {L"UnshuffleBC5.cso", L"BC5CSRootSignature", L"BC5 Unshuffle", 16, 4},
};

// The preview shaders only reverse the currently supported transforms.
constexpr GACL_SHUFFLE_TRANSFORM kRequestedTransformGroup = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED;
#endif

// Maps a transform id (as returned by GACL_ShuffleCompress_BCn) to the gShaderInfo entry / GBaseFormat that reverses
// it. The per-format shaders handle both the base and the space-curve (_SC) variants of their transform.
static GBaseFormat TransformToShaderFormat(GACL_SHUFFLE_TRANSFORM transformId)
{
    switch (transformId)
    {
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224_SC:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44_SC:
        return GBaseFormat::BC1;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224_SC:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664_SC:
        return GBaseFormat::BC3;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116_SC:
        return GBaseFormat::BC4;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116_SC:
        return GBaseFormat::BC5;
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT_SC:
        return GBaseFormat::BC7;
#ifdef NEWBC1345
    case GACL_SHUFFLE_TRANSFORM_ZSTD_SC:
        return GBaseFormat::CurveOnly;
#endif
    default:
        assert(false && "Unexpected transform id returned by GACL_ShuffleCompress_BCn");
        return GBaseFormat::BC1;
    }
}

// Human-readable base format name for the on-screen UI.
static const wchar_t* BaseFormatToString(GBaseFormat baseFormat)
{
    switch (baseFormat)
    {
    case GBaseFormat::BC1: return L"BC1";
    case GBaseFormat::BC3: return L"BC3";
    case GBaseFormat::BC4: return L"BC4";
    case GBaseFormat::BC5: return L"BC5";
#ifdef NEWBC1345
    case GBaseFormat::CurveOnly: return L"CurveOnly";
#endif
    case GBaseFormat::BC7: return L"BC7";
    default: return L"Unknown";
    }
}

// Human-readable name for a transform id (as returned by GACL_ShuffleCompress_BCn) for the on-screen UI.
static const wchar_t* TransformIdToString(GACL_SHUFFLE_TRANSFORM transformId)
{
    switch (transformId)
    {
    case GACL_SHUFFLE_TRANSFORM_NONE:             return L"NONE";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224:     return L"ZSTD_BC1_224";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224_SC:  return L"ZSTD_BC1_224_SC";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224:  return L"ZSTD_BC3_116224";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224_SC: return L"ZSTD_BC3_116224_SC";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116:     return L"ZSTD_BC4_116";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116_SC:  return L"ZSTD_BC4_116_SC";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116:  return L"ZSTD_BC5_116116";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116_SC: return L"ZSTD_BC5_116116_SC";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT:   return L"ZSTD_BC7_SPLIT";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT_SC: return L"ZSTD_BC7_SPLIT_SC";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN:    return L"ZSTD_BC7_JOIN";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN_SC: return L"ZSTD_BC7_JOIN_SC";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY:        return L"ZSTD_ONLY";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_SC:          return L"ZSTD_SC";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44:      return L"ZSTD_BC1_44";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44_SC:   return L"ZSTD_BC1_44_SC";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664:     return L"ZSTD_BC3_664";
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664_SC:  return L"ZSTD_BC3_664_SC";
    default: return L"Unknown";
    }
}

// Returns true when the transform id's shuffled stream holds space-curved data (the "_SC" variants).
static bool TransformIsCurved(GACL_SHUFFLE_TRANSFORM transformId)
{
    switch (transformId)
    {
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224_SC:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_116224_SC:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC4_116_SC:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC5_116116_SC:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT_SC:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN_SC:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_SC:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_44_SC:
    case GACL_SHUFFLE_TRANSFORM_ZSTD_BC3_664_SC:
        return true;
    default:
        return false;
    }
}


Game::Game() noexcept(false) :
    m_frame(0),
    m_frameDeltaTime(0.0),
    m_passDurations{},
    m_clSubmissionToFenceTimeMs(0.0),
    m_offsetIntoHistogram(0),
    m_offsetIntoBinning(0),
    m_offsetIntoHeader(0),
    m_bcnSizeInBytes(0),
    m_shuffledBufferSizeBytes(0),
    m_bcTextureWidthInPixels(0),
    m_bcTextureHeightInPixels(0),
    m_bcTextureSubresourceCount(0),
    m_bcTextureFormat{},
    m_baseFormat(GBaseFormat::BC7),
    m_elementSize(0),
    m_microShuffleId(1),
    m_useSpaceCurveEffective(false),
    m_bcnTransformId(GACL_SHUFFLE_TRANSFORM_NONE),
    m_minHWSupportedWaveSize(0),
    m_maxHWSupportedWaveSize(0),
    m_forceWarp(false),
    m_DHIncrementSize(0),
    m_UAVOutputBufferGpuHandle{},
    m_scrollX(0),
    m_scrollY(0)
{
    m_deviceResources = std::make_unique<DX::DeviceResources>();
    m_deviceResources->RegisterDeviceNotify(this);
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(HWND window, LPWSTR lpCmdLine)
{
    int numArgs = 0;
    LPWSTR* argList = CommandLineToArgvW(lpCmdLine, &numArgs);
    
    if (!lpCmdLine || lpCmdLine[0] == L'\0' || !argList || numArgs == 0)
    {
        throw std::runtime_error("Did not provide any argument for the sample.");
    }

    for (int i = 0; i < numArgs;)
    {
        LPWSTR arg = argList[i];
        
        if (wcscmp(arg, L"/path") == 0 || wcscmp(arg, L"-path") == 0)
        {
            m_inputPath = argList[i + 1];
            i += 2;
        }
        else if (wcscmp(arg, L"/warp") == 0 || wcscmp(arg, L"-warp") == 0)
        {
            m_forceWarp = true;
            i += 1;
        }
        else 
        {
            throw std::runtime_error("Unexpected argument.");
        }
    }
    LocalFree(argList);

    m_deviceResources->SetWindow(window);

    m_deviceResources->CreateDeviceResources(m_forceWarp);
    
    m_deviceResources->CreateWindowSizeDependentResources();

    auto device = m_deviceResources->GetD3DDevice();
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
    DX::ThrowIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1)));
    m_minHWSupportedWaveSize = options1.WaveLaneCountMin;
    m_maxHWSupportedWaveSize = options1.WaveLaneCountMax;
    
    LoadImageAndShuffle(m_inputPath);

    CreateRenderPassResources();

    m_GPUTimer = std::make_unique<DX::GPUTimer>(device, m_deviceResources->GetCommandQueue());
    
    m_CPUTimer = std::make_unique<DX::CPUTimer>();
    m_CPUTimer->Start(FULL_FRAME_TIME);
}

void Game::LoadImageAndShuffle(const std::wstring& ddsPathWStr)
{
    std::error_code ec;
    if (!std::filesystem::exists(ddsPathWStr, ec))
    {
        throw std::runtime_error("File does not exist.");
    }

    std::filesystem::path const filePath = std::filesystem::path(ddsPathWStr);

    // Remember just the file name (without directories) for the on-screen UI.
    m_inputFileName = filePath.filename().wstring();

    // Setup shuffle parameters for GACL
    g_shuffleParams.useSpaceCurve = true;
    g_shuffleParams.modeTransform = BC7ModeSplitModeTransformB;
    g_shuffleParams.ChunkSize = 64 * 1024 * 1024;
    g_shuffleParams.endpointOrderStrategy = 0;

    g_shuffleParams.Patterns[0] = EndpointPair4bit;
    g_shuffleParams.Patterns[1] = EndpointPairSignificantBitInderleaved;
    g_shuffleParams.Patterns[2] = EndpointPair4bit;
    g_shuffleParams.Patterns[3] = EndpointPairSignificantBitInderleaved;
    g_shuffleParams.Patterns[4] = StableIsland;
    g_shuffleParams.Patterns[5] = StableIsland;
    g_shuffleParams.Patterns[6] = StableIsland;
    g_shuffleParams.Patterns[7] = EndpointPairSignificantBitInderleaved;

    // Will hold the data for the buffer shuffled in ProcessTexture
    std::vector<uint8_t> shuffledBuffer;
    m_bcnSizeInBytes = 0u;
    m_bcTextureWidthInPixels = 0u;

    // Ask the library to pick a transform from the requested group (see kRequestedTransformGroup). For BC7 the group
    // value is ignored and ProcessTexture reports one of the mode-split transforms based on space-curve use.
    m_bcnTransformId = kRequestedTransformGroup;

    ProcessTexture(
        filePath,
        shuffledBuffer,   
        m_BCnOriginalData,
        m_bcnSizeInBytes, 
        m_bcTextureWidthInPixels, 
        m_bcTextureHeightInPixels,
        m_bcTextureSubresourceCount, 
        m_bcTextureFormat,
        m_bcnTransformId,
        g_shuffleParams);

    assert(m_bcTextureWidthInPixels > 0);

    m_shuffledBufferSizeBytes = shuffledBuffer.size();

    m_BCnUnshuffledData.resize(m_bcnSizeInBytes);

    // The base format is implied by the transform id ProcessTexture selected. BC7 uses the multi-pass mode-split
    // unshuffle; BC1\3\4\5 use the single-pass shaders.
    m_baseFormat = TransformToShaderFormat(m_bcnTransformId);

    if (m_baseFormat == GBaseFormat::BC7)
    {
        CreateBC7UnshufflePassD3D12Resources(shuffledBuffer);
    }
    else
    {
        m_elementSize = gShaderInfo[m_baseFormat].blockSizes;
        CreateBC1345UnshufflePassD3D12Resources(shuffledBuffer, m_bcnSizeInBytes);
    }
}

#pragma region Frame Update
// Executes the basic game loop.
void Game::Tick()
{
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Frame %llu", m_frame);

    m_timer.Tick([&]()
    {
        Update(m_timer);
    });
    
    Render();
    
    PIXEndEvent();
    m_frame++;
}

// Updates the world.
void Game::Update(DX::StepTimer const& timer)
{
    PIXScopedEvent(PIX_COLOR_DEFAULT, L"Update");
    std::ignore = timer;

    m_CPUTimer->Stop(FULL_FRAME_TIME);
    m_frameDeltaTime = m_CPUTimer->GetElapsedMS(FULL_FRAME_TIME);
    m_CPUTimer->Start(FULL_FRAME_TIME);
}
#pragma endregion

#pragma region Frame Render
// Draws the scene.
void Game::Render()
{
    auto commandList = m_deviceResources->GetCommandList();
    auto cmdQueue = m_deviceResources->GetCommandQueue();
    auto device = m_deviceResources->GetD3DDevice();
    ID3D12CommandList* cmdLists[] = { commandList };

    m_deviceResources->ResetCommandList();
    
    m_GPUTimer->BeginFrame(commandList);

    // Add unshuffling shader commands to the command list
    Unshuffle();

    // Measure the time it takes from commandList submit to when the GPU is finished with the unshuffle passes.
    m_CPUTimer->Start(UNSHUFFLE_CL_SUBMIT_TO_FENCE_SIGNAL);

    // Executing command list so we can record the time to fence completion.
    commandList->Close();
    cmdQueue->ExecuteCommandLists(1, cmdLists);

    m_deviceResources->WaitForTimerFence();
    m_CPUTimer->Stop(UNSHUFFLE_CL_SUBMIT_TO_FENCE_SIGNAL);
    m_clSubmissionToFenceTimeMs = m_CPUTimer->GetElapsedMS(UNSHUFFLE_CL_SUBMIT_TO_FENCE_SIGNAL);
    
    m_deviceResources->ResetCommandList();

    // Copy bcn raw buffer to bcn texture to render on GPU. This can be skipped if using typed UAVs.
    {
        PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"CopyTextureRegion to BCn format");
        m_GPUTimer->Start(commandList, static_cast<uint32_t>(COPY_TO_TEXTURE));

        D3D12_RESOURCE_BARRIER barriers[2] = {};
        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_bcnRenderResource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_outBCnBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->ResourceBarrier(2, barriers);

        D3D12_RESOURCE_DESC baseDesc = CD3DX12_RESOURCE_DESC::Tex2D(m_bcTextureFormat, m_bcTextureWidthInPixels, (UINT)m_bcTextureHeightInPixels, 1, (UINT16)m_bcTextureSubresourceCount, 1, 0);

        uint64_t totalSubresourceSumBytes = 0u;
        for (uint32_t i = 0; i < (UINT)m_bcTextureSubresourceCount; ++i)
        {
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
            UINT64 sizeBytes = 0;
            UINT32 numRows = 0;
            UINT64 rowSizeBytes = 0;
            device->GetCopyableFootprints(&baseDesc, i, 1, 0, &footprint, &numRows, &rowSizeBytes, &sizeBytes);

            D3D12_SUBRESOURCE_FOOTPRINT subFootprint = {};
            subFootprint.RowPitch = static_cast<UINT>(rowSizeBytes); // Using the UnrestrictedRowPitch feature.
            subFootprint.Width = footprint.Footprint.Width;
            subFootprint.Height = footprint.Footprint.Height;
            subFootprint.Format = footprint.Footprint.Format;
            subFootprint.Depth = footprint.Footprint.Depth;

            D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
            srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcLocation.pResource = m_outBCnBuffer.Get();
            srcLocation.PlacedFootprint.Offset = totalSubresourceSumBytes;
            srcLocation.PlacedFootprint.Footprint = subFootprint;

            D3D12_TEXTURE_COPY_LOCATION dstLocation = CD3DX12_TEXTURE_COPY_LOCATION(m_bcnRenderResource.Get(), i);

            commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
            totalSubresourceSumBytes += (rowSizeBytes * numRows);
        }
        assert(totalSubresourceSumBytes == m_bcnSizeInBytes);
           
        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_bcnRenderResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1u, barriers);

        m_GPUTimer->Stop(commandList, static_cast<uint32_t>(COPY_TO_TEXTURE));
        PIXEndEvent(commandList);
    }

    // Copy to readback so CPU can check the resulting buffer
    {
        m_GPUTimer->Start(commandList, static_cast<uint32_t>(COPY_TO_READBACK));
        
        commandList->CopyBufferRegion(m_BCnReadback.Get(), 0ull, m_outBCnBuffer.Get(), 0, m_bcnSizeInBytes);

        m_GPUTimer->Stop(commandList, static_cast<uint32_t>(COPY_TO_READBACK));

        // ExecuteCommandLists
        commandList->Close();
        cmdQueue->ExecuteCommandLists(1, cmdLists);

        // Wait on CPU for gpu copy to complete
        m_deviceResources->WaitForReadbackFence();

        // Get data from readback
        uint8_t* pReadbackBufferData = nullptr;
        DX::ThrowIfFailed(m_BCnReadback->Map(0, nullptr, reinterpret_cast<void**>(&pReadbackBufferData)));
        memcpy(&m_BCnUnshuffledData[0], pReadbackBufferData, m_bcnSizeInBytes);
        m_BCnReadback->Unmap(0, nullptr);
    }
      
    // Compare the unshuffled data with the expected data
    m_validationPassed = CompareAgainstOriginalBCnData();

    // Reset commandList and transition backbuffer to RENDER_TARGET
    m_deviceResources->ResetCommandList();

    const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_deviceResources->GetRenderTarget(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &barrier);

    auto const rtvDescriptor = m_deviceResources->GetRenderTargetView();
    auto const dsvDescriptor = m_deviceResources->GetDepthStencilView();

    // Set the viewport and scissor rect.
    auto const viewport = m_deviceResources->GetScreenViewport();
    auto const scissorRect = m_deviceResources->GetScissorRect();
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect); 
    commandList->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor);

    if (m_validationPassed)
    {
        PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Render BC Texture");
        m_GPUTimer->Start(commandList, static_cast<uint32_t>(RENDER));

        ID3D12DescriptorHeap* descHeaps[] = { m_renderPassDescHeap.Get() };
        commandList->SetDescriptorHeaps(1, descHeaps);

        commandList->SetGraphicsRootSignature(m_renderPassRootSignature.Get());

        commandList->SetGraphicsRoot32BitConstant(0u, static_cast<uint32_t>(m_bcTextureWidthInPixels), 0);
        commandList->SetGraphicsRoot32BitConstant(0u, static_cast<uint32_t>(m_bcTextureHeightInPixels), 1);
        commandList->SetGraphicsRoot32BitConstant(0u, m_scrollX, 2);
        commandList->SetGraphicsRoot32BitConstant(0u, m_scrollY, 3);

        commandList->SetGraphicsRootDescriptorTable(1u, m_renderPassDescHeap->GetGPUDescriptorHandleForHeapStart());

        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->SetPipelineState(m_renderPassPipelineState.Get());
        commandList->DrawInstanced(3, 1, 0, 0);

        m_GPUTimer->Stop(commandList, static_cast<uint32_t>(RENDER));
        PIXEndEvent(commandList);
    }
    else 
    {
        g_bgColor[0] = 0.75f;
        commandList->ClearRenderTargetView(rtvDescriptor, g_bgColor, 0, nullptr);
    }

    m_GPUTimer->EndFrame(commandList);

    m_passDurations[UNSHUFFLE_PREPASS] = m_GPUTimer->GetAverageMS(static_cast<uint32_t>(UNSHUFFLE_PREPASS));
    m_passDurations[UNSHUFFLE_PREFIX_SUM] = m_GPUTimer->GetAverageMS(static_cast<uint32_t>(UNSHUFFLE_PREFIX_SUM));
    m_passDurations[UNSHUFFLE_BINNING] = m_GPUTimer->GetAverageMS(static_cast<uint32_t>(UNSHUFFLE_BINNING));
    m_passDurations[UNSHUFFLE_FINAL] = m_GPUTimer->GetAverageMS(static_cast<uint32_t>(UNSHUFFLE_FINAL));
    m_passDurations[COPY_TO_TEXTURE] = m_GPUTimer->GetAverageMS(static_cast<uint32_t>(COPY_TO_TEXTURE));
    m_passDurations[COPY_TO_READBACK] = m_GPUTimer->GetAverageMS(static_cast<uint32_t>(COPY_TO_READBACK));
    m_passDurations[RENDER] = m_GPUTimer->GetAverageMS(static_cast<uint32_t>(RENDER));

    RenderUI();

    m_graphicsMemory->Commit(m_deviceResources->GetCommandQueue());

    m_deviceResources->Present();
}

void Game::Unshuffle()
{
    auto commandList = m_deviceResources->GetCommandList();

    if (m_baseFormat != GBaseFormat::BC7)
    {
        // Single-pass BC1\3\4\5 (or CurveOnly) unshuffle. One shader per base format reverses the transform that
        // GACL_ShuffleCompress_BCn selected; the specific transform id is passed in as a shader constant.
        const ShuffleShaderInfo& info = gShaderInfo[m_baseFormat];

        PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, info.pixName.data());
        m_GPUTimer->Start(commandList, static_cast<uint32_t>(UNSHUFFLE_FINAL));

        uint32_t totalBlocks = static_cast<uint32_t>(m_bcnSizeInBytes) / info.blockSizes;
        uint32_t numPairsOrQuads = totalBlocks / info.blocksPerThread;
        uint32_t numStragglers = totalBlocks % info.blocksPerThread;
        uint32_t numThreadsRequired = numPairsOrQuads + numStragglers;

        uint32_t numThreadGroupsX = (m_baseFormat == GBaseFormat::CurveOnly) ?
            (numThreadsRequired + 31u) / 32u :
            (numThreadsRequired + (TG_THREAD_COUNT - 1)) / TG_THREAD_COUNT;

#ifdef NEWBC1345
        uint32_t shaderConstants[4] = { static_cast<uint32_t>(m_bcnSizeInBytes), 0,
            static_cast<uint32_t>(m_bcnTransformId), static_cast<uint32_t>(m_bcTextureWidthInPixels) };
#else
        uint32_t shaderConstants[2] = { static_cast<uint32_t>(m_bcnSizeInBytes), 0 };
#endif

        commandList->SetPipelineState(m_bcnCSPipelineState[m_baseFormat].Get());
        commandList->SetComputeRootSignature(m_bcnCSRootSignature[m_baseFormat].Get());

        // The render/readback path leaves the output buffer in COPY_SOURCE; transition it back to UAV before writing.
        {
            const D3D12_RESOURCE_BARRIER toUav =
                CD3DX12_RESOURCE_BARRIER::Transition(
                    m_outBCnBuffer.Get(),
                    D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &toUav);
        }

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

        const D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_outBCnBuffer.Get());
        commandList->ResourceBarrier(1, &uavBarrier);

        m_GPUTimer->Stop(commandList, static_cast<uint32_t>(UNSHUFFLE_FINAL));
        PIXEndEvent(commandList);
        return;
    }

    uint32_t numBlocks = static_cast<uint32_t>(m_bcnSizeInBytes) / BC7_BYTES_PER_BLOCK;
    uint32_t histogramCount = (numBlocks + TG_THREAD_COUNT - 1) / TG_THREAD_COUNT;

    D3D12_RESOURCE_BARRIER barriers[3] = {};

    // BC7 Prepass
    {
        PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"BC7 Prepass");
        m_GPUTimer->Start(commandList, static_cast<uint32_t>(UNSHUFFLE_PREPASS));

        commandList->SetPipelineState(m_prepassCSPipelineState.Get());

        commandList->SetComputeRootSignature(m_prepassCSRootSignature.Get());

        commandList->SetComputeRoot32BitConstant(0, numBlocks, 0);
        commandList->SetComputeRoot32BitConstant(0, m_offsetIntoHistogram, 1);

        commandList->SetComputeRootShaderResourceView(1, m_shuffledBuffer->GetGPUVirtualAddress());

        commandList->SetComputeRootUnorderedAccessView(2, m_scratchBuffer->GetGPUVirtualAddress());

        commandList->Dispatch(histogramCount, 1, 1);

        m_GPUTimer->Stop(commandList, static_cast<uint32_t>(UNSHUFFLE_PREPASS));
        PIXEndEvent(commandList);
    }

    barriers[0] = CD3DX12_RESOURCE_BARRIER::UAV(m_scratchBuffer.Get());
    commandList->ResourceBarrier(1, barriers);

    // BC7 Compute Prefix-Sum
    {
        PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"BC7 Prefix-Sum");
        m_GPUTimer->Start(commandList, static_cast<uint32_t>(UNSHUFFLE_PREFIX_SUM));
        
        commandList->SetPipelineState(m_prefixSumCSPipelineState.Get());

        commandList->SetComputeRootSignature(m_prefixSumCSRootSignature.Get());

        commandList->SetComputeRoot32BitConstant(0, histogramCount, 0);
        commandList->SetComputeRoot32BitConstant(0, m_offsetIntoHistogram, 1);

        commandList->SetComputeRootUnorderedAccessView(1, m_scratchBuffer->GetGPUVirtualAddress());

        commandList->Dispatch(1, BC7_MODES_COUNT, 1);

        m_GPUTimer->Stop(commandList, static_cast<uint32_t>(UNSHUFFLE_PREFIX_SUM));
        PIXEndEvent(commandList);
    }

    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_unshufflePassIndirectBuffer.Get(), 
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::UAV(m_scratchBuffer.Get());
    commandList->ResourceBarrier(2, barriers);

    // BC7 Mode Binning Pass
    {
        PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"BC7 ModeBinning");
        m_GPUTimer->Start(commandList, static_cast<uint32_t>(UNSHUFFLE_BINNING));
        
        commandList->SetPipelineState(m_modeBinningCSPipelineState.Get());

        commandList->SetComputeRootSignature(m_modeBinningCSRootSignature.Get());

        commandList->SetComputeRoot32BitConstant(0, histogramCount, 0);
        commandList->SetComputeRoot32BitConstant(0, numBlocks, 1);
        commandList->SetComputeRoot32BitConstant(0, static_cast<uint32_t>(m_bcTextureWidthInPixels), 2);
        commandList->SetComputeRoot32BitConstant(0, g_shuffleParams.useSpaceCurve, 3);
        commandList->SetComputeRoot32BitConstant(0, m_offsetIntoHistogram, 4);
        commandList->SetComputeRoot32BitConstant(0, m_offsetIntoBinning, 5);

        commandList->SetComputeRootShaderResourceView(1, m_shuffledBuffer->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(2, m_scratchBuffer->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(3, m_outBCnBuffer->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(4, m_unshufflePassIndirectBuffer->GetGPUVirtualAddress());

        commandList->Dispatch(histogramCount, 1, 1);

        m_GPUTimer->Stop(commandList, static_cast<uint32_t>(UNSHUFFLE_BINNING));
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
        m_GPUTimer->Start(commandList, static_cast<uint32_t>(UNSHUFFLE_FINAL));
        
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

        m_GPUTimer->Stop(commandList, static_cast<uint32_t>(UNSHUFFLE_FINAL));
        PIXEndEvent(commandList);
    }
}
#pragma endregion

#pragma region Message Handlers
// Occurs when the game is being suspended.
void Game::OnSuspending()
{
    m_deviceResources->Suspend();
}

// Occurs when the game is resuming.
void Game::OnResuming()
{
    m_deviceResources->Resume();
    m_timer.ResetElapsedTime();
}
#pragma endregion

#pragma region Direct3D Resources
void Game::CreateRenderPassResources() 
{
    auto device = m_deviceResources->GetD3DDevice();

    // Fullscreen pass PSO and Root Signature.
    {
        auto vertexShaderBlob = DX::ReadData(L"FullscreenQuadVS.cso");
        auto pixelShaderBlob = DX::ReadData(L"FullscreenQuadPS.cso");

        DX::ThrowIfFailed(device->CreateRootSignature(0, vertexShaderBlob.data(), vertexShaderBlob.size(),
            IID_PPV_ARGS(m_renderPassRootSignature.ReleaseAndGetAddressOf())));

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_renderPassRootSignature.Get();
        psoDesc.VS = { vertexShaderBlob.data(), vertexShaderBlob.size() };
        psoDesc.PS = { pixelShaderBlob.data(), pixelShaderBlob.size() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = m_deviceResources->GetBackBufferFormat();
        psoDesc.SampleDesc.Count = 1;
        DX::ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc,
            IID_PPV_ARGS(m_renderPassPipelineState.ReleaseAndGetAddressOf())));
    }

    // Create DescriptorHeap
    D3D12_DESCRIPTOR_HEAP_DESC dHeapDesc = {};
    dHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    dHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    dHeapDesc.NumDescriptors = 1;
    DX::ThrowIfFailed(device->CreateDescriptorHeap(&dHeapDesc, IID_PPV_ARGS(m_renderPassDescHeap.ReleaseAndGetAddressOf())));

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    // Texture with BC7 format to render this to the screen
    D3D12_RESOURCE_DESC bc7TexDesc = CD3DX12_RESOURCE_DESC::Tex2D(m_bcTextureFormat, m_bcTextureWidthInPixels,
        static_cast<uint32_t>(m_bcTextureHeightInPixels), 1u, static_cast<uint16_t>(m_bcTextureSubresourceCount));

    DX::ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bc7TexDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(m_bcnRenderResource.ReleaseAndGetAddressOf())));

    // The BC7 texture might have mips > 1, but we only render mip 0, so the view only exposes this mip level
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = m_bcTextureFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = static_cast<uint16_t>(1u);
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device->CreateShaderResourceView(m_bcnRenderResource.Get(), &srvDesc, m_renderPassDescHeap->GetCPUDescriptorHandleForHeapStart());

    // Text rendering setup
    m_graphicsMemory = std::make_unique<DirectX::GraphicsMemory>(device);
    m_resourceDescriptorHeap = std::make_unique<DirectX::DescriptorHeap>(device, ResourceDescriptors::Count);

    DirectX::ResourceUploadBatch resourceUpload(device);
    resourceUpload.Begin();

    const DirectX::RenderTargetState renderTargetState(
        m_deviceResources->GetBackBufferFormat(),
        m_deviceResources->GetDepthBufferFormat());
    DirectX::SpriteBatchPipelineStateDescription pipelineDesc(renderTargetState);
    m_spriteBatch = std::make_unique<DirectX::SpriteBatch>(device, resourceUpload, pipelineDesc);

    wchar_t strFilePath[MAX_PATH] = {};
    DX::FindMediaFile(strFilePath, MAX_PATH, L"SegoeUI_24.spritefont");
    m_font = std::make_unique<DirectX::SpriteFont>(
        device, resourceUpload,
        strFilePath,
        m_resourceDescriptorHeap->GetCpuHandle(ResourceDescriptors::Font),
        m_resourceDescriptorHeap->GetGpuHandle(ResourceDescriptors::Font));

    auto uploadDone = resourceUpload.End(m_deviceResources->GetCommandQueue());
    uploadDone.wait();
}

void Game::CreateBC7UnshufflePassD3D12Resources(std::vector<uint8_t>& shuffledBuffer)
{
    auto device = m_deviceResources->GetD3DDevice();

    // Create RootSignature and PSO
    {
        // PrePass
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

    // Shuffled buffer data. Since this is going to be accessed as a raw buffer which needs to be dword aligned, size is aligned to the next dword boundary
    uint32_t shuffledBufferSizeBytes = static_cast<uint32_t>(shuffledBuffer.size());
    uint32_t rem = shuffledBufferSizeBytes % 4;
    if (rem != 0) shuffledBufferSizeBytes += (4 - rem);

    uint32_t scratchBufferSizeBytes, indirectArgBufferSize, headerSizeBytes, histogramBufferSizeInBytes;
    GetInputAndIntermediateResourceSizes(m_bcnSizeInBytes, scratchBufferSizeBytes, indirectArgBufferSize, headerSizeBytes, histogramBufferSizeInBytes);

    // Since header, histogram and binning buffers were fused into one, we need offsets to
    // access the correct memory region in the shaders. Regions are dword aligned.
    m_offsetIntoHeader = 0;
    m_offsetIntoHistogram = headerSizeBytes;
    m_offsetIntoBinning = headerSizeBytes + histogramBufferSizeInBytes;

    // Resource Description struct for all intermediates
    D3D12_RESOURCE_DESC scratchBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(scratchBufferSizeBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    D3D12_RESOURCE_DESC indirectBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indirectArgBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    std::vector<D3D12_RESOURCE_ALLOCATION_INFO> allocInfoList;
    allocInfoList.push_back(device->GetResourceAllocationInfo(0, 1, &scratchBufferDesc));
    allocInfoList.push_back(device->GetResourceAllocationInfo(0, 1, &indirectBufferDesc));

    uint64_t intermediateResourcesOffsetsIntoHeap[2];
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
    
    // OUTPUT RESOURCE
    {
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC outBC7BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(m_bcnSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        
        DX::ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
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
        D3D12_RESOURCE_DESC readbackBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(m_bcnSizeInBytes);

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
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
            nullptr,
            IID_PPV_ARGS(m_unshufflePassIndirectBuffer.ReleaseAndGetAddressOf())));
    }

    // Execute Indirect command signature
    {
        D3D12_INDIRECT_ARGUMENT_DESC arguments[1] = {};
        arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    
        D3D12_COMMAND_SIGNATURE_DESC cmdSigDesc = {};
        cmdSigDesc.NumArgumentDescs = static_cast<uint32_t>(std::size(arguments));
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

void Game::CreateBC1345UnshufflePassD3D12Resources(std::vector<uint8_t>& shuffledBuffer, size_t sizeInBytes)
{
    auto device = m_deviceResources->GetD3DDevice();

    // Create the per-format root signatures and PSOs once. gShaderInfo is indexed by GBaseFormat.
    static bool oneTime = true;
    if (oneTime)
    {
        oneTime = false;

        for (size_t i = 0; i < _countof(gShaderInfo); i++)
        {
            auto csBlob = DX::ReadData(gShaderInfo[i].fileName.data());
            DX::ThrowIfFailed(device->CreateRootSignature(0, csBlob.data(), csBlob.size(),
                IID_PPV_ARGS(m_bcnCSRootSignature[i].ReleaseAndGetAddressOf())));
            m_bcnCSRootSignature[i]->SetName(gShaderInfo[i].signatureName.data());

            D3D12_COMPUTE_PIPELINE_STATE_DESC computePSODesc = {};
            computePSODesc.CS = { csBlob.data(), csBlob.size() };
            computePSODesc.pRootSignature = m_bcnCSRootSignature[i].Get();
            DX::ThrowIfFailed(device->CreateComputePipelineState(&computePSODesc,
                IID_PPV_ARGS(m_bcnCSPipelineState[i].ReleaseAndGetAddressOf())));
        }
    }

    // When the shaders bind the output buffer via a descriptor table (USE_ROOT_DESCRIPTORS not defined), we need a
    // single-entry shader-visible CBV_SRV_UAV heap for its UAV. With root descriptors the buffer is bound directly by
    // GPU virtual address, so no heap is required.
#ifndef USE_ROOT_DESCRIPTORS
    {
        D3D12_DESCRIPTOR_HEAP_DESC dHeapDesc = {};
        dHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        dHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        dHeapDesc.NumDescriptors = 1; // output buffer UAV only.
        DX::ThrowIfFailed(device->CreateDescriptorHeap(&dHeapDesc, IID_PPV_ARGS(m_uavSrvCbvHeap.ReleaseAndGetAddressOf())));
        m_DHIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
#endif

    uint32_t shuffledBufferSizeBytes = uint32_t(shuffledBuffer.size() + 3) & ~0b111ul; // round up to DWORD

    // OUTPUT RESOURCE (reuses the generic m_outBCnBuffer that the copy/readback path already references).
    {
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC outBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        DX::ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &outBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_outBCnBuffer.ReleaseAndGetAddressOf())));
        m_outBCnBuffer->SetName(L"OutBCnBuffer");
    }

    // INPUT + READBACK RESOURCES
    {
        D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        // Shuffled buffer related resources (input)
        D3D12_RESOURCE_DESC shuffledBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(shuffledBufferSizeBytes);

        DX::ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &shuffledBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(m_shuffledBufferUpload.ReleaseAndGetAddressOf())));
        m_shuffledBufferUpload->SetName(L"ShuffledBufferUpload");

        DX::ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &shuffledBufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(m_shuffledBuffer.ReleaseAndGetAddressOf())));
        m_shuffledBuffer->SetName(L"ShuffledBuffer");

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

    // Create the RAW UAV over the output buffer at descriptor index 0 (descriptor-table path only). With root
    // descriptors the shader binds the buffer directly by GPU virtual address, so no view is needed.
#ifndef USE_ROOT_DESCRIPTORS
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = static_cast<uint32_t>(sizeInBytes) / sizeof(uint32_t);
        uavDesc.Buffer.StructureByteStride = 0;

        auto cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_uavSrvCbvHeap->GetCPUDescriptorHandleForHeapStart(), 0, m_DHIncrementSize);
        device->CreateUnorderedAccessView(m_outBCnBuffer.Get(), nullptr, &uavDesc, cpuHandle);

        m_UAVOutputBufferGpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_uavSrvCbvHeap->GetGPUDescriptorHandleForHeapStart(), 0, m_DHIncrementSize);
    }
#endif

    // Upload shuffled data to GPU
    {
        m_deviceResources->ResetCommandList();
        auto commandList = m_deviceResources->GetCommandList();

        uint8_t* data = nullptr;
        DX::ThrowIfFailed(m_shuffledBufferUpload->Map(0, nullptr, reinterpret_cast<void**>(&data)));
        memcpy(data, shuffledBuffer.data(), shuffledBuffer.size());
        m_shuffledBufferUpload->Unmap(0, nullptr);

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
#pragma endregion

void Game::GetInputAndIntermediateResourceSizes(size_t const bc7SizeInBytes, uint32_t& scratchBufferSizeBytes, uint32_t& indirectArgBufferSize, uint32_t& headerSizeBytes, uint32_t& histogramBufferSizeInBytes)
{
    uint32_t blockCount = static_cast<uint32_t>(bc7SizeInBytes) / BC7_BYTES_PER_BLOCK;

    uint32_t modeBinningBufferSizeInBytes = blockCount * sizeof(uint32_t);

    uint32_t histogramCount = (blockCount + TG_THREAD_COUNT - 1) / TG_THREAD_COUNT;
    uint32_t histogramEntrySize = BC7_MODES_COUNT * sizeof(uint32_t);
    histogramBufferSizeInBytes = histogramCount * histogramEntrySize;

    headerSizeBytes = sizeof(CommonHeader);
    uint32_t rem = headerSizeBytes % 4;
    if (rem != 0) headerSizeBytes += (4 - rem);

    // This is the size of the buffer holding the intermediate resources (minus the indirect arguments)
    scratchBufferSizeBytes = histogramBufferSizeInBytes + modeBinningBufferSizeInBytes + headerSizeBytes;

    indirectArgBufferSize = BC7_MODES_COUNT * sizeof(UnshufflePassIndirectBuffer);
}

bool Game::CompareAgainstOriginalBCnData()
{
    bool passed = true;
    
	uint64_t* originalDataQWPtr = reinterpret_cast<uint64_t*>(m_BCnOriginalData.data());
	uint64_t* unshuffleDataQWPtr = reinterpret_cast<uint64_t*>(m_BCnUnshuffledData.data());

	uint64_t qwCount = (m_bcnSizeInBytes + sizeof(uint64_t) - 1) / sizeof(uint64_t);

    for (size_t i = 0; i < qwCount; ++i)
    {
        passed = passed && (originalDataQWPtr[i] == unshuffleDataQWPtr[i]);
    }

    return passed;
}

void Game::OnWindowSizeChanged(int width, int height)
{
    if (!m_deviceResources->WindowSizeChanged(width, height))
        return;
}

void Game::OnWindowMoved()
{
    auto const r = m_deviceResources->GetOutputSize();
    m_deviceResources->WindowSizeChanged(r.right, r.bottom);
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

    // Single-pass BC1\3\4\5 (and CurveOnly) unshuffle
    for (uint32_t i = 0; i < _countof(gShaderInfo); ++i)
    {
        m_bcnCSRootSignature[i].Reset();
        m_bcnCSPipelineState[i].Reset();
    }
    m_uavSrvCbvHeap.Reset();

    // Render pass
    m_bcnRenderResource.Reset();
    m_renderPassRootSignature.Reset();
    m_renderPassPipelineState.Reset();
    m_renderPassDescHeap.Reset();

    // Resources that need allocation
    m_outBCnBuffer.Reset();
    m_scratchBuffer.Reset();
    m_unshufflePassIndirectBuffer.Reset();

    // And their heaps
    m_intermediateHeap.Reset();

    // Inputs and Aux
    m_shuffledBuffer.Reset();
    m_shuffledBufferUpload.Reset();
    m_BCnReadback.Reset();
}

void Game::OnDeviceLost()
{
    ResetD3D12Resources();

    m_font.reset();
    m_spriteBatch.reset();
    m_resourceDescriptorHeap.reset();
    m_graphicsMemory.reset();
    m_GPUTimer.reset();
}

void Game::OnDeviceRestored()
{
    CreateRenderPassResources();
}

void Game::RenderUI()
{
    auto commandList = m_deviceResources->GetCommandList();
    auto viewport    = m_deviceResources->GetScreenViewport();

    ID3D12DescriptorHeap* heaps[] = { m_resourceDescriptorHeap->Heap() };

    m_spriteBatch->Begin(commandList);
    commandList->SetDescriptorHeaps(_countof(heaps), heaps);
    m_spriteBatch->SetViewport(viewport);

    float yText = 20.0f;
    float xText = static_cast<float>(m_deviceResources->GetOutputSize().right - 550ull);

    m_font->DrawString(m_spriteBatch.get(), L"BCn Unshuffle Sample", DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;

    m_font->DrawString(m_spriteBatch.get(), m_inputFileName.c_str(), DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;

    wchar_t info[256];
    swprintf_s(info, L"%zux%zu  (%zu subresource(s))", m_bcTextureWidthInPixels, m_bcTextureHeightInPixels, m_bcTextureSubresourceCount);
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;

    swprintf_s(info, L"Base format: %s", BaseFormatToString(m_baseFormat));
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;

    swprintf_s(info, L"Transform: %s (%u)", TransformIdToString(m_bcnTransformId), static_cast<uint32_t>(m_bcnTransformId));
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;

    if (m_forceWarp) 
    {
        swprintf_s(info, L"Using WARP");
        m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
        yText += 35.0f;
    }
    
    if (!g_shuffleParams.useSpaceCurve)
    {
        swprintf_s(info, L"Space Curve: disabled");
    }
    else
    {
        swprintf_s(info, L"Space Curve: allowed, %s", TransformIsCurved(m_bcnTransformId) ? L"true" : L"false");
    }
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;

    // Endpoint order strategy only applies to the BC7 mode-split transform; hide it for other formats.
    if (m_baseFormat == GBaseFormat::BC7)
    {
        swprintf_s(info, L"Enpoint strategy: %u", g_shuffleParams.endpointOrderStrategy);
        m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
        yText += 35.0f;
    }

    swprintf_s(info, L"BCn size:      %I64u bytes", m_bcnSizeInBytes);
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;

    swprintf_s(info, L"Shuffled size: %I64u bytes", m_shuffledBufferSizeBytes);
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 70.0f;

    double unshuffleTotalTime = m_passDurations[UNSHUFFLE_PREPASS] + m_passDurations[UNSHUFFLE_PREFIX_SUM] + m_passDurations[UNSHUFFLE_BINNING] + m_passDurations[UNSHUFFLE_FINAL];

    swprintf_s(info, L"GPU Unshuffle took: %.2f ms", unshuffleTotalTime);
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;

    swprintf_s(info, L"Copy to bcn texture took: %.2f ms", m_passDurations[COPY_TO_TEXTURE]);
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;
    
    swprintf_s(info, L"Copy to readback took: %.2f ms", m_passDurations[COPY_TO_READBACK]);
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;
    
    swprintf_s(info, L"Render took: %.2f ms", m_passDurations[RENDER]);
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 70.0f;

    double framerate = 1000.0 / m_frameDeltaTime;
    swprintf_s(info, L"Framerate: %.0f", framerate);
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;

    swprintf_s(info, L"Unshuffle ECL to fence took: %.2f ms", m_clSubmissionToFenceTimeMs);
    m_font->DrawString(m_spriteBatch.get(), info, DirectX::SimpleMath::Vector2(xText, yText));
    yText += 35.0f;

    const wchar_t* status = m_validationPassed ? L"Validation: PASS" : L"Validation: FAIL";
    XMVECTOR statusColor = m_validationPassed ? XMVectorSet(0.f, 1.f, 0.f, 1.f) : XMVectorSet(1.f, 0.f, 0.f, 1.f);
    m_font->DrawString(m_spriteBatch.get(), status, DirectX::SimpleMath::Vector2(xText, yText), statusColor);
    yText += 35.0f;

    m_spriteBatch->End();
}

// Properties
void Game::GetDefaultSize(int& width, int& height) const
{
    width = 1280;
    height = 720;
}