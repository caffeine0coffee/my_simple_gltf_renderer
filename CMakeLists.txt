cmake_minimum_required(VERSION 3.28)
project(gltf_renderer LANGUAGES CXX)

# clangd等のIDEツールがConan経由の依存関係のインクルードパスを解決できるようにする
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(glfw3 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(spdlog REQUIRED)

add_executable(gltf_renderer
    src/main.cpp
    src/window.cpp
    src/vulkan_app.cpp
)
target_link_libraries(gltf_renderer PRIVATE glfw Vulkan::Vulkan spdlog::spdlog)
target_compile_options(gltf_renderer PRIVATE -Wall -Wextra -Wpedantic)
