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
    ../src/ide.cpp
    ../src/operations.cpp
    ../src/operations_cli.cpp
    ../src/performance.cpp
    ../src/query.cpp
    ../src/memory.cpp
    ../src/indexing.cpp
    ../src/performance_cli.cpp
    ../src/http_support.cpp
    ../src/integration_http.cpp
    ../src/web_ui.cpp
    ../src/workload_http.cpp
    ../src/server.cpp
)

target_include_directories(masterai_core PUBLIC ../src)

if(MSVC)
    target_compile_options(masterai_core PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_compile_definitions(masterai_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(masterai_core PUBLIC bcrypt crypt32 ws2_32 advapi32 psapi)
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

message(STATUS "Third-party coding foundations: prohibited")
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
    add_executable(masterai_tests ../test/tests.cpp)
    target_link_libraries(masterai_tests PRIVATE masterai_core)
    add_test(NAME masterai_core_tests COMMAND masterai_tests)
endif()

set_target_properties(masterai masterai_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
