#ifndef FLASH_STORE_H
#define FLASH_STORE_H


#include "FreeRTOS.h"
#include <stdint.h>

#define FLASH_STORE_MAX_PIN     32
#define FLASH_STORE_PIN_LEN     4
#define FLASH_STORE_PIN_EMPTY   ((char)0xFF)    

/* Co khoa */
BaseType_t xFlashStoreIsLocked(void);                       /* pdTRUE neu dang khoa */
BaseType_t xFlashStoreSetLocked(BaseType_t xLocked);        /* pdPASS neu ghi / xoa thanh cong */

/* Bang PIN: pcPins[i] la PIN cua ID i (khong co '\0'); o trong: pcPins[i][0] = FLASH_STORE_PIN_EMPTY.
 * Load tra ve pdFALSE neu trang PIN chua tung duoc ghi (lan dau): khi do moi o deu trong. */
BaseType_t xFlashStoreLoadPins(char pcPins[][FLASH_STORE_PIN_LEN]);
BaseType_t xFlashStoreSavePins(char pcPins[][FLASH_STORE_PIN_LEN]);  /* xoa trang 62 roi ghi lai ca bang */

#endif /* FLASH_STORE_H */