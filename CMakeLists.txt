cmake_minimum_required(VERSION 3.20)

# 保持 MSVC 应用程序使用一致的动态 CRT：
# Debug 配置使用 /MDd，其他配置使用 /MD。下面的 Enzyme 编译选项会与其保持一致。
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(EnzymeSphere LANGUAGES CXX)

add_subdirectory(tools/ClangEnzyme)

#设置项目路径
set(ProjectPath "${CMAKE_SOURCE_DIR}/Projects/pole9prjshape1")

add_executable(app
    "${ProjectPath}/main.cpp"
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
    "${ProjectPath}/loss.cpp"
    DEPENDS "${ProjectPath}/loss.h"
    INCLUDE_DIRECTORIES "${ProjectPath}"
    COMPILE_OPTIONS ${ENZYME_APP_COMPILE_OPTIONS}
)

add_dependencies(app loss_gradient)

target_link_libraries(app PRIVATE "${loss_gradient_OBJECT}")
target_link_libraries(app PRIVATE
    user32
    gdi32
    shell32
    ole32
    d2d1
    dwrite
)