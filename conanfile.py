from conan import ConanFile
from conan.tools.cmake import CMake


class GendbConan(ConanFile):
    name = "gendb"
    version = "0.1.0"
    license = "MIT"
    description = "An embedded C++ database generated from schemas, with replication"
    package_type = "static-library"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
    exports_sources = "CMakeLists.txt", "LICENSE", "cmake/*", "gen/*", "lib/*", "!gen/__pycache__/*", "!gen/.pytest_cache/*"
    options = {"with_grpc": [True, False]}
    default_options = {
        "with_grpc": True,
        # Only the C++ code generator is needed from gRPC.
        "grpc/*:csharp_plugin": False,
        "grpc/*:node_plugin": False,
        "grpc/*:objective_c_plugin": False,
        "grpc/*:php_plugin": False,
        "grpc/*:python_plugin": False,
        "grpc/*:ruby_plugin": False,
        "grpc/*:otel_plugin": False,
    }

    def requirements(self):
        self.requires("abseil/20250512.1", transitive_headers=True, transitive_libs=True)
        if self.options.with_grpc:
            self.requires("protobuf/6.33.5", transitive_headers=True, transitive_libs=True)
            self.requires("grpc/1.78.1", transitive_headers=True, transitive_libs=True)

    def build_requirements(self):
        # For this repository's own tests and benchmarks, and not for those using the package.
        self.test_requires("gtest/1.14.0")
        self.test_requires("benchmark/1.9.0")
        if self.options.with_grpc:
            # protoc and grpc_cpp_plugin, for the optional gRPC replication transport.
            self.tool_requires("protobuf/<host_version>")
            self.tool_requires("grpc/<host_version>")

    def build(self):
        cmake = CMake(self)
        cmake.configure(variables={"GENDB_BUILD_TESTS": "OFF"})
        cmake.build()

    def package(self):
        CMake(self).install()

    def package_info(self):
        # find_package(gendb) finds the config the install wrote, which also has the generators:
        # gen_db_messages and gen_db_schema.
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs = ["lib/cmake/gendb"]
