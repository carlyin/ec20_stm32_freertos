#include "usbh_ec20.h"

#ifdef EC20_ECM_INTERFACE

#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/lwip_timers.h"
#include "lwip/tcpip.h"

extern uint8_t cdc_ether_iMACAddress;

#define ECM_MTU 1514
#define IntrUrbBufLen 64
#define H_IPV4 (0x0800)
#define H_IPV6 (0x86dd)
#define H_ARP  (0x0806)

#define ETH_ALEN	6
struct ethhdr {
    uint8_t 	h_dest[ETH_ALEN];	/* destination eth addr	*/
    uint8_t	       h_source[ETH_ALEN];	/* source ether addr	*/
    uint16_t	h_proto;		/* packet type ID field	*/
};

struct ecm_ctx {
    SemaphoreHandle_t sendSemaphoreId;
    uint8_t sendUrbBuf[ECM_MTU];
    struct urb sendUrb;
    int sendBusy;

    SemaphoreHandle_t recvSemaphoreId;
    uint8_t recvUrbBuf[ECM_MTU];
    struct urb recvUrb;
    int recvReady;

    uint8_t intrUrbBuf[IntrUrbBufLen];
    struct urb intrUrb;
    struct urb controlUrb;

    struct netif netif;
    uint8_t carrier;
};

static struct ecm_ctx ecm_ctx[1];

static void dump_hex(uint8_t *buf, uint16_t len) {
#if 1
    (void)buf;
    (void)len;
#else
    int i = 0;

    for (i = 0; i < len; i++) {
        printf("%02x", *buf++);
    }
    printf("\n");
#endif
}

static uint8_t ch2value(uint8_t ch) {
    uint8_t ret = 0xFF;

    if (('0' <= ch) && (ch <= '9'))
        ret = ch - '0';
    else if (('a' <= ch) && (ch <= 'f'))
        ret = ch - 'a' + 10;
    else if (('A' <= ch) && (ch <= 'F'))
        ret = ch - 'A' + 10;
    return ret;
}

static int ecm_set_interface(struct ecm_ctx *ctx) {
    struct urb *urb = &ctx->controlUrb;
    struct control_setup  setup;
    
    printf("%s\n", __func__);

    setup.bRequestType = 0x01;
    setup.bRequest = 0x0b;
    setup.wValue = 0x01;
    setup.wIndex= EC20_ECM_INTERFACE+1;
    setup.wLength = 0;

    urb->transfer_buffer = ctx->sendUrbBuf;
    urb->transfer_buffer_length = 0xFF;

    while (xSemaphoreTake(ctx->sendSemaphoreId, msec_to_ticks(0)) == pdTRUE);
    usb_submit_control_urb(urb, (uint8_t *)&setup);
    if (xSemaphoreTake(ctx->sendSemaphoreId, msec_to_ticks(3000)) != pdTRUE) {
        printf("%s timeout\n", __func__);
        return 0;
    }

    return 1;
}

static int ecm_get_ethernet_addr(struct ecm_ctx *ctx) {
    struct urb *urb = &ctx->controlUrb;
    struct ethhdr *ethhdr = (struct ethhdr *)ctx->sendUrbBuf;
    struct control_setup  setup;
    
    printf("%s iMACAddress:%u\n", __func__, cdc_ether_iMACAddress);
    if (cdc_ether_iMACAddress == 0)
        return 0;

#define USB_DESC_TYPE_STRING 0x03    
    setup.bRequestType = 0x80;
    setup.bRequest = 0x06;
    setup.wValue = (USB_DESC_TYPE_STRING<< 8) | cdc_ether_iMACAddress;
    setup.wIndex= 0x0409;
    setup.wLength = 0xFF;

    urb->transfer_buffer = ctx->sendUrbBuf + sizeof(struct ethhdr);
    urb->transfer_buffer_length = 0xFF;

    while (xSemaphoreTake(ctx->sendSemaphoreId, msec_to_ticks(0)) == pdTRUE);
    usb_submit_control_urb(urb, (uint8_t *)&setup);
    if (xSemaphoreTake(ctx->sendSemaphoreId, msec_to_ticks(3000)) != pdTRUE) {
        printf("%s timeout\n", __func__);
        return 0;
    }

    dump_hex(urb->transfer_buffer, urb->actual_length);
    if (urb->actual_length >= 26 && urb->transfer_buffer[0] >= 26
        && urb->transfer_buffer[1] == USB_DESC_TYPE_STRING) {
        int i = 0;
        for (i = 0; i < ETH_ALEN; i++) {
            uint8_t h = ch2value(urb->transfer_buffer[2+i*4+0]);
            uint8_t l = ch2value(urb->transfer_buffer[2+i*4+2]);
            ethhdr->h_source[i] = (h << 4) + l;
        }
    }
    else {
        printf("%s actual_length:%u, %02x%02x\n", __func__, urb->actual_length,
            urb->transfer_buffer[0], urb->transfer_buffer[1]);
        return 0;
    }

#if 0
    printf("MACAddress: %02x:%02x:%02x:%02x:%02x:%02x\n",
        ethhdr->h_source[0], ethhdr->h_source[1], ethhdr->h_source[2],
        ethhdr->h_source[3], ethhdr->h_source[4], ethhdr->h_source[5]);
#endif

    memset(ethhdr->h_dest, 0xFF, ETH_ALEN);
    ethhdr->h_proto = htons(H_IPV4);
    ctx->netif.hwaddr_len = ETH_ALEN;
    memcpy(ctx->netif.hwaddr, ethhdr->h_source, ETH_ALEN);
   
    return 1;
}

static int ecm_update_filter(struct ecm_ctx *ctx) {
    struct urb *urb = &ctx->controlUrb;
    struct control_setup  setup;
    
    printf("%s\n", __func__);
    setup.bRequestType = 0x21;
    setup.bRequest = 0x43;
    setup.wValue = 0xe;
    setup.wIndex= EC20_ECM_INTERFACE;
    setup.wLength = 0;

    urb->transfer_buffer = NULL;
    urb->transfer_buffer_length = 0;

    while (xSemaphoreTake(ctx->sendSemaphoreId, msec_to_ticks(0)) == pdTRUE);
    usb_submit_control_urb(urb, (uint8_t *)&setup);
    if (xSemaphoreTake(ctx->sendSemaphoreId, msec_to_ticks(3000)) != pdTRUE) {
        printf("%s timeout\n", __func__);
        return 0;
    }

    return 1;
}

int ecm_enable(void) {
    struct ecm_ctx *ctx = &ecm_ctx[0];

    printf("%s\n", __func__);
    if (ecm_set_interface(ctx) != 1)
        return 0;
    
    if (ecm_update_filter(ctx) != 1)
        return 0;

    if (ecm_get_ethernet_addr(ctx) != 1)
        return 0;

    xSemaphoreGive(ctx->sendSemaphoreId);
    usb_submit_urb(&ctx->intrUrb);
    usb_submit_urb(&ctx->recvUrb);

    return 1;
}

static void ecm_netif_config(int up)
{
    struct ecm_ctx *ctx = &ecm_ctx[0];
    struct netif * netif = &ctx ->netif;

    if (up == 0) {
        netif_set_down(netif);
        return;
    }

    netif_set_default(netif);
    netif_set_up(netif);
}

static void ecm_intr_urb_complete(struct urb *urb, int status) {
    struct ecm_ctx *ctx = (struct ecm_ctx *)urb->ctx;

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

        if (dr->bRequestType == 0xa1 && dr->bRequest == 0x00 && dr->wIndex == EC20_ECM_INTERFACE) {
            uint8_t carrier = !!dr->wValue;

            if (ctx->carrier != carrier) {
                ctx->carrier = carrier;
                ecm_netif_config(carrier);
            }
        }

        usb_submit_urb(urb);
    }
}

static void ecm_recv_urb_complete(struct urb *urb, int status) {
    struct ecm_ctx *ctx = (struct ecm_ctx *)urb->ctx;

    if (status != 0) {
        printf("%s status=%d\n", __func__, status);
        return;
    }
    //printf("%s len=%u\n", __func__, urb->actual_length);

    ctx->recvReady = 1;
    xSemaphoreGive(ctx->recvSemaphoreId);
}

static void ecm_recv_task(void *param) {
    struct ecm_ctx *ctx = (struct ecm_ctx *)param;
    struct netif *netif = &ctx->netif;
    struct urb *urb = &ctx->recvUrb;

    while (1) {
        struct ethhdr *ethhdr;
        uint16_t h_proto;

        while (ctx->recvReady == 0) {
            xSemaphoreTake(ctx->recvSemaphoreId, msec_to_ticks(5000));
        }
        ctx->recvReady = 0;

        ethhdr = (struct ethhdr *)urb->transfer_buffer;
        //dump_hex(urb->transfer_buffer, 16);

        h_proto = ntohs(ethhdr->h_proto);
        if (h_proto == H_IPV4) { //IPV4
            err_t err;
            struct pbuf *p, *q;
            u16_t hdr_len = sizeof(struct ethhdr);
            uint8_t *pbuf = urb->transfer_buffer + hdr_len;
            u16_t pkt_len = urb->actual_length - hdr_len;

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
            //printf("IPV4\n");
        }
        else if (h_proto == H_IPV6) { //IPV6
            //printf("IPV6\n");
        }
        else if (h_proto == H_ARP) { //ARP
            //printf("ARP\n");
        }
        else {
        }
#if 0        
        if (*((uint32_t *)ctx->ethhdr.h_dest) == 0xFFFFFFFF) {
            memcpy(ctx->ethhdr.h_dest, ethhdr->h_source, ETH_ALEN);
            printf("h_dest: %02x:%02x:%02x:%02x:%02x:%02x\n",
                ctx->ethhdr.h_dest[0], ctx->ethhdr.h_dest[1], ctx->ethhdr.h_dest[2],
                ctx->ethhdr.h_dest[3], ctx->ethhdr.h_dest[4], ctx->ethhdr.h_dest[5]);
        }
#endif
    
        usb_submit_urb(urb);
    }
}

static void ecm_send_urb_complete(struct urb *urb, int status) {
    struct ecm_ctx *ctx = (struct ecm_ctx *)urb->ctx;

    if (status != 0) {
        printf("%s status=%d\n", __func__, status);
        return;
    }

    //printf("%s len = %u\n", __func__, urb->actual_length);
    ctx->sendBusy = 0;
    xSemaphoreGive(ctx->sendSemaphoreId);
}

static err_t lwip_netif_output(struct netif *netif, struct pbuf *p,  ip_addr_t *ipaddr)
{
    struct ecm_ctx *ctx = &ecm_ctx[0];
    struct urb *urb = &ctx->sendUrb;
    struct pbuf *q;
    u16_t hdr_len = sizeof(struct ethhdr);
    u16_t pkt_len = 0;

    //printf("%s %d\n", __func__, ctx->sendBusy);
    if (xSemaphoreTake(ctx->sendSemaphoreId, msec_to_ticks(5000)) != pdTRUE) {
        printf("%s tx timeout!\n", __func__);
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

    urb->transfer_buffer_length = hdr_len + pkt_len;
    //printf("%s %d\n", __func__, urb->transfer_buffer_length);
    //dump_hex(urb->transfer_buffer, 14);
    //dump_hex(urb->transfer_buffer+14, 20);
    //dump_hex(urb->transfer_buffer+34, 8);
    if (usb_submit_urb(urb) != 0) {
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

static int ecm_intf0_open(struct hc_info *hc_info, uint16_t idProduct)
{
    struct ecm_ctx *ctx = &ecm_ctx[0];
    struct urb *urb;

    printf("%s hc_num=%u\n", __func__,
        get_hc_num(HC_INTR));

    urb = &ctx->controlUrb;
    urb ->urb_state = URB_STATE_IDLE;
    urb ->complete = ecm_send_urb_complete;
    urb->ctx = ctx;

    urb = &ctx->intrUrb;
    urb ->hc_type = HC_INTR;
    urb ->hc_num = get_hc_num(urb ->hc_type);
    urb ->ep_size = get_ep_size(urb ->hc_type);
    urb ->urb_state = URB_STATE_IDLE;
    urb ->transfer_buffer = ctx->intrUrbBuf;
    urb ->transfer_buffer_length = urb ->ep_size;
    urb ->complete = ecm_intr_urb_complete;
    urb->ctx = ctx;
    usb_start_urb(urb);

    return 0;
}

static int ecm_intf1_open(struct hc_info *hc_info, uint16_t idProduct)
{
    struct ecm_ctx *ctx = &ecm_ctx[0];
    struct urb *urb;

    printf("%s hc_num=%u,%u\n", __func__,
        get_hc_num(HC_BULK_OUT), get_hc_num(HC_BULK_IN));

    urb = &ctx->sendUrb;
    urb ->hc_type = HC_BULK_OUT;
    urb ->hc_num = get_hc_num(urb ->hc_type);
    urb ->ep_size = get_ep_size(urb ->hc_type);
    urb ->urb_state = URB_STATE_IDLE;
    urb ->transfer_buffer = ctx->sendUrbBuf;
    urb ->complete = ecm_send_urb_complete;
    urb ->ctx = ctx;
    usb_start_urb(urb);

    urb = &ctx->recvUrb;
    urb ->hc_type = HC_BULK_IN;
    urb ->hc_num = get_hc_num(urb ->hc_type);
    urb ->ep_size = get_ep_size(urb ->hc_type);
    urb ->urb_state = URB_STATE_IDLE;
    urb ->transfer_buffer = ctx->recvUrbBuf;
    urb ->transfer_buffer_length = ECM_MTU;
    urb ->complete = ecm_recv_urb_complete;
    urb ->ctx = ctx;
    usb_start_urb(urb);

    xSemaphoreGive(ctx->recvSemaphoreId);

    return 0;
}

static const struct usb_driver ecm_intf0_dirver = {
    EC20_ECM_INTERFACE,0x02,
    hc_mask_intr,
    ecm_intf0_open,
    NULL
};

static const struct usb_driver ecm_intf1_dirver = {
    EC20_ECM_INTERFACE+1,0x0a,
    hc_mask_bulk_in_out,
    ecm_intf1_open,
    NULL
};

int ecm_init(void)
{
    struct ecm_ctx *ctx = &ecm_ctx[0];
    struct netif * netif = &ctx ->netif;

    vSemaphoreCreateBinary(ctx->sendSemaphoreId);
    vSemaphoreCreateBinary(ctx->recvSemaphoreId);
    xTaskCreate(ecm_recv_task, "ecm", 1024, ctx, tskIDLE_PRIORITY + 1, NULL);
    tcpip_init(NULL, NULL);
    netif_add(netif, NULL, NULL, NULL, NULL, &lwip_netif_init, &tcpip_input);
    netif_set_down(netif);
    netif_set_default(netif);
    usb_register_drv(&ecm_intf0_dirver);
    usb_register_drv(&ecm_intf1_dirver);

    return 0;
}

struct netif * ecm_get_netif(void) {
    struct ecm_ctx *ctx = &ecm_ctx[0];

    return  &ctx ->netif;
}

#endif
