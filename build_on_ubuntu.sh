#!/usr/bin/env bash

ret_status=$(which clang)
if [ ${ret_status} == 1 ]; then
  sudo apt-get install clang
fi

ret_status=$(which cmake)
if [ ${ret_status} == 1 ]; then
  sudo apt-get install cmake
fi

sudo apt-get install libc++-dev libc++abi-dev openssl libssl-dev

which conan
check_status=$?
if [ ${check_status} -eq 1 ]; then
  pip install conan
fi

conan profile detect
cp linux-clang-conan-profile ~/.conan2/profiles/clang

tree ~/.conan2/profiles

conan install . --profile=clang --build=missing

cmake --preset conan-release
cmake --build --preset conan-release