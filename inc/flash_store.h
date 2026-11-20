#ifndef FLASH_STORE_H
#define FLASH_STORE_H

/* Luu du lieu can giu qua mat dien vao 2 trang flash cuoi cua STM32F103C8 (64KB, trang 1KB):
 *   Trang 62 (0x0800F800): bang PIN. 4 byte dau la ma nhan dang, sau do moi ID 1 o 4 byte
 *                          (o thu i = PIN cua ID i; o trong = 0xFF).
 *   Trang 63 (0x0800FC00): co khoa. 0xA5A5 = dang khoa, 0xFFFF (trang) = khong khoa.
 * Keil: Options for Target -> Target -> IROM1 Size = 0xF800 de code khong lan vao 2 trang nay;
 *       Flash Download phai la "Erase Sectors" (khong Erase Full Chip).
 *
 * Day la THU VIEN, khong phai task: chi task dieu khien duoc goi cac ham nay
 * (khong co task nao khac dung flash nen khong can gatekeeper). Neu sau nay co task
 * thu hai can flash thi boc thanh task_flash theo mau gatekeeper (slide p2).
 *
 * Xoa 1 trang mat khoang 20ms, CPU dung cho toi khi xong: chi goi luc van tay dang PAUSE. */

#include "FreeRTOS.h"
#include <stdint.h>

/* So nguoi co PIN (ID 0 .. FLASH_STORE_MAX_PIN - 1). Moi nguoi ton 4 byte flash + 4 byte RAM.
 * Doi duoc toi da 255 (1 trang 1KB, tru 4 byte ma nhan dang). Nguoi co ID lon hon
 * van mo cua bang van tay binh thuong, chi khong co PIN. */
#define FLASH_STORE_MAX_PIN     32
#define FLASH_STORE_PIN_LEN     4
#define FLASH_STORE_PIN_EMPTY   ((char)0xFF)    /* byte dau cua o PIN trong */

/* Co khoa */
BaseType_t xFlashStoreIsLocked(void);                       /* pdTRUE neu dang khoa */
BaseType_t xFlashStoreSetLocked(BaseType_t xLocked);        /* pdPASS neu ghi / xoa thanh cong */

/* Bang PIN: pcPins[i] la PIN cua ID i (khong co '\0'); o trong: pcPins[i][0] = FLASH_STORE_PIN_EMPTY.
 * Load tra ve pdFALSE neu trang PIN chua tung duoc ghi (lan dau): khi do moi o deu trong. */
BaseType_t xFlashStoreLoadPins(char pcPins[][FLASH_STORE_PIN_LEN]);
BaseType_t xFlashStoreSavePins(char pcPins[][FLASH_STORE_PIN_LEN]);  /* xoa trang 62 roi ghi lai ca bang */

#endif /* FLASH_STORE_H */
