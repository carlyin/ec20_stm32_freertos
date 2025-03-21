# ec20_stm32_freertos
stm32 + freertos + lwip
support qxdm + nmea + atc + qmi/rmnet

if you want port to a new MCU and RTOS,
you need implement the fuctions defined in usbh_ec20.h and usbh_ec20.c

extern int usb_submit_control_urb(struct urb *urb, uint8_t *setup);  
extern int usb_submit_urb(struct urb *urb);  
extern int usb_start_urb(struct urb *urb);  
extern void usb_stop_urb(struct urb *urb);  
extern int USBH_EC20_Init(void);  
extern int usb_register_drv(const struct usb_driver *driver);  

very little modifies (for new RTOS) to atc.c/nmea.c/qxdm.c/qmi.c/rmnet.c
