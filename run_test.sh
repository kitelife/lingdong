#!/usr/bin/env bash

target=$1

cwd=$(pwd)
cd build/Release

if [ -z "$target" ]; then
  ctest -E SmmsPluginTest
else
  ctest -R $target
fi

cd ${cwd}