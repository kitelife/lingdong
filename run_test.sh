#!/usr/bin/env bash

target=$1

pushd build/Release
if [ "$target" == "" ]; then
  ctest -E SmmsPluginTest
else
  ctest -R $target
fi
popd