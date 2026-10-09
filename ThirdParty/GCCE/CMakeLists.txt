cmake_minimum_required(VERSION 3.20)
project(gcce VERSION 0.1.0 LANGUAGES CXX)

option(GCCE_BUILD_SHARED "Build the GC runtime as a shared library (DLL)" OFF)
option(GCCE_BUILD_TESTS "Build the test suite" ON)
set(GCCE_DEBUG_CHECKS AUTO CACHE STRING "Owner-thread and misuse checks: AUTO (on unless NDEBUG), ON or OFF")
set_property(CACHE GCCE_DEBUG_CHECKS PROPERTY STRINGS AUTO ON OFF)
option(GCCE_BUILD_BENCHMARKS "Build the heap comparison benchmark" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep executables next to the runtime DLL on Windows.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(GCCE_BUILD_SHARED)
    add_library(gcce SHARED src/domain.cpp src/block_allocator.cpp)
    target_compile_definitions(gcce PUBLIC GCCE_SHARED PRIVATE GCCE_BUILDING)
    set_target_properties(gcce PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
else()
    add_library(gcce STATIC src/domain.cpp src/block_allocator.cpp)
endif()
add_library(gcce::gcce ALIAS gcce)

target_compile_features(gcce PUBLIC cxx_std_23)
target_include_directories(gcce PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gcce PRIVATE ${CMAKE_DL_LIBS})
if(GCCE_DEBUG_CHECKS STREQUAL "ON")
    target_compile_definitions(gcce PUBLIC GC_DEBUG_CHECKS=1)
elseif(GCCE_DEBUG_CHECKS STREQUAL "OFF")
    target_compile_definitions(gcce PUBLIC GC_DEBUG_CHECKS=0)
endif()

if(MSVC)
    target_compile_options(gcce PRIVATE /W4 /permissive-)
else()
    target_compile_options(gcce PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(GCCE_BUILD_BENCHMARKS)
    add_executable(gcce_bench bench/gcce_bench.cpp)
    target_link_libraries(gcce_bench PRIVATE gcce::gcce)
    if(WIN32)
        target_link_libraries(gcce_bench PRIVATE psapi)
    endif()

    # Builds the allocator source directly: it is internal to the library.
    add_executable(gcce_allocator_bench bench/allocator_bench.cpp src/block_allocator.cpp)
    target_include_directories(gcce_allocator_bench PRIVATE src include)
endif()

if(GCCE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
