// GPU vendor telemetry (utilization percent, temperature) -- the Phase 19
// gap PLAN.md records as forward work because "no vendor SDK (NVML/ADL) is
// an approved dependency". NVIDIA and AMD are now both approved, narrowly:
// NVML is consumed with hand-declared prototypes and LoadLibraryA("nvml.dll")
// at runtime -- nvml.dll ships with every NVIDIA driver, so nothing from
// NVIDIA's SDK is linked or vendored, only its publicly documented C ABI.
// AMD is consumed through the vendored ADLX helper in third_party/ADLX,
// which itself LoadLibrary()s the driver-installed ADLX DLL internally --
// so neither vendor's binary is linked at build time, and a host without
// that vendor's driver present fails closed instead of crashing.
#include "masterai.hpp"

#if defined(_WIN32)
#include <windows.h>

#include "../third_party/ADLX/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.h"
// ADLX.h (pulled in by ADLXHelper.h) only declares ISystem.h's forward
// reference to IADLXPerformanceMonitoringServices; the full interface
// definitions (IADLXGPUMetrics, IADLXPerformanceMonitoringServices) live in
// IPerformanceMonitoring.h and must be included separately.
#include "../third_party/ADLX/SDK/Include/IPerformanceMonitoring.h"

// ADLXHelper.cpp defines the required `ADLXHelper g_ADLX;` global; nothing
// else in this translation unit needs its own instance.

namespace masterai {
namespace {

// --- NVML: hand-declared subset of the public NVML C ABI -----------------
// Only the handful of entry points this probe needs. Struct layouts and
// constants are reproduced from NVIDIA's published NVML API reference, not
// copied from nvml.h, since that header is only distributed inside the
// EULA-gated CUDA Toolkit / GPU Deployment Kit installers.
using nvmlReturn_t = int;
constexpr nvmlReturn_t kNvmlSuccess = 0;
using nvmlDevice_t = void*;

struct NvmlUtilization {
    unsigned int gpu;
    unsigned int memory;
};

using NvmlInit_t = nvmlReturn_t(__cdecl*)();
using NvmlShutdown_t = nvmlReturn_t(__cdecl*)();
using NvmlDeviceGetCount_t = nvmlReturn_t(__cdecl*)(unsigned int*);
using NvmlDeviceGetHandleByIndex_t = nvmlReturn_t(__cdecl*)(unsigned int, nvmlDevice_t*);
using NvmlDeviceGetName_t = nvmlReturn_t(__cdecl*)(nvmlDevice_t, char*, unsigned int);
using NvmlDeviceGetUtilizationRates_t = nvmlReturn_t(__cdecl*)(nvmlDevice_t, NvmlUtilization*);
using NvmlDeviceGetTemperature_t = nvmlReturn_t(__cdecl*)(nvmlDevice_t, unsigned int, unsigned int*);

constexpr unsigned int kNvmlTemperatureGpu = 0U;
constexpr unsigned int kNvmlDeviceNameBufferSize = 96U;

std::vector<GpuVendorTelemetry> probe_nvidia_via_nvml() {
    std::vector<GpuVendorTelemetry> results;
    const HMODULE library = LoadLibraryA("nvml.dll");
    if (library == nullptr) return results;

    const auto init_fn = reinterpret_cast<NvmlInit_t>(GetProcAddress(library, "nvmlInit_v2"));
    const auto shutdown_fn = reinterpret_cast<NvmlShutdown_t>(GetProcAddress(library, "nvmlShutdown"));
    const auto count_fn = reinterpret_cast<NvmlDeviceGetCount_t>(
        GetProcAddress(library, "nvmlDeviceGetCount_v2"));
    const auto handle_fn = reinterpret_cast<NvmlDeviceGetHandleByIndex_t>(
        GetProcAddress(library, "nvmlDeviceGetHandleByIndex_v2"));
    const auto name_fn = reinterpret_cast<NvmlDeviceGetName_t>(
        GetProcAddress(library, "nvmlDeviceGetName"));
    const auto utilization_fn = reinterpret_cast<NvmlDeviceGetUtilizationRates_t>(
        GetProcAddress(library, "nvmlDeviceGetUtilizationRates"));
    const auto temperature_fn = reinterpret_cast<NvmlDeviceGetTemperature_t>(
        GetProcAddress(library, "nvmlDeviceGetTemperature"));

    if (init_fn == nullptr || shutdown_fn == nullptr || count_fn == nullptr ||
        handle_fn == nullptr || name_fn == nullptr || utilization_fn == nullptr ||
        temperature_fn == nullptr) {
        FreeLibrary(library);
        return results;
    }

    if (init_fn() != kNvmlSuccess) {
        FreeLibrary(library);
        return results;
    }

    unsigned int device_count = 0U;
    if (count_fn(&device_count) == kNvmlSuccess) {
        for (unsigned int index = 0U; index < device_count; ++index) {
            nvmlDevice_t device = nullptr;
            if (handle_fn(index, &device) != kNvmlSuccess) continue;

            GpuVendorTelemetry telemetry;
            telemetry.vendor = "nvidia";

            char name_buffer[kNvmlDeviceNameBufferSize] = {};
            if (name_fn(device, name_buffer, kNvmlDeviceNameBufferSize) == kNvmlSuccess) {
                telemetry.device_name.assign(name_buffer);
            }

            NvmlUtilization utilization{};
            if (utilization_fn(device, &utilization) == kNvmlSuccess) {
                telemetry.utilization_percent = utilization.gpu;
            }

            unsigned int temperature = 0U;
            if (temperature_fn(device, kNvmlTemperatureGpu, &temperature) == kNvmlSuccess) {
                telemetry.temperature_celsius = static_cast<int>(temperature);
            }

            telemetry.available = true;
            results.push_back(telemetry);
        }
    }

    shutdown_fn();
    FreeLibrary(library);
    return results;
}

// --- AMD: vendored ADLX helper --------------------------------------------
std::vector<GpuVendorTelemetry> probe_amd_via_adlx() {
    std::vector<GpuVendorTelemetry> results;
    if (g_ADLX.Initialize() != ADLX_OK) return results;

    adlx::IADLXSystem* const system_services = g_ADLX.GetSystemServices();
    if (system_services == nullptr) {
        g_ADLX.Terminate();
        return results;
    }

    adlx::IADLXGPUList* gpu_list = nullptr;
    adlx::IADLXPerformanceMonitoringServices* performance_services = nullptr;
    if (system_services->GetGPUs(&gpu_list) == ADLX_OK && gpu_list != nullptr &&
        system_services->GetPerformanceMonitoringServices(&performance_services) == ADLX_OK &&
        performance_services != nullptr) {
        for (adlx_uint index = 0U; index < gpu_list->Size(); ++index) {
            adlx::IADLXGPU* gpu = nullptr;
            if (gpu_list->At(index, &gpu) != ADLX_OK || gpu == nullptr) continue;

            GpuVendorTelemetry telemetry;
            telemetry.vendor = "amd";

            const char* gpu_name = nullptr;
            if (gpu->Name(&gpu_name) == ADLX_OK && gpu_name != nullptr) {
                telemetry.device_name.assign(gpu_name);
            }

            adlx::IADLXGPUMetrics* metrics = nullptr;
            if (performance_services->GetCurrentGPUMetrics(gpu, &metrics) == ADLX_OK &&
                metrics != nullptr) {
                adlx_double usage = 0.0;
                if (metrics->GPUUsage(&usage) == ADLX_OK) {
                    telemetry.utilization_percent = static_cast<unsigned int>(usage);
                }
                adlx_double temperature = 0.0;
                if (metrics->GPUTemperature(&temperature) == ADLX_OK) {
                    telemetry.temperature_celsius = static_cast<int>(temperature);
                }
                telemetry.available = true;
                metrics->Release();
            }

            gpu->Release();
            if (telemetry.available) results.push_back(telemetry);
        }
    }

    if (performance_services != nullptr) performance_services->Release();
    if (gpu_list != nullptr) gpu_list->Release();
    g_ADLX.Terminate();
    return results;
}

}  // namespace

std::vector<GpuVendorTelemetry> probe_gpu_vendor_telemetry() {
    std::vector<GpuVendorTelemetry> results = probe_nvidia_via_nvml();
    std::vector<GpuVendorTelemetry> amd_results = probe_amd_via_adlx();
    results.insert(results.end(), amd_results.begin(), amd_results.end());
    return results;
}

}  // namespace masterai

#else  // !_WIN32

namespace masterai {

// GPU vendor telemetry is Windows-only per the project's Windows-first
// build scope (Linux support is optional and left to a future pass); fails
// closed to an empty, unavailable result rather than a partial probe.
std::vector<GpuVendorTelemetry> probe_gpu_vendor_telemetry() {
    return {};
}

}  // namespace masterai

#endif
