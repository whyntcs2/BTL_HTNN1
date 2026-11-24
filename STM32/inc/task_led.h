#ifndef TASK_LED_H
#define TASK_LED_H

/* Task LED bao trang thai (tach tu main.c 24/11):
 *   nhay cham (0,5s) = binh thuong; nhay nhanh (0,1s) = co task khong tao duoc (het heap).
 * Chi task nay goi thu vien led.c. */

#include "FreeRTOS.h"
#include "task.h"

/* Goi led_init() + tao task. Goi trong main(), truoc vTaskStartScheduler(). */
BaseType_t xTaskLedCreate(UBaseType_t uxPriority);

/* main() goi khi co task tao loi -> LED nhay nhanh */
void       vTaskLedReportError(void);

#endif /* TASK_LED_H */
