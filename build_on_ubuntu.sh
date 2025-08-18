#!/usr/bin/env bash

sudo apt-get update
sudo apt-get -y install cmake clang openssl libssl-dev

which conan
check_status=$?
if [ ${check_status} -eq 1 ]; then
  pip install conan
fi

conan profile detect
cp linux-clang-conan-profile ~/.conan/profiles/clang

tree ~/.conan/profiles

conan install . --profile=clang --build=missing

cmake --preset conan-release
cmake --build --preset conan-release