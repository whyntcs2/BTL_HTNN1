#ifndef TASK_CONTROL_H
#define TASK_CONTROL_H

/* Task dieu khien: bo nao cua khoa cua.
 * - Nhan su kien tu task AS608 va phim tu task keypad.
 * - Ra lenh cho task servo (mo cua), task buzzer (am bao), task OLED (hien thi).
 * - Chi goi API cua cac task (task_xxx.h), khong goi thang thu vien linh kien.
 * - KHONG in ra USART3 (uu tien cao hon AS608), moi thong tin hien tren OLED.
 *
 * Buoc 8a - luong mac dinh:
 *   QUET VAN TAY: MATCH -> mo cua; NO_MATCH 3 lan -> PAUSE van tay, sang NHAP PIN.
 *   NHAP PIN: 4 chu so 0-9, du 4 phim tu kiem tra, C = xoa go lai.
 *     Dung -> mo cua, 30s sau van tay hoat dong lai.
 *     Sai 3 lan -> KHOA.
 *     30s khong bam phim -> ve quet van tay (giu so lan sai PIN).
 *   D = menu (8d).
 *
 * Buoc 8b - khoa 5 phut:
 *   Van tay PAUSE, dong cua, 5 tieng canh bao, OLED dem nguoc.
 *   Chi nhan PIN admin *AB# (C = xoa, D bi khoa). Dung -> mo khoa (KHONG mo cua), ve quet van tay.
 *   Sai admin 3 lan -> dem lai tu dau + canh bao them. Het gio -> ve quet van tay. */

#include "FreeRTOS.h"

/* Tao task dieu khien. Goi trong main(), truoc vTaskStartScheduler(),
 * SAU khi da tao task AS608, keypad, servo, buzzer, OLED (task nay dung queue cua chung).
 * Tra ve pdPASS neu tao thanh cong. */
BaseType_t xTaskControlCreate(UBaseType_t uxPriority);

#endif /* TASK_CONTROL_H */