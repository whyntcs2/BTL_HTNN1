#ifndef DISPLAY_H
#define DISPLAY_H

/* Man hinh OLED 0.42" (72x40, SSD1306) co san tren ESP32-C3 Super Mini.
 * Chi de xem nhanh trang thai, khong can mo Serial Monitor. */

#include <stdint.h>

void display_init(void);            /* bat I2C + man hinh, hien "DANG KHOI DONG" */

/* Goi lien tuc trong loop(); tu gioi han ve lai moi OLED_PERIOD_MS.
 * wifi / stm: 1 = OK; cloud: CLOUD_STATE_...; epoch: gio UTC (0 = chua co); pending: so log chua gui. */
void display_update(uint8_t wifi, uint8_t stm, uint8_t cloud, uint32_t epoch, uint16_t pending,
                    const char *pcIp);

/* Man nap firmware STM32 (thay man trang thai trong luc nap): "NAP STM32" / buoc / thanh % */
void display_ota(const char *pcStage, uint8_t ucPercent);

#endif /* DISPLAY_H */
