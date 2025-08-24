#!/usr/bin/env bash

target=$1

cwd=$(pwd)
cd build/Release || exit

if [ -z "$target" ]; then
  ctest -E SmmsPluginTest
else
  ctest -R "$target"
fi

cd "${cwd}" || exit