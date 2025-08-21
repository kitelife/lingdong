#!/usr/bin/env bash

os=$(uname)
chmod +x ./build_on_*
if [ "$os" == "Darwin" ]; then
  ./build_on_macosx.sh
else
  ./build_on_ubuntu.sh
fi