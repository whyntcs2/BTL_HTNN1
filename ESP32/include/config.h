#ifndef CONFIG_H
#define CONFIG_H

/* ================== WiFi (ghi cung, chot 24/11) ==================
 * Demo: bat diem phat WiFi tren dien thoai voi dung ten / mat khau nay. */
#define WIFI_SSID           "Phong 403."
#define WIFI_PASS           "88888888"

/* ================== Gio (NTP) ==================
 * ESP32 giu gio UTC; Google Sheet tu doi sang gio Viet Nam. */
#define NTP_SERVER_1        "pool.ntp.org"
#define NTP_SERVER_2        "time.google.com"
#define EPOCH_VALID_MIN     1700000000UL    /* nho hon: chua dong bo duoc gio */

/* Mot so lo ESP32-C3 Super Mini anten yeu, phat cong suat cao lai mat WiFi.
 * 1: ha cong suat phat xuong 8.5 dBm (du dung khi dat gan dien thoai phat WiFi).
 * 0: de mac dinh. */
#define WIFI_LOW_TX_POWER   1

/* ================== Noi voi STM32 ==================
 * ESP32-C3 Super Mini. Tranh: GPIO2, 8, 9 (strapping, GPIO9 = nut BOOT),
 * GPIO5/6 (OLED tren board), GPIO18/19 (USB), GPIO20/21 (UART0). */
#define STM_UART_TX_PIN     3       /* ESP32 TX -> STM32 PA10 (RX) */
#define STM_UART_RX_PIN     4       /* ESP32 RX <- STM32 PA9 (TX) */
#define STM_BOOT0_PIN       7       /* buoc 9c-3 (OTA) */
#define STM_NRST_PIN        10      /* buoc 9c-3 (OTA) */
#define STM_BAUD            115200  /* giong comm_uart.c phia STM32 (8N1) */

/* ================== OLED 0.42" 72x40 tren board ==================
 * Ban pho bien: SDA = GPIO5, SCL = GPIO6. Neu man hinh khong sang, thu doi sang
 * SDA = 8, SCL = 9 (mot so ban noi nhu vay) - khi do phai doi ca 2 dong duoi. */
#define OLED_SDA_PIN        5
#define OLED_SCL_PIN        6
#define OLED_PERIOD_MS      500UL   /* ve lai man hinh moi 0.5s */
#define OLED_TZ_OFFSET_S    (7L * 3600L)    /* chi de HIEN THI gio Viet Nam (UTC+7) */

/* ================== Chu ky ==================
 * STM32 coi la mat lien ket neu 6s khong nhan duoc khung hop le (COMM_LINK_TIMEOUT_MS). */
#define STATUS_PERIOD_MS    2000UL  /* gui STATUS (wifi, cloud, so log cho) */
#define TIME_PERIOD_MS      60000UL /* gui lai TIME de STM32 khong troi gio */
#define STM_ALIVE_MS        6000UL  /* qua lau khong nhan HELLO / khung tu STM32 */

/* ================== Google Sheets (Apps Script Web App, buoc 9c-2) ==================
 * CLOUD_URL: dan URL ".../exec" lay luc Trien khai Web App (xem README).
 * CLOUD_KEY: phai GIONG HET bien SECRET trong Apps Script. */
#define CLOUD_URL           "https://script.google.com/macros/s/AKfycbwGVJTnHdRda6Fhb5WXC7cCIeVWbSCrCcVViHkprUieVID1P8a-zWPFccTU7gSsm_LS/exec"
#define CLOUD_KEY           "khoacua2026"
#define CLOUD_BATCH         10U         /* so log gui moi lan */
#define CLOUD_RETRY_MIN_MS  10000UL     /* gui loi: thu lai sau 10s, ... */
#define CLOUD_RETRY_MAX_MS  60000UL     /* ... gap doi moi lan, toi da 60s */
#define CLOUD_HTTP_TIMEOUT  10000U      /* ms cho moi yeu cau HTTPS */
#define USERS_PERIOD_MS     30000UL     /* lay bang ten moi 30s (dung de duoi 30s) */
#define USERS_RESEND_MS     600000UL    /* bang khong doi van gui lai xuong STM32 moi 10 phut */

/* ================== Hang doi log trong flash ESP32 ================== */
#define LOG_STORE_MAX       2000U   /* toi da 2000 ban ghi (64KB LittleFS) */

/* ================== Nap firmware STM32 qua WiFi (OTA, buoc 9c-3) ================== */
#define OTA_USER            "admin"
#define OTA_PASSWORD        "khoacua2026"
#define STM_APP_BYTES       (60UL * 1024UL)  /* = COMM_APP_BYTES phia STM32: trang 0-59 */
#define OTA_READY_TIMEOUT_MS 20000UL        /* cho STM32 tra OTA_READY */

#endif /* CONFIG_H */