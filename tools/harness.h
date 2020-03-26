#ifndef HARNESS_H
#define HARNESS_H
#include "nfc_event.pb.h"
int initialize();
void cleanup();
void TestRW(const nfc::Session& session, uint8_t protocol);

#endif
