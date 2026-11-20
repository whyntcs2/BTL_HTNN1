#ifndef TASK_OLED_H
#define TASK_OLED_H

/* Task OLED: gatekeeper cua man hinh SSD1306 (I2C1 PB6 SCL, PB7 SDA), giong Vi du 16
 * "Gatekeeper cho standard output" trong slide p2.
 * - Chi task nay duoc goi ham oled_... (thu vien oled.c/.h).
 * - Task khac gui tung dong chu bang xTaskOledPrint(), khong goi thang oled.h.
 * - Task nay KHONG tu nghi noi dung: task dieu khien ghep man hinh tu cac dong.
 * Man hinh 8 dong (0..7), moi dong 21 ky tu. Font chi co chu HOA khong dau
 * (chu thuong tu doi sang hoa), so 0-9 va: space > < - : / # ! [ ] . * ?
 * Ky tu khac hien thanh khoang trang.
 * Ve 1 dong mat khoang 12ms (I2C 100kHz, cho ban) nen task co uu tien thap nhat. */

#include "FreeRTOS.h"
#include <stdint.h>

#define OLED_LINES      8       /* so dong 0..7 */
#define OLED_LINE_LEN   21      /* so ky tu toi da moi dong */

/* Phan tu cua queue (gui theo gia tri, giong Vi du 11 trong slide).
 * Chuoi duoc chep vao cText nen task goi khong can giu chuoi sau khi gui. */
typedef struct {
    uint8_t ucLine;                     /* 0..7; OLED_LINE_CLEAR: xoa ca man hinh */
    uint8_t ucInverse;                  /* 1: dao mau (dung cho dong tieu de) */
    char    cText[OLED_LINE_LEN + 1];
} Oled_Message;

#define OLED_LINE_CLEAR 0xFFU

/* Tao queue va task OLED. oled_init() goi BEN TRONG task (sau 100ms cho man hinh
 * khoi dong), nen chi can goi ham nay trong main(), truoc vTaskStartScheduler().
 * Tra ve pdPASS neu tao thanh cong. */
BaseType_t xTaskOledCreate(UBaseType_t uxPriority);

/* Ghi 1 dong (ghi de ca dong, thieu thi dien trang; dai hon 21 ky tu thi cat).
 * Cho toi da xTicksToWait tick neu queue day. Tra ve pdPASS neu gui duoc. */
BaseType_t xTaskOledPrint(uint8_t ucLine, uint8_t ucInverse, const char *pcText, TickType_t xTicksToWait);

/* Xoa ca man hinh (mat khoang 100ms). Tra ve pdPASS neu gui duoc. */
BaseType_t xTaskOledClear(TickType_t xTicksToWait);

#endif /* TASK_OLED_H */