cmake_minimum_required(VERSION 3.20)

# 保持 MSVC 应用程序使用一致的动态 CRT：
# Debug 配置使用 /MDd，其他配置使用 /MD。下面的 Enzyme 编译选项会与其保持一致。
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(EnzymeSphere LANGUAGES CXX)

add_subdirectory(tools/ClangEnzyme)

add_executable(app
    "${CMAKE_SOURCE_DIR}/Source/main.cpp"
)

# OpenCL host API: the headers, import library, and ICD loader are kept in-tree.
set(OPENCL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/External/OpenCL-Headers")
set(OPENCL_IMPORT_LIBRARY "${CMAKE_SOURCE_DIR}/Library/OpenCL.lib")
set(OPENCL_LOADER_DLL "${CMAKE_SOURCE_DIR}/Library/OpenCL.dll")

foreach(_opencl_file IN ITEMS
    "${OPENCL_INCLUDE_DIR}/CL/cl.h"
    "${OPENCL_IMPORT_LIBRARY}"
    "${OPENCL_LOADER_DLL}"
)
    if(NOT EXISTS "${_opencl_file}")
        message(FATAL_ERROR "Required OpenCL file not found: ${_opencl_file}")
    endif()
endforeach()

target_include_directories(app PRIVATE "${OPENCL_INCLUDE_DIR}")
target_link_libraries(app PRIVATE "${OPENCL_IMPORT_LIBRARY}")

# Make the Khronos ICD loader available when launching app.exe from Visual Studio.
add_custom_command(TARGET app POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${OPENCL_LOADER_DLL}"
        "$<TARGET_FILE_DIR:app>/OpenCL.dll"
    VERBATIM
)

# Enzyme 无法区分 MSVC 二级 Debug STL 生成的已检查迭代器失败路径。
# 因此在两个编译单元中统一使用级别 0。
target_compile_definitions(app PRIVATE _ITERATOR_DEBUG_LEVEL=0)

# 应用程序负责配置策略，并将其传递给编译工具。
# ClangEnzyme 本身不了解 app，也不知道这些策略选择。
set(ENZYME_APP_COMPILE_OPTIONS
    "$<$<CONFIG:Debug>:-fms-runtime-lib=dll_dbg>"
    "$<$<NOT:$<CONFIG:Debug>>:-fms-runtime-lib=dll>"
    "-D_ITERATOR_DEBUG_LEVEL=0"
)

add_enzyme_object(loss_gradient
    "${CMAKE_SOURCE_DIR}/Source/loss.cpp"
    DEPENDS "${CMAKE_SOURCE_DIR}/Source/loss.h"
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/Source"
    COMPILE_OPTIONS ${ENZYME_APP_COMPILE_OPTIONS}
)

add_dependencies(app loss_gradient)
target_link_libraries(app PRIVATE "${loss_gradient_OBJECT}")
