#include <android/log.h>
#include <stdint.h>
#include <stddef.h>

#include "harness_common.h"
#include "harness_t2t_detect.h"
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
#include "rw_int.h"

extern tNFC_STATUS nfa_rw_start_ndef_detection(void);

static char buffer[3] = {0};

static int max_op = 50;

void create_t2t_wait_select_sector(const nfc::WaitSelectSector& df, NFC_HDR** p_msgs) {
  size_t data_len = cap_data_len(df.data().length());
  NFC_HDR* p_msg = create_data_msg_meta(data_len, df.data().c_str());
  if (p_msg == NULL) return;
  p_msg->len = (uint16_t)(data_len);
  //copy the rest of the data
  if (data_len > 3)
    memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + 3, df.data().c_str() + 3, data_len - 3);
  p_msgs[0] = p_msg;
  return;
}

void create_t2t_wait_cc(const nfc::WaitCc& wait_cc, NFC_HDR** p_msgs) {
  uint32_t offset = 3;
  size_t data_len = 19;
  NFC_HDR* p_msg = create_data_msg_meta(data_len, buffer);
  int64_t hdr_0 = wait_cc.hdr_0();
  int64_t hdr_1 = wait_cc.hdr_1();
  uint8_t* hdr_0_ptr = (uint8_t*)(&hdr_0);
  uint8_t* hdr_1_ptr = (uint8_t*)(&hdr_1);
  if (p_msg == NULL) return;
  //set data
  memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, hdr_0_ptr, 8);
  offset += 8;
  memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, hdr_1_ptr, 8);
  offset += 8;
  //set cc0, cc1 and cc3.
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + 3 + 0x0F, wait_cc.cc3(), 1);
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + 3 + 0x0D, wait_cc.cc1(), 1);
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + 3 + 0x0C, T2T_CC0_NMN, 1);
  if (get_prob(wait_cc.ack(), 230)) {
    p_msg->len = 4;
  } else {
    p_msg->len = 19;
  }
  p_msgs[0] = p_msg;
  return;
}

void create_t2t_default_response(const nfc::DefaultResponse& df, NFC_HDR** p_msgs) {
  uint32_t offset = 3;
  size_t data_len = 19;
  NFC_HDR* p_msg = create_data_msg_meta(data_len, buffer);
  int64_t hdr_0 = df.hdr_0();
  int64_t hdr_1 = df.hdr_1();
  uint8_t* hdr_0_ptr = (uint8_t*)(&hdr_0);
  uint8_t* hdr_1_ptr = (uint8_t*)(&hdr_1);
  if (p_msg == NULL) return;
  //set data
  memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, hdr_0_ptr, 8);
  offset += 8;
  memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, hdr_1_ptr, 8);
  offset += 8;
  if (get_prob(df.ack(), 230)) {
    p_msg->len = 4;
  } else {
    p_msg->len = 19;
  }
  p_msgs[0] = p_msg;
  return;
}


void handleT2T(const nfc::DetectSession& session) {
  bool free_buf;
  tRW_T2T_CB* p_t2t = &rw_cb.tcb.t2t;
  int wait_cc_counter = 0;
/*  
  int x = 17;
  p_t2t->attr[x] = 1;
*/
  int default_message_counter = 0;
  int wait_select_counter = 0;
  for (int i = 0; i < max_op; i++) {
    NFC_HDR* p_msgs[1] = {0};
    switch (p_t2t->substate) {
        case RW_T2T_SUBSTATE_WAIT_READ_CC:
          if (wait_cc_counter >= session.wait_cc_size()) return;
          create_t2t_wait_cc(session.wait_cc(wait_cc_counter++), p_msgs);
          break;
        case RW_T2T_SUBSTATE_WAIT_SELECT_SECTOR:
          if (wait_select_counter >= session.wait_select_size()) return;
          create_t2t_wait_select_sector(session.wait_select(wait_select_counter++), p_msgs);
          break;
        default:
          if (default_message_counter >= session.dr_size()) return;
          create_t2t_default_response(session.dr(default_message_counter++), p_msgs);
          break;
    }
    if (*p_msgs) {
      set_message_len(*p_msgs);
      NFC_HDR* msg = copy_msg(*p_msgs);
      free_buf = nfc_ncif_process_event(msg);
      if (free_buf) {
        GKI_freebuf(*p_msgs);
      }
      //Short circuit once it's idle, as anything after that is more or less wasted.
      if (p_t2t->state == RW_T2T_STATE_IDLE) {
        return;
      }
    }
  }
}

void TestDetect(const nfc::DetectSession& session) {
  setup(NFC_PROTOCOL_T2T);
  nfa_rw_start_ndef_detection();
  nfa_rw_cb.cur_op = NFA_RW_OP_READ_NDEF;
  handleT2T(session);
  deactivate();
}
