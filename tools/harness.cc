#include <android/log.h>
#include <stdint.h>
#include <stddef.h>

#include "harness.h"
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

static NfcAdaptation* adaptation;

#define T5T_PROTOCOL 0x06 //Need to fix it for some reason.

void initialize_rf_conn() {
  tNFC_CONN_CB* p_cb = &nfc_cb.conn_cb[NFC_RF_CONN_ID];
  p_cb->init_credits = p_cb->num_buff = 0;
  nfc_set_conn_id(p_cb, NFC_RF_CONN_ID);
}
/*
uint32_t append_meta_data_i93(bool use_uid, int64_t uid, const nfc::NfcNci& nfc_nci, NFC_HDR* p_msg) {
  //offset at 3 byte. First 3 bytes belongs to header.
  uint32_t offset = 3;
  uint32_t meta = 0;
  uint8_t* meta_ptr = NULL;
  int64_t this_uid = 0;
  int32_t cc = 0;
  switch(nfc_nci.sequence().sequence_case()) {
    case nfc::Sequence::kWaitUid:
      meta = nfc_nci.sequence().wait_uid().meta();
      meta_ptr = (uint8_t*)(&meta);
      this_uid = use_uid ? uid : nfc_nci.sequence().wait_uid().uid();
      memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, meta_ptr[0] & (meta_ptr[0] - 1), 1); //clear the lowest bit in the flag.
      offset++;
      memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, 0, 1); //set dsfid to zero.
      offset++;
      memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, (uint8_t*)(&this_uid), sizeof(int64_t)); //set uid.     
      offset += sizeof(int64_t);
      break;
    case nfc::Sequence::kSysInfo:
      meta = nfc_nci.sequence().sys_info().meta();
      meta_ptr = (uint8_t*)(&meta);
      this_uid = use_uid ? uid : nfc_nci.sequence().sys_info().uid();
      memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, meta_ptr[0] & (meta_ptr[0] - 1), 1); //clear the lowest bit in the flag.
      offset++;
      memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, meta_ptr[1], 1); //set info_flag.
      offset++;
      memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, (uint8_t*)(&this_uid), sizeof(int64_t)); //set uid.     
      offset += sizeof(int64_t);
      break;
    case nfc::Sequence::kWaitCc:
      meta = nfc_nci.sequence().wait_cc().meta();
      meta_ptr = (uint8_t*)(&meta);
      cc = nfc_nci.sequence().wait_cc().cc();
      memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, meta_ptr[0] & (meta_ptr[0] - 1), 1); //clear the lowest bit in the flag.
      offset++;
      memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, (uint8_t*)(&cc), sizeof(int32_t)); //set cc.
      memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, nfc_nci.sequence().wait_cc().e2() ? 0xE2 : 0xE1, 1); //set cc[0] to magic value 0xE1 or 0xE2
      offset += sizeof(int32_t);
      break;
    default:
      break;
  }
  return offset;
}

void modify_meta_data_t4t(const nfc::NfcNci& nfc_nci, NFC_HDR* p_msg) {
  if (p_msg->len < T4T_RSP_STATUS_WORDS_SIZE + 3) return;
  //modify status words
  uint16_t status_word = nfc_nci.status_word();
  memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + (p_msg->len - T4T_RSP_STATUS_WORDS_SIZE), (uint8_t*)(&status_word), 2);
}
*/
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
    memset((uint8_t*)(p_msg + 1) + p_msg->offset, p_data[0] & NCI_PBF_MASK, 1); //set everything other than pbf to zero (mt -> data event and cid -> NFC_RF_CONN_ID).
    return p_msg;
  }
  return NULL;
}

void create_t5t_wait_uid(nfc::T5TMboxEvt& evt, NFC_HDR** p_msgs, const nfc::WaitUidSysInfo* wait_uid_sys_info) {
  uint32_t offset = 3;
  size_t data_len = cap_data_len(evt.wait_uid().data().length());
  NFC_HDR* p_msg = create_data_msg_meta(data_len, evt.wait_uid().data().c_str());
  if (p_msg == NULL) return;
  int32_t meta = evt.wait_uid().meta();
  uint8_t* meta_ptr = (uint8_t*)(&meta);
  int64_t this_uid = wait_uid_sys_info ? wait_uid_sys_info->uid() : evt.wait_uid().uid();
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, meta_ptr[0] & (meta_ptr[0] - 1), 1); //clear the lowest bit in the flag.
  offset++;
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, 0, 1); //set dsfid to zero.
  offset++;
  memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, (uint8_t*)(&this_uid), sizeof(int64_t)); //set uid.     
  //set the first 3 bytes in uid
  nfc::T5TUid0 uid_0 = wait_uid_sys_info ? wait_uid_sys_info->uid_0() : evt.wait_uid().uid_0();
  if (uid_0 != nfc::UID_0_DEFAULT)
    memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, (uint8_t)uid_0, 1);
  nfc::T5TUid1 uid_1 = wait_uid_sys_info ? wait_uid_sys_info->uid_1() : evt.wait_uid().uid_1();
  if (uid_1 != nfc::UID_1_DEFAULT)
    memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset + 1, (uint8_t)uid_1, 1);
  nfc::T5TUid2 uid_2 = wait_uid_sys_info ? wait_uid_sys_info->uid_2() : evt.wait_uid().uid_2();
  if (uid_2 != nfc::UID_2_DEFAULT)
    memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset + 2, uid_2, 1);
  offset += sizeof(int64_t);
  p_msg->len = (uint16_t)(data_len + offset - 3);
  //truncate
  if (evt.wait_uid().truncate_prob() != 0) {
    p_msg->len = 3 + evt.wait_uid().truncate_wait_uid();
    p_msgs[0] = p_msg;
    return;
  }
  //copy the rest of the data
  if (data_len > 3)
    memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, evt.wait_uid().data().c_str() + 3, data_len - 3);
  p_msgs[0] = p_msg;
}

void create_t5t_sys_info(nfc::T5TMboxEvt& evt, NFC_HDR** p_msgs, const nfc::WaitUidSysInfo* wait_uid_sys_info) {
  uint32_t offset = 3;
  size_t data_len = cap_data_len(evt.sys_info().data().length());
  NFC_HDR* p_msg = create_data_msg_meta(data_len, evt.sys_info().data().c_str());
  if (p_msg == NULL) return;
  int32_t meta = evt.sys_info().meta();
  uint8_t* meta_ptr = (uint8_t*)(&meta);
  int64_t this_uid = wait_uid_sys_info ? wait_uid_sys_info->uid() : evt.sys_info().uid();
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, meta_ptr[0] & (meta_ptr[0] - 1), 1); //clear the lowest bit in the flag.
  offset++;
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, meta_ptr[1], 1); //set info_flag.
  offset++;
  memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, (uint8_t*)(&this_uid), sizeof(int64_t)); //set uid.     
  //set the first 3 bytes in uid
  nfc::T5TUid0 uid_0 = wait_uid_sys_info ? wait_uid_sys_info->uid_0() : evt.sys_info().uid_0(); 
  if (uid_0 != nfc::UID_0_DEFAULT)
    memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, (uint8_t)uid_0, 1);
  nfc::T5TUid1 uid_1 = wait_uid_sys_info ? wait_uid_sys_info->uid_1() : evt.sys_info().uid_1(); 
  if (uid_1 != nfc::UID_1_DEFAULT)
    memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset + 1, (uint8_t)uid_1, 1);
  nfc::T5TUid2 uid_2 = wait_uid_sys_info ? wait_uid_sys_info->uid_2() : evt.sys_info().uid_2(); 
  if (uid_2 != nfc::UID_2_DEFAULT)
    memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset + 2, (uint8_t)uid_2, 1);
  offset += sizeof(int64_t);
  p_msg->len = (uint16_t)(data_len + offset - 3);
  //truncate
  if (evt.sys_info().truncate_prob() != 0) {
    p_msg->len = 3 + evt.sys_info().truncate_sys_info();
    p_msgs[0] = p_msg;
    return;
  }
  //copy the rest of the data
  if (data_len > 3)
    memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, evt.sys_info().data().c_str() + 3, data_len - 3);
  //set ic_reference
  uint16_t ic_reference_offset = 0;
  if (meta_ptr[1] & I93_INFO_FLAG_DSFID) ic_reference_offset++;
  if (meta_ptr[1] & I93_INFO_FLAG_AFI) ic_reference_offset++;
  if (meta_ptr[1] & I93_INFO_FLAG_MEM_SIZE) ic_reference_offset += 2;
  if ( (meta_ptr[1] & I93_INFO_FLAG_IC_REF) && (evt.sys_info().ic_reference() != nfc::ICReference_DEFAULT)) {
    memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset + ic_reference_offset, (uint8_t)(evt.sys_info().ic_reference()), 1);
  }
  p_msgs[0] = p_msg;
  return;
}

void create_t5t_wait_cc(nfc::T5TMboxEvt& evt, NFC_HDR** p_msgs) {
  uint32_t offset = 3;
  size_t data_len = cap_data_len(evt.wait_cc().data().length());
  NFC_HDR* p_msg = create_data_msg_meta(data_len, evt.sys_info().data().c_str());
  if (p_msg == NULL) return;
  int32_t meta = evt.wait_cc().meta();
  uint8_t* meta_ptr = (uint8_t*)(&meta);
  int32_t cc = evt.wait_cc().cc();
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, meta_ptr[0] & (meta_ptr[0] - 1), 1); //clear the lowest bit in the flag.
  offset++;
  memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, (uint8_t*)(&cc), sizeof(int32_t)); //set cc.
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset, evt.wait_cc().e2() ? 0xE2 : 0xE1, 1); //set cc[0] to magic value 0xE1 or 0xE2
  if (evt.wait_cc().multi_block())
    memset((uint8_t*)(p_msg + 1) + p_msg->offset + offset + 1, (uint8_t)I93_ICODE_CC_MBREAD_MASK, 1); //set cc[3] to multi_block
  offset += sizeof(int32_t);
  p_msg->len = (uint16_t)(data_len + offset - 3);
  //truncate
  if (evt.wait_cc().truncate_prob() != 0) {
    p_msg->len = 3 + evt.wait_cc().truncate_wait_cc();
    p_msgs[0] = p_msg;
    return;
  }

  //copy the rest of the data
  if (data_len > 3)
    memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, evt.wait_cc().data().c_str() + 3, data_len - 3);
  p_msgs[0] = p_msg;
  return;
}

void create_t5t_wait_uid_sys_info(nfc::T5TMboxEvt& evt, NFC_HDR** p_msgs) {
  create_t5t_wait_uid(evt, p_msgs, &(evt.wait_uid_sys_info()));
  create_t5t_sys_info(evt, p_msgs + 1, &(evt.wait_uid_sys_info()));
  return;
}

void create_t5t_default_message(nfc::T5TMboxEvt& evt, NFC_HDR** p_msgs) {
  size_t data_len = cap_data_len(evt.df().data().length());
  NFC_HDR* p_msg = create_data_msg_meta(data_len, evt.df().data().c_str());
  if (p_msg == NULL) return;
  p_msg->len = (uint16_t)(data_len);
  //copy the rest of the data
  if (data_len > 3)
    memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + 3, evt.df().data().c_str() + 3, data_len - 3);
  p_msgs[0] = p_msg;
  return;
}

void create_data_msg_t5t(nfc::T5TMboxEvt& evt, NFC_HDR** p_msgs) {
  switch (evt.evt_case()) {
    case nfc::T5TMboxEvt::kWaitUid:
      create_t5t_wait_uid(evt, p_msgs, NULL);
      break;
    case nfc::T5TMboxEvt::kSysInfo:
      create_t5t_sys_info(evt, p_msgs, NULL);
      break;
    case nfc::T5TMboxEvt::kWaitCc:
      create_t5t_wait_cc(evt, p_msgs);
      break;
    case nfc::T5TMboxEvt::kWaitUidSysInfo:
      create_t5t_wait_uid_sys_info(evt, p_msgs);
      break;
    case nfc::T5TMboxEvt::kDf:
      create_t5t_default_message(evt, p_msgs);
      break;
    default:
      return;
  }
}

NFC_HDR* create_t4t_default_message(nfc::T4TMboxEvt& evt) {
  size_t data_len = cap_data_len(evt.df().data().length());
  NFC_HDR* p_msg = create_data_msg_meta(data_len, evt.df().data().c_str());
  if (!p_msg) return NULL;
  p_msg->len = data_len;
  if (data_len > 3)
    memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + 3, evt.df().data().c_str() + 3, data_len - 3);
  if (p_msg->len < T4T_RSP_STATUS_WORDS_SIZE + 3) {
    GKI_freebuf(p_msg);
    return NULL;
  }
  //modify status words
  uint16_t status_word = evt.df().status_word();
  memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + (p_msg->len - T4T_RSP_STATUS_WORDS_SIZE), (uint8_t*)(&status_word), 2);
  return p_msg;
}

void set_message_len(NFC_HDR* p_msg) {
  //Cap message len at 255
  if (p_msg->len > 258) {
    p_msg->len = 258;
  }
  uint8_t len = (uint8_t)(p_msg->len - NCI_MSG_HDR_SIZE);
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + 2, len, 1); //set len to match message length.
}

NFC_HDR* create_data_msg_t4t(nfc::T4TMboxEvt& evt) {
  switch (evt.evt_case()) {
    case nfc::T4TMboxEvt::kDf:
      return create_t4t_default_message(evt);
      break;
    default:
      return NULL;
  }
}

/*
NFC_HDR* create_data_msg(const nfc::NfcNci& nfc_nci, const nfc::MboxEvt& evt, uint8_t protocol) {
  NFC_HDR* p_msg;

  if (nfc_cb.nfc_state == NFC_STATE_W4_HAL_CLOSE) {
    return NULL;
  }
  bool use_uid = evt.use_uid();
  int64_t uid = evt.uid();
  size_t data_len = nfc_nci.data().length();
  const char* p_data = nfc_nci.data().c_str();
  bool use_sequence = nfc_nci.use_sequence();

  //Need at least the length of the header.
  if (data_len < 3) return NULL;
  if (data_len - NCI_MSG_HDR_SIZE > 230) data_len = 230 + NCI_MSG_HDR_SIZE; //Some hack to prevent buffer too large. Looks like message length cannot be longer than max of 8bit.
  p_msg = (NFC_HDR*)GKI_getpoolbuf(NFC_NCI_POOL_ID);
  if (p_msg != NULL) {
    p_msg->event = BT_EVT_TO_NFC_NCI;
    p_msg->offset = NFC_RECEIVE_MSGS_OFFSET;
    //First write the 3 byte header
    memcpy((uint8_t*)(p_msg + 1) + p_msg->offset, p_data, 3);
    memset((uint8_t*)(p_msg + 1) + p_msg->offset, p_data[0] & NCI_PBF_MASK, 1); //set everything other than pbf to zero (mt -> data event and cid -> NFC_RF_CONN_ID).
    if (protocol == NFC_PROTOCOL_T5T) {
	  //Add sequence meta data, if use.
	  uint32_t offset = use_sequence ? append_meta_data_i93(use_uid, uid, nfc_nci, p_msg) : 3;
	  p_msg->len = (uint16_t)(data_len + offset - 3);
	  //Writes the rest of the data.
	  if (data_len > 3) {
		memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + offset, p_data + 3, data_len - 3);
	    if (!use_sequence)
		  memset((uint8_t*)(p_msg + 1) + p_msg->offset + 3, p_data[3] & (p_data[3] - 1), 1);  //clear the lowest bit in the flag.
      }
    } else if(NFC_PROTOCOL_ISO_DEP == protocol) {
      p_msg->len = (uint16_t)data_len;
      //copy rest of data
	  if (data_len > 3)
		memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + 3, p_data + 3, data_len - 3);
      //Add status word.
      modify_meta_data_t4t(nfc_nci, p_msg);
    } else {
      //copy rest of sequence.
	  if (data_len > 3)
		memcpy((uint8_t*)(p_msg + 1) + p_msg->offset + 3, p_data + 3, data_len - 3);
    }
    uint8_t len = (uint8_t)(p_msg->len - NCI_MSG_HDR_SIZE);
    memset((uint8_t*)(p_msg + 1) + p_msg->offset + 2, len, 1); //set len to match message length.

    return p_msg;
  } else {
    return NULL;
  }
  return NULL;
}
*/
tNFC_HAL_EVT_MSG* create_hal_evt(uint8_t hal_evt, tHAL_NFC_STATUS status) {
  tNFC_HAL_EVT_MSG* p_msg;

  p_msg = (tNFC_HAL_EVT_MSG*)GKI_getbuf(sizeof(tNFC_HAL_EVT_MSG));
  if (p_msg != NULL) {
    /* Initialize NFC_HDR */
    p_msg->hdr.len = 0;
    p_msg->hdr.event = BT_EVT_TO_NFC_MSGS;
    p_msg->hdr.offset = 0;
    p_msg->hdr.layer_specific = 0;
    p_msg->hal_evt = hal_evt;
    p_msg->status = status;
    return p_msg;
  } else {
    return NULL;
  }
  return NULL;
}

void handleT5TRead(const nfc::T5TRead& t5t_read) {
  bool free_buf;
  for (nfc::T5TMboxEvt evt : t5t_read.evt()) {
    free_buf = true;
    NFC_HDR* p_msgs[2];
    p_msgs[0] = NULL;
    p_msgs[1] = NULL;
    create_data_msg_t5t(evt, p_msgs);
    for (int i = 0; i < 2; i++) {
      if (p_msgs[i]) {
        set_message_len(p_msgs[i]);
        free_buf = nfc_ncif_process_event(p_msgs[i]);
        if (free_buf) {
          GKI_freebuf(p_msgs[i]);
        }
      }
    }
  }
}

void handleT5TWrite(const nfc::T5TWrite& t5t_write) {
  bool free_buf;
  for (nfc::T5TMboxEvt evt : t5t_write.evt()) {
    free_buf = true;
    NFC_HDR* p_msgs[2];
    p_msgs[0] = NULL;
    p_msgs[1] = NULL;
    create_data_msg_t5t(evt, p_msgs);
    for (int i = 0; i < 2; i++) {
      if (p_msgs[i]) {
        set_message_len(p_msgs[i]);
        free_buf = nfc_ncif_process_event(p_msgs[i]);
        if (free_buf) {
          GKI_freebuf(p_msgs[i]);
        }
      }
    }
  }
}

void handleT4TRead(const nfc::T4TRead& t4t_read) {
  bool free_buf;
  for (nfc::T4TMboxEvt evt : t4t_read.evt()) {
    free_buf = true;
    NFC_HDR* p_msg;
    p_msg = create_data_msg_t4t(evt);
    if (p_msg) {
      set_message_len(p_msg);
      free_buf = nfc_ncif_process_event(p_msg);
      if (free_buf) {
        GKI_freebuf(p_msg);
      }
    }
  }
}

void handleT4TWrite(const nfc::T4TWrite& t4t_write) {
  bool free_buf;
  for (nfc::T4TMboxEvt evt : t4t_write.evt()) {
    free_buf = true;
    NFC_HDR* p_msg;
    p_msg = create_data_msg_t4t(evt);
    if (p_msg) {
      set_message_len(p_msg);
      free_buf = nfc_ncif_process_event(p_msg);
      if (free_buf) {
        GKI_freebuf(p_msg);
      }
    }
  }
}


/*
void handleMboxEvt(const nfc::NFCTask& task, uint8_t protocol) {
  bool free_buf;
  for (nfc::MboxMsg msg : task.mbox_evt().mbox_msg()) {
    free_buf = true;
    switch (msg.msg_case()) {
      case nfc::MboxMsg::kNfcNci:
        NFC_HDR* p_msg;
        p_msg = create_data_msg(msg.nfc_nci(), task.mbox_evt(), protocol);
        if (p_msg) {
          free_buf = nfc_ncif_process_event(p_msg);
          if (free_buf) {
            GKI_freebuf(p_msg);
          }
        }
        break;
      case nfc::MboxMsg::kStartTimer:
        GKI_start_timer(NFC_TIMER_ID, GKI_SECS_TO_TICKS(1), true);
        break;
      case nfc::MboxMsg::kStartQuickTimer:
        GKI_start_timer(
                NFC_QUICK_TIMER_ID,
                ((GKI_SECS_TO_TICKS(1) / QUICK_TIMER_TICKS_PER_SEC)), true);
        break;
      case nfc::MboxMsg::kNfcMsgs:
        tNFC_HAL_EVT_MSG* hal_msg;
        hal_msg = create_hal_evt(msg.nfc_msgs().hal_evt(), msg.nfc_msgs().status());
        if (hal_msg) {
          nfc_main_handle_hal_evt(hal_msg);
          GKI_freebuf(hal_msg);
        }
        break;
      default:
        break;
    }
  }
}
*/
void selectProtocol(nfc::Session::ProtoSessionCase protocol) {
  tNFC_ACTIVATE_DEVT* p_activate_params = new tNFC_ACTIVATE_DEVT;
  /* not a tag NFC_PROTOCOL_NFCIP1:   NFCDEP/LLCP - NFC-A or NFC-F */
  if (false) {
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
  } else if (false) {
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
  } else if (nfc::Session::kT4TRead == protocol || nfc::Session::kT4TWrite == protocol) {
    /* ISODEP/4A,4B- NFC-A or NFC-B */
    p_activate_params->protocol = NFC_PROTOCOL_ISO_DEP;
    p_activate_params->rf_tech_param.mode = NFC_DISCOVERY_TYPE_POLL_B;
    //p_activate_params->rf_tech_param.mode == NFC_DISCOVERY_TYPE_POLL_A;
  } else if (nfc::Session::kT5TRead == protocol || nfc::Session::kT5TWrite == protocol) {
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
  allocate_all_pools();
  adaptation = &NfcAdaptation::GetInstance();
//  NFC_Init(NULL);
  adaptation->InitializeFuzzer();
  return 0;
}

void cleanup() {
  GKI_shutdown();
  NfcConfig::clear();
}

void TestRW(const nfc::Session& session, uint8_t protocol) {
//  adaptation->InitializeFuzzer(); 
  NFC_Init_RW(NULL);
//  gki_buffers_cleanup();
  gki_reset();
  reset_task();
  initialize_rf_conn();
//  NFC_ConnCreate(NCI_DEST_TYPE_REMOTE, NFC_RF_CONN_ID, protocol,NULL);
  selectProtocol(session.ProtoSession_case());
  //Initialize to reader.
  switch (session.ProtoSession_case()) {
    case nfc::Session::kT5TRead:
      nfa_rw_cb.cur_op = NFA_RW_OP_READ_NDEF;
      nfa_rw_start_ndef_detection();
      handleT5TRead(session.t5t_read());
      break;
    case nfc::Session::kT5TWrite:
      nfa_rw_cb.cur_op = NFA_RW_OP_READ_NDEF;
      nfa_rw_start_ndef_detection();
      handleT5TWrite(session.t5t_write());
      break;
    case nfc::Session::kT4TRead:
      nfa_rw_cb.cur_op = NFA_RW_OP_READ_NDEF;
      nfa_rw_start_ndef_detection();
      handleT4TRead(session.t4t_read());
      break;
    case nfc::Session::kT4TWrite:
      nfa_rw_cb.cur_op = NFA_RW_OP_WRITE_NDEF;
      nfa_rw_start_ndef_detection();
      handleT4TWrite(session.t4t_write());
    default:
      break;
  }
  nfa_rw_deactivate_ntf(NULL);
//  cleanup();
//  GKI_shutdown();
//  NfcConfig::clear();
//  NFC_ConnClose(NFC_RF_CONN_ID);
}
/*
void create_initial_sequence(nfc::MboxEvt* evt) {
  std::string data = "abca";
  //Only needs to be 0 for the first step
  data.push_back('\0');
  std::string uid = std::string("abcdefgh");
  //Get UID
  nfc::MboxMsg* msg0 = evt->add_mbox_msg();
  nfc::NfcNci* data_evt0 = new nfc::NfcNci;
  data_evt0->set_data(data + uid);
  msg0->set_allocated_nfc_nci(data_evt0);
  //Get_SYS_INFO
  nfc::MboxMsg* msg1 = evt->add_mbox_msg();
  nfc::NfcNci* data_evt1 = new nfc::NfcNci;
  data_evt1->set_data(data);
  msg1->set_allocated_nfc_nci(data_evt1);
}
*/
