#!/usr/bin/env bash

ret=$(which clang)
if [ -z "${ret}" ]; then
  sudo apt-get install clang
fi

ret=$(which cmake)
if [ -z "${ret}" ]; then
  sudo apt-get install cmake
fi

sudo apt-get install libc++-dev libc++abi-dev openssl libssl-dev

ret=$(which python3)
if [ -z "${ret}" ]; then
  sudo apt-get install python3 python3-virtualenv python3-pip
fi
alias python=python3

if [ ! -d ".venv" ]; then
  virtualenv .venv
fi

source .venv/bin/activate

ret=$(which conan)
if [ -z "${ret}" ]; then
  pip install conan
fi

if [ ! -f ~/.conan2/profiles/default ]; then
  conan profile detect
fi
cp linux-clang-conan-profile ~/.conan2/profiles/clang

tree ~/.conan2/profiles

conan install . --profile=clang --build=missing

cmake --preset conan-release
cmake --build --preset conan-release