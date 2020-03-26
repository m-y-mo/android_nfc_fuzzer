#ifndef HARNESS_COMMON_H
#define HARNESS_COMMON_H

#include "NfcAdaptation.h"
#include "nfc_int.h"
#include "nfa_sys.h"
#include "nfc_api.h"
#include "rw_api.h"
#include "nfa_api.h"
#include "nfa_rw_int.h"
#include "nfc_config.h"
#include "gki_int.h"
#include "nfc_target.h"

int initialize();
void cleanup();
void deactivate();
void setup(uint8_t protocol);
void set_message_len(NFC_HDR* p_msg);
NFC_HDR* create_data_msg_meta(uint16_t data_len, const char* p_data);
uint16_t cap_data_len(size_t data_len);
void initialize_rf_conn();
//True if prob % 255 exceeded threshold.
bool get_prob(uint32_t prob, uint8_t thresh);
NFC_HDR* copy_msg(NFC_HDR* p_msg);
#endif
