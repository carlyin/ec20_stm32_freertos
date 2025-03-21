#include "led.h"
#include "beep.h"
#include "key.h"
#include "usart1.h"
#include "usbhost_user.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

#include <string.h>

#include "ec20/usbh_ec20.h"

#include "lwip/api.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"

void vApplicationIdleHook( void ) {}
void vApplicationTickHook( void ) {}
void vApplicationStackOverflowHook( TaskHandle_t xTask, char * pcTaskName ) {printf("%s\n", __func__);}
void vApplicationMallocFailedHook( void ) {printf("%s\n", __func__);}

static void led_task(void *param) {
    uint32_t i;

    while (1) {
        LED1 = i++&1;
        msleep(200);
    }
}

#if 1 //def EC20_RMNET_INTERFACE
static char tx_buf[TCP_MSS];
static void ec20_rmnet_task(void *param) {
    struct netif *netif = (struct netif *)(param);
    struct netconn *tcpAppConn;
    err_t err;
    struct ip_addr ipaddr;
    struct netbuf *buf;
    TickType_t start;
    int count = 0;
    int send_len, recv_len;

    IP4_ADDR(&ipaddr, 220, 180, 239, 212);

    while (usbh_ec20_init == 0)
        msleep(1000);
    msleep(1000);
#ifdef EC20_RMNET_INTERFACE
    qmi_enable_dtr(1);
#endif

__restart:
    while (!netif_is_up(netif)) {
#ifdef EC20_RMNET_INTERFACE
        if (qmi_setup_data_call(1) == 1 && netif_is_up(netif)) {
            break;
        }
#endif
        msleep(3000);
    }

#ifdef EC20_ECM_INTERFACE
    dhcp_start(netif);
    while (netif->ip_addr.addr == 0)
        msleep(3000);
#endif

    /* Create a new connection identifier. */
    tcpAppConn = netconn_new(NETCONN_TCP);
    printf("tcpAppConn = %p\n", tcpAppConn);

    if (tcpAppConn == NULL)
        goto __restart;

    err = netconn_connect(tcpAppConn, &ipaddr, 8305);
    printf("netconn_connect = %d\n", err);
    if (err) {
        netconn_delete(tcpAppConn);   
        msleep(3000);
        goto __restart;
    }

    count = TCP_SND_BUF - 10; //970;
    start = xTaskGetTickCount();
    while (count++ > 0) {
        int i = 0;
        sprintf(tx_buf, "%08d\r\n", count);

        send_len = count;
        if (send_len > TCP_SND_BUF)
            send_len = TCP_SND_BUF;

        while (i < send_len) {
            int t = send_len -i;
            if (t > sizeof(tx_buf))
                t = sizeof(tx_buf);
            err = netconn_write(tcpAppConn, tx_buf, t, NETCONN_COPY);
            if (ERR_OK != err)
                break;
            i += t;
        }

        if (ERR_OK != err) {
            printf("err = %d\n", err);
#ifdef EC20_RMNET_INTERFACE
            if (qmi_query_data_call() != 1) {
                netconn_close(tcpAppConn);
                netconn_delete(tcpAppConn);
                goto __restart;
            }
#endif
            if (ERR_RST == err) {
                netconn_close(tcpAppConn);
                netconn_delete(tcpAppConn);
                goto __restart;
            }
            continue;
       }
        printf("send_len = %d (%d)\n", send_len, xTaskGetTickCount() - start);

        if ((count%32) == 0) {
#ifdef EC20_RMNET_INTERFACE
            if (qmi_query_data_call() != 1) {
                netconn_close(tcpAppConn);
                netconn_delete(tcpAppConn);
                goto __restart;
            }
#endif
        }

        start = xTaskGetTickCount();
        recv_len = 0;
        while ((err = netconn_recv(tcpAppConn, &buf)) == ERR_OK) {
            do  {
                void *data;
                u16_t len;

                netbuf_data(buf, &data, &len);
                recv_len += len;
                //printf("recv: %d\n", len);
            } while (netbuf_next(buf) >= 0);

            netbuf_delete(buf);
            if (recv_len >= send_len)
                break;
        }
        printf("recv_len = %d (%d)\n", recv_len, xTaskGetTickCount() - start);

        //msleep(100);
    }
    
    /* Close connection and discard connection identifier. */
    netconn_close(tcpAppConn);
    netconn_delete(tcpAppConn);   

    goto __restart;
}
#endif

#ifdef EC20_ATC_INTERFACE
static void ec20_atc_task(void *param) {
    while (usbh_ec20_init == 0)
        msleep(1000);
    msleep(1000);

#ifdef EC20_QXDM_INTERFACE
    qxdm_enable_log(1);
#endif

    at_send_command("ATE0");
    at_send_command("ATI");
    at_send_command("AT+CFUN=1");
    at_send_command("AT+QCFG=\"usbnet\"");
    at_send_command("AT+CGREG=2"); //GPRS Network Registration Status
    at_send_command("AT+CEREG=2"); //EPS Network Registration Status
    at_send_command("AT+C5GREG=2"); //5GS Network Registration Status
    //at_send_command("AT+QCFG=?");
#ifdef EC20_NMEA_INTERFACE
    at_send_command("AT+QGPS=1");
#endif

#ifdef EC20_ECM_INTERFACE
    ecm_enable();
    at_send_command("AT+QNETDEVCTL=?");
    at_send_command("AT+QNETDEVCTL?");
#endif
    while (usbh_ec20_init) {
        at_send_command("AT+CPIN?");
        at_send_command("AT+CGREG?");
        at_send_command("AT+C5GREG?");
        at_send_command("AT+COPS?");
#ifdef EC20_ECM_INTERFACE
        if (atc_qnetdevctl_state == 1) {
            at_send_command("AT+QNETDEVCTL?");
            if (atc_qnetdevctl_state == 0) {
                printf("atc_qnetdevctl_state %u\n", atc_qnetdevctl_state);
            }
        }
        else if (atc_cgreg_stat == 1 || atc_cgreg_stat == 5) {
            at_send_command("AT+CGACT?");
            if (atc_cgact_state == 0)
                at_send_command("AT+CGACT=1,1");
            if (at_send_command("AT+QNETDEVCTL=1,1,1") == 1) {
                msleep(3*1000);
                at_send_command("AT+QNETDEVCTL?");
                if (atc_qnetdevctl_state == 1) {
                    printf("atc_data_data_state %u\n", atc_qnetdevctl_state);
                }
            }
        }
#endif
        msleep(15*1000);
    }
} 
#endif

int main(void)
{ 
    HAL_Init();  
    Stm32_Clock_Init(336,8,2,7); 
    LED_Init();
    BEEP_Init();
    KEY_Init();
    uart_init(1, 460800);
    uart_init(5, 460800);
    uart_init(6, 460800);
#ifdef EC20_QXDM_INTERFACE
    qxdm_init();
#endif
#ifdef EC20_ATC_INTERFACE
    atc_init();
#ifdef EC20_NMEA_INTERFACE
    nmea_init();
#endif
#endif
#ifdef EC20_RMNET_INTERFACE
    qmi_init();
#endif
#ifdef EC20_ECM_INTERFACE
    ecm_init();
#endif

    printf("hello world!\n");

    xTaskCreate(led_task, "led0", 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
#ifdef EC20_ATC_INTERFACE
   xTaskCreate(ec20_atc_task, "ec20", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);
#endif
#ifdef EC20_RMNET_INTERFACE
    xTaskCreate(ec20_rmnet_task, "rmnet", 4086, rmnet_get_netif(), tskIDLE_PRIORITY + 1, NULL);
#endif
#ifdef EC20_ECM_INTERFACE
    xTaskCreate(ec20_rmnet_task, "ecm", 4086, ecm_get_netif(), tskIDLE_PRIORITY + 1, NULL);
#endif

    USBH_EC20_Init();
    vTaskStartScheduler();

    /* We should never get here as control is now taken by the scheduler */
    for( ;; );
}
