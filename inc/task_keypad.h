#ifndef TASK_KEYPAD_H
#define TASK_KEYPAD_H

/* Task keypad: doc numpad 4x4, chong doi, gui phim cho task khac.
 * - Chi task nay duoc goi Keypad_Scan() (thu vien keypad.c/.h giu nguyen).
 * - Task khac lay phim bang xTaskKeypadGetKey(), khong goi thang keypad.h.
 * - Task nay KHONG kiem tra PIN, khong dem sai: viec do thuoc task dieu khien.
 * Moi lan nhan chi ra 1 phim; giu phim khong lap; nhan chong 2 phim tro len bi bo qua. */

#include "FreeRTOS.h"

/* Phim D dung lam phim menu (vao che do quan ly).
 * Luu y: PIN nguoi dung khong nen chua chu D. */
#define KEYPAD_KEY_MENU     'D'

/* Cau hinh GPIO keypad (Keypad_Init), tao queue phim va task keypad.
 * Goi trong main(), truoc vTaskStartScheduler().
 * Tra ve pdPASS neu tao thanh cong. */
BaseType_t xTaskKeypadCreate(UBaseType_t uxPriority);

/* Lay 1 phim, cho toi da xTicksToWait tick.
 * *pcKey la mot trong: '0'..'9', '*', '#', 'A'..'D'.
 * Tra ve pdPASS neu co phim trong *pcKey. */
BaseType_t xTaskKeypadGetKey(char *pcKey, TickType_t xTicksToWait);

#endif /* TASK_KEYPAD_H */