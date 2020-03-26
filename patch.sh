#!/bin/bash

if [ "$#" -ne 1 ]; then
  echo "AOSP root directory not supplied: Usage: ./patch.sh <AOSP_ROOT>"
  exit
fi

echo "Patching nfc"

patch -p1 -d $1/system/nfc < nfc.patch

echo "Patching protobuf to enable rtti for libprotobuf-mutator"

patch -p1 -d $1/external/protobuf < protobuf.patch

echo "Copying fuzzers to nfc"

cp -r tools $1/system/nfc
