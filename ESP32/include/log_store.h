#ifndef LOG_STORE_H
#define LOG_STORE_H

/* Hang doi log trong LittleFS (khong mat khi ESP32 mat dien).
 * Moi ban ghi 32 byte: 24 byte log nguyen ban tu STM32 + gio ESP32 nhan + du phong.
 * STM32 chi xoa log sau khi ESP32 tra ACK_STORED, nen ESP32 chi ACK SAU KHI da ghi file.
 * An toan khi goi tu nhieu task (mutex ben trong). */

#include <stdint.h>

#define LOG_RAW_BYTES   24U     /* = COMM_LOG_BYTES */

typedef struct {
    uint8_t  raw[LOG_RAW_BYTES];    /* nguyen ban khung LOG tu STM32 */
    uint32_t rx_epoch;              /* gio UTC luc ESP32 nhan (0: chua co gio) */
    uint32_t reserved;
} StoredLog;

int      log_store_init(void);                                      /* 1: OK */
uint16_t log_store_count(void);                                     /* so ban ghi chua gui len Sheet */
int      log_store_append(const uint8_t *raw, uint32_t rx_epoch);   /* 1: da ghi, 0: day / loi */
uint16_t log_store_peek(StoredLog *out, uint16_t max);             /* doc toi da max ban ghi cu nhat, tra so da doc */
void     log_store_drop(uint16_t count);                            /* bo count ban ghi cu nhat (da gui xong) */

/* Chong ghi trung: STM32 gui lai cung 1 log neu ACK bi mat */
int      log_store_seen(uint32_t device, uint64_t event);           /* 1: vua nhan log nay roi */
void     log_store_mark_seen(uint32_t device, uint64_t event);

#endif /* LOG_STORE_H */