cmake_minimum_required(VERSION 3.20)

cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(EnzymeSphere LANGUAGES CXX)

add_subdirectory(wrapper)

set(VULKAN_HEADERS "${CMAKE_SOURCE_DIR}/External/Vulkan-Headers/include")
set(VULKAN_IMPORT_LIBRARY "${CMAKE_SOURCE_DIR}/Library/vulkan-1.lib")

if(NOT EXISTS "${VULKAN_HEADERS}/vulkan/vulkan.h")
    message(FATAL_ERROR "Vulkan headers not found: ${VULKAN_HEADERS}")
endif()
if(NOT EXISTS "${VULKAN_IMPORT_LIBRARY}")
    message(FATAL_ERROR "Vulkan loader import library not found: ${VULKAN_IMPORT_LIBRARY}")
endif()

add_executable(app "${CMAKE_SOURCE_DIR}/Source/main.cpp")
target_include_directories(app PRIVATE
    "${CMAKE_SOURCE_DIR}/Source"
    "${VULKAN_HEADERS}"
)
target_compile_definitions(app PRIVATE _ITERATOR_DEBUG_LEVEL=0)
target_link_libraries(app PRIVATE "${VULKAN_IMPORT_LIBRARY}")

# Host API from loss.h is backed by an Enzyme-generated MSVC-compatible object.
set(ENZYME_APP_COMPILE_OPTIONS
    "$<$<CONFIG:Debug>:-fms-runtime-lib=dll_dbg>"
    "$<$<NOT:$<CONFIG:Debug>>:-fms-runtime-lib=dll>"
    "-D_ITERATOR_DEBUG_LEVEL=0"
)
add_enzyme_object(loss_gradient
    "${CMAKE_SOURCE_DIR}/wrapper/loss_wrapper.cpp"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/Source/loss.cpp"
        "${CMAKE_SOURCE_DIR}/Source/loss.h"
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/Source"
    COMPILE_OPTIONS ${ENZYME_APP_COMPILE_OPTIONS} -DLOSS_WRAPPER_CPU -O1
)
add_dependencies(app loss_gradient)
target_link_libraries(app PRIVATE "${loss_gradient_OBJECT}")

# GPU implementation is built as Vulkan 1.3 compute SPIR-V by the wrapper module.
add_enzyme_vulkan_spirv(loss_gradient_vulkan
    "${CMAKE_SOURCE_DIR}/wrapper/loss_wrapper.cpp"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/Source/loss.cpp"
        "${CMAKE_SOURCE_DIR}/Source/loss.h"
)
add_dependencies(app loss_gradient_vulkan)
add_custom_command(TARGET app POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${loss_gradient_vulkan_SPV}"
        "$<TARGET_FILE_DIR:app>/loss_gradient_vulkan.spv"
    VERBATIM
)
