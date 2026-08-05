//--------------------------------------------------------------------------------------
// DeviceResources.cpp
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "DeviceResources.h"

using namespace DirectX;
using namespace DX;

#ifdef __clang__
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

#pragma warning(disable : 4061)

using Microsoft::WRL::ComPtr;

// Constructor for DeviceResources.
DeviceResources::DeviceResources() noexcept(false) :
        m_fenceValue(0),
        m_readbackFenceValue(0),
        m_d3dFeatureLevel(D3D_FEATURE_LEVEL_12_0)
{
}

// Destructor for DeviceResources.
DeviceResources::~DeviceResources()
{
    // Ensure that the GPU is no longer referencing resources that are about to be destroyed.
    WaitForGpu();
}

// Configures the Direct3D device, and stores handles to it and the device context.
void DeviceResources::CreateDeviceResources(bool forceWarp)
{
    DWORD dxgiFactoryFlags = 0;

    #if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()))))
        {
            debugController->EnableDebugLayer();
        }
        else
        {
            OutputDebugStringA("WARNING: Direct3D Debug Device is not available\n");
        }

        ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.GetAddressOf()))))
        {
            dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

            dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
            dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);

            DXGI_INFO_QUEUE_MESSAGE_ID hide[] =
            {
                80 /* IDXGISwapChain::GetContainingOutput: The swapchain's adapter does not control the output on which the swapchain's window resides. */,
            };
            DXGI_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = static_cast<UINT>(std::size(hide));
            filter.DenyList.pIDList = hide;
            dxgiInfoQueue->AddStorageFilterEntries(DXGI_DEBUG_DXGI, &filter);
        }
    }
    #endif

    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf())));

    ComPtr<IDXGIAdapter1> adapter;
    GetAdapter(adapter.GetAddressOf(), forceWarp);

    // Create the DX12 API device object.
    DX::ThrowIfFailed(D3D12CreateDevice(
        adapter.Get(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(m_d3dDevice.ReleaseAndGetAddressOf())
    ));

    m_d3dDevice->SetName(L"D3dDevice");

#ifndef NDEBUG
    // Configure debug device (if active).
    ComPtr<ID3D12InfoQueue> d3dInfoQueue;
    if (SUCCEEDED(m_d3dDevice.As(&d3dInfoQueue)))
    {
#ifdef _DEBUG
        d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
#endif

        D3D12_MESSAGE_ID hide[] =
        {
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
            // Workarounds for debug layer issues on hybrid-graphics systems
            D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_WRONGSWAPCHAINBUFFERREFERENCE,
            D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
        };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumIDs = static_cast<UINT>(std::size(hide));
        filter.DenyList.pIDList = hide;
        d3dInfoQueue->AddStorageFilterEntries(&filter);
    }
#endif

    // Determine maximum supported feature level for this device
    static const D3D_FEATURE_LEVEL s_featureLevels[] =
    {
#if defined(NTDDI_WIN10_FE) && (NTDDI_VERSION >= NTDDI_WIN10_FE)
        D3D_FEATURE_LEVEL_12_2,
#endif
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D12_FEATURE_DATA_FEATURE_LEVELS featLevels =
    {
        static_cast<UINT>(std::size(s_featureLevels)), s_featureLevels, D3D_FEATURE_LEVEL_11_0
    };

    HRESULT hr = m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featLevels, sizeof(featLevels));
    if (SUCCEEDED(hr))
    {
        m_d3dFeatureLevel = featLevels.MaxSupportedFeatureLevel;
    }
    else
    {
        m_d3dFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    }

    // Create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_commandQueue.ReleaseAndGetAddressOf())));

    m_commandQueue->SetName(L"DeviceResources");

    ThrowIfFailed(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_commandAllocator.ReleaseAndGetAddressOf())));
    m_commandAllocator->SetName(L"DeviceResources");

    ThrowIfFailed(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_readbackCommandAllocator.ReleaseAndGetAddressOf())));
    
    // Create a command list for recording graphics commands.
    ThrowIfFailed(m_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf())));
    ThrowIfFailed(m_commandList->Close());

    m_commandList->SetName(L"DeviceResources");

    // Create a fence for tracking GPU execution progress.
    ThrowIfFailed(m_d3dDevice->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf())));
    m_fenceValue++;

    m_fence->SetName(L"DeviceResources");

    m_fenceEvent.Attach(CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE));
    if (!m_fenceEvent.IsValid())
    {
        throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()), "CreateEventEx");
    }

    // Create a fence for synchronizing GPU to CPU writes.
    ThrowIfFailed(m_d3dDevice->CreateFence(m_readbackFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_readbackFence.ReleaseAndGetAddressOf())));
    m_readbackFenceValue++;

    m_readbackFence->SetName(L"ReadbackFence");

    m_readbackFenceEvent.Attach(CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE));
    if (!m_readbackFenceEvent.IsValid())
    {
        throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()), "CreateEventEx");
    }
}

// Prepare the command list and render target for rendering.
void DeviceResources::ResetCommandList()
{
    // Reset command list and allocator.
    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));
}

void DeviceResources::ResetCommandListToReadbackAllocator()
{
    ThrowIfFailed(m_readbackCommandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_readbackCommandAllocator.Get(), nullptr));
}

// Wait for pending GPU work to complete.
void DeviceResources::WaitForGpu() noexcept
{
    if (m_commandQueue && m_fence && m_fenceEvent.IsValid())
    {
        // Schedule a Signal command in the GPU queue.
        const UINT64 fenceValue = m_fenceValue;
        if (SUCCEEDED(m_commandQueue->Signal(m_fence.Get(), fenceValue)))
        {
            // Wait until the Signal has been processed.
            if (SUCCEEDED(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent.Get())))
            {
                std::ignore = WaitForSingleObjectEx(m_fenceEvent.Get(), INFINITE, FALSE);

                // Increment the fence value for the current frame.
                m_fenceValue++;
            }
        }
    }
}

// This method acquires the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, try WARP. Otherwise throw an exception.
void DeviceResources::GetAdapter(IDXGIAdapter1** ppAdapter, bool forceWarp)
{
    *ppAdapter = nullptr;
    ComPtr<IDXGIAdapter1> adapter;

    if (forceWarp)
    {
        if (FAILED(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()))))
        {
            throw std::runtime_error("WARP12 not available. Enable the 'Graphics Tools' optional feature");
        }
    }
    else 
    {
        for (UINT adapterIndex = 0;
            SUCCEEDED(m_dxgiFactory->EnumAdapterByGpuPreference(
                adapterIndex,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())));
            adapterIndex++)
        {
            DXGI_ADAPTER_DESC1 desc;
            ThrowIfFailed(adapter->GetDesc1(&desc));

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                continue;
            }

            // Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
            {
                #ifdef _DEBUG
                wchar_t buff[256] = {};
                swprintf_s(buff, L"Direct3D Adapter (%u): VID:%04X, PID:%04X - %ls\n", adapterIndex, desc.VendorId, desc.DeviceId, desc.Description);
                OutputDebugStringW(buff);
                #endif
                break;
            }
        }
    }

    #if !defined(NDEBUG)
    if (!adapter)
    {
        // Try WARP12 instead
        if (FAILED(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()))))
        {
            throw std::runtime_error("WARP12 not available. Enable the 'Graphics Tools' optional feature");
        }

        OutputDebugStringA("Direct3D Adapter - WARP12\n");
    }
    #endif

    if (!adapter)
    {
        throw std::runtime_error("No Direct3D 12 device found");
    }

    *ppAdapter = adapter.Detach();
}

void DeviceResources::WaitForReadbackFence() 
{
    auto nextFenceValue = m_readbackFence->GetCompletedValue() + 1;

    // Change the fence to the new value after some GPU work
    DX::ThrowIfFailed(m_commandQueue->Signal(m_readbackFence.Get(), nextFenceValue));
    
    if (m_readbackFence->GetCompletedValue() < nextFenceValue)
    {
        DX::ThrowIfFailed(m_readbackFence->SetEventOnCompletion(nextFenceValue, m_readbackFenceEvent.Get()));
        std::ignore = WaitForSingleObject(m_readbackFenceEvent.Get(), INFINITE);
        ResetEvent(m_readbackFenceEvent.Get());
    }
}

void DeviceResources::ReadbackSignalBackToGPU()
{
    auto nextFence = m_readbackFence->GetCompletedValue() + 1;
    m_readbackFence->Signal(nextFence);

    DX::ThrowIfFailed(m_commandQueue->Wait(m_readbackFence.Get(), nextFence));
}