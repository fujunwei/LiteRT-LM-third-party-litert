// Copyright 2025 Google LLC.
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

#ifndef THIRD_PARTY_ODML_LITERT_LITERT_RUNTIME_LEVEL_ZERO_BUFFER_MANAGER_H_
#define THIRD_PARTY_ODML_LITERT_LITERT_RUNTIME_LEVEL_ZERO_BUFFER_MANAGER_H_

#include <memory>

#include "absl/container/node_hash_map.h"  // from @com_google_absl
#include "litert/cc/litert_expected.h"
#include "litert/vendors/intel_openvino/openvino_shared_core.h"
#include "litert/c/litert_model_types.h"
#include "litert/vendors/intel_openvino/utils.h"
#include "openvino/runtime/intel_npu/level_zero/level_zero.hpp"

namespace litert {
namespace openvino {

typedef void *HANDLE;

class LevelZeroBufferManager {
 public:
  using Ptr = std::unique_ptr<LevelZeroBufferManager>;

  LevelZeroBufferManager(const LevelZeroBufferManager&) = delete;
  LevelZeroBufferManager& operator=(const LevelZeroBufferManager&) = delete;
  LevelZeroBufferManager(LevelZeroBufferManager&&) = default;
  LevelZeroBufferManager& operator=(LevelZeroBufferManager&&) = default;

  ~LevelZeroBufferManager() = default;

  static litert::Expected<Ptr> Create() {   
    LITERT_LOG(LITERT_ERROR, "======LevelZeroBufferManager::Create.");
    return Ptr(new LevelZeroBufferManager(OpenVINOSharedCore::GetInstance()->getCore()));
  }

  litert::Expected<void*> Alloc(const LiteRtRankedTensorType& tensor_type, size_t size) {
    auto context = core_->get_default_context("NPU")
                         .as<ov::intel_npu::level_zero::ZeroContext>();
    ov::element::Type ov_element_type =
      litert::openvino::MapLiteTypeToOV(tensor_type.element_type);
    std::vector<int32_t> ov_shape_vec(tensor_type.layout.rank);
    for (int i = 0; i < ov_shape_vec.size(); i++)
      ov_shape_vec[i] = tensor_type.layout.dimensions[i];
    auto level_zero_buffer = context.create_l0_host_tensor(
            ov_element_type, ov::Shape{ov_shape_vec.begin(), ov_shape_vec.end()});
    void* level_zero_ptr = level_zero_buffer.get();
    LITERT_LOG(LITERT_ERROR, "======context.create_l0_host_tensor %p, records_.size %d", level_zero_ptr, records_.size());

    records_[level_zero_ptr] = Record{.level_zero_buffer = level_zero_buffer,
      .level_zero_ptr = level_zero_ptr, .size = size};
    return level_zero_ptr;
  }

  litert::Expected<ov::intel_npu::level_zero::ZeroBufferTensor> GetZeroBufferTensor(void* level_zero_ptr) {
    auto iter = records_.find(level_zero_ptr);
    if (iter == records_.end()) {
      LITERT_LOG(LITERT_ERROR, "======Failed to GetZeroBufferTensor %d.", records_.size());
      return litert::Unexpected(kLiteRtStatusErrorInvalidArgument, "Failed to GetZeroBufferTensor");
    }
    auto& record = iter->second;
    return record.level_zero_buffer;
  }

  void Free(void* level_zero_ptr) {
    auto iter = records_.find(level_zero_ptr);
    if (iter == records_.end()) {
      return;
    }
    auto& record = iter->second;
    records_.erase(iter);
  }

 private:
  struct Record {
    ov::intel_npu::level_zero::ZeroBufferTensor level_zero_buffer;
    void* level_zero_ptr;
    size_t size;
  };

  explicit LevelZeroBufferManager(std::shared_ptr<ov::Core> core) : core_(core) {}

  std::shared_ptr<ov::Core> core_;
  absl::node_hash_map<void*, Record> records_;
};

LevelZeroBufferManager* g_level_zero_buffer_manager;

litert::Expected<void> InitLevelZeroBufferManagerIfNeeded() {
  if (!g_level_zero_buffer_manager) {
    if (auto manager = LevelZeroBufferManager::Create(); manager) {
      g_level_zero_buffer_manager = manager->release();
    } else {
      return litert::Unexpected(manager.Error());
    }
  }
  return {};
}

litert::Expected<void*> Alloc(const LiteRtRankedTensorType& tensor_type, size_t size) {
  if (auto status = InitLevelZeroBufferManagerIfNeeded(); !status) {
    return litert::Unexpected(status.Error());
  }
  return g_level_zero_buffer_manager->Alloc(tensor_type, size);
}

litert::Expected<ov::intel_npu::level_zero::ZeroBufferTensor> GetZeroBufferTensor(void* level_zero_ptr) {
  if (auto status = InitLevelZeroBufferManagerIfNeeded(); !status) {
    return litert::Unexpected(status.Error());
  }
  return g_level_zero_buffer_manager->GetZeroBufferTensor(level_zero_ptr);
}

void Free(void* level_zero_ptr) {
  if (g_level_zero_buffer_manager) {
    g_level_zero_buffer_manager->Free(level_zero_ptr);
  }
}

}  // namespace openvino
}  // namespace litert

#endif  // THIRD_PARTY_ODML_LITERT_LITERT_RUNTIME_LEVEL_ZERO_BUFFER_MANAGER_H_
