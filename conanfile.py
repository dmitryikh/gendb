from conan import ConanFile


class GendbConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
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
        self.requires("abseil/20250512.1")
        self.requires("gtest/1.14.0")
        self.requires("benchmark/1.9.0")
        self.requires("flatbuffers/2.0.8")
        if self.options.with_grpc:
            self.requires("protobuf/6.33.5")
            self.requires("grpc/1.78.1")

    def build_requirements(self):
        if self.options.with_grpc:
            # protoc and grpc_cpp_plugin, for the optional gRPC replication transport.
            self.tool_requires("protobuf/<host_version>")
            self.tool_requires("grpc/<host_version>")
