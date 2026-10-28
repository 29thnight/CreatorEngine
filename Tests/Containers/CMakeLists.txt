cmake_minimum_required(VERSION 3.28)
project(CreatorContainerTests LANGUAGES CXX)
get_filename_component(CE_IMPLEMENTATION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/Containers" ABSOLUTE)
find_package(Threads REQUIRED)
option(CE_ASAN "Enable AddressSanitizer" OFF)
enable_testing()
add_library(creator_containers_headers INTERFACE)
target_include_directories(creator_containers_headers INTERFACE "${CE_IMPLEMENTATION_ROOT}/include")
target_compile_features(creator_containers_headers INTERFACE cxx_std_23)
function(container_header_test name source)
  add_executable(${name} "tests/${source}")
  target_link_libraries(${name} PRIVATE creator_containers_headers Threads::Threads)
  if(MSVC)
    target_compile_options(${name} PRIVATE /permissive- /EHsc /utf-8)
    if(CE_ASAN)
      target_compile_options(${name} PRIVATE /fsanitize=address /Zi)
      target_link_options(${name} PRIVATE /INCREMENTAL:NO /DEBUG)
    endif()
  elseif(CE_ASAN)
    target_compile_options(${name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME ${name} COMMAND ${name} ${ARGN})
endfunction()
container_header_test(container_tests main.cpp --test)
container_header_test(container_library_tests library.cpp)
container_header_test(container_node_map_tests node_map_migration.cpp)
container_header_test(container_bounded_mpsc_tests bounded_mpsc.cpp)
container_header_test(container_concurrent_tests concurrent.cpp --test)
container_header_test(container_lock_free_tests lock_free.cpp --test)
target_compile_definitions(container_lock_free_tests PRIVATE CE_USE_FAA=1)
container_header_test(container_alignment_tests allocation_alignment.cpp)

container_header_test(container_array_migration_tests array_migration.cpp)
container_header_test(container_fixed_bitset_tests fixed_bitset_migration.cpp)
container_header_test(container_node_map_advanced_tests node_map_advanced.cpp)
set(container_multi_sources)
foreach(source multi_tu.cpp bridge_a.cpp bridge_b.cpp)
  file(READ "${PROJECT_SOURCE_DIR}/tests/modules/${source}" text)
  string(REPLACE "import Creator.Containers;" "#include <ce/all.h>" text "${text}")
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/header_${source}")
  file(WRITE "${generated}" "${text}")
  list(APPEND container_multi_sources "${generated}")
endforeach()

# String ownership and Unicode boundaries must remain part of the ordinary
# container gate, rather than unreferenced one-off probe sources.
container_header_test(container_unicode_tests unicode.cpp)
container_header_test(container_string_storage_tests ../../../Tools/regression/string_storage_contract_probe.cpp)
target_compile_definitions(container_string_storage_tests PRIVATE
  CE_CONTRACT_STANDALONE=1 CE_STRING_STORAGE_STANDALONE=1)
container_header_test(container_diagnostic_string_tests ../../../Tools/regression/diagnostic_string_migration_probe.cpp)
target_sources(container_diagnostic_string_tests PRIVATE
  ../../Engine/Utility_Framework/Authoring/DiagnosticDocument.cpp)
target_compile_definitions(container_diagnostic_string_tests PRIVATE CE_CONTRACT_STANDALONE=1)
container_header_test(container_workspace_string_tests ../../../Tools/regression/editor_workspace_string_probe.cpp
  "${PROJECT_SOURCE_DIR}/../../Tools/regression/fixtures/imgui-ini")
target_sources(container_workspace_string_tests PRIVATE ../../Editor/EngineGUIWindow/EditorWorkspaceFile.cpp)
target_compile_definitions(container_workspace_string_tests PRIVATE CE_CONTRACT_STANDALONE=1)
target_include_directories(container_workspace_string_tests PRIVATE ../../Editor/ImGuiHelper)
container_header_test(container_catalog_string_tests ../../../Tools/regression/editor_component_catalog_probe.cpp)
target_compile_definitions(container_catalog_string_tests PRIVATE CE_CONTRACT_STANDALONE=1)

# Supply the pinned ImGui source tree to exercise the actual widgets without a
# renderer/backend. Match vcpkg's wchar32 ABI on the library and its consumers.
set(CE_STRING_IMGUI_SOURCE_DIR "" CACHE PATH "ImGui source directory for string input contracts")
if(CE_STRING_IMGUI_SOURCE_DIR)
  add_library(creator_string_imgui STATIC
    "${CE_STRING_IMGUI_SOURCE_DIR}/imgui.cpp"
    "${CE_STRING_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${CE_STRING_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${CE_STRING_IMGUI_SOURCE_DIR}/imgui_widgets.cpp")
  target_include_directories(creator_string_imgui PUBLIC "${CE_STRING_IMGUI_SOURCE_DIR}")
  target_compile_definitions(creator_string_imgui PUBLIC IMGUI_USE_WCHAR32)
  if(CE_ASAN AND NOT MSVC)
    target_compile_options(creator_string_imgui PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
  container_header_test(container_string_input_tests ../../../Tools/regression/string_input_probe.cpp)
  target_link_libraries(container_string_input_tests PRIVATE creator_string_imgui)
  container_header_test(container_reflected_string_input_tests ../../../Tools/regression/reflected_string_input_probe.cpp)
  target_compile_definitions(container_reflected_string_input_tests PRIVATE CE_CONTRACT_STANDALONE=1)
  target_link_libraries(container_reflected_string_input_tests PRIVATE creator_string_imgui)
  set(CE_STRING_NODE_EDITOR_SOURCE_DIR "" CACHE PATH "vcpkg-patched imgui-node-editor source for node string contracts")
  if(CE_STRING_NODE_EDITOR_SOURCE_DIR)
    add_library(creator_string_node_editor STATIC
      "${CE_STRING_NODE_EDITOR_SOURCE_DIR}/imgui_node_editor.cpp"
      "${CE_STRING_NODE_EDITOR_SOURCE_DIR}/imgui_node_editor_api.cpp"
      "${CE_STRING_NODE_EDITOR_SOURCE_DIR}/imgui_canvas.cpp"
      "${CE_STRING_NODE_EDITOR_SOURCE_DIR}/crude_json.cpp")
    target_include_directories(creator_string_node_editor PUBLIC "${CE_STRING_NODE_EDITOR_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/node-editor-include")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/node-editor-include/imgui-node-editor")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/node-editor-include/imgui-node-editor/imgui_node_editor.h"
      "#include <imgui_node_editor.h>\n")
    target_link_libraries(creator_string_node_editor PUBLIC creator_string_imgui)
    target_compile_features(creator_string_node_editor PUBLIC cxx_std_23)
    if(MSVC)
      target_compile_options(creator_string_node_editor PRIVATE /utf-8 /EHsc)
    elseif(CE_ASAN)
      target_compile_options(creator_string_node_editor PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
    container_header_test(container_node_editor_string_tests ../../../Tools/regression/node_editor_string_probe.cpp
      "${CMAKE_CURRENT_BINARY_DIR}/string-node-settings")
    target_sources(container_node_editor_string_tests PRIVATE ../../Editor/ImGuiHelper/NodeEditor.cpp)
    target_compile_definitions(container_node_editor_string_tests PRIVATE CE_CONTRACT_STANDALONE=1)
    target_link_libraries(container_node_editor_string_tests PRIVATE creator_string_node_editor)
  endif()
endif()
set(CE_STRING_SPDLOG_INCLUDE_DIR "" CACHE PATH "spdlog include directory for log string contracts")
set(CE_STRING_FMT_INCLUDE_DIR "" CACHE PATH "fmt include directory for log string contracts")
if(CE_STRING_SPDLOG_INCLUDE_DIR AND CE_STRING_FMT_INCLUDE_DIR)
  container_header_test(container_log_string_tests ../../../Tools/regression/log_store_probe.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/string-log.html")
  target_include_directories(container_log_string_tests PRIVATE
    "${CE_STRING_SPDLOG_INCLUDE_DIR}" "${CE_STRING_FMT_INCLUDE_DIR}" ../../Editor/EngineGUIWindow)
  target_compile_definitions(container_log_string_tests PRIVATE
    CE_CONTRACT_STANDALONE=1 CE_LOG_STORAGE_PORTABLE=1 FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL)
endif()

set(CE_STRING_RYML_SOURCE_DIR "" CACHE PATH "rapidyaml source directory with c4core submodule for authoring contracts")
if(CE_STRING_RYML_SOURCE_DIR)
  file(GLOB string_ryml_sources CONFIGURE_DEPENDS
    "${CE_STRING_RYML_SOURCE_DIR}/src/c4/yml/*.cpp"
    "${CE_STRING_RYML_SOURCE_DIR}/ext/c4core.src/c4/*.cpp")
  add_library(creator_string_ryml STATIC ${string_ryml_sources})
  target_compile_features(creator_string_ryml PUBLIC cxx_std_23)
  target_include_directories(creator_string_ryml PUBLIC
    "${CE_STRING_RYML_SOURCE_DIR}/src" "${CE_STRING_RYML_SOURCE_DIR}/ext/c4core.src"
    "${CMAKE_CURRENT_BINARY_DIR}/ryml-include")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ryml-include/ryml")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ryml-include/ryml/ryml.hpp" "#include <ryml.hpp>\n")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ryml-include/ryml/ryml_std.hpp" "#include <ryml_std.hpp>\n")
  if(MSVC)
    target_compile_options(creator_string_ryml PRIVATE /utf-8 /EHsc)
  elseif(CE_ASAN)
    target_compile_options(creator_string_ryml PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
  container_header_test(container_authoring_string_tests ../../../Tools/regression/authoring_string_migration_probe.cpp)
  target_sources(container_authoring_string_tests PRIVATE
    ../../Engine/Utility_Framework/Authoring/AuthoringParsedDocument.cpp
    ../../Engine/Utility_Framework/Authoring/AuthoringCookedDocument.cpp
    ../../Engine/Utility_Framework/Authoring/CookedDocumentTree.cpp
    ../../Engine/Utility_Framework/Authoring/AuthoringRymlErrorPolicy.cpp
    ../../Engine/Utility_Framework/Authoring/AuthoringScalarConvert.cpp)
  target_compile_definitions(container_authoring_string_tests PRIVATE CE_CONTRACT_STANDALONE=1)
  target_link_libraries(container_authoring_string_tests PRIVATE creator_string_ryml)
  set(CE_STRING_REFLGEN_INCLUDE_DIR "" CACHE PATH "reflgen include directory for reflection string contracts")
  if(CE_STRING_REFLGEN_INCLUDE_DIR)
    container_header_test(container_reflection_string_tests ../../../Tools/regression/reflection_string_probe.cpp)
    target_sources(container_reflection_string_tests PRIVATE
      ../../Engine/Utility_Framework/Authoring/AuthoringScalarConvert.cpp
      ../../Engine/Utility_Framework/Authoring/AuthoringRymlErrorPolicy.cpp)
    target_include_directories(container_reflection_string_tests PRIVATE "${CE_STRING_REFLGEN_INCLUDE_DIR}")
    target_compile_definitions(container_reflection_string_tests PRIVATE CE_CONTRACT_STANDALONE=1)
    target_link_libraries(container_reflection_string_tests PRIVATE creator_string_ryml)
  endif()
endif()
add_executable(container_multi_tu ${container_multi_sources})
target_link_libraries(container_multi_tu PRIVATE creator_containers_headers Threads::Threads)
if(MSVC)
  target_compile_options(container_multi_tu PRIVATE /permissive- /EHsc /utf-8)
  if(CE_ASAN)
    target_compile_options(container_multi_tu PRIVATE /fsanitize=address /Zi)
    target_link_options(container_multi_tu PRIVATE /INCREMENTAL:NO /DEBUG)
  endif()
endif()
add_test(NAME container_multi_tu COMMAND container_multi_tu)
container_header_test(container_storage_migration_tests ../../../Tools/regression/storage_migration_probe.cpp)
container_header_test(container_ordered_set_migration_tests ../../../Tools/regression/ordered_set_migration_probe.cpp)
container_header_test(container_func_migration_tests ../../../Tools/regression/func_migration_probe.cpp)
foreach(target container_storage_migration_tests container_ordered_set_migration_tests container_func_migration_tests)
  target_compile_definitions(${target} PRIVATE CE_CONTRACT_STANDALONE=1)
endforeach()
