#ifndef UART_H
#define UART_H

#include "stm32f10x.h"

typedef enum {
    UART_OKE,
    UART_TIMEOUT
} UART_Status;

/* Cau hinh chan va thong so:
 * USART1 (PA9 TX, PA10 RX): AS608, 57600 8N1
 * USART3 (PB10 TX, PB11 RX): debug, 115200 8N1 */
void uart_cfg(void);

void uart_send_byte(USART_TypeDef *usart, uint8_t data);
void uart_send_string(USART_TypeDef *usart, const char *s);
void uart_send_hex(USART_TypeDef *usart, uint8_t b);      /* vd 0x55 -> "55" */

/* Cho nhan 1 byte, toi da timeout_ms. Can goi tim2_cfg() truoc. */
UART_Status uart_recv_byte(USART_TypeDef *usart, uint8_t *data, uint32_t timeout_ms);

/* Doc bo byte con sot trong thanh ghi nhan */
void uart_flush_rx(USART_TypeDef *usart);

#endif /* UART_H */
