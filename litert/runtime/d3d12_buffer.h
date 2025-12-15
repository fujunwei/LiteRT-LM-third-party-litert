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

#ifndef ODML_LITERT_LITERT_RUNTIME_D3D12_BUFFER_H_
#define ODML_LITERT_LITERT_RUNTIME_D3D12_BUFFER_H_

#include "litert/cc/litert_expected.h"


#        include <wrl.h>
#        include <initguid.h>  // it has to be placed before dxcore

#include "platform_functions.h"
#include "dxcore.h"
#include "dxcore_interface.h"
#include "d3dx12_core.h"


namespace litert::internal {

struct D3D12Buffer {
  HANDLE handle;
  void* addr;

  static bool IsSupported();
  static Expected<D3D12Buffer> Alloc(size_t size);
  static void Free(void* addr);
};

}  // namespace litert::internal

#endif  // ODML_LITERT_LITERT_RUNTIME_D3D12_BUFFER_H_
