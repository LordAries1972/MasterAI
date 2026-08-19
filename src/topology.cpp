// Phase 28: hardware topology probing and thread-placement recommendation.
// See the scope note on the declarations in masterai.hpp -- real probing,
// a pure recommendation function, and a real-but-unused-by-default Windows
// NUMA-pinning primitive; no code path in this pass actually applies it.
#include "masterai.hpp"

#include <thread>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <filesystem>
#endif

namespace masterai {

std::string to_string(ThreadClass klass) {
    switch (klass) {
        case ThreadClass::inference_compute:
            return "inference_compute";
        case ThreadClass::http_streaming:
            return "http_streaming";
        case ThreadClass::retrieval:
            return "retrieval";
        case ThreadClass::indexing:
            return "indexing";
        case ThreadClass::storage_completion:
            return "storage_completion";
        case ThreadClass::download:
            return "download";
        case ThreadClass::background_maintenance:
            return "background_maintenance";
    }
    return "unknown";
}

namespace {

bool is_latency_sensitive(ThreadClass klass) {
    return klass == ThreadClass::inference_compute ||
          klass == ThreadClass::http_streaming ||
          klass == ThreadClass::retrieval;
}

}  // namespace

#if defined(_WIN32)

HardwareTopology probe_hardware_topology() {
    HardwareTopology topology;
    topology.logical_core_count = std::max(1U, std::thread::hardware_concurrency());

    DWORD bytes = 0U;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &bytes);
    if (bytes == 0U) {
        topology.physical_core_count = topology.logical_core_count;
        return topology;
    }
    std::vector<unsigned char> buffer(bytes);
    if (GetLogicalProcessorInformationEx(
            RelationAll,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                buffer.data()),
            &bytes) == 0) {
        topology.physical_core_count = topology.logical_core_count;
        return topology;
    }

    unsigned int packages = 0U;
    unsigned int physical_cores = 0U;
    unsigned int performance_cores = 0U;
    unsigned int efficiency_cores = 0U;
    bool saw_efficiency_class = false;
    unsigned int highest_efficiency_class = 0U;
    std::vector<unsigned int> numa_node_ids;

    std::size_t offset = 0U;
    while (offset < bytes) {
        const auto* entry =
            reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                buffer.data() + offset);
        if (entry->Size == 0U) break;
        switch (entry->Relationship) {
            case RelationProcessorPackage:
                ++packages;
                break;
            case RelationProcessorCore: {
                ++physical_cores;
                const auto efficiency_class =
                    entry->Processor.EfficiencyClass;
                if (efficiency_class > 0U) saw_efficiency_class = true;
                if (efficiency_class >= highest_efficiency_class) {
                    highest_efficiency_class = efficiency_class;
                }
                break;
            }
            case RelationNumaNode:
                numa_node_ids.push_back(entry->NumaNode.NodeNumber);
                break;
            default:
                break;
        }
        offset += entry->Size;
    }

    // A second pass now that the highest efficiency class is known: cores
    // at the highest class are "performance", everything below is
    // "efficiency" -- Windows does not otherwise label which class means
    // which, only that a higher EfficiencyClass value is more capable.
    if (saw_efficiency_class) {
        offset = 0U;
        while (offset < bytes) {
            const auto* entry =
                reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                    buffer.data() + offset);
            if (entry->Size == 0U) break;
            if (entry->Relationship == RelationProcessorCore) {
                if (entry->Processor.EfficiencyClass == highest_efficiency_class) {
                    ++performance_cores;
                } else {
                    ++efficiency_cores;
                }
            }
            offset += entry->Size;
        }
    }

    topology.package_count = std::max(1U, packages);
    topology.physical_core_count =
        physical_cores > 0U ? physical_cores : topology.logical_core_count;
    topology.hybrid_cores = saw_efficiency_class && efficiency_cores > 0U &&
                            performance_cores > 0U;
    topology.performance_core_count = topology.hybrid_cores ? performance_cores : 0U;
    topology.efficiency_core_count = topology.hybrid_cores ? efficiency_cores : 0U;

    for (const auto node_id : numa_node_ids) {
        NumaNodeInfo node;
        node.numa_node_id = node_id;
        // Logical-processor-per-node counting would require walking each
        // node's GroupMask array against per-group active counts; not
        // needed for the placement decision this phase makes (node
        // selection only), so it stays 0 -- an honest "not computed" rather
        // than a fabricated even split across nodes.
        node.logical_processor_count = 0U;
        topology.numa_nodes.push_back(node);
    }
    topology.numa_node_count = std::max<unsigned int>(
        1U, static_cast<unsigned int>(topology.numa_nodes.size()));
    topology.multi_node = topology.numa_node_count > 1U;

    const auto group_count = GetActiveProcessorGroupCount();
    for (WORD group = 0U; group < group_count; ++group) {
        ProcessorGroupInfo info;
        info.group_id = group;
        info.processor_count = GetActiveProcessorCount(group);
        topology.processor_groups.push_back(info);
    }

    return topology;
}

bool apply_current_thread_to_numa_node(unsigned int numa_node_id) {
    GROUP_AFFINITY affinity{};
    if (GetNumaNodeProcessorMaskEx(static_cast<USHORT>(numa_node_id),
                                   &affinity) == 0) {
        return false;
    }
    return SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != 0;
}

bool probe_on_battery_power() {
    SYSTEM_POWER_STATUS status{};
    if (GetSystemPowerStatus(&status) == 0) {
        return false;
    }
    // ACLineStatus: 0 = offline (on battery), 1 = online, 255 = unknown.
    return status.ACLineStatus == 0;
}

#else  // !_WIN32

// Linux topology probing is intentionally minimal: only the Windows target
// is validated/built for this project (docs/PLAN.md build-scope
// convention); a single-node, no-hybrid-split fallback keeps every
// recommendation honestly "not applicable" rather than fabricated.
HardwareTopology probe_hardware_topology() {
    HardwareTopology topology;
    topology.logical_core_count = std::max(1U, std::thread::hardware_concurrency());
    topology.physical_core_count = topology.logical_core_count;
    unsigned int node_count = 1U;
    std::error_code error;
    if (std::filesystem::exists("/sys/devices/system/node", error)) {
        unsigned int found = 0U;
        for (const auto& entry : std::filesystem::directory_iterator(
                 "/sys/devices/system/node", error)) {
            if (entry.path().filename().string().rfind("node", 0U) == 0U) {
                ++found;
            }
        }
        if (found > 0U) node_count = found;
    }
    topology.numa_node_count = node_count;
    topology.multi_node = node_count > 1U;
    for (unsigned int i = 0U; i < node_count; ++i) {
        NumaNodeInfo node;
        node.numa_node_id = i;
        topology.numa_nodes.push_back(node);
    }
    return topology;
}

bool apply_current_thread_to_numa_node(unsigned int) { return false; }

bool probe_on_battery_power() { return false; }

#endif

ThreadPlacementRecommendation recommend_thread_placement(
    const HardwareTopology& topology, ThreadClass klass,
    const TopologyAffinityPolicy& policy, bool on_battery_power) {
    ThreadPlacementRecommendation result;

    if (!topology.multi_node || !policy.numa_local_placement_enabled) {
        result.reason = !topology.multi_node
                            ? "single-node host: normal OS scheduling applies"
                            : "NUMA-local placement disabled by policy";
    } else {
        // Deterministic placement (node 0) absent live per-node measurement:
        // a real allocator/scheduler would prefer the node already hosting
        // the model's mapped weights, but no caller in this pass measures
        // that -- this is intentionally the simplest safe default rather
        // than a fabricated load-balancing heuristic.
        result.preferred_numa_node = 0U;
        result.reason = "multi-node host with NUMA-local placement enabled";
    }

    if (!topology.hybrid_cores || !policy.hybrid_core_policy_enabled) {
        result.prefer_efficiency_core = false;
        if (!topology.hybrid_cores) {
            result.reason += "; no hybrid core split detected";
        }
        return result;
    }

    if (is_latency_sensitive(klass)) {
        // Latency-sensitive classes stay on performance cores even on
        // battery power -- the plan re-evaluates hybrid policy on battery,
        // it does not mandate throttling interactive inference.
        result.prefer_efficiency_core = false;
        result.reason += "; latency-sensitive class kept on performance cores";
    } else {
        result.prefer_efficiency_core = true;
        result.reason += on_battery_power
                             ? "; background class moved to efficiency cores "
                               "on battery power"
                             : "; background class prefers efficiency cores";
    }
    return result;
}

std::string hardware_topology_json(const HardwareTopology& topology) {
    std::string groups = "[";
    bool first = true;
    for (const auto& group : topology.processor_groups) {
        if (!first) groups += ",";
        first = false;
        groups += "{\"groupId\":" + std::to_string(group.group_id) +
                  ",\"processorCount\":" + std::to_string(group.processor_count) +
                  "}";
    }
    groups += "]";
    std::string nodes = "[";
    first = true;
    for (const auto& node : topology.numa_nodes) {
        if (!first) nodes += ",";
        first = false;
        nodes += "{\"numaNodeId\":" + std::to_string(node.numa_node_id) +
                 ",\"logicalProcessorCount\":" +
                 std::to_string(node.logical_processor_count) + "}";
    }
    nodes += "]";
    return "{\"packageCount\":" + std::to_string(topology.package_count) +
          ",\"numaNodeCount\":" + std::to_string(topology.numa_node_count) +
          ",\"physicalCoreCount\":" + std::to_string(topology.physical_core_count) +
          ",\"logicalCoreCount\":" + std::to_string(topology.logical_core_count) +
          ",\"performanceCoreCount\":" +
          std::to_string(topology.performance_core_count) +
          ",\"efficiencyCoreCount\":" +
          std::to_string(topology.efficiency_core_count) +
          ",\"multiNode\":" + (topology.multi_node ? "true" : "false") +
          ",\"hybridCores\":" + (topology.hybrid_cores ? "true" : "false") +
          ",\"processorGroups\":" + groups + ",\"numaNodes\":" + nodes + "}";
}

}  // namespace masterai
