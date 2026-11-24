#ifndef OTA_H
#define OTA_H

/* Nap firmware STM32 qua WiFi: trang web tren ESP32 nhan file .bin -> hoi STM32 (OTA_PREPARE)
 * -> STM32 dong y (OTA_READY) -> nap qua bootloader ROM (stm32_boot.cpp).
 * STM32 khong phan hoi (vd lan nap truoc hong giua chung) -> nap thang ("nap cuu ho"). */

#include <stdint.h>
#include <stddef.h>

#define OTA_ST_IDLE         0U
#define OTA_ST_HANDSHAKE    1U      /* da gui OTA_PREPARE, cho STM32 tra loi */
#define OTA_ST_FLASHING     2U      /* dang nap (task rieng) */
#define OTA_ST_DONE         3U
#define OTA_ST_FAIL         4U

void    ota_init(void);             /* bat web server cong 80 */
void    ota_poll(void);             /* goi moi vong loop(): web + buoc bat tay */
uint8_t ota_uart_busy(void);        /* 1: loop() khong duoc gui gi cho STM32 */

/* Thong tin cho OLED. Tra 1 neu nen hien man OTA (dang nap, hoac vua xong / loi trong 15s) */
int     ota_screen(char *pcStage, size_t uxCap, uint8_t *pucPercent);

#endif /* OTA_H */
