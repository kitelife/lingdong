#!/usr/bin/env bash
#
git submodule update
# https://google.github.io/googletest/quickstart-cmake.html
#
cmake -S . -B build
#
cmake --build build