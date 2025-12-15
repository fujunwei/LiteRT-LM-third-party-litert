// Copyright 2024 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "litert/runtime/d3d12_buffer.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "absl/base/attributes.h"  // from @com_google_absl
#include "absl/base/const_init.h"  // from @com_google_absl
#include "absl/container/node_hash_map.h"  // from @com_google_absl
#include "absl/synchronization/mutex.h"  // from @com_google_absl
#include "litert/c/litert_common.h"
#include "litert/cc/litert_expected.h"


namespace litert::internal {

namespace {

  std::string GetHResultErrorMessage(HRESULT hr) {
    LPWSTR messageBuffer = nullptr;
    DWORD chars = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        hr,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
        (LPWSTR)&messageBuffer,
        0,
        NULL
    );

    if (chars == 0) {
        // Handle cases where FormatMessage fails or the HRESULT is not a standard Win32 error
        if (HRESULT_FACILITY(hr) == FACILITY_WIN32) {
            // For Win32 errors, extract the code and try again
            chars = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                HRESULT_CODE(hr),
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPWSTR)&messageBuffer,
                0,
                NULL
            );
        }
    }

    std::wstring wMessage;
    if (chars > 0 && messageBuffer != nullptr) {
        wMessage = messageBuffer;
        LocalFree(messageBuffer); // Free the buffer allocated by FormatMessage
    }
    else {
        wMessage = L"Unknown error (0x" + std::to_wstring(hr) + L")";
    }

    // Convert wstring to string for standard output
    return std::string(wMessage.begin(), wMessage.end());
}

#define RETURN_IF_FAILED(res, error_message)                                 \
do {                                                             \
  if (res != S_OK) {                                              \
    LITERT_LOG(LITERT_ERROR,    error_message);                          \
    return Unexpected(kLiteRtStatusErrorRuntimeFailure,  \
                        error_message);        \
  }                                                              \
} while (0)

Expected<Microsoft::WRL::ComPtr<ID3D12Resource>> CreateUploadResource(Microsoft::WRL::ComPtr<ID3D12Device9> device, size_t byte_size) {
  // CD3DX12_HEAP_PROPERTIES dx12_heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
  // UMA heap properties (default heap with CPU access)
  D3D12_HEAP_PROPERTIES heap_properties;
  heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT,
  heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heap_properties.CreationNodeMask = 1;
  heap_properties.VisibleNodeMask = 1;
  heap_properties =
            device->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_UPLOAD);
  D3D12_RESOURCE_DESC desc;
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Alignment = 0;
  desc.Width = byte_size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc = {1, 0};
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  Microsoft::WRL::ComPtr<ID3D12Resource> comitted_resource;
  auto res = device->CreateCommittedResource(&heap_properties,
                                              D3D12_HEAP_FLAG_NONE,
                                              &desc,
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                              nullptr,
                                              IID_PPV_ARGS(comitted_resource.ReleaseAndGetAddressOf()));
  RETURN_IF_FAILED(res, "CreateCommittedResource failed.");
  return comitted_resource;
}

class D3D12Memory {
 public:
  using Ptr = std::unique_ptr<D3D12Memory>;

  D3D12Memory(const D3D12Memory&) = delete;
  D3D12Memory& operator=(const D3D12Memory&) = delete;
  D3D12Memory(D3D12Memory&&) = default;
  D3D12Memory& operator=(D3D12Memory&&) = default;

  ~D3D12Memory() { 
    // close(heap_fd_); 
  }

  static Expected<Ptr> Create() {
    PlatformFunctions* platform_functions = PlatformFunctions::GetInstance();
    if (!platform_functions || !platform_functions->IsDXCoreSupported()) {
      LITERT_LOG(LITERT_ERROR,    "DXCore is not supported on this platform.");
    }
    auto d3d12_create_device_proc =
    platform_functions->d3d12_create_device_proc();
    Microsoft::WRL::ComPtr<ID3D12Device9> device;
    auto res =
        d3d12_create_device_proc(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));
    RETURN_IF_FAILED(res, "D3D12CreateDevice failed.");
    D3D12_FEATURE_DATA_ARCHITECTURE arch = {};
    res = device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE,
                                                  &arch, sizeof(arch));
    RETURN_IF_FAILED(res, "D3D12 device failed to check feature support.");
    LITERT_LOG(LITERT_ERROR, "===check feature support %d, %d", arch.UMA, arch.CacheCoherentUMA);    
    return Ptr(new D3D12Memory(std::move(device)));
  }

  Expected<D3D12Buffer> Alloc(size_t size) {
    auto upload_resource_result = CreateUploadResource(device_, size);
    if (!upload_resource_result) {
      return Unexpected(kLiteRtStatusErrorRuntimeFailure,
                        "Failed to allocate D3D12 buffer");
    }
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_resource = upload_resource_result.Value();
    void* addr;
    auto res = upload_resource->Map(0, nullptr, &addr);
    RETURN_IF_FAILED(res, "ID3D12Resource Map failed.");
    // if (addr == MAP_FAILED) {
    //   return Unexpected(kLiteRtStatusErrorRuntimeFailure,
    //                     "Failed to mem-map DMA-BUF buffer");
    // }
    HANDLE handle;
    res = device_->CreateSharedHandle(upload_resource.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
    LITERT_LOG(LITERT_ERROR, "CreateSharedHandle %s", GetHResultErrorMessage(res).c_str());  
    RETURN_IF_FAILED(res, "CreateSharedHandle failed: ");

    LITERT_LOG(LITERT_ERROR, "Alloc shared memory handle %p", handle);    
    records_[addr] = Record{.handle = handle, .addr = addr, 
      .upload_resource = std::move(upload_resource), .size = size};
    return D3D12Buffer{.handle = handle, .addr = addr};
  }

  void Free(void* addr) {
    auto iter = records_.find(addr);
    if (iter == records_.end()) {
      return;
    }
    auto& record = iter->second;
    record.upload_resource->Unmap(0, nullptr);
    // close(record.fd);
    records_.erase(iter);
  }

 private:
  struct Record {
    HANDLE handle;
    void* addr;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_resource;
    size_t size;
  };

  explicit D3D12Memory(Microsoft::WRL::ComPtr<ID3D12Device9> device) : device_(std::move(device)) {}

  Microsoft::WRL::ComPtr<ID3D12Device9> device_;
  absl::node_hash_map<void*, Record> records_;
};

D3D12Memory* g_d3d12_memory;
ABSL_CONST_INIT absl::Mutex TheMutex(absl::kConstInit);

Expected<void> InitD3D12MemoryIfNeeded() {
  if (!g_d3d12_memory) {
    if (auto memory = D3D12Memory::Create(); memory) {
      g_d3d12_memory = memory->release();
    } else {
      return Unexpected(memory.Error());
    }
  }
  return {};
}

}  // namespace

bool D3D12Buffer::IsSupported() {
  absl::MutexLock lock(&TheMutex);
  auto status = InitD3D12MemoryIfNeeded();
  return static_cast<bool>(status);
}

Expected<D3D12Buffer> D3D12Buffer::Alloc(size_t size) {
  absl::MutexLock lock(&TheMutex);
  if (auto status = InitD3D12MemoryIfNeeded(); !status) {
    return Unexpected(status.Error());
  }
  return g_d3d12_memory->Alloc(size);
}

void D3D12Buffer::Free(void* addr) {
  absl::MutexLock lock(&TheMutex);
  if (g_d3d12_memory) {
    g_d3d12_memory->Free(addr);
  }
}

}  // namespace litert::internal
