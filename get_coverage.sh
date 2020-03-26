#!/bin/bash

if [ "$#" -ne 4 ]; then
  echo "Usage: ./get_coverage.sh <AOSP_ROOT> <PRODUCT_NAME> <REPO_DIR> <OUT_DIR>, all directories should be absolute path"
  exit
fi

ANDROID_BUILD_TOP=$1
ADB=adb
AOSP_PRODUCT=${ANDROID_BUILD_TOP}/out/target/product/$2
NFC_COVERAGE=/data/fuzz/arm64/nfc_coverage_t2t
CORPUS_DIR=/data/fuzz/arm64/t2t_detect_fuzzer/minimized
TMP_DATA_DIR=/data/local/tmp
COVERAGE_DIR=${AOSP_PRODUCT}/coverage
OUT_DIR=$4/coverage
LLVM_GCOV=$3/llvm_gcov.sh

echo ${LLVM_GCOV}

#clean up
rm -rf $OUT_DIR
mkdir -p $OUT_DIR

#copy .gcnodir across

cp ${COVERAGE_DIR}/system/lib64/libprotobuf-cpp-full.zip ${OUT_DIR}
cp ${COVERAGE_DIR}/system/lib64/libnfc-nci-coverage.zip ${OUT_DIR}
cp ${COVERAGE_DIR}/system/bin/nfc_coverage_t2t.zip ${OUT_DIR}

#extract .gcnodir to .gcno

cd ${OUT_DIR}

unzip ${OUT_DIR}/libprotobuf-cpp-full.zip
unzip ${OUT_DIR}/libnfc-nci-coverage.zip
unzip ${OUT_DIR}/nfc_coverage_t2t.zip

#clean up .gcda files
echo "Clean up"
$ADB shell rm -rf $TMP_DATA_DIR
$ADB shell mkdir -p $TMP_DATA_DIR
$ADB shell find $TMP_DATA_DIR -iname "*.gcda" -exec rm -rf "{}" "\;"

# get coverage
$ADB shell \
  GCOV_PREFIX=$TMP_DATA_DIR \
  GCOV_PREFIX_STRIP=`echo $ANDROID_BUILD_TOP | grep -o / | wc -l` \
  ${NFC_COVERAGE} ${CORPUS_DIR}

$ADB pull ${TMP_DATA_DIR} ${OUT_DIR}

find . -iname "*.gcda" -exec mv "{}" . \;
find . -iname "*.gcno" -exec mv "{}" . \;

llvm-cov gcov -f -b *.gcda

lcov --directory . --base-directory ${ANDROID_BUILD_TOP} --no-external --gcov-tool ${LLVM_GCOV} --capture -o cov.info

genhtml cov.info -o report

rm -rf *.gcov cov.info *.gcno *.gcda

set +x
