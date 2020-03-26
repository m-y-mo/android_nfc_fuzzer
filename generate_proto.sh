#!/bin/bash

$1/out/host/linux-x86/bin/aprotoc --proto_path=$1/system/nfc/tools --cpp_out=$1/system/nfc/tools $1/system/nfc/tools/t2t_detect.proto


