import os

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMakeToolchain, CMakeDeps, CMake, cmake_layout
from conan.tools.files import copy, rmdir

required_conan_version = ">=2.0"


class PhosphorConan(ConanFile):
    name = "phosphor"
    version = "0.1.0"

    url = "https://github.com/gikon3/phosphor"
    homepage = "https://github.com/gikon3/phosphor"
    description = "Waveform viewer"
    license = "Apache-2.0"
    topics = ("waveform", "systemverilog", "eda")

    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"

    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "src/*",
        "tests/*",
        "LICENSE*",
    )

    @property
    def _skip_test(self):
        return self.conf.get("tools.build:skip_test", default=False, check_type=bool)

    def layout(self):
        cmake_layout(self)
        self.cpp.source.includedirs = ["include"]
        self.cpp.build.includedirs = ["src/export"]
        self.cpp.build.bindirs = ["bin"]
        self.cpp.build.libdirs = ["lib"]

    def requirements(self):
        self.requires("wasafe-vcd/0.1.0")

    def build_requirements(self):
        if not self._skip_test:
            self.test_requires("gtest/1.18.0")

    def validate(self):
        check_min_cppstd(self, "23")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["PHOSPHOR_VERSION"] = self.version
        tc.cache_variables["PHOSPHOR_DESCRIPTION"] = self.description
        tc.cache_variables["PHOSPHOR_HOMEPAGE_URL"] = self.homepage
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        cmake.test()

    def package(self):
        copy(self, "LICENSE*", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        CMake(self).install()
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))

    def package_info(self):
        self.cpp_info.libs = ["phosphor"]
        self.cpp_info.set_property("cmake_target_name", "phosphor::phosphor")
        self.cpp_info.set_property("cmake_file_name", "phosphor")
