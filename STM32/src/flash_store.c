#include "flash_store.h"
#include "stm32f10x.h"
#include "stm32f10x_flash.h"

/* ================== Dia chi ================== */
#define FS_PIN_PAGE_ADDR    ((uint32_t)0x0800F800)  /* trang 62 */
#define FS_LOCK_PAGE_ADDR   ((uint32_t)0x0800FC00)  /* trang 63 */
#define FS_PIN_MAGIC_ADDR   FS_PIN_PAGE_ADDR        /* 4 byte ma nhan dang */
#define FS_PIN_SLOT_ADDR    (FS_PIN_PAGE_ADDR + 4U) /* o PIN dau tien (ID 0) */

#define FS_PIN_MAGIC        ((uint16_t)0x5049)      /* "PI": trang PIN da duoc ghi it nhat 1 lan */
#define FS_LOCK_MAGIC       ((uint16_t)0xA5A5)
#define FS_ERASED           ((uint16_t)0xFFFF)

#define FS_READ16(addr)     (*(volatile uint16_t *)(addr))
#define FS_READ8(addr)      (*(volatile uint8_t *)(addr))

static BaseType_t prvErasePage(uint32_t ulAddr);
static BaseType_t prvWrite16(uint32_t ulAddr, uint16_t usData);

/* ================== Co khoa ================== */

BaseType_t xFlashStoreIsLocked(void)
{
    return (FS_READ16(FS_LOCK_PAGE_ADDR) == FS_LOCK_MAGIC) ? pdTRUE : pdFALSE;
}

/* Khoa: chi ghi 1 halfword vao o dang trang (nhanh, khong xoa trang).
 * Mo khoa: xoa trang 63 (~20ms). */
BaseType_t xFlashStoreSetLocked(BaseType_t xLocked)
{
    uint16_t   usNow = FS_READ16(FS_LOCK_PAGE_ADDR);
    BaseType_t xOk   = pdPASS;

    if (xLocked == pdTRUE) {
        if (usNow == FS_LOCK_MAGIC) {
            return pdPASS;                          /* da khoa san (vd dem lai 5 phut) */
        }
        if (usNow != FS_ERASED) {
            xOk = prvErasePage(FS_LOCK_PAGE_ADDR);  /* du lieu la: xoa truoc */
        }
        if (xOk == pdPASS) {
            xOk = prvWrite16(FS_LOCK_PAGE_ADDR, FS_LOCK_MAGIC);
        }
    } else {
        if (usNow != FS_ERASED) {
            xOk = prvErasePage(FS_LOCK_PAGE_ADDR);
        }
    }
    return xOk;
}

/* ================== Bang PIN ================== */

BaseType_t xFlashStoreLoadPins(char pcPins[][FLASH_STORE_PIN_LEN])
{
    uint16_t   i;
    uint8_t    j;
    BaseType_t xValid = (FS_READ16(FS_PIN_MAGIC_ADDR) == FS_PIN_MAGIC) ? pdTRUE : pdFALSE;

    for (i = 0; i < FLASH_STORE_MAX_PIN; i++) {
        for (j = 0; j < FLASH_STORE_PIN_LEN; j++) {
            if (xValid == pdTRUE) {
                pcPins[i][j] = (char)FS_READ8(FS_PIN_SLOT_ADDR + (uint32_t)i * FLASH_STORE_PIN_LEN + j);
            } else {
                pcPins[i][j] = FLASH_STORE_PIN_EMPTY;
            }
        }
    }
    return xValid;
}

/* Xoa trang 62, ghi ma nhan dang roi ghi cac o co PIN (o trong de nguyen 0xFF) */
BaseType_t xFlashStoreSavePins(char pcPins[][FLASH_STORE_PIN_LEN])
{
    uint16_t i;
    uint8_t  j;
    uint32_t ulAddr;
    uint16_t usData;

    if (prvErasePage(FS_PIN_PAGE_ADDR) != pdPASS) {
        return pdFAIL;
    }
    if (prvWrite16(FS_PIN_MAGIC_ADDR, FS_PIN_MAGIC) != pdPASS) {
        return pdFAIL;
    }

    for (i = 0; i < FLASH_STORE_MAX_PIN; i++) {
        if (pcPins[i][0] == FLASH_STORE_PIN_EMPTY) {
            continue;
        }
        ulAddr = FS_PIN_SLOT_ADDR + (uint32_t)i * FLASH_STORE_PIN_LEN;
        for (j = 0; j < FLASH_STORE_PIN_LEN; j += 2) {
            /* little-endian: byte thap o dia chi thap, doc lai tung byte dung thu tu */
            usData = (uint16_t)((uint8_t)pcPins[i][j] | ((uint16_t)(uint8_t)pcPins[i][j + 1] << 8));
            if (prvWrite16(ulAddr + j, usData) != pdPASS) {
                return pdFAIL;
            }
        }
    }
    return pdPASS;
}

/* ================== Ham noi bo (SPL stm32f10x_flash) ================== */

static BaseType_t prvErasePage(uint32_t ulAddr)
{
    FLASH_Status eStatus;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    eStatus = FLASH_ErasePage(ulAddr);
    FLASH_Lock();

    return (eStatus == FLASH_COMPLETE) ? pdPASS : pdFAIL;
}

static BaseType_t prvWrite16(uint32_t ulAddr, uint16_t usData)
{
    FLASH_Status eStatus;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    eStatus = FLASH_ProgramHalfWord(ulAddr, usData);
    FLASH_Lock();

    if (eStatus != FLASH_COMPLETE) {
        return pdFAIL;
    }
    return (FS_READ16(ulAddr) == usData) ? pdPASS : pdFAIL;   /* doc lai kiem tra */
}
