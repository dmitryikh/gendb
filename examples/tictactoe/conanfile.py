from conan import ConanFile


class TicTacToeConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
    # This example talks over its own TCP transport, so it does not need gRPC.
    default_options = {"gendb/*:with_grpc": False}

    def requirements(self):
        self.requires("gendb/0.1.0")
