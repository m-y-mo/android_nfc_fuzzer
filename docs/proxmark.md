# Testing with Proxmark3

[Proxmark3](https://lab401.com/products/proxmark-3-rdv4) is used to create the test cases. This is great tool without which the current research would not have been possible.

## Setting up Proxmark3

In order to use Proxmark3 to reproduce the issues, it is necessary to modify the firmware and flash the firmware. The are 2 different versions of the firmware, the [official](https://github.com/Proxmark/proxmark3) firmware that will work on most versions of Proxmark3 and the [RfidResearchGroup](https://github.com/RfidResearchGroup/proxmark3) one that is mostly customized for the rdv4 version. (Although see [this](https://github.com/RfidResearchGroup/proxmark3#build-for-non-rdv4-proxmark3-platforms) for non rdv4). All testing here is done with rdv4 and I do not know if it'll work for non rdv4. For NFC type 2 tags, the RfidResearchGroup version works for both Pixel3a and Pixel4, while the official version only works for Pixel3a.

The documentation on both repositories are fairly informative and easy to follow. There are, however, a couple of things that are worth pointing out here:

### ModemManager

This is a very important point to note. The official documentation seems to suggest that `modemmanager` is only applicable on Kali, but I had issue with ubuntu and did get my Proxmark3 bricked (this is not the end of the world, as it can normally be recovered easily by flashing the firmware again) So please check [this](https://github.com/RfidResearchGroup/proxmark3/blob/master/doc/md/Installation_Instructions/Linux-Installation-Instructions.md#check-modemmanager
) out before you start.

### Recovering a bricked device

If, in the unfortunate event, the device is bricked, as long as the bootrom is not corrupted, (which is most of the cases), it can be recovered as follows (answer taken from [here](http://www.proxmark.org/forum/viewtopic.php?id=2055))

1. Unplug proxmark from PC
2. Press and hold button and then connect to PC (while still holding the button)
3. Wait until proxmark recognised by host OS.
4. Flash bootrom:

```
./proxmark3/client/flasher -b ./proxmark3/bootrom/obj/bootrom.elf
```

5. Wait until flashing is done and then release the button.

Important thing is that the button needs to be held the whole time until the bootrom had finished flashing.

### Unplug before flashing firmware

The device can sometimes be bricked if you try to flash the firmware while it is in the middle of something. To be on the safe side, unplug the device and then plug it in again before flashing the firmware.

### Avoid flashing bootrom when it is not necessary

During the initial set up, it is necessary to flash the bootrom, but normally when testing new payload, it is sufficient to just flash the fullimage, which would reduce the chance of bootrom getting corrupted. For the official branch, this is done by

```
./proxmark3/client/flasher /dev/ttyACM0 armsrc/obj/fullimage.elf
```

On the RfidResearchGroup branch, this is done by:

```
./proxmark3/pm3-flash-fullimage
```

## Creating test case

Due to the various custom parsing done by the fuzzer, it is probably easiest to reconstruct the payload by starting a debugging session (see [here](coverage_debug.md) for more details)

First set up a remote debugging session, then set break points at the following functions: `create_t2t_default_response`, `create_t2t_wait_cc`, `create_t2t_wait_select_sector` and `num2tlv`. The first 3 functions are used for converting protobuf messages into NFC format, they basically take 2 8 byte integers (normally called `hdr_0` and `hdr_1` ) and concatenating them to form a 16 byte payload. This 16 byte array is the one that is needed to construct the payload. However some bytes are overwritten by other fields, for example, `create_t2t_wait_cc` overwrites the following bytes:

```cpp
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + 3 + 0x0F, wait_cc.cc3(), 1);
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + 3 + 0x0D, wait_cc.cc1(), 1);
  memset((uint8_t*)(p_msg + 1) + p_msg->offset + 3 + 0x0C, T2T_CC0_NMN, 1);
```

In all cases, the final payload is the array that goes from `p_msg->offset + 3` to `p_msg->offset + 19`, so it is probably more useful to inspect them after all the processing is done.

This, however, is not the only modification of the payload. As some data is interpreted as enums with only 6 valid values, while others interpreted as raw data, there is a further processing in the fuzzer to convert data to enums whenever appropriate to make fuzzing more efficient. This is done in `num2tlv`. Whenever `num2tlv` is called, it will convert the entry of the payload into an enum by simply taking a module of 6.

```cpp
static uint8_t num2tlv(uint8_t input) {
  switch (input % 6) {
    case 0:
      return TAG_NULL_TLV;
    case 1:
      return TAG_LOCK_CTRL_TLV;
    case 2:
      return TAG_MEM_CTRL_TLV;
    case 3:
      return TAG_NDEF_TLV;
    case 4:
      return TAG_PROPRIETARY_TLV;
    case 5:
      return TAG_TERMINATOR_TLV;
    default:
      return TAG_NDEF_TLV;
  }
}
```

This is called in `rw_t2t_handle_tlv_detect_rsp` to convert the payload. So to get the final payload, set a break point in `num2tlv`, then go up a frame and replace the `offset - 1` entry by its module of 6 in the current block of 16 byte payload. (In the following, `p_data` is the current block of 16 byte)

```cpp
static void rw_t2t_handle_tlv_detect_rsp(uint8_t* p_data) {
  ...
  for (offset = 0; offset < T2T_READ_DATA_LEN && !failed && !found;) {
    if (rw_t2t_is_lock_res_byte((uint16_t)(p_t2t->work_offset + offset)) ==
        true) {
      /* Skip locks, reserved bytes while searching for TLV */
      offset++;
      continue;
    }
    switch (p_t2t->substate) {
      case RW_T2T_SUBSTATE_WAIT_TLV_DETECT:
        /* Search for the tlv */
        p_t2t->found_tlv = num2tlv(p_data[offset++]);
```

Repeat this procedure and you should be able to get the complete payload in blocks of 16 bytes:

```
{{... array of 16 byte}, {... array of 16 byte}, ...}
```

This is more or less the payload to be used in the proxmark3 [templates](templates). Once you obtained the payload, replace `FUZZER_PAYLOAD` in the template files by the payload arrays, but pad the 16 byte blocks with zeros to form 18 byte blocks, e.g. if the payload is:

```
{
 {0x01, 0x03, 0x00, 0x0a,
  0x00, 0x00, 0x00, 0x00,
  0x01, 0x03, 0x00, 0x0a,
  0x0a, 0x00, 0x00, 0x00
 },
 {0x01, 0x03, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
 },
};
```

then replace `FUZZER_PAYLOAD` in the template with this, and pad each block with 2 `0x00`:

```cpp
uint8_t rsp[][MAX_MIFARE_FRAME_SIZE] = {
 {0x01, 0x03, 0x00, 0x0a,
  0x00, 0x00, 0x00, 0x00,
  0x01, 0x03, 0x00, 0x0a,
  0x0a, 0x00, 0x00, 0x00,
  0x00, 0x00,             //<--- pad with zeros
 },
 {0x01, 0x03, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00,            //<--- pad with zeros
 },
};

```
Also replace the `FUZZER_PAYLOAD_LEN` with the number of blocks (i.e. 2):

```cpp
if (tagType == 7 || tagType == 2) {
    // first blocks of emu are header
    AddCrc14A(rsp[readCount], 16);
    EmSendCmd(rsp[readCount], 18);
	if (readCount < 2)  //<--- Replace FUZZER_PAYLOAD_LEN with 2
		readCount++;
    // We already responded, do not send anything with the EmSendCmd14443aRaw() that is called below
    p_response = NULL;
```

After that, replace the file `iso14443a.c` in the `armsrc` folder in the appropriate proxmark3 repository with the template, compile and flash the firmware, connect to proxmark3:

```
cd client
./proxmark3 /dev/ttyACM0
```

Once inside the proxmark console, run the following to simulate a tag.
```
hf 14a sim t 7 u 120F5C3C
```

The payload can then be tested against a phone.

### General troubleshooting

Once the simulation has stopped, the communications between the phone and proxmark3 can be viewed with the following command in the proxmark console:

```
hf list 14a
```

This will print out details like:

```
      Start |        End | Src | Data (! denotes parity error, ' denotes short bytes)            | CRC | Annotation         |          
------------|------------|-----|-----------------------------------------------------------------|-----|--------------------|          
          0 |       1056 | Rdr | 26'                                                             |     | REQA          
       2228 |       4596 | Tag | 04  00                                                          |     |           
    7804506 |    7805562 | Rdr | 26'                                                             |     | REQA          
    7806734 |    7809102 | Tag | 04  00                                                          |     |           
    7816592 |    7821360 | Rdr | 50  00  57  cd                                                  |  ok | HALT          
    7846488 |    7847480 | Rdr | 52'                                                             |     | WUPA          
    7848716 |    7851084 | Tag | 04  00                                                          |     |           
    7858942 |    7861406 | Rdr | 93  20                                                          |     | ANTICOLL          
    7862578 |    7868402 | Tag | 12  0f  5c  3c  7d                                              |     |           
    7875200 |    7885728 | Rdr | 93  70  12  0f  5c  3c  7d  8d  58                              |  ok | SELECT_UID          
    7886900 |    7890484 | Tag | 00  fe  51                                                      |     |           
    8056736 |    8061504 | Rdr | 50  00  57  cd                                                  |  ok | HALT          
    8172886 |    8173878 | Rdr | 52'                                                             |     | WUPA          
    8175114 |    8177482 | Tag | 04  00                                                          |     |           
    8195234 |    8205762 | Rdr | 93  70  12  0f  5c  3c  7d  8d  58                              |  ok | SELECT_UID          
    8206934 |    8210518 | Tag | 00  fe  51                                                      |     |           
    8250880 |    8255648 | Rdr | 30  00  02  a8                                                  |  ok | READBLOCK(0)          
    8258356 |    8279156 | Tag | 41  41  41  41  41  41  41  41  fa  ff  ff  ff  e1  11  ff  00  |     |           
```

If connection is successful, then you should see the payload in the console output (last line in the above) Sometimes it may take multiple trials to get things working.


