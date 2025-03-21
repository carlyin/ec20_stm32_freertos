#include "usbh_ec20.h"
#ifdef EC20_RMNET_INTERFACE

#define MUX_ID 0x81
extern int rmnet_init(void);
extern int rmnet_open(struct hc_info *hc_info, uint8_t QMAPversion, uint8_t MuxId);
extern void rmnet_netif_config(int up, IPV4_T *ipv4);

enum {
   QMUX_TYPE_CTL  = 0x00,
   QMUX_TYPE_WDS  = 0x01,
   QMUX_TYPE_DMS  = 0x02,
   QMUX_TYPE_NAS  = 0x03,
   QMUX_TYPE_WDS_ADMIN  = 0x1A,
};

#define QMICTL_GET_CLIENT_ID_REQ      0x0022
#define QMICTL_GET_CLIENT_ID_RESP     0x0022
#define QMICTL_SYNC_REQ               0x0027
#define QMICTL_SYNC_RESP              0x0027
#define QMICTL_SYNC_IND               0x0027

#define QMIWDS_ADMIN_SET_DATA_FORMAT_REQ      0x0020
#define QMIWDS_ADMIN_SET_DATA_FORMAT_RESP     0x0020

#define QMINAS_GET_SYS_INFO_REQ                 0x004D
#define QMINAS_GET_SYS_INFO_RESP                0x004D

#define QMIWDS_START_NETWORK_INTERFACE_REQ    0x0020
#define QMIWDS_START_NETWORK_INTERFACE_RESP   0x0020
#define QMIWDS_STOP_NETWORK_INTERFACE_REQ     0x0021
#define QMIWDS_STOP_NETWORK_INTERFACE_RESP    0x0021
#define QMIWDS_GET_PKT_SRVC_STATUS_REQ        0x0022
#define QMIWDS_GET_PKT_SRVC_STATUS_RESP       0x0022
#define QMIWDS_GET_PKT_SRVC_STATUS_IND        0x0022
#define QMIWDS_GET_RUNTIME_SETTINGS_REQ       0x002D
#define QMIWDS_GET_RUNTIME_SETTINGS_RESP      0x002D
#define QMIWDS_SET_CLIENT_IP_FAMILY_PREF_REQ  0x004D
#define QMIWDS_SET_CLIENT_IP_FAMILY_PREF_RESP 0x004D
#define QMIWDS_BIND_MUX_DATA_PORT_REQ         0x00A2
#define QMIWDS_BIND_MUX_DATA_PORT_RESP        0x00A2

#define QWDS_PKT_DATA_DISCONNECTED    0x01
#define QWDS_PKT_DATA_CONNECTED        0x02
#define QWDS_PKT_DATA_SUSPENDED        0x03
#define QWDS_PKT_DATA_AUTHENTICATING   0x04

struct qmi_tlv {
    uint8_t TLVType;
    uint8_t TLVLength[2];
    union {
        uint8_t u8;
        uint8_t u16[2];
        uint8_t u16u16[2][2];
        uint8_t u32[4];
        uint8_t u32u32[2][4];
    }u;
};

typedef struct _QCQMI {
    uint8_t  IFType;
    uint8_t QMILength[2];
    uint8_t  QMIFlags;  // reserved
    uint8_t  QMIType;
    uint8_t  ClientId;

    union {
        struct {
            uint8_t CtlFlags;  // 00-cmd, 01-rsp, 10-ind
            uint8_t TransactionId;
            uint8_t CTLType[2];
            uint8_t CTLLength[2];
        } ctl;
        struct {
            uint8_t  CtlFlags;      // 0: single QMUX Msg; 1:
            uint8_t TransactionId[2];
            uint8_t MUXType[2];
            uint8_t MUXLength[2];
        } mux;
    } b;
} QCQMI, *PQCQMI;

#define SendUrbBufLen 256
#define RecvUrbBufLen 2048
#define IntrUrbBufLen 12

struct qmi_ctx {
    SemaphoreHandle_t xSemaphore;
    uint8_t sendUrbBuf[SendUrbBufLen];
    struct urb sendUrb;

    uint8_t recvUrbBuf[RecvUrbBufLen];
    struct urb recvUrb;

    uint8_t intrUrbBuf[IntrUrbBufLen];
    struct urb intrUrb;

    uint16_t tid;
    uint16_t QMIResult;
    uint16_t QMIError;
    uint8_t QMAPVersion;
    uint8_t clientWDS;
    uint8_t clientNAS;
    uint8_t clientWDA;
    uint8_t  WdsConnectionStatus;
    uint8_t PSAttachedState;
    uint32_t WdsConnectionIPv4Handle;
    IPV4_T ipv4;
};
struct qmi_ctx qmi_ctx[1];


static uint16_t cpu_put16(uint8_t dst[2], uint16_t value) {
    dst[0] = (value >> 0) & 0xff;
    dst[1] = (value >> 8) & 0xff;
    return value;
}

static uint16_t cpu_get16(uint8_t src[2]) {
    return src[0] | (src[1] << 8);
}

static uint32_t cpu_get32(uint8_t src[4]) {
    return src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
}

static uint32_t cpu_put32(uint8_t dst[4], uint32_t value) {
    dst[0] = (value >> 0) & 0xff;
    dst[1] = (value >> 8) & 0xff;
    dst[2] = (value >> 16) & 0xff;
    dst[3] = (value >> 24) & 0xff;
    return value;
}

static uint16_t add_tlv_u8(PQCQMI pQMI, uint8_t TLVType, uint8_t value) {
    struct qmi_tlv *pTLV = (struct qmi_tlv *)(((uint8_t *)pQMI)+(cpu_get16(pQMI->QMILength) + 1));

    pTLV->TLVType = TLVType;
    cpu_put16(pTLV->TLVLength, sizeof(value));
    pTLV->u.u8 = value;
    return 3+sizeof(value);
}

#if 0
static uint16_t add_tlv_u16(PQCQMI pQMI, uint8_t TLVType, uint16_t value) {
    struct qmi_tlv *pTLV = (struct qmi_tlv *)(((uint8_t *)pQMI)+(cpu_get16(pQMI->QMILength) + 1));

    pTLV->TLVType = TLVType;
    cpu_put16(pTLV->TLVLength, sizeof(value));
    cpu_put16(pTLV->u.u16, value);

    return 3+sizeof(value);
}
#endif

static uint16_t add_tlv_u32(PQCQMI pQMI, uint8_t TLVType, uint32_t value) {
    struct qmi_tlv *pTLV = (struct qmi_tlv *)(((uint8_t *)pQMI)+(cpu_get16(pQMI->QMILength) + 1));

    pTLV->TLVType = TLVType;
    cpu_put16(pTLV->TLVLength, sizeof(value));
    cpu_put32(pTLV->u.u32, value);

    return 3+sizeof(value);
}

static uint16_t add_tlv_u32_u32(PQCQMI pQMI, uint8_t TLVType, uint32_t value1, uint32_t value2) {
    struct qmi_tlv *pTLV = (struct qmi_tlv *)(((uint8_t *)pQMI)+(cpu_get16(pQMI->QMILength) + 1));

    pTLV->TLVType = TLVType;
    cpu_put16(pTLV->TLVLength, sizeof(value1)+sizeof(value2));
    cpu_put32(pTLV->u.u32u32[0], value1);
    cpu_put32(pTLV->u.u32u32[1], value2);

    return 3+sizeof(value1)+sizeof(value2);
}

static uint16_t add_tlv_string(PQCQMI pReqQMI, uint8_t TLVType, const char *string) {
    struct qmi_tlv *pTLV = (struct qmi_tlv *)(((uint8_t *)pReqQMI)+(cpu_get16(pReqQMI->QMILength) + 1));
    size_t len = strlen(string);

    pTLV->TLVType = TLVType;
    cpu_put16(pTLV->TLVLength, len);
    memcpy(&pTLV->u, string, len);

    return 3+len;
}

// To retrieve the ith (Index) TLV
static struct qmi_tlv * GetTLV (struct qmi_tlv *pTLV, uint16_t size, uint8_t TLVType) {
    uint16_t offset = 0;
    uint8_t *start = (uint8_t *)pTLV;

    while (offset < size) {
        pTLV = (struct qmi_tlv *)(start + offset);
        if (pTLV->TLVType == TLVType && (offset + cpu_get16((pTLV->TLVLength)) <= size)) {
            return pTLV;
        }

        offset += (3 + cpu_get16((pTLV->TLVLength)));
    }

   return NULL;
}

static void debug_qmi(uint8_t *qmi, uint16_t length) {
    #if 1
    (void)qmi;
    (void)length;
    #else
    uint16_t i;
    for (i = 0; i < length; i++)
        printf("%02x ", qmi[i]);
    printf("\n");
    #endif
}

static int qmi_parse(struct qmi_ctx *ctx, uint8_t *pbuff, uint32_t length) {
    PQCQMI pSendQMI = (PQCQMI) ctx->sendUrbBuf;
    PQCQMI pRecvQMI = (PQCQMI) pbuff;
    struct qmi_tlv *pTLVResp;
    struct qmi_tlv *pTLV;
    uint16_t CTL_MUX_Type;
    uint16_t CTL_MUX_Length;
    uint16_t TransactionId;
    uint16_t QMIResult;
    uint16_t QMIError;

    debug_qmi((uint8_t *)pRecvQMI, cpu_get16(pRecvQMI->QMILength) + 1);
    if ((cpu_get16(pRecvQMI->QMILength) + 1) != length) {
        printf("too short qmi!\n");
        return 0;
    }

    if (pRecvQMI->QMIType == QMUX_TYPE_CTL) {
        CTL_MUX_Type = cpu_get16(pRecvQMI->b.ctl.CTLType);
        CTL_MUX_Length = cpu_get16(pRecvQMI->b.ctl.CTLLength);
        TransactionId = pRecvQMI->b.ctl.TransactionId;
        pTLVResp = (struct qmi_tlv *)(pbuff + 6 + sizeof(pRecvQMI->b.ctl));
        QMIResult = cpu_get16(pTLVResp->u.u16u16[0]);
        QMIError = cpu_get16(pTLVResp->u.u16u16[0]);
    }
    else {
        CTL_MUX_Type = cpu_get16(pRecvQMI->b.mux.MUXType);
        CTL_MUX_Length = cpu_get16(pRecvQMI->b.mux.MUXLength);
        TransactionId = cpu_get16(pRecvQMI->b.mux.TransactionId);
        pTLVResp = (struct qmi_tlv *)(pbuff + 6 + sizeof(pRecvQMI->b.mux));
        QMIResult = cpu_get16(pTLVResp->u.u16u16[0]);
        QMIError = cpu_get16(pTLVResp->u.u16u16[0]);
    }

    if (TransactionId == 0 || pRecvQMI->b.mux.CtlFlags == 0x4)
    {
        //indicate
        if (pRecvQMI->QMIType == QMUX_TYPE_CTL && CTL_MUX_Type == QMICTL_SYNC_IND)
        {
            printf("QMICTL_SYNC_IND\n");
        }
        else if (pRecvQMI->QMIType == QMUX_TYPE_WDS  && CTL_MUX_Type == QMIWDS_GET_PKT_SRVC_STATUS_IND)
        {
            printf("QMIWDS_GET_PKT_SRVC_STATUS_IND\n");
        }
        return 0;
    }
    else {
        if (QMIResult != 0 || QMIError != 0)
        {
            printf("QMIResult = 0x%x, QMIError= 0x%x\n", QMIResult, QMIError);
        }
    }

    if (pRecvQMI->QMIType == QMUX_TYPE_CTL)
    {
        if (CTL_MUX_Type == QMICTL_SYNC_RESP)
        {
            printf("QMICTL_SYNC_RESP\n");
        }
        else if (CTL_MUX_Type == QMICTL_GET_CLIENT_ID_RESP)
        {
            pTLV = GetTLV(pTLVResp, CTL_MUX_Length, 0x01);
            if (pTLV && cpu_get16(pTLV->TLVLength) == 2) {
                switch (pTLV->u.u16[0])
                {
                    case QMUX_TYPE_WDS:ctx->clientWDS = pTLV->u.u16[1]; printf("Get clientWDS = %d\n", ctx->clientWDS); break;
                    case QMUX_TYPE_NAS: ctx->clientNAS = pTLV->u.u16[1]; printf("Get clientNAS = %d\n", ctx->clientNAS); break;
                    case QMUX_TYPE_WDS_ADMIN: ctx->clientWDA = pTLV->u.u16[1]; printf("Get clientWDA = %d\n", ctx->clientWDA); break;
                    default: break;
                }
            }
        }
    }
    else if (pRecvQMI->QMIType == QMUX_TYPE_WDS)
    {
       if (CTL_MUX_Type == QMIWDS_GET_PKT_SRVC_STATUS_RESP)
       {
            printf("QMIWDS_GET_PKT_SRVC_STATUS_RESP\n");
            ctx->WdsConnectionStatus = QWDS_PKT_DATA_DISCONNECTED;
            pTLV = GetTLV(pTLVResp, CTL_MUX_Length, 0x01);
            if (pTLV) {
                ctx->WdsConnectionStatus = pTLV->u.u16[0];
                if ((cpu_get16(pTLV->TLVLength) == 2) && (pTLV->u.u16[1] == 0x01))
                    ctx->WdsConnectionStatus = QWDS_PKT_DATA_DISCONNECTED;
            }
       }
        else if (CTL_MUX_Type == QMIWDS_START_NETWORK_INTERFACE_RESP)
        {
            printf("QMIWDS_START_NETWORK_INTERFACE_RESP\n");
            pTLV = GetTLV(pTLVResp, CTL_MUX_Length, 0x01);
            if (pTLV) {
                ctx->WdsConnectionIPv4Handle = cpu_get32(pTLV->u.u32);
            }
        }
        else if (CTL_MUX_Type == QMIWDS_STOP_NETWORK_INTERFACE_RESP)
        {
            printf("QMIWDS_STOP_NETWORK_INTERFACE_RESP\n");
            if (QMIResult == 0 && QMIError == 0) {
                ctx->WdsConnectionIPv4Handle = 0;
            }
        }
        else if (CTL_MUX_Type == QMIWDS_GET_RUNTIME_SETTINGS_RESP)
        {
            printf("QMIWDS_GET_RUNTIME_SETTINGS_RESP\n");
            pTLV = GetTLV(pTLVResp, CTL_MUX_Length, 0x15);
            if (pTLV && cpu_get16(pTLV->TLVLength) == 4) {
                memcpy(ctx->ipv4.dns1, pTLV->u.u32, 4);
            }

            pTLV = GetTLV(pTLVResp, CTL_MUX_Length, 0x16);
            if (pTLV && cpu_get16(pTLV->TLVLength) == 4) {
                memcpy(ctx->ipv4.dns2, pTLV->u.u32, 4);
            }

            pTLV = GetTLV(pTLVResp, CTL_MUX_Length, 0x20);
            if (pTLV && cpu_get16(pTLV->TLVLength) == 4) {
                memcpy(ctx->ipv4.gate, pTLV->u.u32, 4);
            }

            pTLV = GetTLV(pTLVResp, CTL_MUX_Length, 0x21);
            if (pTLV && cpu_get16(pTLV->TLVLength) == 4) {
                memcpy(ctx->ipv4.mask, pTLV->u.u32, 4);
            }

            pTLV = GetTLV(pTLVResp, CTL_MUX_Length, 0x1E);
            if (pTLV && cpu_get16(pTLV->TLVLength) == 4) {
                memcpy(ctx->ipv4.addr, pTLV->u.u32, 4);
            }

        }
    }
    else if (pRecvQMI->QMIType == QMUX_TYPE_WDS_ADMIN)
    {
        if (CTL_MUX_Type == QMIWDS_ADMIN_SET_DATA_FORMAT_RESP)
        {
            printf("QMIWDS_ADMIN_SET_DATA_FORMAT_RESP\n");
        }
    }
    else if (pRecvQMI->QMIType == QMUX_TYPE_NAS)
    {
        if (CTL_MUX_Type == QMINAS_GET_SYS_INFO_RESP)
        {
            uint8_t caps[] = {0x4B, 0x19, 0x18, 0x17, 0x16, 0x15, 0}; //NR5G LTE WCDMA GSM HDR CDMA
            uint8_t cap = 0;
            while (caps[cap] != 0) {
                if ((pTLV = GetTLV(pTLVResp, CTL_MUX_Length, caps[cap])) != NULL) {
                    if (pTLV->u.u16[0] == 0x01) {
                        if (pTLV->u.u16[1] & 0x2) {
                            ctx->PSAttachedState = 1;
                        }
                    }
                }
                cap++;
            }
            printf("QMINAS_GET_SYS_INFO_RESP\n");
        }
    }

    if (pSendQMI->QMIType == pRecvQMI->QMIType && pSendQMI->ClientId == pRecvQMI->ClientId)
    {
        if ((pSendQMI->QMIType == QMUX_TYPE_CTL && pSendQMI->b.ctl.TransactionId == TransactionId)
            ||(pRecvQMI->QMIType != QMUX_TYPE_CTL && cpu_get16(pSendQMI->b.mux.TransactionId) == TransactionId))
        {
            ctx->QMIResult = QMIResult;
            ctx->QMIError = QMIError;
            xSemaphoreGive(ctx->xSemaphore);
            return 1;
        }
    }

    return 0;
}

static void qmi_send_urb_complete(struct urb *urb, int status) {
    struct qmi_ctx *ctx = (struct qmi_ctx *)urb->ctx;

    if (status != 0/* || urb->actual_length == 0*/) {
        printf("%s status=%d, actual_length=%u\n", __func__, status, urb->actual_length);
        return;
    }

    xSemaphoreGive(ctx->xSemaphore);
}

static void qmi_recv_urb_complete(struct urb *urb, int status) {
    struct qmi_ctx *ctx = (struct qmi_ctx *)urb->ctx;

    if (status != 0 || urb->actual_length == 0) {
        printf("%s status=%d, actual_length=%u\n", __func__, status, urb->actual_length);
        return;
    }

    if (1 == qmi_parse(ctx, urb->transfer_buffer, urb->actual_length)) {
    }
    usb_submit_urb(&ctx->intrUrb);
}

static void qmi_init_command(struct qmi_ctx *ctx, uint16_t QMIType, uint16_t CTL_MUX_Type) {
    PQCQMI pQMI  = (PQCQMI)ctx->sendUrbBuf;

    pQMI->IFType = 0x01;
    cpu_put16(pQMI->QMILength, 6 + ((QMUX_TYPE_CTL == QMIType) ? 6 : 7) - 1);
    pQMI->QMIFlags = 0x00;
    pQMI->QMIType  = QMIType;

    if (QMUX_TYPE_CTL == QMIType) {
        pQMI->ClientId = 0x00;
        pQMI->b.ctl.CtlFlags = 0x00;
        if ((ctx->tid&0xFF) == 0)
            ctx->tid++;
        pQMI->b.ctl.TransactionId = (ctx->tid++)&0xFF;
        cpu_put16(pQMI->b.ctl.CTLType, CTL_MUX_Type);
        cpu_put16(pQMI->b.ctl.CTLLength, 0);
    }
    else {
        switch (QMIType) {
            case QMUX_TYPE_WDS: pQMI->ClientId = ctx->clientWDS; break;
            case QMUX_TYPE_NAS: pQMI->ClientId = ctx->clientNAS; break;
            case QMUX_TYPE_WDS_ADMIN: pQMI->ClientId = ctx->clientWDA; break;
            default: pQMI->ClientId = 0; break;
        }
        pQMI->b.mux.CtlFlags = 0x00;
        if (ctx->tid == 0)
            ctx->tid++;
        cpu_put16(pQMI->b.mux.TransactionId, ctx->tid++);
        cpu_put16(pQMI->b.mux.MUXType, CTL_MUX_Type);
        cpu_put16(pQMI->b.mux.MUXLength, 0);
    }
}

static void qmi_add_payload(PQCQMI pQMI, uint16_t length) {
    cpu_put16(pQMI->QMILength, cpu_get16(pQMI->QMILength) + length);
    if (QMUX_TYPE_CTL == pQMI->QMIType) {
        cpu_put16(pQMI->b.ctl.CTLLength, cpu_get16(pQMI->b.ctl.CTLLength) + length);
    }
    else {
        cpu_put16(pQMI->b.mux.MUXLength, cpu_get16(pQMI->b.mux.MUXLength) + length);
    }
}

static int qmi_send_command(struct qmi_ctx *ctx, PQCQMI pReqQMI) {
    struct urb *urb = &ctx->sendUrb;
    struct control_setup setup;

    setup.bRequestType = 0x21; //USB_REQ_TYPE_CLASS | USB_REQ_RECIPIENT_INTERFACE;
    setup.bRequest = 0x00;
    setup.wValue = 0x00;
    setup.wIndex= EC20_RMNET_INTERFACE;
    setup.wLength = cpu_get16(pReqQMI->QMILength) + 1;

    urb->transfer_buffer = (uint8_t *)pReqQMI;
    urb->transfer_buffer_length = cpu_get16(pReqQMI->QMILength) + 1;

    //msleep(10);
    debug_qmi(urb->transfer_buffer, urb->transfer_buffer_length);
    while (xSemaphoreTake(ctx->xSemaphore, msec_to_ticks(0)) == pdTRUE);
    if (usb_submit_control_urb(urb, (uint8_t *)&setup) != 0) {
        return -1;
    }

    if (xSemaphoreTake(ctx->xSemaphore, msec_to_ticks(3000)) != pdTRUE) {
        rmnet_control_debug = 1;
        printf("send qmi timeout\n");
        return -2;
    }

    if (xSemaphoreTake(ctx->xSemaphore, msec_to_ticks(5000)) != pdTRUE) {
        rmnet_control_debug = 1;
        printf("recv qmi timeout\n");
        return -2;
    }

    return ctx->QMIResult;
}

int qmi_query_data_call(void) {
    struct qmi_ctx *ctx = &qmi_ctx[0];
    PQCQMI pReqQMI  = (PQCQMI)ctx->sendUrbBuf;

    if (ctx->clientNAS == 0)
        return -1;

    qmi_init_command(ctx, QMUX_TYPE_WDS, QMIWDS_GET_PKT_SRVC_STATUS_REQ);
    if (qmi_send_command(ctx, pReqQMI) != 0) {
        return 0;
    }
    printf("IPv4State %s\n", (ctx->WdsConnectionStatus == QWDS_PKT_DATA_CONNECTED) ? "CONNECTED" : "DISCONNECTED");

    if (ctx->WdsConnectionStatus == QWDS_PKT_DATA_DISCONNECTED) {
        rmnet_netif_config(0, NULL);
    }
    return ctx->WdsConnectionStatus == QWDS_PKT_DATA_CONNECTED;
}

int qmi_setup_data_call(int enable) {
    struct qmi_ctx *ctx = &qmi_ctx[0];
    PQCQMI pReqQMI  = (PQCQMI)ctx->sendUrbBuf;

    if (ctx->clientNAS == 0 || ctx->clientWDS == 0)
        return -1;

    qmi_query_data_call();
    if (enable == 1 && ctx->WdsConnectionStatus == QWDS_PKT_DATA_DISCONNECTED) {
        const char *apn = NULL, *user = NULL, *password = NULL;

        ctx->PSAttachedState = 0;
        qmi_init_command(ctx, QMUX_TYPE_NAS, QMINAS_GET_SYS_INFO_REQ);
        printf("QMINAS_GET_SYS_INFO_REQ %u\n", cpu_get16(pReqQMI->QMILength) + 1);
        if (qmi_send_command(ctx, pReqQMI) != 0 || ctx->PSAttachedState == 0) {
            return 0;
        }

        printf("QMIWDS_START_NETWORK_INTERFACE_REQ %u\n", cpu_get16(pReqQMI->QMILength) + 1);
        qmi_init_command(ctx, QMUX_TYPE_WDS, QMIWDS_START_NETWORK_INTERFACE_REQ);
        qmi_add_payload(pReqQMI, add_tlv_u8(pReqQMI, 0x30, 1)); //3GPP
        if (apn) {
            qmi_add_payload(pReqQMI, add_tlv_string(pReqQMI, 0x14, apn));
        }
        if (user && password) {
            qmi_add_payload(pReqQMI, add_tlv_u8(pReqQMI, 0x16, 0));  //0 ~ None, 1 ~ Pap, 2 ~ Chap, 3 ~ MsChapV2
            qmi_add_payload(pReqQMI, add_tlv_string(pReqQMI, 0x17, user));
            qmi_add_payload(pReqQMI, add_tlv_string(pReqQMI, 0x18, password));
        }
        qmi_add_payload(pReqQMI, add_tlv_u8(pReqQMI, 0x19, 4)); //IPV4
        qmi_add_payload(pReqQMI, add_tlv_u8(pReqQMI, 0x31, 1)); //profile_index
        if (qmi_send_command(ctx, pReqQMI) != 0) {
            return 0;
        }
        printf("IPv4Handle: 0x%08x\n", ctx->WdsConnectionIPv4Handle);
        if (ctx->WdsConnectionIPv4Handle == 0) {
            return 0;
        }

        printf("QMIWDS_GET_RUNTIME_SETTINGS_REQ %u\n", cpu_get16(pReqQMI->QMILength) + 1);
        qmi_init_command(ctx, QMUX_TYPE_WDS, QMIWDS_GET_RUNTIME_SETTINGS_REQ);
        qmi_add_payload(pReqQMI, add_tlv_u32(pReqQMI, 0x10, (0x0010 | 0x0100 | 0x0200 | 0x2000)));
        if (qmi_send_command(ctx, pReqQMI) != 0) {
            return 0;
        }
        rmnet_netif_config(1, &ctx->ipv4);

        return 1;
    }
    else if (enable == 0 && ctx->WdsConnectionStatus == QWDS_PKT_DATA_CONNECTED) {
        qmi_init_command(ctx, QMUX_TYPE_WDS, QMIWDS_STOP_NETWORK_INTERFACE_REQ);
        qmi_add_payload(pReqQMI, add_tlv_u32(pReqQMI, 0x01, ctx->WdsConnectionIPv4Handle));
        if (qmi_send_command(ctx, pReqQMI) != 0) {
            return 0;
        }
        ctx->WdsConnectionIPv4Handle = 0;
        rmnet_netif_config(0, NULL);
    }

    return 1;
}

static int qtl_qmi_sync(struct qmi_ctx *ctx)
{
    int i = 0, ret;
    PQCQMI pReqQMI  = (PQCQMI)ctx->sendUrbBuf;

    while (i++ < 10) {
        printf("QMICTL_SYNC_REQ %d\n", i);
        qmi_init_command(ctx, QMUX_TYPE_CTL, QMICTL_SYNC_REQ);
        ret = qmi_send_command(ctx, pReqQMI);
        if (ret == 0)
            break;
    }

    if (ret) {
        printf("QMICTL_SYNC_REQ fail\n");
        return 0;
    }

    qmi_init_command(ctx, QMUX_TYPE_CTL, QMICTL_GET_CLIENT_ID_REQ);
    qmi_add_payload(pReqQMI, add_tlv_u8(pReqQMI, 1, QMUX_TYPE_WDS_ADMIN));
    printf("QMICTL_GET_CLIENT_ID_REQ WDS_ADMIN %u\n", cpu_get16(pReqQMI->QMILength) + 1);
    if (qmi_send_command(ctx, pReqQMI) != 0 || ctx->clientWDA == 0) {
        return 0;
    }

    qmi_init_command(ctx, QMUX_TYPE_WDS_ADMIN, QMIWDS_ADMIN_SET_DATA_FORMAT_REQ);
    qmi_add_payload(pReqQMI, add_tlv_u8(pReqQMI, 0x10, 0));
    qmi_add_payload(pReqQMI, add_tlv_u32(pReqQMI, 0x11, 2));
    qmi_add_payload(pReqQMI, add_tlv_u32(pReqQMI, 0x12, ctx->QMAPVersion));
    qmi_add_payload(pReqQMI, add_tlv_u32(pReqQMI, 0x13, ctx->QMAPVersion));
    qmi_add_payload(pReqQMI, add_tlv_u32(pReqQMI, 0x15, 1));  //DL
    qmi_add_payload(pReqQMI, add_tlv_u32(pReqQMI, 0x16, 1508));
    qmi_add_payload(pReqQMI, add_tlv_u32_u32(pReqQMI, 0x17, 2, 4));
    qmi_add_payload(pReqQMI, add_tlv_u32(pReqQMI, 0x19, 0));
    qmi_add_payload(pReqQMI, add_tlv_u32(pReqQMI, 0x1B, 1));  //UL
    qmi_add_payload(pReqQMI, add_tlv_u32(pReqQMI, 0x1C, 1508));
    printf("QMIWDS_ADMIN_SET_DATA_FORMAT_REQ %u\n", cpu_get16(pReqQMI->QMILength) + 1);
    if (qmi_send_command(ctx, pReqQMI) != 0) {
        return 0;
    }

    qmi_init_command(ctx, QMUX_TYPE_CTL, QMICTL_GET_CLIENT_ID_REQ);
    qmi_add_payload(pReqQMI, add_tlv_u8(pReqQMI, 1, QMUX_TYPE_NAS));
    printf("QMICTL_GET_CLIENT_ID_REQ NAS %u\n", cpu_get16(pReqQMI->QMILength) + 1);
    if (qmi_send_command(ctx, pReqQMI) != 0 || ctx->clientNAS == 0) {
        return 0;
    }

    qmi_init_command(ctx, QMUX_TYPE_CTL, QMICTL_GET_CLIENT_ID_REQ);
    qmi_add_payload(pReqQMI, add_tlv_u8(pReqQMI, 1, QMUX_TYPE_WDS));
    printf("QMICTL_GET_CLIENT_ID_REQ WDS %u\n", cpu_get16(pReqQMI->QMILength) + 1);
    if (qmi_send_command(ctx, pReqQMI) != 0 || ctx->clientWDS == 0) {
        return 0;
    }

    qmi_init_command(ctx, QMUX_TYPE_WDS, QMIWDS_BIND_MUX_DATA_PORT_REQ);
    qmi_add_payload(pReqQMI, add_tlv_u32_u32(pReqQMI, 0x10, 2, 4));
    qmi_add_payload(pReqQMI, add_tlv_u8(pReqQMI, 0x11, MUX_ID));
    qmi_add_payload(pReqQMI, add_tlv_u32(pReqQMI, 0x13, 1));
    printf("QMIWDS_BIND_MUX_DATA_PORT_REQ %u\n", cpu_get16(pReqQMI->QMILength) + 1);
    if (qmi_send_command(ctx, pReqQMI) != 0) {
        return 0;
    }

    qmi_init_command(ctx, QMUX_TYPE_WDS, QMIWDS_SET_CLIENT_IP_FAMILY_PREF_REQ);
    qmi_add_payload(pReqQMI, add_tlv_u32_u32(pReqQMI, 0x10, 2, 4));
    qmi_add_payload(pReqQMI, add_tlv_u8(pReqQMI, 0x01, 4));
    printf("QMIWDS_SET_CLIENT_IP_FAMILY_PREF_REQ WDS %u\n", cpu_get16(pReqQMI->QMILength) + 1);
    if (qmi_send_command(ctx, pReqQMI) != 0) {
        return 0;
    }

    return 1;
}

static void qmi_intr_urb_complete(struct urb *urb, int status) {
    struct qmi_ctx *ctx = (struct qmi_ctx *)urb->ctx;

    if (status != 0 || urb->actual_length == 0) {
        printf("%s status=%d, actual_length=%u\n", __func__, status, urb->actual_length);
        return;
    }

    if (urb->actual_length) {
        struct usb_cdc_notification *dr = (struct usb_cdc_notification *)urb->transfer_buffer;

#if 0
        int i;

        for (i = 0; i < urb->actual_length; i++)
            printf("%02x ", urb->transfer_buffer[i]);
        printf("\n");
#endif

        if (dr->bRequestType == 0xa1 && dr->bRequest == 0x01 && dr->wIndex == EC20_RMNET_INTERFACE) {
            struct control_setup setup;

            setup.bRequestType = 0xa1; //USB_D2H | USB_REQ_TYPE_CLASS | USB_REQ_RECIPIENT_INTERFACE;
            setup.bRequest = 0x01;
            setup.wValue = 0x00;
            setup.wIndex= EC20_RMNET_INTERFACE;
            setup.wLength = RecvUrbBufLen;
            usb_submit_control_urb(&ctx->recvUrb, (uint8_t *)&setup);
        }
        else {
            usb_submit_urb(urb);
        }
    }
}

static int qmi_open(struct hc_info *hc_info, uint16_t idProduct) {
    struct qmi_ctx *ctx = &qmi_ctx[0];
    struct urb *urb;

    printf("%s hc_num=%u,%u,%u\n", __func__,
        get_hc_num(HC_BULK_OUT), get_hc_num(HC_BULK_IN), get_hc_num(HC_INTR));

    urb = &ctx->sendUrb;
    urb ->urb_state = URB_STATE_IDLE;
    urb ->complete = qmi_send_urb_complete;
    urb->ctx = ctx;

    urb = &ctx->recvUrb;
    urb ->urb_state = URB_STATE_IDLE;
    urb ->transfer_buffer = ctx->recvUrbBuf;
    urb ->transfer_buffer_length = RecvUrbBufLen;
    urb ->complete = qmi_recv_urb_complete;
    urb->ctx = ctx;

    urb = &ctx->intrUrb;
    urb ->hc_type = HC_INTR;
    urb ->hc_num = get_hc_num(urb ->hc_type);
    urb ->ep_size = get_ep_size(urb ->hc_type);
    urb ->urb_state = URB_STATE_IDLE;
    urb ->transfer_buffer = ctx->intrUrbBuf;
    urb ->transfer_buffer_length = urb ->ep_size;
    urb ->complete = qmi_intr_urb_complete;
    urb->ctx = ctx;
    usb_start_urb(urb);

    ctx->QMAPVersion = 5; //EC20 ~ 5, RG500 ~ 9
    if (idProduct == 0x0800 || idProduct == 0x0801)
        ctx->QMAPVersion = 9;
    rmnet_open(hc_info, ctx->QMAPVersion, MUX_ID);

    return 0;//usb_submit_urb(urb);;
}

static const struct usb_driver qmi_dirver = {
    EC20_RMNET_INTERFACE,0xFF,
    hc_mask_bulk_in_out_int,
    qmi_open,
    NULL
};

void qmi_init(void) {
    struct qmi_ctx *ctx = &qmi_ctx[0];

    rmnet_init();
    vSemaphoreCreateBinary(ctx->xSemaphore);
    usb_register_drv(&qmi_dirver);
}

void qmi_enable_dtr(int enable) {
    struct qmi_ctx *ctx = &qmi_ctx[0];
    struct urb *urb = &ctx->sendUrb;
    struct control_setup  setup;

    if (ctx->QMAPVersion == 0) {
        return;
    }

    setup.bRequestType = 0x21;
    setup.bRequest = 0x22;
    setup.wValue = enable ? 1 : 0;
    setup.wIndex= EC20_RMNET_INTERFACE;
    setup.wLength = 0;

    urb->transfer_buffer = NULL;
    urb->transfer_buffer_length = 0;
    while (xSemaphoreTake(ctx->xSemaphore, msec_to_ticks(0)) == pdTRUE);
    usb_submit_control_urb(urb, (uint8_t *)&setup);
    if (xSemaphoreTake(ctx->xSemaphore, msec_to_ticks(3000)) != pdTRUE) {
        printf("qmi_enable_dtr timeout\n");
        return;
    }

    usb_submit_urb(&ctx->intrUrb);
    msleep(1000);
    if(qtl_qmi_sync(ctx) != 1)
        return;
}
#endif
