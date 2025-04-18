#!/usr/bin/env bash
# https://google.github.io/googletest/quickstart-cmake.html
#
cmake -S . -B build
#
cmake --build build
#
pushd build
ctest
popd