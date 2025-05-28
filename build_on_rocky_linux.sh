#!/usr/bin/env bash

yum install cmake clang openssl openssl-devel

#
mkdir _install_deps
pushd _install_deps

git clone https://github.com/gabime/spdlog.git
pushd spdlog
mkdir build
pushd build
cmake .. && cmake --build .
make install
popd
popd

git clone https://github.com/gflags/gflags.git
pushd gflags
mkdir build
pushd build
ccmake ..
make
make install
popd
popd

git clone https://github.com/fmtlib/fmt.git
pushd fmt
mkdir build
pushd build
cmake ..
make
make install
popd
popd

popd

sh ./build.sh
