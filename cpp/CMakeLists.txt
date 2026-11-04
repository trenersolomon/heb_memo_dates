cmake_minimum_required(VERSION 3.23)
project(cppbridge LANGUAGES CXX)


find_package(Qt6 REQUIRED COMPONENTS Core Qml)
qt_standard_project_setup(REQUIRES 6.5)

# libhdate: located manually (header + library) instead of via pkg-config so it
# works regardless of whether the distro package ships a .pc file.
find_path(HDATE_INCLUDE_DIR hdate.h REQUIRED)
find_library(HDATE_LIBRARY NAMES hdate REQUIRED)

set(CPP_MODULE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../package/contents/ui/cppbridge")

qt_add_qml_module(cppbridge
    URI cppbridge
    VERSION 1.0
    OUTPUT_DIRECTORY "${CPP_MODULE_OUTPUT_DIR}"
    SOURCES
        src/memobridge.h
        src/memobridge.cpp
        src/translator.h
        src/translator.cpp
        src/csv.h
)

target_include_directories(cppbridge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${HDATE_INCLUDE_DIR}"
)
target_link_libraries(cppbridge PRIVATE Qt6::Core Qt6::Qml "${HDATE_LIBRARY}")
set_target_properties(cppbridge PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

set_target_properties(cppbridge cppbridgeplugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CPP_MODULE_OUTPUT_DIR}"
    BUILD_WITH_INSTALL_RPATH ON
    INSTALL_RPATH "$ORIGIN"
)
