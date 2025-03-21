#ifndef __USBH_EC20_H__
#define __USBH_EC20_H__

#include "stdio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

#define msleep(ms) do { vTaskDelay(ms * portTICK_PERIOD_MS); } while(0)
#define msec_to_ticks(msec) ((TickType_t)(msec * portTICK_PERIOD_MS))

#if 0 //QCOM
//#define EC20_QXDM_INTERFACE 0
//#define EC20_NMEA_INTERFACE 1
#define EC20_ATC_INTERFACE 2
#define EC20_RMNET_INTERFACE 4
#else //ASR or UNISOC
#define EC20_ATC_INTERFACE 2
#define EC20_ECM_INTERFACE 0
#endif
#define EC20_MAX_INTERFACE 5

struct control_setup {
	uint8_t	bRequestType;
	uint8_t	bRequest;
	uint16_t	wValue;
	uint16_t	wIndex;
	uint16_t	wLength;
};
#define usb_cdc_notification control_setup

typedef enum {
  URB_STATE_IDLE= 0,
  URB_STATE_SUBMIT,
  URB_STATE_BUSY,
  URB_STATE_DONE,
  URB_STATE_ERROR,
  URB_STATE_MAX,
} URB_STATE;

struct urb;
typedef void (*usb_complete_t)(struct urb *urb, int status);

struct urb {
    uint8_t hc_num;
    uint8_t hc_type;
    uint16_t ep_size;
    URB_STATE urb_state;
    uint8_t *transfer_buffer;		/* (in) associated data buffer */
    uint16_t transfer_buffer_length;	/* (in) data buffer length */
    uint16_t actual_length;		/* (return) actual transfer length */
    usb_complete_t complete;
    void *ctx;
};

typedef enum {
  HC_BULK_OUT = 0,
  HC_BULK_IN,
  HC_INTR,
  HC_TYPE_MAX,
} HC_NUM_TYPE;

#define get_hc_num(hc_type)   (hc_info->hc_num[hc_type])
#define get_ep_size(hc_type)   (hc_info->ep_size[hc_type])
#define hc_mask_bulk_in                (1<<HC_BULK_IN)
#define hc_mask_bulk_in_out         ((1<<HC_BULK_OUT)|(1<<HC_BULK_IN))
#define hc_mask_bulk_in_out_int   ((1<<HC_BULK_OUT)|(1<<HC_BULK_IN)|(1<<HC_INTR))
#define hc_mask_intr                       (1<<HC_INTR)

struct hc_info {
    uint8_t hc_num[3]; // bulk_out, bulk_in, intr
    uint16_t ep_size[3];
};

extern int usb_submit_control_urb(struct urb *urb, uint8_t *setup);
extern int usb_submit_urb(struct urb *urb);
extern int usb_start_urb(struct urb *urb);
extern void usb_stop_urb(struct urb *urb);
extern int USBH_EC20_Init(void);
typedef int (*interface_drv_probe_func)(struct hc_info *hc_info, uint16_t idProduct);
typedef int (*interface_drv_disconnect_func)(void);

struct usb_driver {
    uint8_t intfNum;
    uint8_t intfClass;
    uint8_t hc_mask;
    interface_drv_probe_func probe;
    interface_drv_disconnect_func disconnect;
};

extern int usb_register_drv(const struct usb_driver *driver);

extern int usbh_ec20_init;

extern void atc_init(void);
extern int at_send_command (const char *command);

extern void nmea_init(void);

extern void qxdm_init(void);
extern void qxdm_enable_log(int enable);

extern void qmi_init(void);
extern void qmi_enable_dtr(int enable);
extern int qmi_setup_data_call(int enable);
extern int qmi_query_data_call(void);
extern struct netif * rmnet_get_netif(void);

extern int ecm_init(void);
extern int ecm_enable(void);
extern struct netif * ecm_get_netif(void);

typedef struct __IPV4 {
    uint8_t addr[4];
    uint8_t gate[4];
    uint8_t mask[4];
    uint8_t dns1[4];
    uint8_t dns2[4];
} IPV4_T;

extern uint8_t atc_cgreg_stat;
extern uint8_t atc_qnetdevctl_state;
extern uint8_t atc_cgact_state;
extern int rmnet_bulk_out_debug;
extern int rmnet_bulk_in_debug;
extern int rmnet_control_debug;

#endif //__USBH_EC20_H__

