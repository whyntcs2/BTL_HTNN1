/* WiFi (SSID ghi cung) + gio NTP (UTC).
 * File .cpp vi WiFi cua Arduino la C++; code viet theo kieu C. */
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "net.h"
#include "config.h"

static uint8_t ucLastWifi = 0xFF;   /* de chi in khi trang thai doi */
static uint8_t ucTimeShown = 0;

void net_init(void)
{
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
#if WIFI_LOW_TX_POWER
    WiFi.setTxPower(WIFI_POWER_8_5dBm);             /* C3 Super Mini anten yeu: phat nho lai on dinh hon */
#endif
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);   /* UTC, khong doi mui gio o day */
    Serial.printf("[NET] Dang ket noi WiFi \"%s\"...\n", WIFI_SSID);
}

void net_poll(void)
{
    uint8_t ucNow = net_wifi_ok();

    if (ucNow != ucLastWifi) {
        ucLastWifi = ucNow;
        if (ucNow) {
            Serial.printf("[NET] WiFi OK, IP %s  -> trang nap firmware: http://%s/\n",
                          WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
        } else {
            Serial.println("[NET] Mat WiFi (tu ket noi lai)");
        }
    }
    if (!ucTimeShown && net_epoch() != 0) {
        ucTimeShown = 1;
        Serial.printf("[NET] Da dong bo gio NTP: epoch %lu\n", (unsigned long)net_epoch());
    }
}

uint8_t net_wifi_ok(void)
{
    return (WiFi.status() == WL_CONNECTED) ? 1 : 0;
}

uint32_t net_epoch(void)
{
    time_t t = time(NULL);
    return ((uint32_t)t >= EPOCH_VALID_MIN) ? (uint32_t)t : 0;
}

/* IP dang chuoi "192.168.0.14" ("" neu chua co WiFi) */
void net_ip(char *pcOut, size_t uxCap)
{
    if (net_wifi_ok()) {
        strncpy(pcOut, WiFi.localIP().toString().c_str(), uxCap - 1U);
        pcOut[uxCap - 1U] = '\0';
    } else {
        pcOut[0] = '\0';
    }
}
