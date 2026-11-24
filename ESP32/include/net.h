#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>

void     net_init(void);            /* bat WiFi (SSID ghi cung) + dong bo gio NTP */
void     net_poll(void);            /* in trang thai khi doi, WiFi tu ket noi lai */
uint8_t  net_wifi_ok(void);         /* 1: da ket noi WiFi */
uint32_t net_epoch(void);           /* gio UTC (giay), 0 neu chua dong bo NTP */
void     net_ip(char *pcOut, size_t uxCap);     /* IP dang chuoi, "" neu chua co WiFi */

#endif /* NET_H */
