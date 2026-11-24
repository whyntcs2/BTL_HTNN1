#ifndef STM32_BOOT_H
#define STM32_BOOT_H

/* Nap firmware STM32F103 qua bootloader ROM co san trong chip (AN3155), cong USART1 PA9/PA10.
 * ESP32 dieu khien BOOT0 (GPIO7, co dien tro 10k keo xuong GND) va NRST (GPIO10, open-drain).
 * Chi xoa cac trang 1KB ma firmware moi can (bat dau tu trang 0), KHONG xoa trang 60-63
 * (boot ID, bang PIN, co khoa). */

#include <stdint.h>

#define BOOT_OK             0
#define BOOT_ERR_SYNC       1   /* khong vao duoc bootloader */
#define BOOT_ERR_ID         2   /* chip la, khong phai STM32F103 trang 1KB */
#define BOOT_ERR_CMD        3   /* bootloader khong co lenh Erase */
#define BOOT_ERR_ERASE      4
#define BOOT_ERR_WRITE      5
#define BOOT_ERR_VERIFY     6   /* doc lai khac du lieu da ghi */
#define BOOT_ERR_SIZE       7

typedef void (*BootProgress)(uint8_t ucPercent, const char *pcStage);

void        boot_init(void);        /* goi som trong setup(): BOOT0 = 0, NRST tha (STM32 chay app) */
int         boot_flash(const uint8_t *pucData, uint32_t ulLen, BootProgress pfProgress);
void        boot_run_app(void);     /* BOOT0 = 0 + reset: STM32 chay firmware trong flash */
const char *boot_error_text(int iErr);
uint16_t    boot_last_pid(void);    /* ma chip doc duoc (0x410 = F103 medium density) */

#endif /* STM32_BOOT_H */
