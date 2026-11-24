#ifndef COMM_UART_H
#define COMM_UART_H
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
#include <stdint.h>
void CommUart_Init(void); 
void CommUart_Start(TaskHandle_t owner); 
int CommUart_Read(uint8_t *byte); 
int CommUart_Send(const uint8_t *data, size_t length); 
uint32_t CommUart_Dropped(void); 
#endif
