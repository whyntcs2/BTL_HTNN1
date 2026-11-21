/* Mo phong RTOS cho test PC; KHONG dung header nay trong firmware STM32. */
#ifndef MOCK_FREERTOS_H
#define MOCK_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define configTICK_RATE_HZ 100U
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms) / 10U)
#define portMAX_DELAY UINT32_MAX
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#endif
