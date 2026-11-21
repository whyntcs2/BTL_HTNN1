#ifndef COMM_UART_H
#define COMM_UART_H
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
#include <stdint.h>
void CommUart_Init(void); /* GPIO PA9/PA10, USART1 115200 8N1; chưa bật ngắt. */
void CommUart_Start(TaskHandle_t owner); /* Gọi trong Comm task sau khi scheduler đã chạy. */
int CommUart_Read(uint8_t *byte); /* Đọc ring RX, không chờ. */
int CommUart_Send(const uint8_t *data, size_t length); /* Gửi bất đồng bộ, trả 0 khi TX đang bận. */
uint32_t CommUart_Dropped(void); /* Đếm lỗi UART/ring đầy để task bỏ khung đang dở. */
#endif
