cmake_minimum_required(VERSION 3.15)

project(libvdi
    VERSION 1.0.0
    DESCRIPTION "Lightweight standalone VirtualBox Disk Image (VDI) library derived from QEMU"
    HOMEPAGE_URL "https://github.com/qemu/qemu"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

# Options
option(LIBVDI_BUILD_TESTS    "Build test suite" OFF)
option(LIBVDI_BUILD_EXAMPLES "Build example programs" OFF)
option(BUILD_SHARED_LIBS     "Build shared library instead of static" OFF)

if (MSVC OR WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Core library target
add_library(vdi
    src/libvdi.c
    src/vdi_internal.h
    include/libvdi.h
    include/libvdi.hpp
    include/vdi.hpp
)

add_library(libvdi::vdi ALIAS vdi)

set_target_properties(vdi PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    PUBLIC_HEADER "include/libvdi.h;include/libvdi.hpp;include/vdi.hpp"
)

target_include_directories(vdi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Installation targets
install(TARGETS vdi
    EXPORT libvdiTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT libvdiTargets
    FILE libvdiTargets.cmake
    NAMESPACE libvdi::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libvdi
)

# Optional Examples
if (LIBVDI_BUILD_EXAMPLES)
    add_executable(example_c_ram examples/c_ram_disk.c)
    target_link_libraries(example_c_ram PRIVATE vdi)

    add_executable(example_cpp_disk examples/cpp_file_disk.cpp)
    target_link_libraries(example_cpp_disk PRIVATE vdi)
endif()

# Optional Tests
if (LIBVDI_BUILD_TESTS)
    enable_testing()
    add_executable(vdi_tests tests/test_vdi.cpp)
    target_link_libraries(vdi_tests PRIVATE vdi)
    add_test(NAME CoreVdiTests COMMAND vdi_tests)
endif()
