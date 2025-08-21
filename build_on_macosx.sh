#!/usr/bin/env bash

which cmake
check_status=$?
if [ ${check_status} -eq 1 ] ; then
  brew install cmake
fi

if [ ! -d ".venv" ]; then
  virtualenv .venv
fi
source .venv/bin/activate

which conan
check_status=$?
if [ ${check_status} -eq 1 ]; then
  pip install conan
fi

git submodule update --init --recursive

profile_default=$(conan profile list | grep default)
if [ "${profile_default}" != "default" ]; then
  conan profile detect
fi

conan install . --build=missing

cmake --preset conan-release
cmake --build --preset conan-release