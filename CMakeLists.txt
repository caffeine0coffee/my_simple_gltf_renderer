cmake_minimum_required(VERSION 3.28)
project(gltf_renderer LANGUAGES CXX)

# clangd等のIDEツールがConan経由の依存関係のインクルードパスを解決できるようにする
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 依存ライブラリのインクルードパスを -isystem ではなく -I で渡し、システム標準パス (/usr/local/include 等) より優先させる
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(glfw3 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(spdlog REQUIRED)

find_program(GLSLC glslc REQUIRED)

# GLSLシェーダーのSPIR-Vコンパイル設定
set(SHADER_SOURCES
    shaders/shader.vert
    shaders/shader.frag
)

set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(SPV_OUTPUTS "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_OUT "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPV_OUT}
        COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${SPV_OUT}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}
        COMMENT "Compiling shader ${SHADER} -> ${SPV_OUT}"
    )
    list(APPEND SPV_OUTPUTS ${SPV_OUT})
endforeach()

add_custom_target(compile_shaders DEPENDS ${SPV_OUTPUTS})

add_executable(gltf_renderer
    src/main.cpp
    src/window.cpp
    src/vulkan_app.cpp
)
add_dependencies(gltf_renderer compile_shaders)

target_link_libraries(gltf_renderer PRIVATE glfw Vulkan::Vulkan spdlog::spdlog)
target_compile_options(gltf_renderer PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(gltf_renderer PRIVATE
    SHADER_BINARY_DIR="${SHADER_OUTPUT_DIR}"
)
