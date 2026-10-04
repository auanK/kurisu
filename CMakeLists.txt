cmake_minimum_required(VERSION 3.28)

project(
    kurisu
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(kurisu_warnings INTERFACE)
if(WIN32)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(
        kurisu_warnings
        INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        kurisu_warnings
        INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Wno-missing-field-initializers>
    )
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(Vulkan REQUIRED)
find_program(GLSLC_EXECUTABLE NAMES glslc REQUIRED)

set(SHADER_SOURCES
    shaders/triangle.vert
    shaders/triangle.frag
)

set(SPV_OUTPUTS "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_FILE "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND ${GLSLC_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}" -o "${SPV_FILE}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}"
        COMMENT "Compiling shader ${SHADER} to SPIR-V"
        VERBATIM
    )
    list(APPEND SPV_OUTPUTS ${SPV_FILE})
endforeach()

add_custom_target(compile_shaders DEPENDS ${SPV_OUTPUTS})

add_executable(
    kurisu
    src/main.cpp
    src/graphics/vulkan/bootstrap.cpp
)

add_dependencies(kurisu compile_shaders)

target_include_directories(
    kurisu
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(
    kurisu
    PRIVATE
    KURISU_SHADERS_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

target_link_libraries(
    kurisu
    PRIVATE
    kurisu_warnings
    SDL3::SDL3
    Vulkan::Vulkan
)
