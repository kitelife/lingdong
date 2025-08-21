#!/usr/bin/env bash

os=$(uname)
if [ "$os" == "Darwin" ]; then
  sh ./build_on_macosx.sh
else
  sh ./build_on_ubuntu.sh
fi