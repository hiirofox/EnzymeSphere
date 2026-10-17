# Bundled ClangEnzyme compiler, CPU object rule, and Vulkan SPIR-V pipeline.
set(_enzyme_tool_dir "${CMAKE_SOURCE_DIR}/tools/ClangEnzyme")
set(_enzyme_clang "${_enzyme_tool_dir}/clang.exe")
set(_enzyme_plugin "${_enzyme_tool_dir}/ClangEnzyme-21.dll")
set(_vulkan21_include "${_enzyme_tool_dir}/vulkan21/include")
set(_vulkan_data_layout
    "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-n8:16:32:64-G10")

set(_enzyme_required_files
    "${_enzyme_clang}"
    "${_enzyme_plugin}"
    "${_enzyme_tool_dir}/opt.exe"
    "${_enzyme_tool_dir}/llvm-link.exe"
    "${_enzyme_tool_dir}/llc.exe"
    "${_enzyme_tool_dir}/spirv-opt.exe"
    "${_enzyme_tool_dir}/spirv-val.exe"
    "${_vulkan21_include}/hlsl.h"
)
foreach(_required IN LISTS _enzyme_required_files)
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "Required bundled Enzyme/Vulkan tool not found: ${_required}")
    endif()
endforeach()

if(NOT TARGET Enzyme::Clang)
    add_executable(Enzyme::Clang IMPORTED GLOBAL)
    set_target_properties(Enzyme::Clang PROPERTIES IMPORTED_LOCATION "${_enzyme_clang}")
endif()

function(add_enzyme_object TARGET SOURCE)
    cmake_parse_arguments(ENZ "" "" "DEPENDS;INCLUDE_DIRECTORIES;COMPILE_OPTIONS" ${ARGN})
    set(_tool_dir "${CMAKE_SOURCE_DIR}/tools/ClangEnzyme")
    set(_clang "${_tool_dir}/clang.exe")
    set(_plugin "${_tool_dir}/ClangEnzyme-21.dll")
    if(NOT EXISTS "${SOURCE}")
        message(FATAL_ERROR "Enzyme source file does not exist: ${SOURCE}")
    endif()

    set(_object_dir "${CMAKE_CURRENT_BINARY_DIR}/objects/${TARGET}/$<CONFIG>")
    set(_object "${_object_dir}/${TARGET}.obj")
    set(_include_args)
    foreach(_include_dir IN LISTS ENZ_INCLUDE_DIRECTORIES)
        list(APPEND _include_args "-I${_include_dir}")
    endforeach()

    add_custom_command(
        OUTPUT "${_object}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_object_dir}"
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_tool_dir}"
            "$<TARGET_FILE:Enzyme::Clang>"
            --target=x86_64-pc-windows-msvc
            "-fpass-plugin=${_plugin}"
            ${ENZ_COMPILE_OPTIONS}
            ${_include_args}
            -c "${SOURCE}" -o "${_object}"
        DEPENDS
            "${SOURCE}" ${ENZ_DEPENDS}
            "${_clang}" "${_plugin}"
        COMMENT "ClangEnzyme: ${SOURCE} -> ${_object}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_custom_target(${TARGET} DEPENDS "${_object}")
    set(${TARGET}_OBJECT "${_object}" PARENT_SCOPE)
endfunction()

function(add_enzyme_vulkan_spirv TARGET SOURCE)
    cmake_parse_arguments(VK "" "" "DEPENDS;INCLUDE_DIRECTORIES;COMPILE_OPTIONS" ${ARGN})
    set(_tool_dir "${CMAKE_SOURCE_DIR}/tools/ClangEnzyme")
    set(_clang "${_tool_dir}/clang.exe")
    set(_plugin "${_tool_dir}/ClangEnzyme-21.dll")
    set(_vulkan_include "${_tool_dir}/vulkan21/include")
    set(_data_layout
        "e-i64:64-v16:16-v24:32-v32:32-v48:64-v96:128-v192:256-v256:256-v512:512-v1024:1024-n8:16:32:64-G10")
    set(_tool_files
        "${_clang}" "${_plugin}"
        "${_tool_dir}/opt.exe" "${_tool_dir}/llvm-link.exe" "${_tool_dir}/llc.exe"
        "${_tool_dir}/spirv-opt.exe" "${_tool_dir}/spirv-val.exe"
        "${_vulkan_include}/hlsl.h")
    if(NOT EXISTS "${SOURCE}")
        message(FATAL_ERROR "Vulkan Enzyme wrapper does not exist: ${SOURCE}")
    endif()

    set(_build_dir "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}/$<CONFIG>")
    set(_gradient_ir "${_build_dir}/loss.gradient.ll")
    set(_retarget_bc "${_build_dir}/loss.vulkan-retarget.bc")
    set(_entry_ir "${_build_dir}/loss.wrapper.ll")
    set(_linked_bc "${_build_dir}/loss.linked.bc")
    set(_optimized_bc "${_build_dir}/loss.ready.bc")
    set(_raw_spv "${_build_dir}/loss.raw.spv")
    set(_final_spv "${_build_dir}/loss_gradient_vulkan.spv")
    set(_include_args)
    foreach(_include_dir IN LISTS VK_INCLUDE_DIRECTORIES)
        list(APPEND _include_args "-I${_include_dir}")
    endforeach()

    add_custom_command(
        OUTPUT "${_final_spv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_build_dir}"
        # Compile ordinary C++ to generic SPIR-V IR, applying Enzyme in Clang.
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_tool_dir}"
            "${_clang}" -cc1 -triple spirv64
            -x c++ -O1 -emit-llvm "-fpass-plugin=${_plugin}"
            -DLOSS_WRAPPER_VULKAN
            -I "${CMAKE_SOURCE_DIR}/Source"
            ${_include_args} ${VK_COMPILE_OPTIONS}
            "${SOURCE}" -o "${_gradient_ir}"
        # Compile the Vulkan compute entry point and its descriptor interface.
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_tool_dir}"
            "${_clang}" -x hlsl --target=spirv-unknown-vulkan1.3-compute
            -Xclang -hlsl-entry -Xclang main -O1 -I "${_vulkan_include}"
            -Wno-invalid-constexpr -Wno-header-guard -Wno-c++14-extensions
            -S -emit-llvm
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/vulkan_compute.hlsl"
            -o "${_entry_ir}"
        # Retarget the generic C++ module to match the Vulkan HLSL module ABI.
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_tool_dir}"
            "${_tool_dir}/opt.exe"
            -mtriple=spirv-unknown-vulkan1.3-compute
            "-data-layout=${_data_layout}"
            "${_gradient_ir}" -o "${_retarget_bc}"
        # Resolve gradient_component from the C++/Enzyme module into main.
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_tool_dir}"
            "${_tool_dir}/llvm-link.exe"
            "${_entry_ir}" "${_retarget_bc}" -o "${_linked_bc}"
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_tool_dir}"
            "${_tool_dir}/opt.exe"
            -internalize-public-api-list=main
            "-passes=internalize,default<O2>"
            "${_linked_bc}" -o "${_optimized_bc}"
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_tool_dir}"
            "${_tool_dir}/llc.exe"
            -mtriple=spirv-unknown-vulkan1.3-compute -O0 -filetype=obj
            "${_optimized_bc}" -o "${_raw_spv}"
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_tool_dir}"
            "${_tool_dir}/spirv-opt.exe"
            --eliminate-dead-code-aggressive --eliminate-dead-const --trim-capabilities
            "${_raw_spv}" -o "${_final_spv}"
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${_tool_dir}"
            "${_tool_dir}/spirv-val.exe"
            --target-env vulkan1.3 "${_final_spv}"
        DEPENDS
            "${SOURCE}" ${VK_DEPENDS}
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/vulkan_compute.hlsl"
            ${_tool_files}
        COMMENT "ClangEnzyme: ${SOURCE} -> Vulkan 1.3 SPIR-V"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_custom_target(${TARGET} DEPENDS "${_final_spv}")
    set(${TARGET}_SPV "${_final_spv}" PARENT_SCOPE)
endfunction()
