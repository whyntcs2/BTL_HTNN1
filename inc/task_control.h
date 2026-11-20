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
 *   Sai admin 3 lan -> dem lai tu dau + canh bao them. Het gio -> ve quet van tay.
 *
 * Buoc 8c - luu flash (flash_store.c/.h):
 *   Co khoa o trang 63: mat dien luc dang khoa thi bat len khoa lai du 5 phut.
 *   Bang PIN o trang 62 (doc ra RAM luc khoi dong; lan dau ghi bang mac dinh).
 *
 * Buoc 8d - menu quan ly:
 *   D (khi quet / nhap PIN) -> PIN admin *AB# (C xoa, D thoat; sai tinh chung voi sai PIN).
 *   Menu: 1 THEM (dang ky + dat PIN bat buoc; B / 30s o buoc PIN -> xoa van tay vua dang ky),
 *         2 XOA (chon ID, xac nhan, xoa van tay + PIN), 3 LAM LAI VAN TAY (giu ID + PIN),
 *         4 TRA, 5 DOI PIN (ID 0-31). # = OK, C = xoa, B = lai / huy / thoat.
 *   Muc 2, 3, 5 chon ID tu DANH SACH doc tu AS608 (AS608_CMD_READ_INDEX):
 *         A len, B xuong, # chon, C ve menu chinh. Muc 5 chi liet ke ID 0-31.
 *   Van tay PAUSE suot luc o menu; 30s khong bam phim -> thoat (tru luc cho dat tay). */

#include "FreeRTOS.h"

/* Tao task dieu khien. Goi trong main(), truoc vTaskStartScheduler(),
 * SAU khi da tao task AS608, keypad, servo, buzzer, OLED (task nay dung queue cua chung).
 * Tra ve pdPASS neu tao thanh cong. */
BaseType_t xTaskControlCreate(UBaseType_t uxPriority);

#endif /* TASK_CONTROL_H */