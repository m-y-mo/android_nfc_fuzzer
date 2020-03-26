#include "harness_common.h"
#include "harness_t2t_detect.h"
#include <iostream>
#include <fstream>

int main(int argc, char* argv[]) {
  nfc::DetectSession session;
  if (argc == 2) {
    std::fstream input(argv[1], std::ios::in | std::ios::binary);
    if (!session.ParseFromIstream(&input)) {
      std::cerr << "Failed to parse input." << std::endl;
      return -1;
    }
  } else {
    return -1;
  }
  initialize();
  TestDetect(session);
  cleanup();
  return 0;
}

