#include <android/log.h>
#include <stdint.h>
#include <stddef.h>

#include "harness_common.h"

extern void nfa_rw_handle_i93_evt(tRW_EVENT event, tRW_DATA* p_rw_data);
extern void nfa_rw_handle_t4t_evt(tRW_EVENT event, tRW_DATA* p_rw_data);
extern void nfa_rw_handle_t3t_evt(tRW_EVENT event, tRW_DATA* p_rw_data);
extern void nfa_rw_handle_t2t_evt(tRW_EVENT event, tRW_DATA* p_rw_data);
extern void nfa_rw_handle_t1t_evt(tRW_EVENT event, tRW_DATA* p_rw_data);
extern tNFC_STATUS nfa_rw_start_ndef_detection(void);
extern void GKI_shutdown();
extern void gki_buffer_init();
extern void gki_timers_init();
extern void NFC_Init_RW(tHAL_NFC_ENTRY* p_hal_entry_tbl);
extern void gki_buffers_cleanup(void);
extern void gki_reset();
extern void allocate_all_pools();
extern void reset_task();

static NfcAdaptation* adaptation;
 
#define T5T_PROTOCOL 0x06 //Need to fix it for some reason.

static uint8_t* fine_memory_pool[256];

void init_fine_memory_pool() {
  for (int i = 0; i < 256; i++) {
    fine_memory_pool[i] = (uint8_t*)malloc(NCI_MSG_HDR_SIZE + i + sizeof(NFC_HDR) + NFC_RECEIVE_MSGS_OFFSET + BUFFER_HDR_SIZE);
  }
}

void cleanup_fine_memory_pool() {
  for (int i = 0; i < 256; i++) {
    free(fine_memory_pool[i]);
    fine_memory_pool[i] = NULL;
  }
}

void initialize_rf_conn() {
  tNFC_CONN_CB* p_cb = &nfc_cb.conn_cb[NFC_RF_CONN_ID];
  p_cb->init_credits = p_cb->num_buff = 0;
  nfc_set_conn_id(p_cb, NFC_RF_CONN_ID);
}

uint16_t cap_data_len(size_t data_len) {
  if (data_len < 3) return data_len;
  if (data_len - NCI_MSG_HDR_SIZE > 255) return 255 + NCI_MSG_HDR_SIZE; //Some hack to prevent buffer too large. Looks like message length cannot be longer than max of 8bit.
  return data_len;
}

NFC_HDR* create_data_msg_meta(uint16_t data_len, const char* p_data) {
  NFC_HDR* p_msg;
  /* ignore all data while shutting down NFCC */
  if (nfc_cb.nfc_state == NFC_STATE_W4_HAL_CLOSE) {
    return NULL;
  }

  if (data_len < 3) return NULL;
  p_msg = (NFC_HDR*)GKI_getpoolbuf(NFC_NCI_POOL_ID);
  if (p_msg != NULL) {
    /* Initialize NFC_HDR */
    p_msg->event = BT_EVT_TO_NFC_NCI;
    p_msg->offset = NFC_RECEIVE_MSGS_OFFSET;
    //First write the 3 byte header
    memcpy((uint8_t*)(p_msg + 1) + p_msg->offset, p_data, 3);
    memset((uint8_t*)(p_msg + 1) + p_msg->offset, 0, 1); //set everything to zero (mt -> data event and cid -> NFC_RF_CONN_ID) pbf to zero to avoid overflow.
//    memset((uint8_t*)(p_msg + 1) + p_msg->offset, p_data[0] & NCI_PBF_MASK, 1); //set everything other than pbf to zero (mt -> data event and cid -> NFC_RF_CONN_ID).
    return p_msg;
  }
  return NULL;
}

void set_message_len(NFC_HDR* p_msg) {
  //Cap message len at 255
  if (p_msg->len > 258) {
    p_msg->len = 258;
  }
  uint8_t len = (uint8_t)(p_msg->len - NCI_MSG_HDR_SIZE);
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + 2, len, 1); //set len to match message length.
}

NFC_HDR* copy_msg(NFC_HDR* p_msg) {
  uint16_t len = p_msg->len;
  if (len < 3) return NULL;
  uint8_t* mem = fine_memory_pool[len - 3];
  memcpy(mem, (uint8_t*)p_msg - BUFFER_HDR_SIZE, BUFFER_HDR_SIZE + NCI_MSG_HDR_SIZE + sizeof(NFC_HDR) + NFC_RECEIVE_MSGS_OFFSET + len - 3);
  return (NFC_HDR*)((uint8_t*)mem + BUFFER_HDR_SIZE);
}

void selectProtocol(uint8_t protocol) {
  tNFC_ACTIVATE_DEVT* p_activate_params = new tNFC_ACTIVATE_DEVT;
  /* not a tag NFC_PROTOCOL_NFCIP1:   NFCDEP/LLCP - NFC-A or NFC-F */
  if (NFC_PROTOCOL_T1T == protocol) {
    /* Type1Tag    - NFC-A */
    p_activate_params->protocol = NFC_PROTOCOL_T1T;
    p_activate_params->rf_tech_param.mode = NFC_DISCOVERY_TYPE_POLL_A;
    //Initialize uid
    p_activate_params->rf_tech_param.param.pa.nfcid1_len = 4;
    for (int i = 0; i < NCI_NFCID1_MAX_LEN; i++) {
      p_activate_params->rf_tech_param.param.pa.nfcid1[i] = 0;
    }
    //Initialize hr
    for (int i = 0; i < NCI_T1T_HR_LEN; i++) {
      p_activate_params->rf_tech_param.param.pa.hr[i] = 0;
      p_activate_params->intf_param.intf_param.frame.param[i] = 0;
    }
    p_activate_params->intf_param.intf_param.frame.param_len = 0;
  } else if (NFC_PROTOCOL_T2T == protocol) {
    /* Type2Tag    - NFC-A */
    p_activate_params->protocol = NFC_PROTOCOL_T2T;
    p_activate_params->rf_tech_param.mode = NFC_DISCOVERY_TYPE_POLL_A;
    p_activate_params->rf_tech_param.param.pa.sel_rsp = NFC_SEL_RES_NFC_FORUM_T2T;
    nfa_rw_cb.pa_sel_res = NFC_SEL_RES_NFC_FORUM_T2T;
    //Initialize uid
    p_activate_params->rf_tech_param.param.pa.nfcid1_len = 4;
    for (int i = 0; i < NCI_NFCID1_MAX_LEN; i++) {
      p_activate_params->rf_tech_param.param.pa.nfcid1[i] = 0;
    }
  } else if (false) {
    /* Type3Tag    - NFC-F */
    p_activate_params->protocol = NFC_PROTOCOL_T3T;
    p_activate_params->rf_tech_param.mode = NFC_DISCOVERY_TYPE_POLL_F; 
  } else if (NFC_PROTOCOL_ISO_DEP == protocol) {
    /* ISODEP/4A,4B- NFC-A or NFC-B */
    p_activate_params->protocol = NFC_PROTOCOL_ISO_DEP;
    //p_activate_params->rf_tech_param.mode = NFC_DISCOVERY_TYPE_POLL_B;
    p_activate_params->rf_tech_param.mode = NFC_DISCOVERY_TYPE_POLL_A;
  } else if (NFC_PROTOCOL_T5T == protocol) {
    /* T5T */
    p_activate_params->protocol = NFC_PROTOCOL_T5T;
    p_activate_params->rf_tech_param.mode = NFC_DISCOVERY_TYPE_POLL_V;
  }
  tNFA_RW_MSG* p_data = new tNFA_RW_MSG;
  p_data->activate_ntf.p_activate_params = p_activate_params;
  nfa_rw_activate_ntf(p_data);
  delete p_data;
  delete p_activate_params;
}

extern "C" int LLVMFuzzerInitialize(int *argc, char*** argv) {
  return initialize();
}

int initialize() {
  adaptation = &NfcAdaptation::GetInstance();
//  NFC_Init(NULL);
  adaptation->InitializeFuzzer();
  init_fine_memory_pool();
  return 0;
}

void cleanup() {
  GKI_shutdown();
  NfcConfig::clear();
}

void deactivate() {
  nfa_rw_deactivate_ntf(NULL);
}

void setup(uint8_t protocol) {
  NFC_Init_RW(NULL);
  gki_reset();
  reset_task();
  initialize_rf_conn();
  selectProtocol(protocol);
}

bool get_prob(uint32_t prob, uint8_t thresh) {
  uint8_t remainder = prob % 256;
  return remainder > thresh;
}
