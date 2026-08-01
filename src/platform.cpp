#include "masterai.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include <intrin.h>
#include <windows.h>
#include <psapi.h>
#include <dxgi.h>
#include <wrl/client.h>
#elif defined(__linux__)
#include <dlfcn.h>
#if defined(__x86_64__)
#include <cpuid.h>
#endif
#include <sys/sysinfo.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>
#else
#error "MasterAI supports only Windows and Linux."
#endif

namespace masterai {
namespace {

const char* level_name(const LogLevel level) noexcept {
    switch (level) {
        case LogLevel::debug: return "debug";
        case LogLevel::info: return "info";
        case LogLevel::warning: return "warning";
        case LogLevel::error: return "error";
    }
    return "unknown";
}

std::string sanitize_log_value(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char value_character : value) {
        const unsigned char character = static_cast<unsigned char>(value_character);
        if (character == '\r' || character == '\n' || character < 0x20U) {
            result.push_back(' ');
        } else {
            result.push_back(value_character);
        }
        if (result.size() == 2048U) {
            break;
        }
    }
    return result;
}

#if defined(_WIN32)
// PCI vendor IDs for the discrete-GPU vendors llama.cpp actually has an
// accelerated backend for (CUDA/NVIDIA, HIP/AMD). Used to score adapters
// below -- Intel (0x8086) is deliberately excluded even though its iGPUs
// support Vulkan, since on the laptops this matters for it is virtually
// always the low-VRAM onboard part sharing system RAM, never the card we
// want the offload decision to land on.
constexpr unsigned int kVendorNvidia = 0x10DEU;
constexpr unsigned int kVendorAmd1 = 0x1002U;
constexpr unsigned int kVendorAmd2 = 0x1022U;

bool is_discrete_gpu_vendor(unsigned int vendor_id) noexcept {
    return vendor_id == kVendorNvidia || vendor_id == kVendorAmd1 ||
           vendor_id == kVendorAmd2;
}

// Real dedicated-VRAM size via DXGI, so the GPU-layer offload decision
// (select_gpu_layers() in calibration.cpp) has real capacity evidence
// instead of only knowing a backend driver is present. Enumerates every
// adapter DXGI reports; the software/WARP adapter DXGI always lists
// reports 0 or a trivial figure, so it's skipped outright. A laptop's
// onboard Intel/AMD-APU GPU can still report a non-trivial "dedicated"
// figure (BIOS-carved graphics memory), which used to be able to outscore
// a real discrete card if DXGI's ordering or reporting quirks favoured it
// -- adapters from a recognised discrete-GPU vendor (NVIDIA/AMD, see
// is_discrete_gpu_vendor()) are now scored ahead of every other adapter
// regardless of reported size, and only the largest card is compared
// within that preferred group; the vendor check is skipped only if no
// adapter reports a discrete vendor at all, in which case the largest
// reported figure among the rest is used as before. On a multi-GPU host
// this deliberately picks a single card rather than summing them, since
// llama.cpp offloads onto one device, not a GPU-memory pool spanning
// several. Returns 0 (not just "unknown", but the same value
// select_gpu_layers() already treats as "cannot offload") if DXGI is
// unavailable or every adapter fails to report a size.
unsigned int probe_gpu_memory_mib() noexcept {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return 0U;
    std::uint64_t largest_discrete_bytes = 0U;
    std::uint64_t largest_other_bytes = 0U;
    for (UINT index = 0U;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(index, &adapter))) break;
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) continue;
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0U) continue;
        const auto bytes = static_cast<std::uint64_t>(desc.DedicatedVideoMemory);
        if (is_discrete_gpu_vendor(desc.VendorId)) {
            largest_discrete_bytes = std::max(largest_discrete_bytes, bytes);
        } else {
            largest_other_bytes = std::max(largest_other_bytes, bytes);
        }
    }
    const std::uint64_t chosen_bytes =
        largest_discrete_bytes > 0U ? largest_discrete_bytes : largest_other_bytes;
    return static_cast<unsigned int>(chosen_bytes / (1024ULL * 1024ULL));
}
#endif

void probe_acceleration(HardwareInfo& info) {
#if defined(_WIN32) && defined(_M_X64)
    int registers[4]{};
    __cpuid(registers, 1);
    if ((registers[2] & (1 << 20)) != 0) info.cpu_features.insert("sse4.2");
    const bool osxsave = (registers[2] & (1 << 27)) != 0;
    const bool avx = (registers[2] & (1 << 28)) != 0;
    if (osxsave && avx && (_xgetbv(0) & 0x6U) == 0x6U) {
        info.cpu_features.insert("avx");
        __cpuidex(registers, 7, 0);
        if ((registers[1] & (1 << 5)) != 0) info.cpu_features.insert("avx2");
    }
    const auto probe_library = [&](const wchar_t* name, const char* backend) {
        HMODULE module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module != nullptr) {
            info.gpu_backends.emplace_back(backend);
            FreeLibrary(module);
        }
    };
    probe_library(L"nvcuda.dll", "cuda");
    probe_library(L"vulkan-1.dll", "vulkan");
    probe_library(L"amdhip64.dll", "hip");
    if (!info.gpu_backends.empty()) info.gpu_memory_mib = probe_gpu_memory_mib();
#elif defined(__linux__) && defined(__x86_64__)
    unsigned int eax = 0U, ebx = 0U, ecx = 0U, edx = 0U;
    if (__get_cpuid(1U, &eax, &ebx, &ecx, &edx) != 0) {
        if ((ecx & bit_SSE4_2) != 0U) info.cpu_features.insert("sse4.2");
        if ((ecx & bit_AVX) != 0U) info.cpu_features.insert("avx");
    }
    if (__get_cpuid_count(7U, 0U, &eax, &ebx, &ecx, &edx) != 0 &&
        (ebx & bit_AVX2) != 0U) {
        info.cpu_features.insert("avx2");
    }
    const auto probe_library = [&](const char* name, const char* backend) {
        void* module = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (module != nullptr) {
            info.gpu_backends.emplace_back(backend);
            dlclose(module);
        }
    };
    probe_library("libcuda.so.1", "cuda");
    probe_library("libvulkan.so.1", "vulkan");
    probe_library("libamdhip64.so", "hip");
#elif defined(__aarch64__) || defined(_M_ARM64)
    info.cpu_features.insert("neon");
#endif
}

// Counts physical processor packages/cores through native topology APIs. A
// conservative logical-count fallback keeps admission safe if probing fails.
unsigned int physical_cpu_count() noexcept {
#if defined(_WIN32)
    DWORD bytes = 0U;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (bytes == 0U) return std::max(1U, std::thread::hardware_concurrency());
    std::vector<unsigned char> buffer(bytes);
    if (GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                buffer.data()),
            &bytes) == 0) {
        return std::max(1U, std::thread::hardware_concurrency());
    }
    unsigned int count = 0U;
    std::size_t offset = 0U;
    while (offset < bytes) {
        const auto* entry =
            reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                buffer.data() + offset);
        if (entry->Relationship == RelationProcessorCore) ++count;
        if (entry->Size == 0U) break;
        offset += entry->Size;
    }
    return std::max(1U, count);
#elif defined(__linux__)
    std::ifstream input("/proc/cpuinfo");
    std::set<std::pair<unsigned int, unsigned int>> cores;
    std::string line;
    unsigned int package = 0U;
    unsigned int core = 0U;
    while (std::getline(input, line)) {
        const auto delimiter = line.find(':');
        if (delimiter == std::string::npos) continue;
        const auto value = line.substr(delimiter + 1U);
        if (line.rfind("physical id", 0U) == 0U) {
            std::istringstream parsed(value);
            parsed >> package;
        } else if (line.rfind("core id", 0U) == 0U) {
            std::istringstream parsed(value);
            parsed >> core;
            cores.emplace(package, core);
        }
    }
    return cores.empty() ? std::max(1U, std::thread::hardware_concurrency())
                         : static_cast<unsigned int>(cores.size());
#endif
}

std::string probe_storage_class(const std::filesystem::path& storage_root) {
#if defined(_WIN32)
    const auto absolute = std::filesystem::absolute(storage_root);
    const auto root = absolute.root_path().wstring();
    const UINT type = GetDriveTypeW(root.c_str());
    if (type == DRIVE_REMOTE) return "network";
    if (type == DRIVE_REMOVABLE) return "removable";
    if (type == DRIVE_CDROM) return "optical";
    if (type == DRIVE_RAMDISK) return "ram";
    if (type == DRIVE_FIXED) return "fixed";
    return "unknown";
#elif defined(__linux__)
    static_cast<void>(storage_root);
    return "local";
#endif
}

}  // namespace

void log(const LogLevel level, const std::string& event, const std::string& detail) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now_time);
#else
    gmtime_r(&now_time, &utc);
#endif
    std::ostringstream timestamp;
    timestamp << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    // Build the whole line first and write it in one shot: now that the
    // server is concurrent, this is called from many request threads at
    // once, and a chain of separate operator<< calls could interleave
    // mid-line even though each individual call is itself data-race-free.
    std::ostringstream line;
    line << "{\"time\":\"" << timestamp.str()
         << "\",\"level\":\"" << level_name(level)
         << "\",\"event\":\"" << sanitize_log_value(event)
         << "\",\"detail\":\"" << sanitize_log_value(detail)
         << "\"}\n";
    static std::mutex log_mutex;
    std::lock_guard<std::mutex> lock(log_mutex);
    std::clog << line.str();
}

bool is_path_within(const std::filesystem::path& root,
                    const std::filesystem::path& candidate) {
    std::error_code absolute_error;
    const auto absolute_root =
        std::filesystem::absolute(root, absolute_error).lexically_normal();
    if (absolute_error) {
        return false;
    }
    const auto absolute_candidate =
        std::filesystem::absolute(candidate, absolute_error).lexically_normal();
    if (absolute_error) {
        return false;
    }

    const auto components_equal = [](const std::filesystem::path& left,
                                     const std::filesystem::path& right) {
#if defined(_WIN32)
        std::string left_text = left.string();
        std::string right_text = right.string();
        std::transform(left_text.begin(), left_text.end(), left_text.begin(),
                       [](const unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        std::transform(right_text.begin(), right_text.end(), right_text.begin(),
                       [](const unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        return left_text == right_text;
#else
        return left == right;
#endif
    };

    auto root_part = absolute_root.begin();
    auto candidate_part = absolute_candidate.begin();
    for (; root_part != absolute_root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == absolute_candidate.end() ||
            !components_equal(*root_part, *candidate_part)) {
            return false;
        }
    }
    return true;
}

HardwareInfo probe_hardware(const std::filesystem::path& storage_root) {
    HardwareInfo info;
    info.logical_cpu_count = std::max(1U, std::thread::hardware_concurrency());
    info.physical_cpu_count = physical_cpu_count();

#if defined(_WIN32)
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) == 0) {
        throw std::runtime_error("GlobalMemoryStatusEx failed");
    }
    constexpr std::uint64_t bytes_per_mib = 1024ULL * 1024ULL;
    info.total_ram_mib = memory.ullTotalPhys / bytes_per_mib;
    info.available_ram_mib = memory.ullAvailPhys / bytes_per_mib;
    info.total_virtual_memory_mib = memory.ullTotalPageFile / bytes_per_mib;
    info.available_virtual_memory_mib =
        memory.ullAvailPageFile / bytes_per_mib;
    DWORD highest_node = 0U;
    if (GetNumaHighestNodeNumber(&highest_node) != 0) {
        info.numa_node_count = highest_node + 1U;
    }
    SYSTEM_INFO system_info{};
    GetNativeSystemInfo(&system_info);
    info.architecture =
        system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64
            ? "x86_64"
            : system_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64
                  ? "arm64"
                  : "unsupported";
    info.platform = "windows";
#elif defined(__linux__)
    struct sysinfo memory {};
    if (sysinfo(&memory) != 0) {
        throw std::runtime_error("sysinfo failed");
    }
    constexpr std::uint64_t bytes_per_mib = 1024ULL * 1024ULL;
    const auto unit = static_cast<std::uint64_t>(memory.mem_unit);
    info.total_ram_mib = static_cast<std::uint64_t>(memory.totalram) * unit / bytes_per_mib;
    info.available_ram_mib =
        static_cast<std::uint64_t>(memory.freeram + memory.bufferram) * unit / bytes_per_mib;
    info.total_virtual_memory_mib =
        (static_cast<std::uint64_t>(memory.totalram) +
         static_cast<std::uint64_t>(memory.totalswap)) *
        unit / bytes_per_mib;
    info.available_virtual_memory_mib =
        (static_cast<std::uint64_t>(memory.freeram + memory.bufferram) +
         static_cast<std::uint64_t>(memory.freeswap)) *
        unit / bytes_per_mib;
    struct utsname platform {};
    if (uname(&platform) != 0) {
        throw std::runtime_error("uname failed");
    }
    info.architecture = platform.machine;
    info.platform = "linux";
#endif

    std::error_code storage_error;
    const auto storage = std::filesystem::space(storage_root, storage_error);
    if (!storage_error) {
        info.free_disk_mib = storage.available / (1024ULL * 1024ULL);
        info.storage_capacity_mib =
            storage.capacity / (1024ULL * 1024ULL);
    }
    info.storage_class = probe_storage_class(storage_root);
    probe_acceleration(info);
    info.backend_capabilities.insert("process-isolated-inference");
    info.backend_capabilities.insert("streaming");
    info.backend_capabilities.insert("cancellation");
    info.backend_capabilities.insert("tokenization");
    return info;
}

// Captures current-process resident/private/commit and cumulative page-fault
// counters using native APIs. Values are evidence only and never authorize
// memory admission on their own.
ProcessResourceSample probe_process_resources() {
    ProcessResourceSample sample;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters)) == 0) {
        throw std::runtime_error("GetProcessMemoryInfo failed");
    }
    sample.resident_memory_bytes =
        static_cast<std::uint64_t>(counters.WorkingSetSize);
    sample.private_memory_bytes =
        static_cast<std::uint64_t>(counters.PrivateUsage);
    sample.commit_bytes = sample.private_memory_bytes;
    sample.page_faults = counters.PageFaultCount;
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    std::uint64_t virtual_pages = 0U;
    std::uint64_t resident_pages = 0U;
    statm >> virtual_pages >> resident_pages;
    const auto page_size = static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
    sample.resident_memory_bytes = resident_pages * page_size;
    sample.private_memory_bytes = virtual_pages * page_size;
    sample.commit_bytes = sample.private_memory_bytes;
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        sample.page_faults =
            static_cast<std::uint64_t>(usage.ru_minflt + usage.ru_majflt);
    }
#endif
    return sample;
}

// Phase 19 calibration evidence: system-wide CPU utilization plus this
// process's own cumulative disk read/write bytes, sampled twice across
// interval_milliseconds and reduced to one delta-based reading. Never used
// for memory admission or suitability decisions -- those remain governed by
// probe_hardware()/probe_process_resources() and MemoryBudgetManager.
SystemUtilizationSample probe_system_utilization(
    const std::uint32_t interval_milliseconds) {
    SystemUtilizationSample sample;
#if defined(_WIN32)
    const auto filetime_to_u64 = [](const FILETIME& time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) |
               static_cast<std::uint64_t>(time.dwLowDateTime);
    };
    FILETIME idle_before{}, kernel_before{}, user_before{};
    GetSystemTimes(&idle_before, &kernel_before, &user_before);
    IO_COUNTERS io_before{};
    GetProcessIoCounters(GetCurrentProcess(), &io_before);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(interval_milliseconds));
    FILETIME idle_after{}, kernel_after{}, user_after{};
    GetSystemTimes(&idle_after, &kernel_after, &user_after);
    IO_COUNTERS io_after{};
    GetProcessIoCounters(GetCurrentProcess(), &io_after);
    const std::uint64_t idle_delta =
        filetime_to_u64(idle_after) - filetime_to_u64(idle_before);
    const std::uint64_t total_delta =
        (filetime_to_u64(kernel_after) - filetime_to_u64(kernel_before)) +
        (filetime_to_u64(user_after) - filetime_to_u64(user_before));
    sample.cpu_percent =
        total_delta > 0U
            ? 100.0 * static_cast<double>(total_delta - idle_delta) /
                  static_cast<double>(total_delta)
            : 0.0;
    sample.disk_read_bytes = static_cast<std::uint64_t>(
        io_after.ReadTransferCount - io_before.ReadTransferCount);
    sample.disk_write_bytes = static_cast<std::uint64_t>(
        io_after.WriteTransferCount - io_before.WriteTransferCount);
#elif defined(__linux__)
    const auto read_cpu_totals = []() {
        std::array<std::uint64_t, 8> fields{};
        std::ifstream input("/proc/stat");
        std::string label;
        input >> label >> fields[0] >> fields[1] >> fields[2] >> fields[3] >>
            fields[4] >> fields[5] >> fields[6] >> fields[7];
        return fields;
    };
    const auto read_process_io = []() {
        std::uint64_t read_bytes = 0U;
        std::uint64_t write_bytes = 0U;
        std::ifstream input("/proc/self/io");
        std::string line;
        while (std::getline(input, line)) {
            if (line.rfind("read_bytes:", 0U) == 0U) {
                read_bytes = std::stoull(line.substr(11U));
            } else if (line.rfind("write_bytes:", 0U) == 0U) {
                write_bytes = std::stoull(line.substr(12U));
            }
        }
        return std::make_pair(read_bytes, write_bytes);
    };
    const auto cpu_before = read_cpu_totals();
    const auto io_before = read_process_io();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(interval_milliseconds));
    const auto cpu_after = read_cpu_totals();
    const auto io_after = read_process_io();
    std::uint64_t total_before = 0U;
    std::uint64_t total_after = 0U;
    for (const auto value : cpu_before) total_before += value;
    for (const auto value : cpu_after) total_after += value;
    const std::uint64_t idle_before = cpu_before[3] + cpu_before[4];
    const std::uint64_t idle_after = cpu_after[3] + cpu_after[4];
    const std::uint64_t total_delta = total_after - total_before;
    const std::uint64_t idle_delta = idle_after - idle_before;
    sample.cpu_percent =
        total_delta > 0U
            ? 100.0 * static_cast<double>(total_delta - idle_delta) /
                  static_cast<double>(total_delta)
            : 0.0;
    sample.disk_read_bytes =
        io_after.first >= io_before.first ? io_after.first - io_before.first : 0U;
    sample.disk_write_bytes = io_after.second >= io_before.second
                                  ? io_after.second - io_before.second
                                  : 0U;
#endif
    return sample;
}

}  // namespace masterai
