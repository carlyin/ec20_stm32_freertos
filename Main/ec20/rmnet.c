#include "usbh_ec20.h"
#ifdef EC20_RMNET_INTERFACE

#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/lwip_timers.h"
#include "lwip/tcpip.h"

struct qmap_hdr {
    uint8_t cd_rsvd_pad;
    uint8_t mux_id;
    uint8_t pkt_len[2];
};

#define RMNET_MTU 1508

struct rmnet_ctx {
    SemaphoreHandle_t sendSemaphoreId;
    uint8_t sendUrbBuf[RMNET_MTU];
    struct urb sendUrb;
    int sendBusy;

    SemaphoreHandle_t recvSemaphoreId;
    uint8_t recvUrbBuf[RMNET_MTU];
    struct urb recvUrb;
    int recvReady;

    uint8_t QMAPversion;
    struct netif netif;
};

static struct rmnet_ctx rmnet_ctx[1];

#if 0
static void dump_hex(uint8_t *buf, uint16_t len) {
    int i = 0;

    for (i = 0; i < len; i++) {
        printf("%02x", *buf++);
    }
    printf("\n");
}
#endif

static void rmnet_recv_urb_complete(struct urb *urb, int status) {
    struct rmnet_ctx *ctx = (struct rmnet_ctx *)urb->ctx;

    if (status != 0) {
        printf("%s status=%d\n", __func__, status);
        return;
    }
    //printf("%s len=%u\n", __func__, urb->actual_length);

    ctx->recvReady = 1;
    xSemaphoreGive(ctx->recvSemaphoreId);
}

static void rmnet_recv_task(void *param) {
    struct rmnet_ctx *ctx = (struct rmnet_ctx *)param;
    struct netif *netif = &ctx->netif;
    struct urb *urb = &ctx->recvUrb;
    u16_t hdr_len;

    while (ctx->QMAPversion == 0) {
        xSemaphoreTake(ctx->recvSemaphoreId,  msec_to_ticks(1000));
    }
    hdr_len = ctx->QMAPversion == 9 ? 8 : 4;;

    while (1) {
        err_t err;
        struct pbuf *p, *q;
        u16_t offset = 0;

        while (ctx->recvReady == 0) {
            xSemaphoreTake(ctx->recvSemaphoreId, msec_to_ticks(1000));
        }

        while ((offset + hdr_len) <= urb->actual_length) {
            uint8_t *pbuf = urb->transfer_buffer + offset;
            struct qmap_hdr *hdr = (struct qmap_hdr*)pbuf;
            u16_t pkt_len = (hdr->pkt_len[0]<<8) + hdr->pkt_len[1];

            //printf("offset=%u, pkt_len=%u\n", offset, pkt_len);
            //dump_hex(pbuf, 12);
            pbuf += hdr_len;
            offset += (hdr_len + pkt_len);
            pkt_len -= (hdr->cd_rsvd_pad&0x3f);

            p = pbuf_alloc(PBUF_RAW, pkt_len, PBUF_POOL);
            if (p) {
                for (q = p; q != NULL; q = q->next)
                {
                    //printf("{%u, %u}\n", (pkt_len - q->tot_len), q->len);
                    memcpy(q->payload, pbuf + (pkt_len - q->tot_len), q->len);
                }

                err = netif->input(p, netif);
                if (err != ERR_OK)
                {
                    printf("IP input error\n");
                    pbuf_free(p);
                    p = NULL;
                }
            }
        }

        ctx->recvReady = 0;
        urb->transfer_buffer[0] = 0;
        urb->transfer_buffer[1] = 0;
        urb->transfer_buffer[2] = 0;
        urb->transfer_buffer[3] = 0;
        usb_submit_urb(urb);
    }
}

static void rmnet_send_urb_complete(struct urb *urb, int status) {
    struct rmnet_ctx *ctx = (struct rmnet_ctx *)urb->ctx;

    if (status != 0) {
        printf("%s status=%d\n", __func__, status);
        return;
    }

    //printf("%s len = %u\n", __func__, urb->actual_length);
    ctx->sendBusy = 0;
    xSemaphoreGive(ctx->sendSemaphoreId);
}

extern int rmnet_bulk_out_debug;
static err_t lwip_netif_output(struct netif *netif, struct pbuf *p,  ip_addr_t *ipaddr)
{
    struct rmnet_ctx *ctx = &rmnet_ctx[0];
    struct urb *urb = &ctx->sendUrb;
    struct pbuf *q;
    u16_t hdr_len = ctx->QMAPversion == 9 ? 8 : 4;
    u16_t pkt_len = 0;
    u16_t pad_len = 0;
    struct qmap_hdr *hdr = (struct qmap_hdr*)urb->transfer_buffer;

    //printf("%s %d\n", __func__, ctx->sendBusy);
    if (xSemaphoreTake(ctx->sendSemaphoreId, msec_to_ticks(5000)) != pdTRUE) {
        printf("%s tx timeout!\n", __func__);
        rmnet_bulk_out_debug = 1;
        return ERR_TIMEOUT;
    }
    ctx->sendBusy = 1;

    for(q = p; q != NULL; q = q->next)
    {
        /* Add code source to send data from the pbuf to the interface,
        one pbuf at a time. The size of the data in each pbuf is kept
        in the ->len variable. */
        memcpy(urb->transfer_buffer + hdr_len + pkt_len, q->payload, q->len);
        pkt_len += q->len;
    }

    pad_len = (pkt_len%4);
    if (pad_len) {
        pad_len = 4 - pad_len;
        pkt_len += pad_len;
    }
#if 1
        if (((pkt_len + hdr_len)%urb->ep_size) == 0) {
            //printf("tx usb_zero_bug_fix! %d\n", (pkt_len + hdr_len));
            pad_len += 4;
            pkt_len += 4;
        }
#endif
    if (ctx->QMAPversion == 9) {
        hdr->cd_rsvd_pad = 0x40|pad_len;
    }
    else {
        hdr->cd_rsvd_pad = pkt_len;
    }

    hdr->pkt_len[0] = pkt_len>>8;
    hdr->pkt_len[1] = pkt_len&0xFF;

    urb->transfer_buffer_length = hdr_len + pkt_len;
    //printf("%s %d\n", __func__, urb->transfer_buffer_length);
    //dump_hex(urb->transfer_buffer, 12);
    if (usb_submit_urb(&ctx->sendUrb) != 0 ) {
        printf("%s tx usb error!\n", __func__);
        return ERR_TIMEOUT;
    }

    /* signal that packet should be sent */
    return ERR_OK;
}

/**
  * Should be called at the beginning of the program to set up the
  * network interface. It calls the function low_level_init() to do the
  * actual setup of the hardware.
  *
  * This function should be passed as a parameter to netif_add().
  *
  * @param netif the lwip network interface structure for this ethernetif
  * @return ERR_OK if the loopif is initialized
  *         ERR_MEM if private data couldn't be allocated
  *         any other err_t on error
  */
static err_t lwip_netif_init(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  /* Initialize interface hostname */
  netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

  netif->name[0] = 's';
  netif->name[1] = 't';

/* We directly use etharp_output() here to save a function call.
   * You can instead declare your own function an call etharp_output()
   * from it if you have to do some checks before sending (e.g. if link
   * is available...) */
  netif->output = lwip_netif_output;
  //netif->linkoutput = low_level_output;

  netif->mtu = 1500;
  netif->flags = NETIF_FLAG_LINK_UP;

  return ERR_OK;
}

int rmnet_init(void)
{
    struct rmnet_ctx *ctx = &rmnet_ctx[0];
    struct netif * netif = &ctx ->netif;

    vSemaphoreCreateBinary(ctx->sendSemaphoreId);
    vSemaphoreCreateBinary(ctx->recvSemaphoreId);
    xTaskCreate(rmnet_recv_task, "rmnet", 1024, ctx, tskIDLE_PRIORITY + 1, NULL);
    tcpip_init(NULL, NULL);
    netif_add(netif, NULL, NULL, NULL, NULL, &lwip_netif_init, &tcpip_input);
    netif_set_down(netif);
    netif_set_default(netif);

    return 0;
}

int rmnet_open(struct hc_info *hc_info, uint8_t QMAPversion, uint8_t MuxId)
{
    struct rmnet_ctx *ctx = &rmnet_ctx[0];
    struct urb *urb;
    struct qmap_hdr *hdr = (struct qmap_hdr*)ctx->sendUrbBuf;

    ctx ->QMAPversion = QMAPversion;
    hdr->mux_id = MuxId;
    if (QMAPversion == 9) {
        ctx->sendUrbBuf[4] = 0x04;
        ctx->sendUrbBuf[5] = 0x80;
        ctx->sendUrbBuf[6] = 0x00;
        ctx->sendUrbBuf[7] = 0x00;
    }

    urb = &ctx->sendUrb;
    urb ->hc_type = HC_BULK_OUT;
    urb ->hc_num = get_hc_num(urb ->hc_type);
    urb ->ep_size = get_ep_size(urb ->hc_type);
    urb ->urb_state = URB_STATE_IDLE;
    urb ->transfer_buffer = ctx->sendUrbBuf;
    urb ->complete = rmnet_send_urb_complete;
    urb ->ctx = ctx;
    usb_start_urb(urb);

    urb = &ctx->recvUrb;
    urb ->hc_type = HC_BULK_IN;
    urb ->hc_num = get_hc_num(urb ->hc_type);
    urb ->ep_size = get_ep_size(urb ->hc_type);
    urb ->urb_state = URB_STATE_IDLE;
    urb ->transfer_buffer = ctx->recvUrbBuf;
    urb ->transfer_buffer_length = RMNET_MTU;
    urb ->complete = rmnet_recv_urb_complete;
    urb ->ctx = ctx;
    usb_start_urb(urb);
    usb_submit_urb(urb);
    xSemaphoreGive(ctx->recvSemaphoreId);

    return 0;
}

void rmnet_netif_config(int up, IPV4_T *ipv4)
{
    struct rmnet_ctx *ctx = &rmnet_ctx[0];
    struct netif * netif = &ctx ->netif;
    struct ip_addr ipaddr;
    struct ip_addr netmask;
    struct ip_addr gw;

    if (up == 0) {
        netif_set_down(netif);
        return;
    }

    printf("ipaddr: %d.%d.%d.%d\n", ipv4->addr[3], ipv4->addr[2], ipv4->addr[1], ipv4->addr[0]);
    printf("netmask: %d.%d.%d.%d\n", ipv4->mask[3], ipv4->mask[2], ipv4->mask[1], ipv4->mask[0]);
    printf("gw: %d.%d.%d.%d\n", ipv4->gate[3], ipv4->gate[2], ipv4->gate[1], ipv4->gate[0]);

    IP4_ADDR(&ipaddr, ipv4->addr[3], ipv4->addr[2], ipv4->addr[1], ipv4->addr[0]);
    IP4_ADDR(&netmask,  ipv4->mask[3], ipv4->mask[2], ipv4->mask[1], ipv4->mask[0]);
    IP4_ADDR(&gw, ipv4->gate[3], ipv4->gate[2], ipv4->gate[1], ipv4->gate[0]);

    netif_set_addr(netif, &ipaddr, &netmask, &gw);
    netif_set_default(netif);
    netif_set_up(netif);
}

struct netif * rmnet_get_netif(void) {
    struct rmnet_ctx *ctx = &rmnet_ctx[0];

    return  &ctx ->netif;
}

#endif
