cmake_minimum_required(VERSION 3.20)
project(RDM_X_Tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Google Test via FetchContent ─────────────────────────────────────────
include(FetchContent)

cmake_policy(SET CMP0135 NEW)   # suppress DOWNLOAD_EXTRACT_TIMESTAMP warning

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Keep GTest on the same static runtime (/MT) as the rest of the project
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── FTDI imported library (mirrors root CMakeLists.txt) ──────────────────
set(FTDI_DIR "${CMAKE_SOURCE_DIR}/thirdparty/ftdi")
add_library(ftd2xx_tests STATIC IMPORTED)
set_target_properties(ftd2xx_tests PROPERTIES
    IMPORTED_LOCATION             "${FTDI_DIR}/ftd2xx.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${FTDI_DIR}"
)

# ── Source files compiled into every test executable ────────────────────
# rdm_x_api.cpp is intentionally excluded — it owns global hardware
# singletons (g_enttec / g_peperoni) that conflict with test isolation.
set(CORE_TEST_SRCS
    ${CMAKE_SOURCE_DIR}/src/rdm.cpp
    ${CMAKE_SOURCE_DIR}/src/enttec_pro.cpp
    ${CMAKE_SOURCE_DIR}/src/peperoni_rodin.cpp
    ${CMAKE_SOURCE_DIR}/src/validator.cpp
    ${CMAKE_SOURCE_DIR}/src/parameter_loader.cpp
)

# ── Helper macro: create a test target with common settings ──────────────
macro(add_rdm_test target_name source_file)
    add_executable(${target_name} ${source_file} ${CORE_TEST_SRCS})
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${FTDI_DIR}
    )
    target_link_libraries(${target_name} PRIVATE
        GTest::gtest_main
        ftd2xx_tests
    )
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W3)
        set_property(TARGET ${target_name} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    include(GoogleTest)
    gtest_discover_tests(${target_name})
endmacro()

# ── Test executables ─────────────────────────────────────────────────────
add_rdm_test(rdm_core_tests          test_rdm_core.cpp)
add_rdm_test(parameter_loader_tests  test_parameter_loader.cpp)
