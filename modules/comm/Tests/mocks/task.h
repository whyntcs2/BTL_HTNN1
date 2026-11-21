/* Mo phong RTOS cho test PC; KHONG dung header nay trong firmware STM32. */
#ifndef MOCK_TASK_H
#define MOCK_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, size_t stack, void *arg, UBaseType_t priority, TaskHandle_t *handle);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
#endif
