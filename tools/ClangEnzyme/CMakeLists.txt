# ClangEnzyme 作为编译器工具提供。
# 此模块不定义也不依赖任何应用程序目标；
# 调用方选择源文件，并将所有面向使用者的编译选项传递给 add_enzyme_object()。

set(_enzyme_clang "${CMAKE_CURRENT_LIST_DIR}/clang.exe")
set(_enzyme_plugin "${CMAKE_CURRENT_LIST_DIR}/ClangEnzyme-21.dll")

if(NOT TARGET Enzyme::Clang)
    add_executable(Enzyme::Clang IMPORTED GLOBAL)
    set_target_properties(Enzyme::Clang PROPERTIES
        IMPORTED_LOCATION "${_enzyme_clang}"
    )
endif()

function(add_enzyme_object TARGET SOURCE)
    cmake_parse_arguments(ENZ "" "" "DEPENDS;INCLUDE_DIRECTORIES;COMPILE_OPTIONS" ${ARGN})

    set(_plugin "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ClangEnzyme-21.dll")

    if(NOT EXISTS "${SOURCE}")
        message(FATAL_ERROR "Enzyme 源文件不存在: ${SOURCE}")
    endif()

    set(_object_dir "${CMAKE_CURRENT_BINARY_DIR}/enzyme/${TARGET}/$<CONFIG>")
    set(_object "${_object_dir}/${TARGET}.obj")
    set(_include_args)
    foreach(_include_dir IN LISTS ENZ_INCLUDE_DIRECTORIES)
        list(APPEND _include_args "-I${_include_dir}")
    endforeach()

    add_custom_command(
        OUTPUT "${_object}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_object_dir}"
        COMMAND "$<TARGET_FILE:Enzyme::Clang>"
            --target=x86_64-pc-windows-msvc
            -fpass-plugin=${_plugin}
            ${ENZ_COMPILE_OPTIONS}
            ${_include_args}
            -c "${SOURCE}"
            -o "${_object}"
        DEPENDS
            "${SOURCE}"
            ${ENZ_DEPENDS}
            "${_enzyme_clang}"
            "${_plugin}"
        COMMENT "ClangEnzyme: ${SOURCE} -> ${_object}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    add_custom_target(${TARGET} DEPENDS "${_object}")
    set(${TARGET}_OBJECT "${_object}" PARENT_SCOPE)
endfunction()