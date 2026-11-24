/* Man hinh OLED 0.42" 72x40 tren ESP32-C3 Super Mini, dung thu vien U8g2.
 * File .cpp vi U8g2 cua Arduino la C++; code viet theo kieu C (doi tuong xOled
 * la doi tuong co san cua thu vien, giong Serial / WiFi).
 *
 * Bo cuc (font 5x7, 14 ky tu x 5 dong):
 *   WIFI : OK
 *   STM  : OK
 *   SHEET: OK          (OK / LOI / ..)
 *   LOG CHO: 0
 *   18:05  123s        (gio VN + thoi gian chay) <-> IP (doi moi 4s, de mo trang nap firmware)
 */
#include <Arduino.h>
#include <U8g2lib.h>
#include "display.h"
#include "config.h"
#include "cloud.h"

/* "72X40_ER": dung loai man EastRising 0.42" (da tinh san do lech cot cua man nay) */
static U8G2_SSD1306_72X40_ER_F_HW_I2C xOled(U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);

static uint32_t ulLastDraw = 0;

void display_init(void)
{
    xOled.begin();
    xOled.setFont(u8g2_font_5x7_tf);
    xOled.clearBuffer();
    xOled.drawStr(0, 15, "KHOA CUA");
    xOled.drawStr(0, 31, "DANG KHOI");
    xOled.drawStr(0, 39, "DONG...");
    xOled.sendBuffer();
}

void display_update(uint8_t wifi, uint8_t stm, uint8_t cloud, uint32_t epoch, uint16_t pending,
                    const char *pcIp)
{
    uint32_t ulNow = millis();
    uint32_t ulLocal;
    char cLine[16];

    if (ulNow - ulLastDraw < OLED_PERIOD_MS) {
        return;
    }
    ulLastDraw = ulNow;

    xOled.clearBuffer();

    xOled.drawStr(0, 7, wifi ? "WIFI : OK" : "WIFI : ...");
    xOled.drawStr(0, 15, stm ? "STM  : OK" : "STM  : --");
    xOled.drawStr(0, 23, (cloud == CLOUD_STATE_OK) ? "SHEET: OK" :
                         ((cloud == CLOUD_STATE_ERROR) ? "SHEET: LOI" : "SHEET: .."));

    snprintf(cLine, sizeof(cLine), "LOG CHO: %u", (unsigned)pending);
    xOled.drawStr(0, 31, cLine);

    if (pcIp[0] != '\0' && ((ulNow / 4000UL) & 1UL) != 0UL) {
        snprintf(cLine, sizeof(cLine), "%s", pcIp);         /* "192.168.0.14" = 12 ky tu */
    } else if (epoch != 0) {
        ulLocal = epoch + (uint32_t)OLED_TZ_OFFSET_S;
        snprintf(cLine, sizeof(cLine), "%02lu:%02lu %lus",
                 (unsigned long)((ulLocal / 3600UL) % 24UL),
                 (unsigned long)((ulLocal / 60UL) % 60UL),
                 (unsigned long)(ulNow / 1000UL));
    } else {
        snprintf(cLine, sizeof(cLine), "--:-- %lus", (unsigned long)(ulNow / 1000UL));
    }
    xOled.drawStr(0, 39, cLine);

    xOled.sendBuffer();
}

void display_ota(const char *pcStage, uint8_t ucPercent)
{
    uint32_t ulNow = millis();
    char cLine[16];

    if (ulNow - ulLastDraw < 200UL) {
        return;
    }
    ulLastDraw = ulNow;

    xOled.clearBuffer();
    xOled.drawStr(0, 7, "NAP STM32");
    xOled.drawStr(0, 17, pcStage);
    xOled.drawFrame(0, 22, 72, 8);                          /* thanh tien do */
    xOled.drawBox(1, 23, (uint8_t)(70U * ucPercent / 100U), 6);
    snprintf(cLine, sizeof(cLine), "%u%%", (unsigned)ucPercent);
    xOled.drawStr(0, 39, cLine);
    xOled.sendBuffer();
}
