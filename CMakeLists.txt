cmake_minimum_required(VERSION 3.20)

cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(EnzymeSphere LANGUAGES CXX)

add_subdirectory(tools/ClangEnzyme)

set(ENZYME_CLANG "${CMAKE_SOURCE_DIR}/tools/ClangEnzyme/clang.exe")
set(ENZYME_PLUGIN "${CMAKE_SOURCE_DIR}/tools/ClangEnzyme/ClangEnzyme-21.dll")
set(VULKAN21_DIR "${CMAKE_SOURCE_DIR}/tools/ClangEnzyme/vulkan21")
set(VULKAN21_BIN "${VULKAN21_DIR}/bin")
set(VULKAN_HEADERS "${CMAKE_SOURCE_DIR}/External/Vulkan-Headers/include")
set(VULKAN_IMPORT_LIBRARY "${CMAKE_SOURCE_DIR}/Library/vulkan-1.lib")

foreach(_required IN ITEMS
    "${ENZYME_CLANG}"
    "${ENZYME_PLUGIN}"
    "${VULKAN21_BIN}/llc.exe"
    "${VULKAN21_BIN}/llvm-link.exe"
    "${VULKAN21_BIN}/opt.exe"
    "${VULKAN21_BIN}/spirv-opt.exe"
    "${VULKAN21_BIN}/spirv-val.exe"
    "${VULKAN_HEADERS}/vulkan/vulkan.h"
    "${VULKAN_IMPORT_LIBRARY}"
)
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "Required Vulkan/Enzyme file not found: ${_required}")
    endif()
endforeach()

add_executable(app "${CMAKE_SOURCE_DIR}/Source/main.cpp")
target_include_directories(app PRIVATE "${VULKAN_HEADERS}")
target_link_libraries(app PRIVATE "${VULKAN_IMPORT_LIBRARY}")
target_compile_definitions(app PRIVATE _ITERATOR_DEBUG_LEVEL=0)

set(ENZYME_APP_COMPILE_OPTIONS
    "$<$<CONFIG:Debug>:-fms-runtime-lib=dll_dbg>"
    "$<$<NOT:$<CONFIG:Debug>>:-fms-runtime-lib=dll>"
    "-D_ITERATOR_DEBUG_LEVEL=0"
)
add_enzyme_object(loss_gradient
    "${CMAKE_SOURCE_DIR}/Source/loss_wrapper.cpp"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/Source/loss.cpp"
        "${CMAKE_SOURCE_DIR}/Source/loss.h"
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/Source"
    COMPILE_OPTIONS ${ENZYME_APP_COMPILE_OPTIONS} -DLOSS_WRAPPER_CPU -O1
)
add_dependencies(app loss_gradient)
target_link_libraries(app PRIVATE "${loss_gradient_OBJECT}")

set(VULKAN_BUILD_DIR "${CMAKE_BINARY_DIR}/vulkan/$<CONFIG>")
set(VULKAN_GRADIENT_LL "${VULKAN_BUILD_DIR}/loss.gradient.ll")
set(VULKAN_WRAPPER_LL "${VULKAN_BUILD_DIR}/loss.wrapper.ll")
set(VULKAN_LINKED_BC "${VULKAN_BUILD_DIR}/loss.linked.bc")
set(VULKAN_READY_BC "${VULKAN_BUILD_DIR}/loss.ready.bc")
set(VULKAN_RAW_SPV "${VULKAN_BUILD_DIR}/loss.raw.spv")
set(VULKAN_SPV "${VULKAN_BUILD_DIR}/loss_gradient_vulkan.spv")

set(VULKAN_HLSL_SOURCE "${CMAKE_BINARY_DIR}/loss_vulkan_wrapper.hlsl")
file(WRITE "${VULKAN_HLSL_SOURCE}" [=[
StructuredBuffer<float> params : register(t0);
RWStructuredBuffer<float> gradients : register(u1);
float gradient_component(int task, float p0, float p1, float p2, int component);
[numthreads(2, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const int task = int(id.x);
    const float p0 = params[0], p1 = params[1], p2 = params[2];
    gradients[task * 3] = gradient_component(task, p0, p1, p2, 0);
    gradients[task * 3 + 1] = gradient_component(task, p0, p1, p2, 1);
    gradients[task * 3 + 2] = gradient_component(task, p0, p1, p2, 2);
}
]=])

add_custom_command(
    OUTPUT "${VULKAN_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VULKAN_BUILD_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${CMAKE_SOURCE_DIR}/tools/ClangEnzyme"
        "${ENZYME_CLANG}" -cc1 -triple spirv-unknown-vulkan1.3-compute
        -x c++ -O1 -emit-llvm "-fpass-plugin=${ENZYME_PLUGIN}"
        -DLOSS_WRAPPER_VULKAN -I "${CMAKE_SOURCE_DIR}/Source"
        "${CMAKE_SOURCE_DIR}/Source/loss_wrapper.cpp" -o "${VULKAN_GRADIENT_LL}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${CMAKE_SOURCE_DIR}/tools/ClangEnzyme"
        "${ENZYME_CLANG}" -x hlsl --target=spirv-unknown-vulkan1.3-compute
        -Xclang -hlsl-entry -Xclang main -O1 -I "${VULKAN21_DIR}/include"
        -Wno-invalid-constexpr -Wno-header-guard -Wno-c++14-extensions
        -S -emit-llvm "${VULKAN_HLSL_SOURCE}" -o "${VULKAN_WRAPPER_LL}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${CMAKE_SOURCE_DIR}/tools/ClangEnzyme"
        "${VULKAN21_BIN}/llvm-link.exe" "${VULKAN_WRAPPER_LL}" "${VULKAN_GRADIENT_LL}" -o "${VULKAN_LINKED_BC}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${CMAKE_SOURCE_DIR}/tools/ClangEnzyme"
        "${VULKAN21_BIN}/opt.exe" -internalize-public-api-list=main
        "-passes=internalize,default<O2>" "${VULKAN_LINKED_BC}" -o "${VULKAN_READY_BC}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${CMAKE_SOURCE_DIR}/tools/ClangEnzyme"
        "${VULKAN21_BIN}/llc.exe" -mtriple=spirv-unknown-vulkan1.3-compute
        -O0 -filetype=obj
        "${VULKAN_READY_BC}" -o "${VULKAN_RAW_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${CMAKE_SOURCE_DIR}/tools/ClangEnzyme"
        "${VULKAN21_BIN}/spirv-opt.exe" --eliminate-dead-code-aggressive
        --eliminate-dead-const --trim-capabilities "${VULKAN_RAW_SPV}" -o "${VULKAN_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${CMAKE_SOURCE_DIR}/tools/ClangEnzyme"
        "${VULKAN21_BIN}/spirv-val.exe" --target-env vulkan1.3 "${VULKAN_SPV}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/Source/loss.cpp"
        "${CMAKE_SOURCE_DIR}/Source/loss_wrapper.cpp"
        "${CMAKE_SOURCE_DIR}/Source/loss.h"
        "${VULKAN_HLSL_SOURCE}"
        "${ENZYME_CLANG}"
        "${ENZYME_PLUGIN}"
        "${VULKAN21_DIR}/include/hlsl.h"
        "${VULKAN21_BIN}/llc.exe"
        "${VULKAN21_BIN}/llvm-link.exe"
        "${VULKAN21_BIN}/opt.exe"
        "${VULKAN21_BIN}/spirv-opt.exe"
        "${VULKAN21_BIN}/spirv-val.exe"
    COMMENT "ClangEnzyme: loss wrapper -> Vulkan compute SPIR-V"
    VERBATIM
)
add_custom_target(loss_gradient_vulkan_spirv DEPENDS "${VULKAN_SPV}")
add_dependencies(app loss_gradient_vulkan_spirv)
add_custom_command(TARGET app POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${VULKAN_SPV}" "$<TARGET_FILE_DIR:app>/loss_gradient_vulkan.spv"
    VERBATIM
)

set(OPENCL_SPIRV_INCLUDE_DIR "${CMAKE_BINARY_DIR}/opencl-spirv-include")
file(MAKE_DIRECTORY "${OPENCL_SPIRV_INCLUDE_DIR}")
file(WRITE "${OPENCL_SPIRV_INCLUDE_DIR}/opencl-c-base.h"
    "typedef unsigned long size_t;\ntypedef unsigned int uint;\n")
set(OPENCL_SPV "${CMAKE_BINARY_DIR}/loss_gradient_opencl.spv")
add_custom_command(
    OUTPUT "${OPENCL_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=${CMAKE_SOURCE_DIR}/tools/ClangEnzyme"
        "${ENZYME_CLANG}"
        -x cl -cl-std=clc++ -target spirv64
        "-fpass-plugin=${ENZYME_PLUGIN}"
        -I "${OPENCL_SPIRV_INCLUDE_DIR}" -I "${CMAKE_SOURCE_DIR}/Source"
        -DLOSS_WRAPPER_OPENCL -O1 -c "${CMAKE_SOURCE_DIR}/Source/loss_wrapper.cpp" -o "${OPENCL_SPV}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/Source/loss.cpp"
        "${CMAKE_SOURCE_DIR}/Source/loss_wrapper.cpp"
        "${ENZYME_CLANG}"
        "${ENZYME_PLUGIN}"
    COMMENT "ClangEnzyme: OpenCL wrapper -> OpenCL SPIR-V"
    VERBATIM
)
add_custom_target(loss_gradient_opencl_spirv DEPENDS "${OPENCL_SPV}")
