## Getting coverage information

To get coverage information, first create a directory called `minimized` and minimize the corpus:

```
sargo:/data/fuzz/arm64/t2t_detect_fuzzer # mkdir minimized
sargo:/data/fuzz/arm64/t2t_detect_fuzzer # ASAN_OPTIONS=detect_container_overflow=0 ./t2t_detect_fuzzer -merge=1 minimized corpus
```

On the host machine, change into the aosp root directory and compile the `nfc_coverage_t2t` target:

```
NATIVE_COVERAGE=true COVERAGE_PATHS="*" make -j($nproc) nfc_coverage_t2t
```

then push this and some other libraries to the device, which requires remounting as it is pushing into `system/bin` (If verity is not disabled, then the first time `adb remount -R` is run, the device will reboot and disable verity. In this case, `adb root` and `adb remount -R` will need to be done again after the reboot):

```
adb root
adb remount -R
adb push <AOSP_ROOT>/out/target/product/<PRODUCT_NAME>/system/bin/nfc_coverage_t2t /system/bin
adb push <AOSP_ROOT>/out/target/product/<PRODUCT_NAME>/system/lib64/libnfc-nci-coverage.so /system/lib64
adb push <AOSP_ROOT>/out/target/product/<PRODUCT_NAME>/system/bin/libprotobuf-cpp-full.so /system/lib64
```

Then create an output directory for the coverage data and run the `get_coverage.sh` script on the root directory of this repo:

```
mkdir OUT_DIR
./get_coverage.sh <AOSP_ROOT> <PRODUCT_NAME> <REPO_DIR> <OUT_DIR>
```

where `<PRODUCT_NAME>` is the name of the built for the device, e.g. `sargo` for Pixel3a and all directories should be absolute path. This will create a directory `<OUT_DIR>/coverage` directory with a `report` directory under it that contains coverage information.

## Debugging crash samples

To debug crash samples, build the target `t2t_detect_dbg`:

```
make -j($nproc) t2t_detect_dbg
```

Then copy the relevant binary and libraries to the device, again, this requires a remount:

```
adb root
adb remount -R
adb push <AOSP_ROOT>/out/target/product/<PRODUCT_NAME>/system/bin/t2t_detect_dbg /system/bin
adb push <AOSP_ROOT>/out/target/product/<PRODUCT_NAME>/system/lib64/libnfc-nci-dbg.so /system/lib64
adb push <AOSP_ROOT>/out/target/product/<PRODUCT_NAME>/system/bin/libprotobuf-cpp-full.so /system/lib64
```

To debug, use `gdb` for remote debugging. First forward the port `5039` for debug (other port can also be used):

```
adb forward tcp:5039 tcp:5039
```

Then in the device, start a debugging session with `gdbserver64`:

```
sargo:/data/fuzz/arm64/t2t_detect_fuzzer #   gdbserver64 :5039 t2t_detect_fuzzer crash-ID
Process t2t_detect_dbg created; pid = 15700
Listening on port 5039
```

This should start listening on port 5039 for a remote debugging session.

Now on host, change directory to the aosp root, and start a debugging session with `gdb`(this has to be done on aosp root, and `gdb` under `prebuilts/gdb/linux-x86/bin` is to be used)

```
prebuilts/gdb/linux-x86/bin/gdb out/target/product/sargo/symbols/system/bin/t2t_detect_dbg 
``` 

Once inside `gdb`, set the search path for the shared-library symbols (with `<AOSP_ROOT>` set according to your setup) and set target to be remote. I suggest adding a `.gdbinit` file that include these lines in `<AOSP_ROOT>`:

```
set sysroot <AOSP_ROOT>/out/target/product/sargo/symbols
set solib-search-path <AOSP_ROOT>/out/target/product/sargo/symbols/system/lib64
target remote :5039
```
