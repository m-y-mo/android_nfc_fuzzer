#include "src/libfuzzer/libfuzzer_macro.h"
#include <cmath>
#include "harness_t2t_detect.h"
#include <android/log.h>
#include <stdint.h>
#include <stddef.h>

DEFINE_BINARY_PROTO_FUZZER(const nfc::DetectSession& session) {
  TestDetect(session);
}
