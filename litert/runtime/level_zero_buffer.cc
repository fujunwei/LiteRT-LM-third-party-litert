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

#include "litert/runtime/level_zero_buffer.h"

#include <cstddef>
#include <memory>
#include <utility>

#include "absl/base/attributes.h"  // from @com_google_absl
#include "absl/base/const_init.h"  // from @com_google_absl
#include "absl/container/node_hash_map.h"  // from @com_google_absl
#include "absl/synchronization/mutex.h"  // from @com_google_absl
#include "litert/c/litert_common.h"
#include "litert/cc/litert_expected.h"
#include "openvino/runtime/core.hpp"
#include "litert/vendors/intel_openvino/level_zero_buffer_manager.h"


namespace litert::internal {

namespace {

// class LevelZeroBufferManager {
//  public:
//   using Ptr = std::unique_ptr<LevelZeroBufferManager>;

//   LevelZeroBufferManager(const LevelZeroBufferManager&) = delete;
//   LevelZeroBufferManager& operator=(const LevelZeroBufferManager&) = delete;
//   LevelZeroBufferManager(LevelZeroBufferManager&&) = default;
//   LevelZeroBufferManager& operator=(LevelZeroBufferManager&&) = default;

//   ~LevelZeroBufferManager() = default;

//   static Expected<Ptr> Create() {   
//     return Ptr(new LevelZeroBufferManager(OpenVINOSharedCore::GetInstance()->getCore()));
//   }

//   Expected<LevelZeroBuffer> Alloc(LiteRtRankedTensorType& tensor_type, size_t size) {
//     auto context = core_->get_default_context("NPU")
//                          .as<ov::intel_npu::level_zero::ZeroContext>();
//     ov::element::Type ov_element_type =
//       litert::openvino::MapLiteTypeToOV(tensor_type.element_type);
//     std::vector<int32_t> ov_shape_vec(tensor_type.layout.rank);
//     for (int i = 0; i < ov_shape_vec.size(); i++)
//       ov_shape_vec[i] = tensor_type.layout.dimensions[i];
//     auto level_zero_buffer = context.create_l0_host_tensor(
//             ov_element_type, ov::Shape{ov_shape_vec.begin(), ov_shape_vec.end()});
//     void* level_zero_ptr = level_zero_buffer.get();
//     LITERT_LOG(LITERT_ERROR, "======context.create_l0_host_tensor %p, ", level_zero_ptr);

//     records_[level_zero_ptr] = Record{.level_zero_buffer = level_zero_buffer,
//       .level_zero_ptr = level_zero_ptr, .size = size};
//     return LevelZeroBuffer{.level_zero_buffer = level_zero_buffer, .level_zero_ptr = level_zero_ptr};
//   }

//   void Free(void* level_zero_ptr) {
//     auto iter = records_.find(level_zero_ptr);
//     if (iter == records_.end()) {
//       return;
//     }
//     auto& record = iter->second;
//     records_.erase(iter);
//   }

//  private:
//   struct Record {
//     ov::intel_npu::level_zero::ZeroBufferTensor level_zero_buffer;
//     void* level_zero_ptr;
//     size_t size;
//   };

//   explicit LevelZeroBufferManager(std::shared_ptr<ov::Core> core) : core_(core) {}

//   std::shared_ptr<ov::Core> core_;
//   absl::node_hash_map<void*, Record> records_;
// };

// LevelZeroBufferManager* g_level_zero_buffer_manager;
ABSL_CONST_INIT absl::Mutex TheMutex(absl::kConstInit);

// Expected<void> InitLevelZeroBufferManagerIfNeeded() {
//   if (!g_level_zero_buffer_manager) {
//     if (auto manager = LevelZeroBufferManager::Create(); manager) {
//       g_level_zero_buffer_manager = manager->release();
//     } else {
//       return Unexpected(manager.Error());
//     }
//   }
//   return {};
// }

}  // namespace

bool LevelZeroBuffer::IsSupported() {
  absl::MutexLock lock(&TheMutex);
  auto status = litert::openvino::InitLevelZeroBufferManagerIfNeeded();
  return static_cast<bool>(status);
}

Expected<LevelZeroBuffer> LevelZeroBuffer::Alloc(const LiteRtRankedTensorType& tensor_type, size_t size) {
  absl::MutexLock lock(&TheMutex);
  if (auto status = litert::openvino::InitLevelZeroBufferManagerIfNeeded(); !status) {
    return Unexpected(status.Error());
  }
  LITERT_ASSIGN_OR_RETURN(void* level_zero_ptr, litert::openvino::Alloc(tensor_type, size));
  return LevelZeroBuffer{.level_zero_ptr = level_zero_ptr};
}

void LevelZeroBuffer::Free(void* level_zero_ptr) {
  absl::MutexLock lock(&TheMutex);
  litert::openvino::Free(level_zero_ptr);
}

}  // namespace litert::internal
