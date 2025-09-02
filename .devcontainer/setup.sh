#!/usr/bin/env bash
set -e

conan profile detect --force

conan install . -s build_type=Debug --build=missing --output-folder build/debug
rm -f CMakeUserPresets.json
conan install . -s build_type=Release --build=missing --output-folder build/release
rm -f CMakeUserPresets.json