//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//

//
// Game.h
//

#pragma once

#include "DeviceResources.h"
#include "StepTimer.h"
#include "..\\Common\\ArchiveFile.h"

using Microsoft::WRL::ComPtr;

struct Parameter
{
    wchar_t sourceFile[512];
    bool bUseCPU;

    Parameter()
        : bUseCPU(false)
    {
        sourceFile[0] = L'\0';
    }
};

struct TextureInfo
{
    ComPtr<ID3D12Resource>     texture = nullptr;
    ShuffledTextureMetadata*   metadata = nullptr;
    std::vector<ChunkMetadata> chunkMetadatas;
    std::atomic<bool>          bUsing = false;
    float                      durationMS = 0.0f;
};

struct handle_closer { void operator()(HANDLE h) { if (h) CloseHandle(h); } };
typedef std::unique_ptr<void, handle_closer> ScopedHandle;
inline HANDLE safe_handle(HANDLE h) { return (h == INVALID_HANDLE_VALUE) ? 0 : h; }

constexpr auto _1MiB = (1024.0f * 1024.0f);
constexpr auto _1GiB = (1024.0f * 1024.0f * 1024.0f);

// A basic game implementation that creates a D3D12 device and
// provides a game loop.
class Game final : public DX::IDeviceNotify
{
    static std::wstring to_lowercase(const std::wstring& input)
    {
        std::wstring result = input;
        std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::tolower(c)); });
        return result;
    }

public:

    Game() noexcept(false);
    ~Game();

    Game(Game&&) = delete;
    Game& operator= (Game&&) = delete;

    Game(Game const&) = delete;
    Game& operator= (Game const&) = delete;

    // Initialization and management
    void Initialize(HWND window, int width, int height, Parameter parameter);

    // Basic game loop
    void Tick();

    // IDeviceNotify
    void OnDeviceLost() override;
    void OnDeviceRestored() override;

    // Messages
    void OnActivated();
    void OnDeactivated();
    void OnSuspending();
    void OnResuming();
    void OnWindowMoved();
    void OnDisplayChange();
    void OnWindowSizeChanged(int width, int height);
    void OnKeyDown(UINT8 key);

    // Properties
    void GetDefaultSize(int& width, int& height) const noexcept;

private:

    void Update(DX::StepTimer const& timer);
    void Render();

    void Clear();

    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();

    std::wstring GetGpuNameFromDevice(ID3D12Device* device);
    void InitDirectStorage();
    void ThreadFunc();

    // Device resources.
    std::unique_ptr<DX::DeviceResources>     m_deviceResources;

    // Rendering loop timer.
    DX::StepTimer                            m_timer;

    std::unique_ptr<DirectX::GraphicsMemory> m_graphicsMemory;
    std::unique_ptr<DirectX::DescriptorPile> m_resourceDescriptors;

    // Font rendering.
    std::unique_ptr<DirectX::SpriteFont>     m_font;
    std::unique_ptr<DirectX::SpriteBatch>    m_spriteBatch;

    // DirectStorage objects
    ComPtr<IDStorageFactory>                 m_dsFactory;
    ComPtr<IDStorageQueue2>                  m_dsQueue;
    ComPtr<IDStorageFile>                    m_dsFile;

    Parameter                                m_parameter;
    std::thread*                             m_threadFunc;

    static const uint32_t m_maxOnMemoryTextures = 4;

    TextureInfo                              m_textureInfo[m_maxOnMemoryTextures];
    std::vector<ShuffledTextureMetadata>     m_textureMetadatas;

    uint32_t                                 m_displayMode;
    std::wstring                             m_gpuName;

    enum Descriptors
    {
        FontSRV,
        Count
    };

    const wchar_t* DXGIFormatToString(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_BC1_TYPELESS: return L"BC1_TYPELESS";
        case DXGI_FORMAT_BC1_UNORM: return L"BC1_UNORM";
        case DXGI_FORMAT_BC1_UNORM_SRGB: return L"BC1_UNORM_SRGB";
        case DXGI_FORMAT_BC2_TYPELESS: return L"BC2_TYPELESS";
        case DXGI_FORMAT_BC2_UNORM: return L"BC2_UNORM";
        case DXGI_FORMAT_BC2_UNORM_SRGB: return L"BC2_UNORM_SRGB";
        case DXGI_FORMAT_BC3_TYPELESS: return L"BC3_TYPELESS";
        case DXGI_FORMAT_BC3_UNORM: return L"BC3_UNORM";
        case DXGI_FORMAT_BC3_UNORM_SRGB: return L"BC3_UNORM_SRGB";
        case DXGI_FORMAT_BC4_TYPELESS: return L"BC4_TYPELESS";
        case DXGI_FORMAT_BC4_UNORM: return L"BC4_UNORM";
        case DXGI_FORMAT_BC4_SNORM: return L"BC4_SNORM";
        case DXGI_FORMAT_BC5_TYPELESS: return L"BC5_TYPELESS";
        case DXGI_FORMAT_BC5_UNORM: return L"BC5_UNORM";
        case DXGI_FORMAT_BC5_SNORM: return L"BC5_SNORM";
        case DXGI_FORMAT_BC6H_TYPELESS: return L"BC6H_TYPELESS";
        case DXGI_FORMAT_BC6H_UF16: return L"BC6H_UF16";
        case DXGI_FORMAT_BC6H_SF16: return L"BC6H_SF16";
        case DXGI_FORMAT_BC7_TYPELESS: return L"BC7_TYPELESS";
        case DXGI_FORMAT_BC7_UNORM: return L"BC7_UNORM";
        case DXGI_FORMAT_BC7_UNORM_SRGB: return L"BC7_UNORM_SRGB";
        default: return L"UNSUPPORTED_FORMAT";
        }
    }
};
