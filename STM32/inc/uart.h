#ifndef UART_H
#define UART_H

#include "stm32f10x.h"

typedef enum {
    UART_OKE,
    UART_TIMEOUT
} UART_Status;

#define AS608_UART      USART3

void uart_cfg(void);
void uart_send_byte(USART_TypeDef *usart, uint8_t data);
void uart_send_string(USART_TypeDef *usart, const char *s);
void uart_send_hex(USART_TypeDef *usart, uint8_t b);    

UART_Status uart_recv_byte(USART_TypeDef *usart, uint8_t *data, uint32_t timeout_ms);


void uart_flush_rx(USART_TypeDef *usart);

#endif 