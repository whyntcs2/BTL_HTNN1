#ifndef OLED_H
#define OLED_H

#include "stm32f10x_conf.h"

/* 0.96 inch SSD1306 I2C module, default address 0x3C (7-bit). */
#ifndef OLED_I2C_ADDRESS
#define OLED_I2C_ADDRESS 0x3CU
#endif

void oled_init(void);
void oled_clear(void);
/* Them 20/11 (khoa cua): ghi 1 dong 21 ky tu vao trang page (0..7), inverse = 1: dao mau.
 * Dong duoc ghi de ca 21 o (thieu thi dien trang). */
void oled_write_line(uint8_t page, const char *text, uint8_t inverse);
void oled_show_wifi_connecting(void);
void oled_show_wifi_connected(const char *time, const char *date);
void oled_show_accepted(const char *id, const char *name, const char *time, const char *status);
void oled_show_denied(void);
void oled_show_enrollment(const char *id, const char *step);
void oled_show_deletion(const char *id);
void oled_show_security_warning(void);

#endif /* OLED_H */