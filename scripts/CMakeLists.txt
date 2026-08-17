cmake_minimum_required(VERSION 3.20)

project(MasterAI VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD STREQUAL "17" AND DEFINED CMAKE_CXX_STANDARD)
    message(FATAL_ERROR "MasterAI requires exactly ISO C++17.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MASTERAI_BUILD_TESTS "Build MasterAI validation tests" ON)

add_library(masterai_core STATIC
    ../src/json.cpp
    ../src/config.cpp
    ../src/platform.cpp
    ../src/async_storage.cpp
    ../src/shared_buffer.cpp
    ../src/security.cpp
    ../src/storage.cpp
    ../src/identity.cpp
    ../src/models.cpp
    ../src/workflows.cpp
    ../src/inference.cpp
    ../src/downloads.cpp
    ../src/benchmarks.cpp
    ../src/mcp.cpp
    ../src/mcp_outbound.cpp
    ../src/mcp_outbound_service.cpp
    ../src/tool_exec.cpp
    ../src/ide.cpp
    ../src/operations.cpp
    ../src/operations_cli.cpp
    ../src/performance.cpp
    ../src/query.cpp
    ../src/memory.cpp
    ../src/indexing.cpp
    ../src/project_watcher.cpp
    ../src/retrieval.cpp
    ../src/cache.cpp
    ../src/session_cache.cpp
    ../src/prompt_assembly.cpp
    ../src/calibration.cpp
    ../src/optimization_registry.cpp
    ../src/scheduler.cpp
    ../src/kv_cache.cpp
    ../src/topology.cpp
    ../src/model_routing.cpp
    ../src/scratch_storage.cpp
    ../src/runner_pool.cpp
    ../src/performance_cli.cpp
    ../src/ml.cpp
    ../src/ml_engine.cpp
    ../src/ml_knowledge.cpp
    ../src/ml_finetune.cpp
    ../src/ml_safety_scan.cpp
    ../src/telemetry_agent.cpp
    ../src/inference_metrics.cpp
    ../src/parquet_bridge.cpp
    ../src/http_support.cpp
    ../src/integration_http.cpp
    ../src/web_ui.cpp
    ../src/workload_http.cpp
    ../src/server.cpp
    ../src/gpu_vendor.cpp
    ../src/intranet_worker.cpp
    ../src/adaptive_controller.cpp
    ../src/speculative_decoding.cpp
    ../src/regression_gate.cpp
)

target_include_directories(masterai_core PUBLIC ../src)

# Phase 33 (INTRANET-WORKER slice): OpenSSL is this codebase's ONE vendored
# TLS/crypto dependency, used only by src/intranet_worker.cpp for the
# optional mutual-TLS intranet worker protocol -- every other network path
# in this codebase (the loopback administrator HTTP server, the loopback
# runner IPC, outbound MCP) is either loopback-only or explicitly fail-
# closed without it (see mcp_outbound.cpp's http_exchange() comment).
# Optional, not REQUIRED: a build without OpenSSL development files still
# succeeds, matching the PAM-optional pattern below -- IntranetWorkerPool/
# WorkerListener/issue_worker_certificate() all fail closed at runtime with
# a clear "not available in this build" error (see intranet_worker.cpp's
# reject_no_openssl()) instead of the whole build refusing to configure.
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_compile_definitions(masterai_core PUBLIC MASTERAI_HAS_OPENSSL=1)
    target_link_libraries(masterai_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "Intranet worker mutual TLS: OpenSSL found, Phase 33 enabled")
else()
    message(WARNING "OpenSSL development files unavailable: Phase 33 "
                    "intranet worker mTLS fails closed at runtime")
endif()

if(MSVC)
    target_compile_options(masterai_core PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_definitions(masterai_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(masterai_core PUBLIC bcrypt crypt32 ws2_32 advapi32 psapi dxgi)

    # AMD ADLX (Phase 19 GPU utilization/thermal probing): vendored headers
    # plus its own helper source under third_party/ADLX. ADLXHelper.cpp
    # dynamically loads the driver-installed ADLX DLL itself at runtime, so
    # this compiles against public interface headers only -- no AMD binary
    # is linked. See docs/architecture/ADR-0001 for the vendor-SDK exception.
    # WinAPIs.cpp supplies ADLXHelper.cpp's adlx_load_library/
    # adlx_free_library/adlx_get_proc_address (thin LoadLibraryA/
    # GetProcAddress/FreeLibrary wrappers) -- without it the link fails with
    # unresolved externals, since ADLXHelper.cpp only declares, never
    # defines, that platform boundary.
    target_sources(masterai_core PRIVATE
        ../third_party/ADLX/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.cpp
        ../third_party/ADLX/SDK/Platform/Windows/WinAPIs.cpp)
    target_include_directories(masterai_core PRIVATE
        ../third_party/ADLX/SDK/Include)
else()
    target_compile_options(masterai_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_link_libraries(masterai_core PUBLIC pthread dl)
    find_path(MASTERAI_PAM_INCLUDE_DIR security/pam_appl.h)
    find_library(MASTERAI_PAM_LIBRARY pam)
    if(MASTERAI_PAM_INCLUDE_DIR AND MASTERAI_PAM_LIBRARY)
        target_compile_definitions(masterai_core PUBLIC MASTERAI_HAS_PAM=1)
        target_include_directories(masterai_core PRIVATE ${MASTERAI_PAM_INCLUDE_DIR})
        target_link_libraries(masterai_core PUBLIC ${MASTERAI_PAM_LIBRARY})
    else()
        message(WARNING "PAM development files unavailable: OS identity fails closed")
    endif()
endif()

message(STATUS "Optional external inference backend: llama.cpp")
message(STATUS "Password authentication: native OS-backed identity; no password storage")

add_executable(masterai ../src/main.cpp)
target_link_libraries(masterai PRIVATE masterai_core)

if(MASTERAI_BUILD_TESTS)
    enable_testing()
    add_executable(masterai_fake_llama ../test/fake_llama_server.cpp)
    if(WIN32)
        target_link_libraries(masterai_fake_llama PRIVATE ws2_32)
    endif()
    add_executable(masterai_fake_mcp ../test/fake_mcp_server.cpp)
    add_executable(masterai_fake_curl ../test/fake_curl.cpp)
    add_executable(masterai_fake_duckdb ../test/fake_duckdb.cpp)
    add_executable(masterai_tests ../test/tests.cpp)
    target_link_libraries(masterai_tests PRIVATE masterai_core)
    add_test(NAME masterai_core_tests COMMAND masterai_tests)
endif()

set_target_properties(masterai masterai_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
