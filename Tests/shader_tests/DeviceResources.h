//--------------------------------------------------------------------------------------
// DeviceResources.h
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

namespace DX
{
    // Controls all the DirectX device resources.
    class DeviceResources
    {
    public:
        DeviceResources() noexcept(false);
        ~DeviceResources();

        DeviceResources(DeviceResources&&) = default;
        DeviceResources& operator= (DeviceResources&&) = default;

        DeviceResources(DeviceResources const&) = delete;
        DeviceResources& operator= (DeviceResources const&) = delete;

        void CreateDeviceResources(bool forceWarp);

        void ResetCommandList();
        void ResetCommandListToReadbackAllocator();
        void WaitForGpu() noexcept;

        auto                        GetD3DDevice() const noexcept          { return m_d3dDevice.Get(); }
        D3D_FEATURE_LEVEL           GetDeviceFeatureLevel() const noexcept { return m_d3dFeatureLevel; }
        ID3D12CommandQueue*         GetCommandQueue() const noexcept       { return m_commandQueue.Get(); }
        ID3D12CommandAllocator*     GetCommandAllocator() const noexcept   { return m_commandAllocator.Get(); }
        auto                        GetCommandList() const noexcept        { return m_commandList.Get(); }
        
        void WaitForReadbackFence();
        void ReadbackSignalBackToGPU();

    private:
        void GetAdapter(IDXGIAdapter1** ppAdapter, bool forceWarp);

        // Direct3D objects.
        Microsoft::WRL::ComPtr<ID3D12Device>                m_d3dDevice;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>   m_commandList;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue>          m_commandQueue;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator>      m_commandAllocator;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator>      m_readbackCommandAllocator;

        // Swap chain objects.
        Microsoft::WRL::ComPtr<IDXGIFactory6>               m_dxgiFactory;

        // Presentation fence objects.
        Microsoft::WRL::ComPtr<ID3D12Fence>                 m_fence;
        UINT64                                              m_fenceValue;
        Microsoft::WRL::Wrappers::Event                     m_fenceEvent;

        Microsoft::WRL::ComPtr<ID3D12Fence>                 m_readbackFence;
        UINT64                                              m_readbackFenceValue;
        Microsoft::WRL::Wrappers::Event                     m_readbackFenceEvent;

        // Cached device properties.
        D3D_FEATURE_LEVEL                                   m_d3dFeatureLevel;
    };
}
