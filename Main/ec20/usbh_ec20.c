#include "usbh_core.h"
#include "usbh_stdreq.h"
#include "usb_bsp.h"
#include "usbh_ioreq.h"
#include "usbh_hcs.h"

int rmnet_bulk_out_debug = 0;
int rmnet_bulk_in_debug = 0;
int rmnet_control_debug = 0;

#define TX_FIX_URB_LEN(a) ((a) < (urb->ep_size*1) ? (a) : (urb->ep_size*1))
#define RX_FIX_URB_LEN(a) ((a) < (urb->ep_size*1) ? (a) : (urb->ep_size*1))
#define GetXferLen(pdev, hc_num) (pdev->host.hc[hc_num].xfer_len)
#define GetXferCount(pdev, hc_num) (pdev->host.hc[hc_num].xfer_count)

#include "usbh_ec20.h"

extern USBH_Usr_cb_TypeDef USR_cb;

int usbh_ec20_init = 0;

#define EC20_MAX_URB (HC_MAX-2)

struct USBH_EC20 {
    USB_OTG_CORE_HANDLE  USB_OTG_Core;
    USBH_HOST USB_Host;
    uint32_t irq_cnt;
    SemaphoreHandle_t xSemaphore;
    struct urb *urbs[EC20_MAX_URB];
    struct hc_info hc_info[EC20_MAX_INTERFACE];
    struct urb *control_in_urb;
    struct urb *control_out_urb;
    struct urb *control_urb;
    USB_Setup_TypeDef     control_in_setup;
    USB_Setup_TypeDef     control_out_setup;
    const struct usb_driver *drivers[EC20_MAX_INTERFACE];
};

static struct USBH_EC20 usbh_ec20;
USB_OTG_CORE_HANDLE *pUSB_OTG_Core;

static void dump_state(USB_OTG_CORE_HANDLE *pdev, struct urb *urb) {
    printf("hc_num:%d, urb:%d, hc:%d\n", urb->hc_num,
        HCD_GetURB_State(pdev , urb->hc_num),
        HCD_GetHCState(pdev, urb->hc_num));
    printf("xfer_len: %u, xfer_count: %u\n",
        GetXferLen(pdev, urb->hc_num), GetXferCount(pdev, urb->hc_num));
}

static void handle_bulk_in_urb(struct urb *urb, USB_OTG_CORE_HANDLE *pdev) {
    USBH_Status USBH_Status;
    USB_OTG_URBStateTypeDef URB_Status;

#if 0
    static URB_STATE s_urb_state = URB_STATE_MAX;
    if (s_urb_state != urb->urb_state) {
        const char * urb_state_str[] = {"idle", "submit", "busy", "done", "error", "invail"};
        s_urb_state = urb->urb_state;
        printf("%s urb_state=%s\n", __func__, urb_state_str[urb->urb_state]);
    }
#endif

_restart:
    if (urb->urb_state == URB_STATE_SUBMIT) {
        URB_Status = HCD_GetURB_State(pdev , urb->hc_num);
        if (URB_Status != URB_IDLE && URB_Status != URB_DONE) {
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -1);
            return;
        }

        USBH_Status = USBH_BulkReceiveData(pdev,
                                    urb->transfer_buffer + urb->actual_length,
                                    RX_FIX_URB_LEN(urb->transfer_buffer_length - urb->actual_length),
                                    urb->hc_num);
        if (USBH_Status != USBH_OK)  {
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -2);
            return;
        }

        urb->urb_state = URB_STATE_BUSY;
    }
    else if (urb->urb_state == URB_STATE_BUSY) {
        URB_Status = HCD_GetURB_State(pdev , urb->hc_num);
        if (rmnet_bulk_in_debug) {
            dump_state(pdev, urb);
        }

        if  (URB_Status == URB_DONE) {
            uint32_t xfer_count = GetXferCount(pdev, urb->hc_num);

            urb->actual_length += xfer_count;
#if 1
            if (xfer_count == urb->ep_size) {
                uint16_t rmnet_len = ((urb->transfer_buffer[2]<<8) + urb->transfer_buffer[3] + 8);

                //printf("actual_length=%u, rmnet_len=%u, %02x%02x%02x%02x\n",
               //     urb->actual_length, rmnet_len,
               //     urb->transfer_buffer[0], urb->transfer_buffer[1],
               //     urb->transfer_buffer[2], urb->transfer_buffer[3]);
                if (rmnet_len == urb->actual_length) {
                    //printf("rx usb_zero_bug_fix! %u\n", urb->actual_length);
                    xfer_count = 0;
                }
            }
#endif
            if (xfer_count == 0 || (xfer_count % urb->ep_size)) {
                urb->urb_state = URB_STATE_DONE;
                urb->complete(urb, 0);
            }
            else if (urb->actual_length >= urb->transfer_buffer_length) {
                urb->urb_state = URB_STATE_DONE;
                urb->complete(urb, 0);
            }
            else {
                urb->urb_state = URB_STATE_SUBMIT;
                goto _restart;
            }
        }
        else if (URB_Status == URB_NOTREADY || URB_Status == URB_IDLE) {
            //msleep(1);
        }
        else {
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -4);
        }
    }
}

static void handle_bulk_out_urb(struct urb *urb, USB_OTG_CORE_HANDLE *pdev) {
    USBH_Status USBH_Status;
    USB_OTG_URBStateTypeDef URB_Status;

#if 0
    static URB_STATE s_urb_state = URB_STATE_MAX;
    if (s_urb_state != urb->urb_state) {
        const char * urb_state_str[] = {"idle", "submit", "busy", "done", "error", "invail"};
        s_urb_state = urb->urb_state;
        printf("%s urb_state=%s\n", __func__, urb_state_str[urb->urb_state]);
    }
#endif

_restart:
    if (urb->urb_state == URB_STATE_SUBMIT) {
        URB_Status = HCD_GetURB_State(pdev , urb->hc_num);
        if (URB_Status != URB_IDLE && URB_Status != URB_DONE) {
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -1);
            return;
        }

        USBH_Status = USBH_BulkSendData(pdev,
                                    urb->transfer_buffer + urb->actual_length,
                                    TX_FIX_URB_LEN(urb->transfer_buffer_length - urb->actual_length),
                                    urb->hc_num);
        if (USBH_Status != USBH_OK)  {
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -2);
            return;
        }

        urb->urb_state = URB_STATE_BUSY;
    }
    else if (urb->urb_state == URB_STATE_BUSY) {
        URB_Status = HCD_GetURB_State(pdev , urb->hc_num);
        if (rmnet_bulk_out_debug) {
            dump_state(pdev, urb);
        }

        if (URB_Status == URB_DONE) {
            uint32_t xfer_count = GetXferCount(pdev, urb->hc_num);
            uint32_t xfer_len = GetXferLen(pdev, urb->hc_num);

            xfer_len = xfer_count ? xfer_count : xfer_len;
            urb->actual_length += xfer_count ? xfer_count : xfer_len;
            if (urb->actual_length >= urb->transfer_buffer_length) {
                if ((xfer_len % urb->ep_size) || xfer_len == 0) {
                    urb->urb_state = URB_STATE_DONE;
                    urb->complete(urb, 0);
                }
                else {
                    urb->urb_state = URB_STATE_SUBMIT;
                }
            }
            else {
                urb->urb_state = URB_STATE_SUBMIT;
                goto _restart;
            }
        }
        else if (URB_Status == URB_NOTREADY || URB_Status == URB_IDLE) {
            //msleep(1);
        }
        else {
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -3);
        }
    }
}

static void handle_intr_urb(struct urb *urb, USB_OTG_CORE_HANDLE *pdev) {
    USBH_Status USBH_Status;
    USB_OTG_URBStateTypeDef URB_Status;
    static uint32_t timer;

#if 0
    static URB_STATE s_urb_state = URB_STATE_MAX;
    if (s_urb_state != urb->urb_state) {
        const char * urb_state_str[] = {"idle", "submit", "busy", "done", "error", "invail"};
        s_urb_state = urb->urb_state;
        printf("%s urb_state=%s\n", __func__, urb_state_str[urb->urb_state]);
    }
#endif

    if (urb->urb_state == URB_STATE_SUBMIT) {
        URB_Status = HCD_GetURB_State(pdev , urb->hc_num);
        if (URB_Status != URB_IDLE && URB_Status != URB_DONE) {
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -1);
            return;
        }

        USBH_Status = USBH_InterruptReceiveData(pdev,
                                    urb->transfer_buffer,
                                    urb->transfer_buffer_length,
                                    urb->hc_num);
        if (USBH_Status != USBH_OK)  {
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -2);
            return;
        }

        urb->urb_state = URB_STATE_BUSY;
        timer = HCD_GetCurrentFrame(pdev);
    }
    else if (urb->urb_state == URB_STATE_BUSY) {
        URB_Status = HCD_GetURB_State(pdev , urb->hc_num);
        //dump_state(pdev, urb);

        if  (URB_Status == URB_DONE) {
            uint32_t xfer_count = GetXferCount(pdev, urb->hc_num);

            urb->actual_length = xfer_count;
            urb->urb_state = URB_STATE_DONE;
            urb->complete(urb, 0);
        }
        else if (URB_Status == URB_NOTREADY || URB_Status == URB_IDLE) {
             if (( HCD_GetCurrentFrame(pdev) - timer) >= 256) {
                urb->urb_state = URB_STATE_SUBMIT;
             }
        }
        else {
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -4);
        }
    }
}

static int handle_control_urb(struct USBH_EC20 *ec20, struct urb *urb) {
    USB_OTG_CORE_HANDLE *pdev= &ec20->USB_OTG_Core;
    USBH_HOST *phost = &ec20->USB_Host;

    USBH_Status USBH_Status;
    USB_OTG_URBStateTypeDef URB_Status;

    if (rmnet_control_debug) {
        static URB_STATE s_urb_state = URB_STATE_MAX;
        if (s_urb_state != urb->urb_state) {
            const char * urb_state_str[] = {"idle", "submit", "busy", "done", "error", "invail"};
            s_urb_state = urb->urb_state;
            printf("%s urb_state=%s\n", __func__, urb_state_str[urb->urb_state]);
        }
        printf("gState:%u, RequestState:%u, Control.state:%u\n",
            phost->gState, phost->RequestState, phost->Control.state);
    }

    if (urb->urb_state == URB_STATE_SUBMIT) {
        URB_Status = HCD_GetURB_State(pdev , phost->Control.hc_num_in);
        if (URB_Status != URB_IDLE && URB_Status != URB_DONE) {
            printf("control hc_num=%u, URB_Status=%u\n", phost->Control.hc_num_in, URB_Status);
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -1);
            return 1;
        }
        URB_Status = HCD_GetURB_State(pdev , phost->Control.hc_num_out);
        if (URB_Status != URB_IDLE && URB_Status != URB_DONE) {
            printf("control hc_num=%u, URB_Status=%u\n", phost->Control.hc_num_out, URB_Status);
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -1);
            return 1;
        }

        if (urb == ec20->control_in_urb)
            phost->Control.setup = ec20->control_in_setup;
        else
            phost->Control.setup = ec20->control_out_setup;
        USBH_Status = USBH_CtlReq(pdev, phost,
                                    urb->transfer_buffer,
                                    urb->transfer_buffer_length);
        if (USBH_Status != USBH_BUSY)  {
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -2);
            return 1;
        }

        urb->urb_state = URB_STATE_BUSY;
    }
    else if (urb->urb_state == URB_STATE_BUSY) {
        USBH_Status = USBH_CtlReq(pdev, phost,
                                    urb->transfer_buffer,
                                    urb->transfer_buffer_length);
        if (rmnet_control_debug)
            dump_state(pdev, urb);

        if (USBH_Status == USBH_OK) {
            uint32_t xfer_count = GetXferCount(pdev, urb->hc_num);

            urb->actual_length = xfer_count;
            urb->urb_state = URB_STATE_DONE;
            urb->complete(urb, 0);
            return 1;
        }
        else if (USBH_Status == USBH_BUSY ) {
            //msleep(1);
        }
        else {
            urb->urb_state = URB_STATE_ERROR;
            urb->complete(urb, -4);
            return 1;
        }
    }

    return 0;
}

int usb_register_drv(const struct usb_driver *driver) {
    struct USBH_EC20 *ec20 = &usbh_ec20;

    if (driver->intfNum < EC20_MAX_INTERFACE &&  ec20->drivers[driver->intfNum] == NULL) {
        ec20->drivers[driver->intfNum] = driver;
        return 0;
    }
    return -1;
}

static USBH_Status USBH_EC20_InterfaceInit ( USB_OTG_CORE_HANDLE *pdev,
                                        void *phost)
{
    struct USBH_EC20 *ec20 = (struct USBH_EC20 *)pdev;
    USBH_HOST *pphost = phost;
    USBH_DevDesc_TypeDef *pDevDesc = &pphost->device_prop.Dev_Desc;
    USBH_CfgDesc_TypeDef *pCfgDesc = &pphost->device_prop.Cfg_Desc;
    USBH_InterfaceDesc_TypeDef *pItfDesc;
    USBH_EpDesc_TypeDef *pEpDesc;
    uint8_t intf, ep, hc;

    printf("%s idVendor: %04x, idProduct: %04x, bNumInterfaces: %d\n", __func__,
        pDevDesc->idVendor,  pDevDesc->idProduct, pCfgDesc->bNumInterfaces);

    if (pDevDesc->idVendor != 0x2c7c) {
        pphost->usr_cb->DeviceNotSupported();
        return USBH_NOT_SUPPORTED;
   }

    for (intf= 0; intf < pCfgDesc->bNumInterfaces; intf++) {
        pItfDesc = &pphost->device_prop.Itf_Desc[intf];

        printf("Intf[%d]: Number %d, Endpoints %d, Class %d\n",
            intf, pItfDesc->bInterfaceNumber, pItfDesc->bNumEndpoints, pItfDesc->bInterfaceClass);

        if (intf >= EC20_MAX_INTERFACE)
            continue;
        if (ec20->drivers[intf] == NULL)
            continue;
        if (ec20->drivers[intf]->intfClass != pItfDesc->bInterfaceClass)
            continue;

        for (ep = 0; ep < pItfDesc->bNumEndpoints; ep++) {
            HC_NUM_TYPE hc_type = HC_TYPE_MAX;
            pEpDesc = &pphost->device_prop.Ep_Desc[intf][ep];

            printf("\tbAddress %x, Attributes %x, PacketSize %x\n",
                pEpDesc->bEndpointAddress,pEpDesc->bmAttributes,pEpDesc->wMaxPacketSize);

            if (pEpDesc->bmAttributes == USB_EP_TYPE_BULK) {
                if ((pEpDesc->bEndpointAddress & USB_EP_DIR_MSK) == USB_EP_DIR_OUT) {
                    hc_type = HC_BULK_OUT;
                }
                else if ((pEpDesc->bEndpointAddress & USB_EP_DIR_MSK) == USB_EP_DIR_IN)
                    hc_type = HC_BULK_IN;
            }
            else if (pEpDesc->bmAttributes == USB_EP_TYPE_INTR) {
                hc_type = HC_INTR;
            }

            if (hc_type != HC_TYPE_MAX && ( ec20->drivers[intf]->hc_mask & (1<<hc_type))) {
                ec20->hc_info[intf].hc_num[hc_type] = USBH_Alloc_Channel(pdev, pEpDesc->bEndpointAddress);
                ec20->hc_info[intf].ep_size[hc_type] = pEpDesc->wMaxPacketSize;
                if (ec20->hc_info[intf].hc_num[hc_type] == 0xFF) {
                    ec20->hc_info[intf].hc_num[hc_type] = 0;
                    printf("not enough hc_num for interface %u\n", intf);
                    //return USBH_FAIL;
                }
            }
        }
    }

    for (intf = 0; intf < EC20_MAX_INTERFACE; intf++) {
        struct hc_info *hc_info = &ec20->hc_info[intf];

        for (hc = 0; hc < 3; hc++) {
            if (hc_info->hc_num[hc] != 0) {
                USBH_Open_Channel  (pdev,
                                    hc_info->hc_num[hc],
                                    pphost->device_prop.address,
                                    pphost->device_prop.speed,
                                    hc == HC_INTR ? EP_TYPE_INTR : EP_TYPE_BULK,
                                    hc_info->ep_size[hc]);
            }
        }
    }

    return USBH_OK;
}

static void USBH_EC20_InterfaceDeInit ( USB_OTG_CORE_HANDLE *pdev,
                                void *phost)
{
    int i, intf, hc;
    struct USBH_EC20 *ec20 = (struct USBH_EC20 *)pdev;

    usbh_ec20_init = 0;
    printf("%s\n", __func__);
    for (i = 0; i < EC20_MAX_URB; i++) {
        if (ec20->urbs[i]) {
            ec20->urbs[i] = NULL;
        }
    }

    for (intf = 0; intf < EC20_MAX_INTERFACE; intf++) {
        struct hc_info *hc_info = &ec20->hc_info[intf];

        if (ec20->drivers[intf] != NULL) {
            if (ec20->drivers[intf]->disconnect)
                ec20->drivers[intf]->disconnect();
        }

        for (hc = 0; hc < 3; hc++) {
            if (hc_info->hc_num[hc] != 0) {
                USB_OTG_HC_Halt(pdev, hc_info->hc_num[hc]);
                USBH_Free_Channel (pdev, hc_info->hc_num[hc]);
                hc_info->hc_num[hc] = 0;     /* Reset the Channel as Free */
            }
        }
    }
}

static USBH_Status USBH_EC20_ClassRequest(USB_OTG_CORE_HANDLE *pdev ,
                                        void *phost)
{
    struct USBH_EC20 *ec20 = (struct USBH_EC20 *)pdev;
    USBH_HOST *pphost = &ec20->USB_Host;
    USBH_DevDesc_TypeDef *pDevDesc = &pphost->device_prop.Dev_Desc;
    int intf, hc;

    printf("%s\n", __func__);

    for (intf = 0; intf < EC20_MAX_INTERFACE; intf++) {
        struct hc_info *hc_info = &ec20->hc_info[intf];

        if ( ec20->drivers[intf] == NULL)
            continue;

        for (hc = 0; hc < 3; hc++) {
            if (( ec20->drivers[intf]->hc_mask & (1<<hc)) && (hc_info->hc_num[hc] == 0)) {
                hc = -1;
                break;
            }
        }

        if (hc != -1) {
             ec20->drivers[intf]->probe(hc_info, pDevDesc->idProduct);
        }
    }

    usbh_ec20_init = 1;
    return USBH_OK;
}

static USBH_Status USBH_EC20_Handle(USB_OTG_CORE_HANDLE *pdev ,
                                   void   *phost)
{
    return USBH_BUSY;
}

static USBH_Class_cb_TypeDef  USBH_EC20_cb =
{
    USBH_EC20_InterfaceInit,
    USBH_EC20_InterfaceDeInit,
    USBH_EC20_ClassRequest,
    USBH_EC20_Handle,
};

void usbh_wakeup_task(USB_OTG_CORE_HANDLE *pdev) {
    struct USBH_EC20 *ec20 = (struct USBH_EC20 *)pdev;

    ec20->irq_cnt++;
    xSemaphoreGiveFromISR(ec20->xSemaphore, NULL);
}

static void USBH_Process_Task(void *param) {
    struct USBH_EC20 *ec20 = (struct USBH_EC20 *)param;
    uint32_t irq_cnt = 0;
    int i;

    while (1) {
        USB_OTG_CORE_HANDLE *pdev= &ec20->USB_OTG_Core;
        USBH_HOST *phost = &ec20->USB_Host;
    //printf("gState:%u, RequestState:%u, Control.state:%u\n",
	//	    phost->gState, phost->RequestState, phost->Control.state);

       if (irq_cnt == ec20->irq_cnt) {
            xSemaphoreTake(ec20->xSemaphore, msec_to_ticks(10));
        }
        irq_cnt = ec20->irq_cnt;

    	USBH_Process(&ec20->USB_OTG_Core, phost);

        for (i = 0; i < EC20_MAX_URB; i++) {
            struct urb *urb = ec20->urbs[i];

            if (urb) {
                if (urb->hc_type == HC_BULK_IN)
                    handle_bulk_in_urb(urb, pdev);
                else if (urb->hc_type == HC_BULK_OUT)
                    handle_bulk_out_urb(urb, pdev);
                else if (urb->hc_type == HC_INTR)
                    handle_intr_urb(urb, pdev);
            }
        }

        if (ec20->control_urb == NULL && phost->gState == HOST_CLASS
            && 1/*(( HCD_GetCurrentFrame(pdev) - control_urb) > 64)*/) {
            if (ec20->control_in_urb)
                ec20->control_urb = ec20->control_in_urb;
            else if (ec20->control_out_urb)
                ec20->control_urb = ec20->control_out_urb;
        }

        if (ec20->control_urb && handle_control_urb(ec20, ec20->control_urb)) {
            if (ec20->control_urb == ec20->control_in_urb)
                ec20->control_in_urb = NULL;
            else
                ec20->control_out_urb = NULL;
            ec20->control_urb = NULL;
        }

        if (phost->gState == HOST_CTRL_XFER) { //add interval for steps in USBH_HandleControl()
            uint32_t control_urb = HCD_GetCurrentFrame(pdev);
            while (( HCD_GetCurrentFrame(pdev) - control_urb) < 8)
                msleep(1);
        }
    }
}

int USBH_EC20_Init(void) {
    struct USBH_EC20 *ec20 = &usbh_ec20;

    pUSB_OTG_Core = &ec20->USB_OTG_Core;
    ec20->irq_cnt = 0;
    vSemaphoreCreateBinary(ec20->xSemaphore);
    USBH_Init(&ec20->USB_OTG_Core, USB_OTG_FS_CORE_ID,
        &ec20->USB_Host, &USBH_EC20_cb, &USR_cb);

    xTaskCreate(USBH_Process_Task, "usb", 2048, ec20, tskIDLE_PRIORITY + 3, NULL);
    return 0;
}

int usb_submit_urb(struct urb *urb) {
    struct USBH_EC20 *ec20 = &usbh_ec20;

    if (urb->urb_state != URB_STATE_IDLE && urb->urb_state != URB_STATE_DONE) {
        printf("%s hc=%u, state=%d\n", __func__, urb->hc_num,urb->urb_state);
        return -1;
    }

    urb->actual_length= 0;
    urb->urb_state = URB_STATE_SUBMIT;
    xSemaphoreGive(ec20->xSemaphore);

    return 0;
}

int usb_submit_control_urb(struct urb *urb, uint8_t *setup) {
    struct USBH_EC20 *ec20 = &usbh_ec20;
    USBH_HOST *phost = &ec20->USB_Host;

    if (urb->urb_state != URB_STATE_IDLE && urb->urb_state != URB_STATE_DONE) {
        printf("contrl_urb %x state=%d\n", (phost->Control.setup.b.bmRequestType&USB_D2H), urb->urb_state);
        return -1;
    }

    urb->actual_length = 0;
    if ((setup[0]&USB_D2H)) {
        if (ec20->control_in_urb) {
            printf("control_in_urb busy\n");
            return -2;
        }
        ec20->control_in_setup = *(USB_Setup_TypeDef *)setup;
        urb->hc_num = phost->Control.hc_num_in;
        urb->urb_state = URB_STATE_SUBMIT;
        ec20->control_in_urb = urb;
    }
    else {
        if (ec20->control_out_urb) {
            printf("control_out_urb busy\n");
            return -2;
        }
        ec20->control_out_setup = *(USB_Setup_TypeDef *)setup;
        urb->hc_num = phost->Control.hc_num_out;
        urb->urb_state = URB_STATE_SUBMIT;
        ec20->control_out_urb = urb;
    }

    xSemaphoreGive(ec20->xSemaphore);

    return 0;
}

int usb_start_urb(struct urb *urb) {
    int urbi;
    struct USBH_EC20 *ec20 = &usbh_ec20;

    for (urbi = 0; urbi < EC20_MAX_URB; urbi++) {
        if (ec20->urbs[urbi] == urb) {
            return 0;
        }
    }

    for (urbi = 0; urbi < EC20_MAX_URB; urbi++) {
        if (ec20->urbs[urbi] == NULL) {
            ec20->urbs[urbi] = urb;
            return 0;
        }
    }

    return -1;
}

void usb_stop_urb(struct urb *urb) {
    int i;
    struct USBH_EC20 *ec20 = &usbh_ec20;

    for (i = 0; i < EC20_MAX_URB; i++) {
        if (ec20->urbs[i] == urb) {
            ec20->urbs[i] = NULL;
            return;
        }
    }
}
