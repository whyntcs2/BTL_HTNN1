#ifndef STM32_LINK_H
#define STM32_LINK_H

/* Lien ket UART voi STM32 theo giao thuc cua task Comm (comm_protocol.c dung chung 2 ben):
 * khung A5 5A | ver 1 | type | len (2) | seq (2) | payload | CRC16-CCITT (2). */

#include <stdint.h>
#include "cloud.h"

void     link_init(void);           /* Serial1 115200 8N1 tren chan trong config.h */
void     link_poll(void);           /* doc UART, xu ly moi khung da nhan du */
uint8_t  link_stm32_alive(void);    /* 1: vua nhan khung hop le tu STM32 */
uint32_t link_rx_bytes(void);       /* tong byte da nhan (chan doan: 0 = day / STM32 khong gui) */
uint32_t link_rx_frames(void);      /* tong khung dung CRC (byte > 0 ma khung = 0: sai baud / nhieu) */
void     link_send_status(uint8_t wifi, uint8_t cloud, uint16_t pending);
void     link_send_time(uint32_t epoch);
void     link_send_users(const CloudUsers *pxUsers);    /* gui bang ten xuong STM32 */
uint8_t  link_take_need_users(void);

/* OTA (buoc 9c-3) */
void     link_suspend(void);            /* ngung dung Serial1 (dang nap qua bootloader) */
void     link_resume(void);             /* tra Serial1 ve 8N1 sau khi nap */
uint32_t link_stm32_version(void);      /* version firmware trong HELLO (0: chua nhan) */
uint16_t link_send_ota_prepare(uint32_t ulSize, uint32_t ulCrc);   /* tra seq */
void     link_send_ota_abort(uint16_t usSeq);
int      link_take_ota_reply(uint8_t *pucType, uint16_t *pusSeq);  /* 1: co READY/ABORT moi */    /* 1 (roi tu xoa): STM32 vua bat / noi lai, can gui lai bang ten */

#endif /* STM32_LINK_H */
