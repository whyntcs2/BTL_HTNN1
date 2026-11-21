/* Mo phong RTOS cho test PC; KHONG dung header nay trong firmware STM32. */
#ifndef MOCK_QUEUE_H
#define MOCK_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t size);
void vQueueDelete(QueueHandle_t queue);
BaseType_t xQueueSendToBack(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t wait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
#endif
