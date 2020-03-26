#include "harness_common.h"
#include "harness_t2t_detect.h"
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <dirent.h>

int main(int argc, char* argv[]) {
  std::string dir_name = argv[1];
  DIR* dirp = opendir(argv[1]);
  struct dirent* dp = NULL;
  while ((dp = readdir(dirp)) != NULL) {
    nfc::DetectSession session;
    std::fstream input(dir_name + "/" + dp->d_name, std::ios::in | std::ios::binary);
    if (session.ParseFromIstream(&input)) {
      initialize();
      TestDetect(session);
      cleanup();
    }
  }
  closedir(dirp);
  return 0;
}

