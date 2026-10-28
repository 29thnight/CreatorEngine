cmake_minimum_required(VERSION 3.25)
project(CreatorScriptReflectionProbe LANGUAGES CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
find_program(DOTNET_EXECUTABLE NAMES dotnet REQUIRED)
set(REFLGEN_EXECUTABLE "" CACHE FILEPATH "Built reflgen generator")
set(REFLGEN_INCLUDE_DIR "" CACHE PATH "Matching reflgen include directory")
if(NOT EXISTS "${REFLGEN_EXECUTABLE}" OR NOT EXISTS "${REFLGEN_INCLUDE_DIR}/reflgen/runtime/value_descriptor.h")
    message(FATAL_ERROR "Provide REFLGEN_EXECUTABLE and matching REFLGEN_INCLUDE_DIR with runtime value descriptors")
endif()
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(table "${CMAKE_CURRENT_BINARY_DIR}/table")
set(math_include "${ENGINE_ROOT}/ThirdParty/Mathematics/include")
set(clang_arguments -std=c++23 -DCE_CONTRACT_STANDALONE=1 "-I${REFLGEN_INCLUDE_DIR}" "-I${math_include}")
set(table_arguments -std=c++20)
if(WIN32)
    list(APPEND clang_arguments -target x86_64-pc-windows-msvc)
    list(APPEND table_arguments -target x86_64-pc-windows-msvc)
else()
    list(APPEND table_arguments -D__stdcall= -D__cdecl=)
endif()

add_custom_command(
    OUTPUT "${generated}/reflgen_script_reflection_probe.h" "${generated}/reflgen_script_reflection_probe.cpp" "${generated}/types.reflgen.h"
    COMMAND "${REFLGEN_EXECUTABLE}" --module script_reflection_probe --output "${generated}"
        --registration-header "${CMAKE_CURRENT_SOURCE_DIR}/types.h" "${CMAKE_CURRENT_SOURCE_DIR}/types.h" -- ${clang_arguments}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/types.h" "${REFLGEN_EXECUTABLE}"
    VERBATIM)
add_custom_command(
    OUTPUT "${table}/reflgen_creator_script_native.cpp" "${table}/reflgen_creator_script_native.fs" "${table}/reflgen_creator_script_native.json"
    COMMAND "${REFLGEN_EXECUTABLE}" --module creator_script --output "${table}" --fsharp-table
        --fsharp-namespace CreatorEngine.Native "${ENGINE_ROOT}/Engine/SceneRuntime/ScriptApiTable.h" -- ${table_arguments}
    DEPENDS "${REFLGEN_EXECUTABLE}" "${ENGINE_ROOT}/Engine/SceneRuntime/ScriptApiTable.h"
        "${ENGINE_ROOT}/Engine/SceneRuntime/ScriptBindingValues.h"
        "${ENGINE_ROOT}/Engine/SceneRuntime/ScriptPhysicsValues.h"
        "${ENGINE_ROOT}/Engine/SceneRuntime/SimulationExchangeABI.h"
        "${ENGINE_ROOT}/Engine/Utility_Framework/Runtime/ScriptApiVersion.h"
    VERBATIM)
add_library(ScriptReflectionProvider SHARED native.cpp "${generated}/reflgen_script_reflection_probe.cpp" "${table}/reflgen_creator_script_native.cpp")
target_compile_features(ScriptReflectionProvider PRIVATE cxx_std_23)
target_include_directories(ScriptReflectionProvider PRIVATE "${REFLGEN_INCLUDE_DIR}" "${generated}" "${math_include}")
target_compile_definitions(ScriptReflectionProvider PRIVATE CE_CONTRACT_STANDALONE=1)
target_link_libraries(ScriptReflectionProvider PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(ScriptReflectionProvider PRIVATE /utf-8 /EHsc /bigobj /permissive- /Zc:__cplusplus /wd5030)
else()
    target_compile_definitions(ScriptReflectionProvider PRIVATE __stdcall= __cdecl=)
    target_compile_options(ScriptReflectionProvider PRIVATE -Wno-attributes)
endif()

add_test(NAME script_reflection_component_inventory
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../verify-native-reflection-inventory.py" --engine "${ENGINE_ROOT}")
add_test(NAME script_reflection_fsharp
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_probe.py"
        --dotnet "${DOTNET_EXECUTABLE}" --generator "${REFLGEN_EXECUTABLE}"
        --library "$<TARGET_FILE:ScriptReflectionProvider>" --configuration "$<CONFIG>"
        --work "${CMAKE_CURRENT_BINARY_DIR}/managed/$<CONFIG>")
set_tests_properties(script_reflection_fsharp PROPERTIES TIMEOUT 180)
