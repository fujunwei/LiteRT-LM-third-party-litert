# WebNN in LiteRT

## What is WebNN?

**WebNN** (Web Neural Network API) is a [W3C standard](https://www.w3.org/TR/webnn/)
that provides web applications with access to hardware-accelerated machine
learning inference. It defines a common API surface that allows ML workloads
to run on the CPU, GPU, or NPU available on the host device, without requiring
web developers to write platform-specific shader or kernel code.

In the context of **LiteRT**, WebNN is surfaced as an accelerator backend that
can be registered alongside the GPU and NPU accelerators. On Windows this
backend is implemented on top of **DirectML** (a hardware-agnostic Direct3D 12
ML library), enabling LiteRT models to take advantage of the full range of
DirectX-capable hardware—including discrete and integrated GPUs as well as
dedicated Neural Processing Units.

## Key Concepts

| Concept | Description |
|---------|-------------|
| **Device type** | Selects the physical device to run inference on: CPU, GPU, or NPU. |
| **Power preference** | Hints to the runtime whether to optimize for throughput (`HighPerformance`) or battery life (`LowPower`). Omit to use the system default. |
| **Precision** | Controls numeric precision: `FP32` (default, highest accuracy) or `FP16` (faster on hardware with native half-precision support). |

## LiteRT WebNN Options

LiteRT exposes WebNN accelerator configuration through a pair of C and C++ APIs.

### C API

```c
#include "litert/c/options/litert_webnn_options.h"

LiteRtOpaqueOptions options;
LiteRtCreateWebNnOptions(&options);

// Target the GPU with low-power preference at FP16 precision.
LiteRtSetWebNnOptionsDevicePreference(options, kLiteRtWebNnDeviceTypeGpu);
LiteRtSetWebNnOptionsPowerPreference(options,
                                     kLiteRtWebNnPowerPreferenceLowPower);
LiteRtSetWebNnOptionsPrecision(options, kLiteRtWebNnPrecisionFp16);

// Pass `options` to LiteRtCreateCompiledModel(...).

LiteRtDestroyOpaqueOptions(options);
```

### C++ API

```cpp
#include "litert/cc/options/litert_webnn_options.h"

auto webnn_options = litert::WebNnOptions::Create();

// Target the NPU with default power preference at FP32 precision.
webnn_options->SetDevicePreference(kLiteRtWebNnDeviceTypeNpu);
webnn_options->SetPowerPreference(kLiteRtWebNnPowerPreferenceDefault);
webnn_options->SetPrecision(kLiteRtWebNnPrecisionFp32);

// Pass `webnn_options` to litert::CompiledModel::Create(...).
```

### Available Option Values

**`LiteRtWebNnDeviceType`**

| Value | Description |
|-------|-------------|
| `kLiteRtWebNnDeviceTypeCpu` (default) | Run inference on the CPU. |
| `kLiteRtWebNnDeviceTypeGpu` | Run inference on the GPU. |
| `kLiteRtWebNnDeviceTypeNpu` | Run inference on the NPU/dedicated AI accelerator. |

**`LiteRtWebNnPowerPreference`**

| Value | Description |
|-------|-------------|
| `kLiteRtWebNnPowerPreferenceDefault` (default) | System default power policy. |
| `kLiteRtWebNnPowerPreferenceHighPerformance` | Maximize throughput; may increase power consumption. |
| `kLiteRtWebNnPowerPreferenceLowPower` | Minimize power consumption; may reduce throughput. |

**`LiteRtWebNnPrecision`**

| Value | Description |
|-------|-------------|
| `kLiteRtWebNnPrecisionFp32` (default) | 32-bit floating-point arithmetic. |
| `kLiteRtWebNnPrecisionFp16` | 16-bit floating-point arithmetic. Faster on hardware with native FP16 support. |

## Platform Support

| Platform | Status |
|----------|--------|
| Windows (DirectML / D3D12) | Available |
| Other platforms | Coming soon |

On Windows, the backend dynamically loads `D3D12.dll` and, when available,
`DXCore.dll` (required for NPU enumeration on newer Windows 10/11 systems).

## Accelerator Registration

The WebNN accelerator is registered automatically when statically linked into
a LiteRT environment:

```cpp
// In the application entry point (or a registration TU):
extern "C" LiteRtStatus LiteRtRegisterStaticLinkedAcceleratorWebNn(
    LiteRtEnvironmentT& environment);
```

LiteRT's auto-registration logic calls this function during environment
initialization and logs a confirmation message when it succeeds.

## Further Reading

- [W3C Web Neural Network API specification](https://www.w3.org/TR/webnn/)
- [DirectML documentation](https://learn.microsoft.com/en-us/windows/ai/directml/dml)
- [LiteRT documentation](https://ai.google.dev/edge/litert)
