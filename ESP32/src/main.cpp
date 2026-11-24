/* Firmware ESP32-C3 Super Mini (OLED 0.42") - cau noi WiFi cho khoa cua van tay STM32.
 * Buoc 9c-1: lien ket UART voi STM32 (nhan LOG -> luu LittleFS -> ACK),
 *            WiFi ghi cung + gio NTP -> gui TIME, gui STATUS moi 2s,
 *            hien trang thai len OLED tren board.
 * Buoc 9c-2: task cloud gui log len Google Sheets, lay bang ten -> gui xuong STM32.
 * Buoc 9c-3: OTA - trang web nhan .bin, nap firmware STM32 qua bootloader ROM (AN3155).
 * Viet theo kieu C: chi ham va bien static (Serial / WiFi / LittleFS la doi tuong co san cua Arduino). */
#include <Arduino.h>
#include "config.h"
#include "net.h"
#include "log_store.h"
#include "stm32_link.h"
#include "display.h"
#include "cloud.h"
#include "ota.h"
#include "stm32_boot.h"

static uint32_t ulLastStatus = 0;
static uint32_t ulLastTime   = 0;
static uint8_t  ucTimeSent   = 0;   /* da gui TIME lan dau sau khi co gio */
static uint32_t ulLastBeat   = 0;   /* dong trang thai moi 5s */
static CloudUsers xUsers;           /* static: ~600 byte, khong de tren stack loop() */

#define BEAT_PERIOD_MS  5000UL

void setup(void)
{
    boot_init();        /* som nhat: BOOT0 = 0, NRST tha -> STM32 chay app binh thuong */
    Serial.begin(115200);
    display_init();     /* bat man hinh truoc de thay ngay board da chay */
    /* USB noi thang vao chip: sau reset cong USB can 1 luc moi san sang.
     * Cho may tinh mo cong toi da 3s, roi van chay tiep (khong cam USB van chay binh thuong). */
    while (!Serial && millis() < 3000UL) {
        delay(10);
    }
    delay(200);
    Serial.println("\n=== ESP32-C3 khoa cua - buoc 9c-3 ===");

    log_store_init();
    net_init();
    link_init();
    cloud_init();       /* tao task cloud (sau net_init va log_store_init) */
    ota_init();         /* trang nap firmware http://<IP>/ */
}

void loop(void)
{
    uint32_t ulNow = millis();
    uint32_t ulEpoch;
    char     cIp[16];
    char     cStage[24];
    uint8_t  ucPct;

    net_poll();
    ota_poll();
    link_poll();                        /* dang nap thi link_poll tu bo qua */

    /* Dang nap firmware: Serial1 thuoc bootloader STM32, khong gui STATUS / TIME / bang ten */
    if (ota_uart_busy()) {
        ulLastStatus = ulNow;
    }

    /* STATUS moi 2s: STM32 vua biet con lien ket (timeout 6s) vua biet trang thai WiFi / Sheet. */
    if (ulNow - ulLastStatus >= STATUS_PERIOD_MS) {
        ulLastStatus = ulNow;
        link_send_status(net_wifi_ok(), (cloud_state() == CLOUD_STATE_OK) ? 1 : 0, log_store_count());
    }

    /* Bang ten: co ban moi tu Sheet, hoac STM32 vua bat lai (bang ten trong RAM STM32 da mat) */
    if (link_stm32_alive() && !ota_uart_busy()) {
        uint8_t ucFresh = (uint8_t)cloud_take_users(&xUsers);
        uint8_t ucAsk   = link_take_need_users();

        if (ucFresh || (ucAsk && cloud_copy_users(&xUsers))) {
            link_send_users(&xUsers);
        }
    }

    /* TIME: ngay khi co gio NTP, sau do moi 60s */
    ulEpoch = net_epoch();
    if (ulEpoch != 0 && !ota_uart_busy() && (!ucTimeSent || ulNow - ulLastTime >= TIME_PERIOD_MS)) {
        ulLastTime = ulNow;
        ucTimeSent = 1;
        link_send_time(ulEpoch);
    }

    /* Dong trang thai moi 5s: mo Serial Monitor muon van biet chuong trinh con chay */
    if (ulNow - ulLastBeat >= BEAT_PERIOD_MS) {
        ulLastBeat = ulNow;
        Serial.printf("[ESP] chay %lus | WiFi %s | Sheet %s | STM32 %s | gio %s | log cho %u | UART nhan %lu byte, %lu khung\n",
                      (unsigned long)(ulNow / 1000UL),
                      net_wifi_ok() ? "OK" : "chua",
                      (cloud_state() == CLOUD_STATE_OK) ? "OK" : ((cloud_state() == CLOUD_STATE_ERROR) ? "LOI" : ".."),
                      link_stm32_alive() ? "OK" : "chua thay",
                      (ulEpoch != 0) ? "OK" : "chua",
                      (unsigned)log_store_count(),
                      (unsigned long)link_rx_bytes(),
                      (unsigned long)link_rx_frames());
    }

    if (ota_screen(cStage, sizeof(cStage), &ucPct)) {
        display_ota(cStage, ucPct);
    } else {
        net_ip(cIp, sizeof(cIp));
        display_update(net_wifi_ok(), link_stm32_alive(), cloud_state(), ulEpoch, log_store_count(), cIp);
    }

    delay(2);
}
