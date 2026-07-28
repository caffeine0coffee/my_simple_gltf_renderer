from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class GltfRendererConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("glfw/3.4")
        # vulkan-headers/vulkan-loader はバージョンを揃えて要求する
        # (loader は headers に依存するため、揃えないと解決時にバージョン衝突しうる)
        self.requires("vulkan-headers/1.3.290.0")
        self.requires("vulkan-loader/1.3.290.0")
        self.requires("spdlog/1.17.0")

    def layout(self):
        cmake_layout(self)
