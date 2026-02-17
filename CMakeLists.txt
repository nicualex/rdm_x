cmake_minimum_required(VERSION 3.20)
project(RDM_X LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── FTDI D2XX (vendored) ────────────────────────────────────────────────
set(FTDI_DIR "${CMAKE_SOURCE_DIR}/thirdparty/ftdi")
add_library(ftd2xx STATIC IMPORTED)
set_target_properties(ftd2xx PROPERTIES
    IMPORTED_LOCATION "${FTDI_DIR}/ftd2xx.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${FTDI_DIR}"
)

# ── Core shared library (DLL) ───────────────────────────────────────────
set(CORE_SOURCES
    src/enttec_pro.cpp
    src/rdm.cpp
    src/parameter_loader.cpp
    src/validator.cpp
    src/rdm_x_api.cpp
)

add_library(rdm_x_core SHARED ${CORE_SOURCES})

target_include_directories(rdm_x_core PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${FTDI_DIR}
)

target_link_libraries(rdm_x_core PRIVATE
    ftd2xx
)

target_compile_definitions(rdm_x_core PRIVATE RDX_EXPORTS)

# Copy CSV data file next to DLL
configure_file(
    "${CMAKE_SOURCE_DIR}/docs/Vaya_RDM_map.csv"
    "${CMAKE_BINARY_DIR}/Vaya_RDM_map.csv"
    COPYONLY
)
