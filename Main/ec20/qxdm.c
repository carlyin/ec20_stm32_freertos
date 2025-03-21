#include "usbh_ec20.h"
#ifdef EC20_QXDM_INTERFACE

#define RecvUrbBufLen 2048
#define QXDMBufLen (RecvUrbBufLen*8)

struct qxdm_ctx {
    SemaphoreHandle_t xSemaphore;
    struct urb sendUrb;
    uint32_t sendBusy;

    uint8_t recvUrbBuf[RecvUrbBufLen];
    struct urb recvUrb;

    uint32_t qxdm_rp;
    uint32_t qxdm_wp;
    uint32_t qxdm_wait;
    uint32_t qxdm_drop;
    uint8_t qxdm_buf[QXDMBufLen];
};

static struct qxdm_ctx qxdm_ctx[1];

static const uint8_t qxdm_default_cfg[] = {
0x1d,0x1c,0x3b,0x7e,0x00,0x78,0xf0,0x7e,0x4b,0x32,0x06,0x00,0xba,0x4d,0x7e,0x7c,0x93,0x49,0x7e,0x1c,0x95,0x2a,0x7e,0x0c,0x14,0x3a,0x7e,0x63,0xe5,0xa1,0x7e,0x4b,
0x0f,0x00,0x00,0xbb,0x60,0x7e,0x4b,0x09,0x00,0x00,0x62,0xb6,0x7e,0x4b,0x08,0x00,0x00,0xbe,0xec,0x7e,0x4b,0x08,0x01,0x00,0x66,0xf5,0x7e,0x4b,0x04,0x00,0x00,0x1d,
0x49,0x7e,0x4b,0x04,0x0f,0x00,0xd5,0xca,0x7e,0x4b,0x0f,0x18,0x00,0x01,0x9e,0xa9,0x7e,0x4b,0x0f,0x18,0x00,0x02,0x05,0x9b,0x7e,0x4b,0x0f,0x2c,0x00,0x28,0xea,0x7e,
0x4b,0x12,0x39,0x00,0xeb,0x7b,0x7e,0x4b,0x12,0x3c,0x00,0x53,0x05,0x7e,0x4b,0x12,0x37,0x00,0xfb,0xe1,0x7e,0x4b,0x12,0x3b,0x00,0x5b,0x48,0x7e,0x4b,0x12,0x35,0x00,
0x4b,0xd2,0x7e,0x4b,0x12,0x3a,0x00,0x83,0x51,0x7e,0x4b,0x12,0x00,0x08,0x19,0x96,0x7e,0x7d,0x5d,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x74,0x41,0x7e,0x7d,0x5d,0x04,
0x00,0x00,0x02,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x4c,0x06,0x7e,0x7d,0x5d,0x04,0x05,0x00,0x05,0x00,0x00,0x00,0x1f,0x00,
0x00,0x00,0xce,0xa7,0x7e,0x7d,0x5d,0x04,0x07,0x00,0x08,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0xd0,0x71,0x7e,0x7d,0x5d,0x04,0x0b,0x00,0x0c,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x7c,0x68,0x7e,0x7d,0x5d,0x04,0x0e,0x00,0x12,0x00,0x00,0x00,0xff,0x01,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0xb5,0x3a,0x7e,0x7d,0x5d,0x04,0x14,0x00,0x15,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x2e,0xbb,
0x7e,0x7d,0x5d,0x04,0x19,0x00,0x19,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x88,0xd0,0x7e,0x7d,0x5d,0x04,0x20,0x00,0x20,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0xea,0xaa,
0x7e,0x7d,0x5d,0x04,0x27,0x00,0x28,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0xfe,0x01,0x00,0x00,0x89,0x11,0x7e,0x7d,0x5d,0x04,0x2a,0x00,0x2b,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0x01,0x1a,0x7e,0x7d,0x5d,0x04,0x33,0x00,0x33,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0xc2,0xc1,0x7e,0x7d,0x5d,0x04,0x36,0x00,0x36,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0xa3,0xd6,0x7e,0x7d,0x5d,0x04,0x39,0x00,0x3a,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x65,0xdf,0x7e,0x7d,0x5d,0x04,
0x3f,0x00,0x41,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0xfe,0xff,0x1f,0x00,0x1f,0x00,0x00,0x00,0xc9,0x67,0x7e,0x7d,0x5d,0x04,0x44,0x00,0x45,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0x2e,0x6c,0x7e,0x7d,0x5d,0x04,0x48,0x00,0x4a,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x4f,0xf2,
0x7e,0x7d,0x5d,0x04,0x4c,0x00,0x4c,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0xf2,0x66,0x7e,0x7d,0x5d,0x04,0x4e,0x00,0x4e,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x11,0x0f,
0x7e,0x7d,0x5d,0x04,0x58,0x00,0x58,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x9a,0x49,0x7e,0x7d,0x5d,0x04,0x5a,0x00,0x5b,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x0e,0x43,0x7e,0x7d,0x5d,0x04,0x63,0x00,0x63,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0xd9,0x60,0x7e,0x7d,0x5d,0x04,0x70,0x00,0x70,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x4a,0x17,0x7e,0x7d,0x5d,0x04,0x75,0x00,0x75,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0xc8,0x6d,0x7e,0x7d,0x5d,0x04,0xea,0x03,0xea,0x03,0x00,0x00,0x1e,0x00,
0x00,0x00,0xa9,0x9e,0x7e,0x7d,0x5d,0x04,0xee,0x03,0xef,0x03,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x9b,0x20,0x7e,0x7d,0x5d,0x04,0xd0,0x07,0xd7,0x07,
0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0xdc,0x10,0x7e,0x7d,0x5d,0x04,0xb8,0x0b,0xc5,0x0b,0x00,0x00,0x1f,0x00,0x00,0x00,0xfe,0xff,0x7f,0x00,0x7f,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0x1f,0x00,
0x00,0x00,0xff,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x1c,0x00,0x00,0x00,0xa9,0x36,0x7e,0x7d,0x5d,0x04,0xa0,0x0f,0xaa,0x0f,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0xfe,0x01,0x00,0x00,0x1e,0x00,0x00,0x00,0xfe,0xff,0x01,0x00,0xfe,0xff,0x07,0x00,0xfe,0xff,0x01,0x00,0xfe,0x07,0x00,0x00,0x1e,0x00,0x00,0x00,0xc3,0xb9,
0x7e,0x7d,0x5d,0x04,0x05,0x12,0x05,0x12,0x00,0x00,0x1f,0x00,0x00,0x00,0xd2,0x41,0x7e,0x7d,0x5d,0x04,0x07,0x12,0x07,0x12,0x00,0x00,0x1f,0x00,0x00,0x00,0xf3,0x12,
0x7e,0x7d,0x5d,0x04,0x88,0x13,0xa8,0x13,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0xf4,0x4c,0x7e,0x7d,0x5d,0x04,0x72,0x17,0x72,0x17,0x00,0x00,0x1e,0x00,0x00,0x00,0xcc,0x20,
0x7e,0x7d,0x5d,0x04,0x74,0x17,0x74,0x17,0x00,0x00,0x3f,0x00,0x00,0x00,0x47,0x46,0x7e,0x7d,0x5d,0x04,0x93,0x17,0x93,0x17,0x00,0x00,0x1e,0x00,0x00,0x00,0x8f,0xca,
0x7e,0x7d,0x5d,0x04,0x97,0x17,0x97,0x17,0x00,0x00,0x1e,0x00,0x00,0x00,0xcd,0x6c,0x7e,0x7d,0x5d,0x04,0xa4,0x17,0xb7,0x17,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,
0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x8d,0xd1,0x7e,0x7d,0x5d,0x04,0xc0,0x17,0xc0,0x17,0x00,0x00,0x1e,0x00,0x00,0x00,0x96,0x89,0x7e,0x7d,0x5d,0x04,
0x34,0x21,0x34,0x21,0x00,0x00,0x1e,0x00,0x00,0x00,0x10,0xc3,0x7e,0x7d,0x5d,0x04,0x1c,0x25,0x25,0x25,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0xff,0x1f,0x00,0x7d,0x5e,
0x00,0x00,0x00,0x3e,0x00,0x00,0x00,0x7d,0x5e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0xfe,0x03,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x3e,0xd0,0x00,0x00,
0x90,0xed,0x7e,0x7d,0x5d,0x04,0x0b,0x28,0x0f,0x28,0x00,0x00,0x1c,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,
0x71,0x86,0x7e,0x7d,0x5d,0x04,0x6e,0x28,0x89,0x28,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,
0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,
0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,
0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x1e,0x00,0x00,0x00,0x52,0x90,0x7e,0x73,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xda,0x81,0x7e,0x73,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0xfe,0x0f,0x00,0x00,0xf0,0x07,0xc8,0x00,0x00,0x40,
0xc4,0x00,0x00,0x00,0x00,0xc0,0x49,0xf3,0xc7,0x5b,0x7c,0xf3,0x0b,0x01,0x00,0x00,0x00,0x20,0xec,0x00,0xcc,0x83,0x01,0x00,0x00,0x00,0x00,0x00,0x38,0x00,0x38,0x00,
0x38,0x00,0x38,0x00,0x00,0x01,0x01,0x00,0x40,0x08,0xf0,0x07,0x0c,0xf8,0x47,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x40,0x04,0x00,0xff,0xf7,0x7f,0xf0,0xfc,0xff,0xff,0xad,0xe0,0x7f,0x02,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x78,
0xe0,0xff,0xff,0xff,0x48,0x1c,0x1e,0x00,0x03,0x10,0x18,0xff,0xff,0xff,0xff,0xbf,0x00,0x00,0x00,0x00,0x00,0x90,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x80,0x1b,0x80,0xff,0x5f,0x06,0x00,0x00,0x41,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0xc0,0x07,0x01,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x10,0x00,0x80,0x00,0x00,0x7f,0xce,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x00,0x00,
0x08,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0xf8,0x07,0x00,0x00,0x00,
0x07,0x00,0x00,0xc0,0x7f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x30,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0xb7,0xb2,0x7e,0x73,0x00,0x00,
0x00,0x03,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x09,0x08,0x00,0x00,0x31,0x00,0x09,0x80,0xf8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7f,0xef,0x20,0xf0,0x90,0x3c,0x1d,0x60,0x04,0x00,0x00,0x4f,0x03,0xfe,0x07,0x43,0x0b,0x02,
0x01,0x00,0x00,0x07,0xf4,0x45,0x00,0x38,0x00,0x00,0x00,0x00,0x00,0x86,0x8a,0x45,0xf8,0x25,0x10,0x00,0xcc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x82,0xc3,0x1e,0x00,0x7d,0x5e,0x00,0x4e,0x00,0xff,0x03,0x00,0x3c,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x09,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x01,0x32,0x3e,0x7e,0x73,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x5d,
0x0c,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x70,0x90,0x6f,0x3b,0xfc,0x01,0x13,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xd0,0xff,0xd7,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0xf0,0xff,0xe7,0xff,0xbf,0xf3,0x43,0x3f,0x02,0x00,0xe0,0xe3,0x01,0xff,0x0d,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x70,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x70,0x90,0x6f,0x1f,0xfc,0x71,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xd0,0xff,0x57,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0xe0,0xe7,0xff,0xf7,0xc3,0x3f,0x01,0x00,0xe0,0xe3,0x01,0x10,0x5d,0xe6,0x7e,0x73,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x57,0x0b,
0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x01,0x00,0x7f,0x02,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0xff,0x37,0x06,0xea,0x07,0x00,0x07,0x00,0x00,0x00,0x00,0xfc,0xf0,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x01,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x77,0x00,0x00,0x7f,0x9f,0xaa,0x7e,0x73,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x0b,0x00,0x00,0x00,0xd0,0x01,0x00,0x00,
0x06,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x5f,0x00,0x00,0x00,0xce,0x04,0xce,0x00,0x3f,0x00,0x3f,0x00,0xdf,0x24,0x00,0x00,0x7f,0xfc,0x3c,0x00,
0x00,0x00,0x3e,0x28,0x4e,0x50,0x05,0x12,0x51,0xe0,0x00,0x00,0xff,0xff,0xff,0xe2,0xcf,0xe1,0x7d,0x5d,0x51,0x7f,0x00,0x01,0x74,0x42,0xe0,0xac,0x34,0x7e,0x73,0x00,
0x00,0x00,0x03,0x00,0x00,0x00,0x0d,0x00,0x00,0x00,0x39,0x01,0x00,0x00,0x79,0xa6,0xff,0xff,0xff,0xa1,0x1f,0x00,0xdf,0x01,0x00,0x00,0x03,0x40,0x00,0x00,0x00,0x9f,
0x00,0x00,0x00,0x00,0xc0,0x06,0x00,0x00,0x00,0x00,0x5f,0x00,0x00,0x00,0xff,0x37,0xff,0xbf,0x1b,0x05,0x00,0x01,0x2b,0x17,0x7e,0x60,0x00,0x12,0x6a,0x7e,0x60,0x01,
0x9b,0x7b,0x7e,0x82,0x00,0x00,0x00,0x55,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xb7,0x0f,0x00,0x00,0x00,0x00,0x00,0x88,0xf4,0x0b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x61,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x04,0x00,0xd8,0x8f,0xff,0x3d,0xf8,0x48,0xfa,0x3f,0x06,0x00,0x00,0x19,0x00,0x00,0x00,0x0a,0xe0,0x0f,0x22,0x00,0x00,0x00,0xf8,0x84,0x2f,0x40,0x00,0x00,
0x0a,0x80,0xff,0xef,0x01,0x00,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0xe0,0x01,0xff,0x3f,0x82,0x00,0x01,
0x7d,0x5e,0x00,0x00,0x80,0xff,0x00,0x00,0x00,0x00,0xbe,0x19,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,0xff,0x0f,0xfe,0x7f,0x00,0x18,0x00,0x00,0x00,0xe0,0x01,0x00,0x00,
0xc0,0xfd,0xbf,0x95,0x03,0x00,0x00,0x00,0x00,0x00,0x80,0x81,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0xc0,0x3f,0x12,0x8c,0x04,0x00,0x60,0xc8,
0x2f,0xf8,0xe7,0xf9,0xff,0x7f,0xff,0xff,0x07,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x03,0xfc,0x6f,0x00,0xc7,0x9f,0x00,0x03,0x80,0xf9,0xfa,0x0f,0x80,
0x7b,0x80,0x37,0x24,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x23,0x8c,
0x0d,0x00,0x00,0x01,0x98,0x04,0x00,0x30,0x00,0x00,0x00,0x00,0x06,0x00,0xf2,0x07,0x00,0x44,0x00,0xf8,0x1f,0x20,0x73,0x7e
};
static const uint8_t qxdm_disable_log_mask[] = { 0x73, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t qxdm_disable_msg_mask[] = { 0x7D, 0x05, 0, 0, 0, 0, 0, 0};
static const uint8_t qxdm_disable_event_mask[] = { 0x60, 0};

static void qxdm_recv_urb_complete(struct urb *urb, int status) {
    struct qxdm_ctx *ctx = (struct qxdm_ctx *)urb->ctx;

    if (status != 0) {
        printf("%s status=%d\n", __func__, status);
        return;
    }

    if (urb->actual_length > 0) {
        uint32_t tail_room, qxdm_wp;
        //printf("qxdm: %u\n", urb->actual_length);

        if ((QXDMBufLen - (ctx->qxdm_wp - ctx->qxdm_rp)) < urb->actual_length) {
            ctx->qxdm_drop += urb->actual_length;
            goto _resubmit;
        }

        qxdm_wp = ctx->qxdm_wp%QXDMBufLen;
        tail_room = QXDMBufLen - qxdm_wp;
        if (tail_room < urb->actual_length) {
            memcpy(ctx->qxdm_buf + qxdm_wp, urb->transfer_buffer, tail_room);
            memcpy(ctx->qxdm_buf, urb->transfer_buffer + tail_room, urb->actual_length - tail_room);
        }
        else {
            memcpy(ctx->qxdm_buf + qxdm_wp, urb->transfer_buffer, urb->actual_length);
        }
        ctx->qxdm_wp += urb->actual_length;
        //printf("wp %u\n", qxdm_wp);
    }

_resubmit:
    if (ctx->qxdm_wait)
        xSemaphoreGive(ctx->xSemaphore);
    usb_submit_urb(urb);
}

static void qxdm_send_urb_complete(struct urb *urb, int status) {
    struct qxdm_ctx *ctx = (struct qxdm_ctx *)urb->ctx;

    if (status != 0) {
        printf("%s status=%d\n", __func__, status);
        return;
    }
    ctx->sendBusy = 0;
}

static int qxdm_open(struct hc_info *hc_info, uint16_t idProduct) {
    struct qxdm_ctx *ctx = &qxdm_ctx[0];
    struct urb *urb;

    printf("%s hc_num=%u,%u\n", __func__,
        get_hc_num(HC_BULK_OUT), get_hc_num(HC_BULK_IN));

    urb = &ctx->sendUrb;
    urb ->hc_type = HC_BULK_OUT;
    urb ->hc_num = get_hc_num(urb ->hc_type);
    urb ->ep_size = get_ep_size(urb ->hc_type);
    urb ->urb_state = URB_STATE_IDLE;
    urb ->complete = qxdm_send_urb_complete;
    urb ->ctx = ctx;
    usb_start_urb(urb);

    urb = &ctx->recvUrb;
    urb ->hc_type = HC_BULK_IN;
    urb ->hc_num = get_hc_num(urb ->hc_type);
    urb ->ep_size = get_ep_size(urb ->hc_type);
    urb ->urb_state = URB_STATE_IDLE;
    urb ->transfer_buffer = ctx->recvUrbBuf;
    urb ->transfer_buffer_length = RecvUrbBufLen;
    urb ->complete = qxdm_recv_urb_complete;
    urb ->ctx = ctx;
    usb_start_urb(urb);

    return usb_submit_urb(urb);;
}

static int qxdm_send_command(struct qxdm_ctx *ctx, const uint8_t *buf, size_t size)
{
    struct urb *urb = &ctx->sendUrb;

    if (ctx->sendBusy) {
        printf("%s send busyx\n", __func__);
        return -1;
    }

    urb->transfer_buffer = (uint8_t *)buf;
    urb->transfer_buffer_length = size;
    urb->actual_length = 0;
    //printf("%s size=%zd\n", __func__, size);
    ctx->sendBusy = 1;
    if (usb_submit_urb(&ctx->sendUrb) != 0 ) {
        return -2;
    }

    return 0;
}

static void qxdm_save_task(void *param) {
    struct qxdm_ctx *ctx = (struct qxdm_ctx *)param;

    while (1) {
        uint32_t qxdm_len;

        qxdm_len = ctx ->qxdm_wp - ctx ->qxdm_rp;
        while (qxdm_len == 0) {
            ctx ->qxdm_wait = 1;
            xSemaphoreTake(ctx ->xSemaphore, msec_to_ticks(100));
            qxdm_len = ctx ->qxdm_wp - ctx ->qxdm_rp;
        }
        ctx->qxdm_wait = 0;

        while (qxdm_len) {
            uint32_t qxdm_rp = ctx ->qxdm_rp%QXDMBufLen;
            uint32_t tail_room = QXDMBufLen - qxdm_rp;

            if (qxdm_len > tail_room)
                qxdm_len = tail_room;
            //save to qxdm_rp - qxdm_len to disk
            //printf("save rp:%u, len:%u\n", qxdm_rp, qxdm_len);
            ctx ->qxdm_rp += qxdm_len;
            qxdm_len = ctx ->qxdm_wp - ctx ->qxdm_rp;
        }

        if (ctx ->qxdm_drop) {
            printf("rp %u, drop %u\n", ctx ->qxdm_rp, ctx ->qxdm_drop);
            ctx ->qxdm_drop = 0;
        }
    }
}

static const struct usb_driver qxdm_dirver = {
    EC20_QXDM_INTERFACE,0xFF,
    hc_mask_bulk_in_out,
    qxdm_open,
    NULL
};

void qxdm_init(void) {
    struct qxdm_ctx *ctx = &qxdm_ctx[0];

    vSemaphoreCreateBinary(ctx->xSemaphore);
    xTaskCreate(qxdm_save_task, "qxdm", 2048, ctx, tskIDLE_PRIORITY + 1, NULL);
    usb_register_drv(&qxdm_dirver);
}

void qxdm_enable_log(int enable) {
    struct qxdm_ctx *ctx = &qxdm_ctx[0];
    const uint8_t *buf = qxdm_default_cfg;
    size_t size = sizeof(qxdm_default_cfg);
    size_t wc = 0;

    if (enable == 0) {
        qxdm_send_command(ctx, qxdm_disable_log_mask, sizeof(qxdm_disable_log_mask));
        msleep(100);
        qxdm_send_command(ctx,qxdm_disable_msg_mask, sizeof(qxdm_disable_msg_mask));
        msleep(100);
        qxdm_send_command(ctx,qxdm_disable_event_mask, sizeof(qxdm_disable_event_mask));
        msleep(100);

#if 0
{
        uint32_t retry = 0;
        uint32_t last_wp = 0;

        while (last_wp != qxdm_wp && retry++ < 200) {
            printf("qxdm %u -> %u\n", last_wp, qxdm_wp);
            last_wp = qxdm_wp;
            msleep(100);
        }
        printf("qxdm stop cost %u\n", retry);
}
#endif
        return;
    }

    while (wc < size) {
        size_t flag = wc;
        const uint8_t *cur = buf + wc;
        unsigned short len = cur[2] + (((unsigned short)cur[3]) << 8) + 5;

        if (cur[0] == 0x7e && cur[1] == 0x01 && (wc + len) <= size && cur[len - 1] == 0x7e) {
            flag += (len - 1);
        }
        else {
            if (flag == 0 && buf[flag] == 0x7E)
                flag++;

            while (buf[flag] != 0x7E && flag < size)
                flag++;
        }

        if (buf[flag] == 0x7E || flag == size) {
            qxdm_send_command(ctx, buf + wc, flag - wc + 1);
        }

        wc = flag + 1;
        msleep(100);
    }
}
#endif

