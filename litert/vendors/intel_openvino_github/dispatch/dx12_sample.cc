// Dx12Samples.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <iostream>
#include <vector>
#include <cstring>

// Smart pointer for Dx12 objects (auto-manages reference counting)
using Microsoft::WRL::ComPtr;

// Helper macro to check Dx12 HRESULT errors
#define CHECK_HR(hr, msg) if (FAILED(hr)) { std::cerr << msg << " (HRESULT: 0x" << std::hex << hr << ")" << std::endl; return 1; }

int main() {
    // ==============================================
    // Step 1: Initialize Dx12 Device & Core Objects
    // ==============================================
    ComPtr<ID3D12Device> dx12Device;
    ComPtr<ID3D12CommandQueue> cmdQueue;
    ComPtr<ID3D12CommandAllocator> cmdAllocator;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;

    // 1.1 Create Dx12 Device (use default GPU adapter)
    HRESULT hr = D3D12CreateDevice(
        nullptr,                     // Default adapter
        D3D_FEATURE_LEVEL_12_0,      // Minimum Dx12 feature level
        IID_PPV_ARGS(&dx12Device)
    );
    CHECK_HR(hr, "Failed to create Dx12 Device");
    std::cout << "Dx12 Device created successfully" << std::endl;

    // 1.2 Create Command Queue (for executing copy commands)
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;  // Supports direct GPU commands
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = dx12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue));
    CHECK_HR(hr, "Failed to create Command Queue");

    // 1.3 Create Command Allocator (manages command list memory)
    hr = dx12Device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmdAllocator)
    );
    CHECK_HR(hr, "Failed to create Command Allocator");

    // 1.4 Create Command List (for recording copy commands)
    hr = dx12Device->CreateCommandList(
        0,                          // Node mask (single GPU)
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        cmdAllocator.Get(),         // Associate with command allocator
        nullptr,                    // No pipeline state (for copy commands)
        IID_PPV_ARGS(&cmdList)
    );
    CHECK_HR(hr, "Failed to create Command List");

    // 1.5 Create Fence (for synchronizing GPU → CPU)
    hr = dx12Device->CreateFence(
        0,                          // Initial fence value
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&fence)
    );
    CHECK_HR(hr, "Failed to create Fence");

    // 1.6 Create Event (for CPU to wait on GPU completion)
    fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (!fenceEvent) {
        std::cerr << "Failed to create Fence Event" << std::endl;
        return 1;
    }

    // ==============================================
    // Step 2: Allocate Buffers (GPU + Readback)
    // ==============================================
    const size_t kBufferSize = 4 * 1024;  // 4KB buffer (adjust as needed)
    std::vector<uint32_t> hostInputData(kBufferSize / sizeof(uint32_t), 0x12345678);  // Dummy input data

    // 2.1 Allocate GPU-Only Buffer (D3D12_HEAP_TYPE_DEFAULT)
    // This buffer is fast for GPU access but cannot be mapped to host directly
    ComPtr<ID3D12Resource> gpuBuffer;
    D3D12_RESOURCE_DESC gpuBufferDesc = {};
    gpuBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    gpuBufferDesc.Width = kBufferSize;  // Buffer size in bytes
    gpuBufferDesc.Height = 1;
    gpuBufferDesc.DepthOrArraySize = 1;
    gpuBufferDesc.MipLevels = 1;
    gpuBufferDesc.Format = DXGI_FORMAT_UNKNOWN;  // Buffers have no format
    gpuBufferDesc.SampleDesc.Count = 1;
    gpuBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES gpuHeapProps = {};
    gpuHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;  // GPU-only memory

    hr = dx12Device->CreateCommittedResource(
        &gpuHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &gpuBufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,  // Initial state: ready to receive copy
        nullptr,
        IID_PPV_ARGS(&gpuBuffer)
    );
    CHECK_HR(hr, "Failed to create GPU Buffer");

    // 2.2 Allocate Readback Buffer (D3D12_HEAP_TYPE_READBACK)
    // This buffer is GPU-writable and host-readable (for data readback)
    ComPtr<ID3D12Resource> readbackBuffer;
    D3D12_HEAP_PROPERTIES readbackHeapProps = {};
    readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;  // Readback memory

    hr = dx12Device->CreateCommittedResource(
        &readbackHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &gpuBufferDesc,  // Same size as GPU buffer
        D3D12_RESOURCE_STATE_COPY_DEST,  // Initial state: ready to receive copy
        nullptr,
        IID_PPV_ARGS(&readbackBuffer)
    );
    CHECK_HR(hr, "Failed to create Readback Buffer");

    // ==============================================
    // Step 3: Copy Host Data → GPU Buffer (Setup)
    // ==============================================
    // First, copy dummy host data to the GPU buffer (required to have data to read back)
    ComPtr<ID3D12Resource> uploadBuffer;  // Intermediate upload buffer (host → GPU)
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;  // Host-writable, GPU-readable

    hr = dx12Device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &gpuBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)
    );
    CHECK_HR(hr, "Failed to create Upload Buffer");

    // Map upload buffer to host memory and copy input data
    void* uploadBufferHostPtr = nullptr;
    hr = uploadBuffer->Map(0, nullptr, &uploadBufferHostPtr);
    CHECK_HR(hr, "Failed to map Upload Buffer");
    memcpy(uploadBufferHostPtr, hostInputData.data(), kBufferSize);
    uploadBuffer->Unmap(0, nullptr);  // Unmap after writing

    // ==============================================
    // Step 4: Record Commands (Copy GPU → Readback)
    // ==============================================
    // Reset command allocator (required before recording new commands)
    //hr = cmdAllocator->Reset();
    //CHECK_HR(hr, "Failed to reset Command Allocator");

    //// Reset command list (associate with reset allocator)
    //hr = cmdList->Reset(cmdAllocator.Get(), nullptr);
    //CHECK_HR(hr, "Failed to reset Command List");

    // 4.1 Transition GPU buffer to COPY_SOURCE state (ready to be copied from)
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = gpuBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    // 4.2 Copy data: Host → Upload Buffer → GPU Buffer (initial setup)
    cmdList->CopyBufferRegion(
        gpuBuffer.Get(),    // Destination: GPU buffer
        0,                  // Destination offset
        uploadBuffer.Get(), // Source: Upload buffer
        0,                  // Source offset
        kBufferSize         // Number of bytes to copy
    );

    // 4.3 Copy data: GPU Buffer → Readback Buffer (the readback step)
    cmdList->CopyBufferRegion(
        readbackBuffer.Get(),  // Destination: Readback buffer
        0,                     // Destination offset
        gpuBuffer.Get(),       // Source: GPU buffer
        0,                     // Source offset
        kBufferSize            // Number of bytes to copy
    );

    // 4.4 Close command list (ready to execute)
    hr = cmdList->Close();
    CHECK_HR(hr, "Failed to close Command List");

    // ==============================================
    // Step 5: Execute Commands & Synchronize
    // ==============================================
    // Execute the command list on the queue
    ID3D12CommandList* cmdLists[] = { cmdList.Get() };
    cmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    // Signal fence to track when GPU completes commands
    fenceValue++;
    hr = cmdQueue->Signal(fence.Get(), fenceValue);
    CHECK_HR(hr, "Failed to signal Fence");

    // Wait for GPU to complete (CPU blocks until fence is signaled)
    if (fence->GetCompletedValue() < fenceValue) {
        hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
        CHECK_HR(hr, "Failed to set Fence Event");
        WaitForSingleObject(fenceEvent, INFINITE);  // Block until GPU is done
    }

    std::cout << "GPU commands executed successfully" << std::endl;

    // ==============================================
    // Step 6: Read Back Data from Readback Buffer
    // ==============================================
    // Map readback buffer to host memory (read-only)
    void* readbackHostPtr = nullptr;
    hr = readbackBuffer->Map(0, nullptr, &readbackHostPtr);
    CHECK_HR(hr, "Failed to map Readback Buffer");

    // Copy data from readback buffer to host vector
    std::vector<uint32_t> hostReadbackData(kBufferSize / sizeof(uint32_t));
    memcpy(hostReadbackData.data(), readbackHostPtr, kBufferSize);

    // Unmap readback buffer (critical to avoid memory leaks)
    readbackBuffer->Unmap(0, nullptr);

    // ==============================================
    // Step 7: Verify Data Integrity
    // ==============================================
    bool dataMatch = (memcmp(hostInputData.data(), hostReadbackData.data(), kBufferSize) == 0);
    if (dataMatch) {
        std::cout << "Readback successful! Data matches original input." << std::endl;
        std::cout << "Sample data (first 4 bytes): 0x" << std::hex << hostReadbackData[0] << std::endl;
    }
    else {
        std::cerr << "Error: Readback data does not match original input!" << std::endl;
        return 1;
    }

    // ==============================================
    // Cleanup Resources
    // ==============================================
    CloseHandle(fenceEvent);
    // Dx12 objects (ComPtr) are auto-released when they go out of scope

    return 0;
}