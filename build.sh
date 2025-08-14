#!/usr/bin/env bash
#
git submodule update
# https://google.github.io/googletest/quickstart-cmake.html
#
conan install . --build=missing
cmake --preset conan-release
cmake --build --preset conan-release