//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//

//
// Game.cpp
//

#include "pch.h"
#include "Game.h"

extern void ExitGame() noexcept;

using namespace DirectX;
using namespace DX;

using Microsoft::WRL::ComPtr;

Game::Game() noexcept(false)
    : m_threadFunc(nullptr)
    , m_displayMode(0)
{
    m_deviceResources = std::make_unique<DX::DeviceResources>();
    m_deviceResources->RegisterDeviceNotify(this);
}

Game::~Game()
{
    if (m_threadFunc && m_threadFunc->joinable())
    {
        m_threadFunc->join();
        delete m_threadFunc;
    }

    if (m_deviceResources)
    {
        m_deviceResources->WaitForGpu();
    }

    if (m_dsFile)
    {
        m_dsFile->Close();
    }
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(HWND window, int width, int height, Parameter parameter)
{
    m_parameter = parameter;

    m_deviceResources->SetWindow(window, width, height);

    m_deviceResources->CreateDeviceResources();
    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();

    InitDirectStorage();
}

#pragma region Frame Update
// Executes the basic game loop.
void Game::Tick()
{
    m_timer.Tick([&]()
        {
            Update(m_timer);
        });

    Render();
}

// Updates the world.
void Game::Update(DX::StepTimer const& timer)
{
    float elapsedTime = float(timer.GetElapsedSeconds());

    // TODO: Add your game logic here.
    (void)elapsedTime;

    static bool bFirst = true;
    if (bFirst)
    {
        bFirst = false;
        m_threadFunc = new std::thread(&Game::ThreadFunc, this);
    }
}
#pragma endregion

#pragma region Frame Render
// Draws the scene.
void Game::Render()
{
    // Don't try to render anything before the first Update.
    if (m_timer.GetFrameCount() == 0)
    {
        return;
    }

    // Prepare the command list to render a new frame.
    m_deviceResources->Prepare();
    Clear();

    auto commandList = m_deviceResources->GetCommandList();

    ID3D12DescriptorHeap* pHeaps[] = { m_resourceDescriptors->Heap() };
    commandList->SetDescriptorHeaps(_countof(pHeaps), pHeaps);

    m_spriteBatch->Begin(commandList);
    {
        const RECT outputSize = m_deviceResources->GetOutputSize();
        const RECT safeRect = SimpleMath::Viewport::ComputeTitleSafeArea(outputSize.right - outputSize.left, outputSize.bottom - outputSize.top);

        if (m_displayMode == 0) // display stats
        {
            for (uint32_t i = 0; i < m_maxOnMemoryTextures; i++)
            {
                auto& texInfo = m_textureInfo[i];
                if (texInfo.bUsing.load(std::memory_order_acquire))
                {
                    m_spriteBatch->Draw(
                        m_resourceDescriptors->GetGpuHandle(Descriptors::Count + i + 1),
                        GetTextureSize(texInfo.texture.Get()),
                        XMFLOAT2(i * 200.0f, i * 200.0f + 160.0f));
                }
            }

            for (uint32_t i = 0; i < m_maxOnMemoryTextures; i++)
            {
                auto& texInfo = m_textureInfo[i];
                if (texInfo.bUsing.load(std::memory_order_acquire))
                {
                    wchar_t buffer[1024];
                    XMFLOAT2 pos(float(safeRect.left), float(safeRect.top));
                    pos.x = i * 200.0f;
                    pos.y = i * 200.0f + 160.0f;
                    swprintf_s(
                        buffer, 1024,
                        L"[%s] loaded in %.2f ms (%.2f GiB/s)\n"
                        L"  Compressed: %.2f MiB / Uncompressed: %.2f MiB (%.2f%%)",
                        texInfo.metadata->name,
                        texInfo.durationMS,
                        texInfo.metadata->loadSize / _1GiB / (texInfo.durationMS / 1000.0f),
                        texInfo.metadata->loadSize / _1MiB,
                        texInfo.metadata->uncompressedSize / _1MiB,
                        100.0f * float(texInfo.metadata->loadSize) / float(texInfo.metadata->uncompressedSize));
                    m_font->DrawString(m_spriteBatch.get(), buffer, pos, Colors::White);
                }
            }
        }
        else // display single texture
        {
            uint32_t textureIndex = m_displayMode - 1;
            auto& texInfo = m_textureInfo[textureIndex];
            if (texInfo.bUsing.load(std::memory_order_acquire))
            {
                DirectX::XMUINT2 textureSize = GetTextureSize(texInfo.texture.Get());

                textureSize.x = std::min(textureSize.x, 1024u);
                textureSize.y = std::min(textureSize.y, 1024u);

                m_spriteBatch->Draw(
                    m_resourceDescriptors->GetGpuHandle(Descriptors::Count + textureIndex + 1),
                    textureSize,
                    XMFLOAT2(1920 / 2.0f - textureSize.x / 2.0f, 1080 / 2.0f - textureSize.y / 2.0f));

                wchar_t buffer[1024];
                XMFLOAT2 pos(128.0f, 160.0f);
                swprintf_s(
                    buffer, 1024,
                    L"[%s] loaded in %.2f ms (%.2f GiB/s)\n"
                    L"  Compressed: %.2f MiB / Uncompressed: %.2f MiB (%.2f%%)\n"
                    L"  Width: %u, Height: %u Format: %s",
                    texInfo.metadata->name,
                    texInfo.durationMS,
                    texInfo.metadata->loadSize / _1GiB / (texInfo.durationMS / 1000.0f),
                    texInfo.metadata->loadSize / _1MiB,
                    texInfo.metadata->uncompressedSize / _1MiB,
                    100.0f * float(texInfo.metadata->loadSize) / float(texInfo.metadata->uncompressedSize),
                    texInfo.metadata->width,
                    texInfo.metadata->height,
                    DXGIFormatToString(texInfo.metadata->format));
                m_font->DrawString(m_spriteBatch.get(), buffer, pos, Colors::White);
            }
        }

        XMFLOAT2 pos(float(safeRect.left), float(safeRect.top));
        m_font->DrawString(m_spriteBatch.get(), L"GameAssetConditioningDemo : Runtime", pos, Colors::White);
        pos.x += 1100.0f;
        pos.y += 32.0f;
        m_font->DrawString(m_spriteBatch.get(), m_parameter.bUseCPU ? L"CPU decompression" : m_gpuName.c_str(), pos, Colors::White);
        pos.y += 32.0f;
        m_font->DrawString(m_spriteBatch.get(), L"(Press <- -> keys to change showing texture.)", pos, Colors::White);
    }
    m_spriteBatch->End();

    // Show the new frame.
    m_deviceResources->Present();
    m_graphicsMemory->Commit(m_deviceResources->GetCommandQueue());
}

// Helper method to clear the back buffers.
void Game::Clear()
{
    auto commandList = m_deviceResources->GetCommandList();

    // Clear the views.
    auto const rtvDescriptor = m_deviceResources->GetRenderTargetView();
    auto const dsvDescriptor = m_deviceResources->GetDepthStencilView();

    commandList->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor);
    commandList->ClearRenderTargetView(rtvDescriptor, Colors::Black, 0, nullptr);
    commandList->ClearDepthStencilView(dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Set the viewport and scissor rect.
    auto const viewport = m_deviceResources->GetScreenViewport();
    auto const scissorRect = m_deviceResources->GetScissorRect();
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);
}
#pragma endregion

#pragma region Message Handlers
// Message handlers
void Game::OnActivated()
{
    // TODO: Game is becoming active window.
}

void Game::OnDeactivated()
{
    // TODO: Game is becoming background window.
}

void Game::OnSuspending()
{
    // TODO: Game is being power-suspended (or minimized).
}

void Game::OnResuming()
{
    m_timer.ResetElapsedTime();

    // TODO: Game is being power-resumed (or returning from minimize).
}

void Game::OnWindowMoved()
{
    auto const r = m_deviceResources->GetOutputSize();
    m_deviceResources->WindowSizeChanged(r.right, r.bottom);
}

void Game::OnDisplayChange()
{
    m_deviceResources->UpdateColorSpace();
}

void Game::OnWindowSizeChanged(int width, int height)
{
    if (!m_deviceResources->WindowSizeChanged(width, height))
        return;

    CreateWindowSizeDependentResources();

    // TODO: Game window is being resized.
}

void Game::OnKeyDown(UINT8 key)
{
    if (key == VK_LEFT)
    {
        if (m_displayMode > 0)
        {
            m_displayMode--;
        }
    }
    else if (key == VK_RIGHT)
    {
        if (m_displayMode < m_maxOnMemoryTextures)
        {
            m_displayMode++;
        }
    }
}

// Properties
void Game::GetDefaultSize(int& width, int& height) const noexcept
{
    width = 1920;
    height = 1080;
}
#pragma endregion

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Game::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();

    // Check Shader Model 6 support
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_0 };
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))
        || (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_0))
    {
#ifdef _DEBUG
        OutputDebugStringA("ERROR: Shader Model 6.0 is not supported!\n");
#endif
        throw std::runtime_error("Shader Model 6.0 is not supported!");
    }

    m_gpuName = GetGpuNameFromDevice(device);

    if (m_parameter.bUseCPU)
    {
        debugPrint(L"Using CPU Decompression\n");
    }
    else
    {
        debugPrint(L"Using GPU: %s\n", m_gpuName.c_str());
    }

    m_graphicsMemory = std::make_unique<GraphicsMemory>(device);
    m_resourceDescriptors = std::make_unique<DirectX::DescriptorPile>(
        device,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
        128,
        Descriptors::Count);

    // Upload resources to video memory
    ResourceUploadBatch resourceUpload(device);
    resourceUpload.Begin();

    // Font rendering
    {
        RenderTargetState rtState(m_deviceResources->GetBackBufferFormat(), m_deviceResources->GetDepthBufferFormat());
        SpriteBatchPipelineStateDescription pd(rtState);
        m_spriteBatch = std::make_unique<SpriteBatch>(device, resourceUpload, pd);

        m_font = std::make_unique<SpriteFont>(
            device, resourceUpload,
            L"Assets\\SegoeUI_24.spritefont",
            m_resourceDescriptors->GetCpuHandle(Descriptors::FontSRV),
            m_resourceDescriptors->GetGpuHandle(Descriptors::FontSRV));
    }

    auto uploadResourcesFinished = resourceUpload.End(m_deviceResources->GetCommandQueue());
    uploadResourcesFinished.wait();
}

// Allocate all memory resources that change on a window SizeChanged event.
void Game::CreateWindowSizeDependentResources()
{
    auto viewport = m_deviceResources->GetScreenViewport();
    m_spriteBatch->SetViewport(viewport);
}

void Game::OnDeviceLost()
{
    OutputDebugStringA("Device Lost\n");

    m_dsFile.Reset();
    m_dsQueue.Reset();
    m_dsFactory.Reset();

    m_font.reset();
    m_spriteBatch.reset();
    m_resourceDescriptors.reset();

    m_graphicsMemory.reset();
}

void Game::OnDeviceRestored()
{
    OutputDebugStringA("Device Restored\n");

    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();

    InitDirectStorage();

    if (m_threadFunc && m_threadFunc->joinable())
    {
        m_threadFunc->join();
    }
    delete m_threadFunc;

    m_threadFunc = new std::thread(&Game::ThreadFunc, this);
}
#pragma endregion

std::wstring Game::GetGpuNameFromDevice(ID3D12Device* device)
{
    const LUID luid = device->GetAdapterLuid();

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    {
        return L"";
    }

    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))))
    {
        return L"";
    }

    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)))
    {
        return L"";
    }

    return desc.Description;
}

void Game::InitDirectStorage()
{
    if (m_parameter.bUseCPU)
    {
        DSTORAGE_CONFIGURATION1 config{};
        config.DisableGpuDecompression = true;
        ThrowIfFailed(DStorageSetConfiguration1(&config));
    }

    ThrowIfFailed(DStorageGetFactory(IID_PPV_ARGS(m_dsFactory.ReleaseAndGetAddressOf())));
    m_dsFactory->SetDebugFlags(DSTORAGE_DEBUG_BREAK_ON_ERROR | DSTORAGE_DEBUG_SHOW_ERRORS);
    ThrowIfFailed(m_dsFactory->SetStagingBufferSize(256 * 1024 * 1024));

    // Create a DirectStorage queue which will be used to load data into a buffer on the GPU
    DSTORAGE_QUEUE_DESC dsQueueDesc{};
    dsQueueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    dsQueueDesc.Priority = DSTORAGE_PRIORITY_REALTIME;
    dsQueueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    dsQueueDesc.Device = m_deviceResources->GetD3DDevice();
    dsQueueDesc.Name = "m_dsQueue";
    ThrowIfFailed(m_dsFactory->CreateQueue(&dsQueueDesc, IID_PPV_ARGS(m_dsQueue.ReleaseAndGetAddressOf())));

    DSTORAGE_COMPRESSION_SUPPORT compressionSupport = m_dsQueue->GetCompressionSupport(DSTORAGE_COMPRESSION_FORMAT_ZSTD);
    if (compressionSupport & DSTORAGE_COMPRESSION_SUPPORT_GPU_OPTIMIZED) debugPrint(L"GDSTORAGE_COMPRESSION_SUPPORT_GPU_OPTIMIZED\n");
    if (compressionSupport & DSTORAGE_COMPRESSION_SUPPORT_GPU_FALLBACK) debugPrint(L"DSTORAGE_COMPRESSION_SUPPORT_GPU_FALLBACK\n");

    ThrowIfFailed(m_dsFactory->OpenFile(
        m_parameter.sourceFile,
        IID_PPV_ARGS(m_dsFile.ReleaseAndGetAddressOf())));

    // Read header
    ArchiveFileHeader header = {};

    DSTORAGE_REQUEST request = {};
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
    request.Source.File.Source = m_dsFile.Get();
    request.Source.File.Offset = 0;
    request.Source.File.Size = sizeof(ArchiveFileHeader);
    request.UncompressedSize = sizeof(ArchiveFileHeader);
    request.Destination.Memory.Buffer = &header;
    request.Destination.Memory.Size = sizeof(ArchiveFileHeader);

    m_dsQueue->EnqueueRequest(&request);

    ScopedHandle headerLoadedEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    ThrowIfFalse(headerLoadedEvent.get());
    m_dsQueue->EnqueueSetEvent(headerLoadedEvent.get());
    m_dsQueue->Submit();

    // Wait for DS work to complete
    WaitForSingleObject(headerLoadedEvent.get(), INFINITE);

    m_textureMetadatas.resize(header.fileCount);

    for (uint32_t i = 0; i < m_maxOnMemoryTextures; i++)
    {
        m_textureInfo[i].bUsing.store(false, std::memory_order_release);
    }
}

void Game::ThreadFunc()
{
    SetThreadDescription(GetCurrentThread(), L"Sample worker Thread");

    uint32_t numTexture = (uint32_t)m_textureMetadatas.size();

    // Read metadata

    for (uint32_t i = 0; i < numTexture; i++)
    {
        DSTORAGE_REQUEST request = {};
        request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
        request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
        request.Source.File.Source = m_dsFile.Get();
        request.Source.File.Offset = sizeof(ArchiveFileHeader) + i * sizeof(ShuffledTextureMetadata);
        request.Source.File.Size = sizeof(ShuffledTextureMetadata);
        request.UncompressedSize = sizeof(ShuffledTextureMetadata);
        request.Destination.Memory.Buffer = &m_textureMetadatas[i];
        request.Destination.Memory.Size = sizeof(ShuffledTextureMetadata);

        m_dsQueue->EnqueueRequest(&request);
    }

    ScopedHandle textureMetadataLoadedEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    ThrowIfFalse(textureMetadataLoadedEvent.get());
    m_dsQueue->EnqueueSetEvent(textureMetadataLoadedEvent.get());
    m_dsQueue->Submit();

    // Wait for DS work to complete
    WaitForSingleObject(textureMetadataLoadedEvent.get(), INFINITE);

    for (uint32_t i = 0; i < numTexture; i++)
    {
        debugPrint(
            L"File: %s, width: %d, height: %d, format: %s, compressedSize: %d, uncompressedSize: %d\n",
            m_textureMetadatas[i].name,
            m_textureMetadatas[i].width,
            m_textureMetadatas[i].height,
            DXGIFormatToString(m_textureMetadatas[i].format),
            m_textureMetadatas[i].loadSize,
            m_textureMetadatas[i].uncompressedSize);

        // Read chunk metadata
        TextureInfo& texInfo = m_textureInfo[i];
        texInfo.chunkMetadatas.resize(m_textureMetadatas[i].chunkCount);

        for (uint32_t j = 0; j < m_textureMetadatas[i].chunkCount; j++)
        {
            DSTORAGE_REQUEST request = {};
            request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
            request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
            request.Source.File.Source = m_dsFile.Get();
            request.Source.File.Offset = m_textureMetadatas[i].offset + sizeof(ChunkMetadata) * j;
            request.Source.File.Size = sizeof(ChunkMetadata);
            request.UncompressedSize = sizeof(ChunkMetadata);
            request.Destination.Memory.Buffer = &texInfo.chunkMetadatas[j];
            request.Destination.Memory.Size = sizeof(ChunkMetadata);
            m_dsQueue->EnqueueRequest(&request);
        }

        ScopedHandle chunkMetadataLoadedEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr));
        ThrowIfFalse(chunkMetadataLoadedEvent.get());
        m_dsQueue->EnqueueSetEvent(chunkMetadataLoadedEvent.get());
        m_dsQueue->Submit();

        // Wait for DS work to complete
        WaitForSingleObject(chunkMetadataLoadedEvent.get(), INFINITE);

        // Validate chunk metadata
        debugPrint(L"Texture %d chunk metadata:\n", i);
        for (uint32_t j = 0; j < m_textureMetadatas[i].chunkCount; j++)
        {
            debugPrint(L"  Chunk %d: offset=%llu, compressed=%llu, uncompressed=%llu, transform=%d\n",
                j,
                texInfo.chunkMetadatas[j].offset,
                texInfo.chunkMetadatas[j].compressedSize,
                texInfo.chunkMetadatas[j].uncompressedSize,
                texInfo.chunkMetadatas[j].transformId);
        }
    }

    auto device = m_deviceResources->GetD3DDevice();

    // To measure each loading time, this sample blocks the thread for each texture. 
    // Not recommended for real-world usage.

    for (uint32_t slot = 0; slot < m_maxOnMemoryTextures; slot++)
    {
        TextureInfo& texInfo = m_textureInfo[slot];
        texInfo.metadata = &m_textureMetadatas[slot];

        // Create the ID3D12Resource buffer which will be populated with the file's contents.
        D3D12_HEAP_PROPERTIES bufferHeapProps = {};
        bufferHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            texInfo.metadata->format,
            texInfo.metadata->width,
            texInfo.metadata->height,
            1, 1, 1, 0); // array, mip, sample, flag

        ThrowIfFailed(device->CreateCommittedResource(
            &bufferHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(texInfo.texture.ReleaseAndGetAddressOf())));

        texInfo.texture->SetName(texInfo.metadata->name);

        // Create the SRV for the texture
        CreateShaderResourceView(
            device,
            texInfo.texture.Get(),
            m_resourceDescriptors->GetCpuHandle(Descriptors::Count + 1 + slot));

        const uint32_t chunkNum = texInfo.metadata->chunkCount;
        const uint32_t blockSize = 4;   // BCn formats use 4x4 pixel blocks
        const size_t blockSizeInBytes = DirectX::BytesPerBlock(texInfo.metadata->format);
        const uint32_t widthInBlocks = (texInfo.metadata->width + blockSize - 1) / blockSize;

        for (uint32_t i = 0; i < chunkNum; i++)
        {
            // Calculate uncompressed offset in bytes
            uint64_t uncompressedByteOffset = 0;
            for (uint32_t j = 0; j < i; j++)
            {
                uncompressedByteOffset += texInfo.chunkMetadatas[j].uncompressedSize;
            }

            // Calculate block coordinates
            const uint64_t blocksOffset = uncompressedByteOffset / blockSizeInBytes;
            const uint64_t blocksInChunk = texInfo.chunkMetadatas[i].uncompressedSize / blockSizeInBytes;

            const uint32_t startBlockY = static_cast<uint32_t>(blocksOffset / widthInBlocks);
            const uint32_t endBlockY = static_cast<uint32_t>((blocksOffset + blocksInChunk + widthInBlocks - 1) / widthInBlocks);

            // Convert block coordinates to pixel coordinates
            const uint32_t startRow = startBlockY * blockSize;
            const uint32_t endRow = std::min(endBlockY * blockSize, texInfo.metadata->height);

            // Calculate file offset for chunk data
            const uint64_t chunkDataBaseOffset = texInfo.metadata->offset + (sizeof(ChunkMetadata) * chunkNum);
            const uint64_t chunkDataOffset = chunkDataBaseOffset + texInfo.chunkMetadatas[i].offset;

            // Enqueue the request to load the texture data
            DSTORAGE_REQUEST request = {};
            request.Options.CompressionFormat = (texInfo.metadata->uncompressedSize == texInfo.metadata->loadSize) ?
                DSTORAGE_COMPRESSION_FORMAT_NONE : DSTORAGE_COMPRESSION_FORMAT_ZSTD;
            request.Options.GaclTransformType = static_cast<DSTORAGE_GACL_SHUFFLE_TRANSFORM_TYPE>(texInfo.chunkMetadatas[i].transformId);
            request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
            request.Source.File.Source = m_dsFile.Get();
            request.Source.File.Offset = chunkDataOffset;
            request.Source.File.Size = static_cast<uint32_t>(texInfo.chunkMetadatas[i].compressedSize);
            request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_TEXTURE_REGION;
            request.Destination.Texture.Resource = texInfo.texture.Get();
            request.Destination.Texture.SubresourceIndex = 0;
            request.Destination.Texture.Region = { 0, startRow, 0, texInfo.metadata->width, endRow, 1 };
            request.UncompressedSize = (request.Options.CompressionFormat == DSTORAGE_COMPRESSION_FORMAT_NONE) ?
                0 : static_cast<uint32_t>(texInfo.chunkMetadatas[i].uncompressedSize);

            debugPrint(L"  Chunk %d: FileOffset=%llu, CompSize=%llu, UncompSize=%llu\n",
                i, chunkDataOffset, texInfo.chunkMetadatas[i].compressedSize, texInfo.chunkMetadatas[i].uncompressedSize);
            debugPrint(L"    Region=(%u,%u)-(%u,%u), BlocksY: %u-%u, WidthInBlocks=%u\n",
                0, startRow, texInfo.metadata->width, endRow,
                startBlockY, endBlockY, widthInBlocks);

            m_dsQueue->EnqueueRequest(&request);
        }

        // Configure a event to be signaled when the request is completed
        ScopedHandle textureLoadedEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr));
        ThrowIfFalse(textureLoadedEvent.get());
        m_dsQueue->EnqueueSetEvent(textureLoadedEvent.get());

        auto startTime = std::chrono::high_resolution_clock::now();

        // Tell DirectStorage to start executing all queued items.
        m_dsQueue->Submit();

        // Wait for the submitted work to complete
        WaitForSingleObject(textureLoadedEvent.get(), INFINITE);
        auto endTime = std::chrono::high_resolution_clock::now();

        // Check the status array for errors. If an error was detected the first failure record can be retrieved to get
        // more details.
        DSTORAGE_ERROR_RECORD errorRecord{};
        m_dsQueue->RetrieveErrorRecord(&errorRecord);
        if (FAILED(errorRecord.FirstFailure.HResult))
        {
            debugPrint(
                L"The DirectStorage request failed! HRESULT=0x%08X FailureCount=%d\n",
                errorRecord.FirstFailure.HResult,
                errorRecord.FailureCount);
        }

        texInfo.durationMS = std::chrono::duration<float, std::milli>(endTime - startTime).count();
        debugPrint(L"DirectStorage request completed %s in %.2f ms\n", texInfo.metadata->name, texInfo.durationMS);

        texInfo.bUsing.store(true, std::memory_order_release);
    }
}

