# ClangEnzyme 作为编译器工具提供。
# 此模块不定义也不依赖任何应用程序目标；
# 调用方选择源文件，并将所有面向使用者的编译选项传递给 add_enzyme_object()。

set(_enzyme_clang "${CMAKE_CURRENT_LIST_DIR}/clang.exe")
set(_enzyme_plugin "${CMAKE_CURRENT_LIST_DIR}/ClangEnzyme-21.dll")

# 保留大对象中浮点缓冲区后方的整数状态类型；Enzyme 默认只保留到偏移 500。
set(CLANG_ENZYME_MAX_TYPE_OFFSET 10000 CACHE STRING
    "Maximum memory offset retained by Enzyme type analysis")

if(NOT TARGET Enzyme::Clang)
    add_executable(Enzyme::Clang IMPORTED GLOBAL)
    set_target_properties(Enzyme::Clang PROPERTIES
        IMPORTED_LOCATION "${_enzyme_clang}"
    )
endif()

function(add_enzyme_object TARGET SOURCE)
    cmake_parse_arguments(ENZ "" "" "DEPENDS;INCLUDE_DIRECTORIES;COMPILE_OPTIONS" ${ARGN})

    set(_plugin "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ClangEnzyme-21.dll")
    set(_clang "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/clang.exe")

    if(NOT EXISTS "${SOURCE}")
        message(FATAL_ERROR "Enzyme 源文件不存在: ${SOURCE}")
    endif()

    set(_object_dir "${CMAKE_CURRENT_BINARY_DIR}/enzyme/${TARGET}/$<CONFIG>")
    set(_object "${_object_dir}/${TARGET}.obj")
    set(_ir "${_object_dir}/${TARGET}.ll")
    set(_depfile "${_object_dir}/${TARGET}.d")
    set(_depfile_options)
    # Visual Studio 的 DEPFILE 支持从 CMake 3.21 开始提供。
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
        list(APPEND _depfile_options DEPFILE "${_depfile}")
    endif()
    set(_include_args)
    foreach(_include_dir IN LISTS ENZ_INCLUDE_DIRECTORIES)
        list(APPEND _include_args "-I${_include_dir}")
    endforeach()

    add_custom_command(
        OUTPUT "${_object}"
        BYPRODUCTS "${_ir}" "${_depfile}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_object_dir}"
        # 先生成优化过的 IR；将向量化和循环展开留到自动微分之后。
        COMMAND "$<TARGET_FILE:Enzyme::Clang>"
            --target=x86_64-pc-windows-msvc
            -std=c++17
            -O2 -fno-vectorize -fno-slp-vectorize -fno-unroll-loops
            ${ENZ_COMPILE_OPTIONS}
            ${_include_args}
            -MD -MF "${_depfile}" -MT "${_object}"
            -S -emit-llvm "${SOURCE}"
            -o "${_ir}"
        # 在 IR 输入上提前加载插件，使 -mllvm 识别 Enzyme 参数，
        # 同时避免在 C++ 前端执行该工具包的 AST 插件。
        COMMAND "$<TARGET_FILE:Enzyme::Clang>"
            --target=x86_64-pc-windows-msvc
            -Xclang -load -Xclang "${_plugin}"
            "-fpass-plugin=${_plugin}"
            -mllvm -enzyme-inline=1
            -mllvm "-enzyme-max-type-offset=${CLANG_ENZYME_MAX_TYPE_OFFSET}"
            -O2
            -c "${_ir}"
            -o "${_object}"
        DEPENDS
            "${SOURCE}"
            ${ENZ_DEPENDS}
            "${_clang}"
            "${_plugin}"
            "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
        ${_depfile_options}
        COMMENT "ClangEnzyme: ${SOURCE} -> ${_object}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_custom_target(${TARGET} DEPENDS "${_object}")
    set(${TARGET}_OBJECT "${_object}" PARENT_SCOPE)
endfunction()
