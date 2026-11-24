/* Task cloud: gui log len Google Sheets va lay bang ten, qua Apps Script Web App.
 *
 * Giao tiep bang HTTPS GET (Web App luon tra chuyen huong 302, GET di qua chuyen huong on dinh):
 *   ?k=<CLOUD_KEY>&a=log&d=<hex>   d = cac ban ghi 28 byte noi tiep (24 byte log + 4 byte gio ESP32 nhan)
 *        -> "OK"                   Sheet da ghi (log trung ma su kien thi Sheet tu bo qua)
 *   ?k=<CLOUD_KEY>&a=users
 *        -> "OK\n0|NGUYEN VAN A\n1|TRAN B\n..."   (script da bo dau, viet hoa, <= 16 ky tu)
 * Bang ten lay moi USERS_PERIOD_MS (30s) nhung chi gui xuong STM32 khi bang doi (CRC32 khac),
 * khi nguoi dung bam "Cap nhat ten ngay", hoac moi USERS_RESEND_MS (10 phut) cho chac.
 *
 * Log chi bi xoa khoi LittleFS sau khi Sheet tra "OK". Loi -> giu lai, thu lai sau 10s, 20s, ... 60s.
 * File .cpp vi HTTPClient / WiFiClientSecure cua Arduino la C++; code viet theo kieu C. */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <stdlib.h>
#include "cloud.h"
#include "config.h"
#include "net.h"
#include "log_store.h"
#include "comm_protocol.h"

#define CLOUD_TASK_STACK    8192U       /* byte (ESP32 tinh stack theo byte); TLS can nhieu stack */
#define CLOUD_TASK_PRIO     1U          /* bang loop() */
#define REC_BYTES           28U         /* 24 byte log + 4 byte rx_epoch */
#define URL_MAX             (256U + CLOUD_BATCH * REC_BYTES * 2U)
#define BODY_MAX            1024U       /* 32 dong "id|TEN 16 KY TU" ~ 700 byte */

static volatile uint8_t ucState = CLOUD_STATE_UNKNOWN;

static SemaphoreHandle_t xUsersMutex = NULL;
static CloudUsers xUsers;               /* bang ten gan nhat (task cloud ghi, loop() doc) */
static uint8_t    ucUsersValid = 0;
static uint8_t    ucUsersNew = 0;
static volatile uint8_t ucForceUsers = 0;   /* nut tren trang web */
static uint32_t   ulUsersOkMs = 0;      /* millis() lan lay bang ten thanh cong gan nhat */
static uint32_t   ulUsersPushMs = 0;    /* millis() lan danh dau gui xuong STM32 gan nhat */

static char cUrl[URL_MAX];
static char cBody[BODY_MAX];
static StoredLog xBatch[CLOUD_BATCH];

static void vTaskCloud(void *pvParameters);
static int  prvHttpGet(const char *pcUrl, char *pcBody, size_t uxCap);
static int  prvSendLogs(void);
static int  prvFetchUsers(void);
static int  prvParseUsers(const char *pcBody, CloudUsers *pxOut);
static uint32_t prvUsersCrc(const CloudUsers *pxUsers);

int cloud_init(void)
{
    xUsersMutex = xSemaphoreCreateMutex();
    if (xUsersMutex == NULL) {
        return 0;
    }
    if (strstr(CLOUD_URL, "DAN_ID_WEB_APP") != NULL) {
        Serial.println("[CLOUD] Chua dan URL Web App vao CLOUD_URL (config.h) -> khong gui Sheet");
        return 0;
    }
    if (xTaskCreate(vTaskCloud, "cloud", CLOUD_TASK_STACK, NULL, CLOUD_TASK_PRIO, NULL) != pdPASS) {
        Serial.println("[CLOUD] Khong tao duoc task cloud");
        return 0;
    }
    return 1;
}

uint8_t cloud_state(void)
{
    return ucState;
}

int cloud_take_users(CloudUsers *out)
{
    int r = 0;

    xSemaphoreTake(xUsersMutex, portMAX_DELAY);
    if (ucUsersNew) {
        memcpy(out, &xUsers, sizeof(xUsers));
        ucUsersNew = 0;
        r = 1;
    }
    xSemaphoreGive(xUsersMutex);
    return r;
}

void cloud_request_users(void)
{
    ucForceUsers = 1;
}

int cloud_users_info(uint16_t *pusCount, uint32_t *pulAgeSec)
{
    int r = 0;

    if (xUsersMutex == NULL) {
        return 0;
    }
    xSemaphoreTake(xUsersMutex, portMAX_DELAY);
    if (ucUsersValid) {
        *pusCount  = xUsers.count;
        *pulAgeSec = (millis() - ulUsersOkMs) / 1000UL;
        r = 1;
    }
    xSemaphoreGive(xUsersMutex);
    return r;
}

int cloud_copy_users(CloudUsers *out)
{
    int r = 0;

    if (xUsersMutex == NULL) {
        return 0;
    }
    xSemaphoreTake(xUsersMutex, portMAX_DELAY);
    if (ucUsersValid) {
        memcpy(out, &xUsers, sizeof(xUsers));
        r = 1;
    }
    xSemaphoreGive(xUsersMutex);
    return r;
}

/* ================== Task ================== */

static void vTaskCloud(void *pvParameters)
{
    uint32_t ulNextLog   = 0;                   /* millis() duoc phep gui log lan toi */
    uint32_t ulRetry     = CLOUD_RETRY_MIN_MS;
    uint32_t ulLastUsers = 0;
    uint8_t  ucUsersOnce = 0;
    uint32_t ulNow;

    (void)pvParameters;
    Serial.println("[CLOUD] Task cloud chay");

    for (;;) {
        if (!net_wifi_ok()) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);  /* portTICK_PERIOD_MS = ten ESP-IDF cua portTICK_RATE_MS */
            continue;
        }
        ulNow = millis();

        /* Bang ten: ngay khi co WiFi, sau do moi 30s, hoac khi bam nut tren trang web */
        if (!ucUsersOnce || ucForceUsers || ulNow - ulLastUsers >= USERS_PERIOD_MS) {
            ulLastUsers = ulNow;
            ucUsersOnce = 1;
            prvFetchUsers();
        }

        /* Log: con log cho va da het thoi gian cho thu lai */
        if (log_store_count() > 0U && (int32_t)(ulNow - ulNextLog) >= 0) {
            if (prvSendLogs()) {
                ulRetry   = CLOUD_RETRY_MIN_MS;
                ulNextLog = millis();               /* con log thi gui lo tiep ngay */
            } else {
                ulNextLog = millis() + ulRetry;
                Serial.printf("[CLOUD] Gui loi, thu lai sau %lus\n", (unsigned long)(ulRetry / 1000UL));
                ulRetry *= 2UL;
                if (ulRetry > CLOUD_RETRY_MAX_MS) {
                    ulRetry = CLOUD_RETRY_MAX_MS;
                }
            }
        }

        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

/* ================== HTTPS ================== */

/* GET, theo chuyen huong cua Google, chep noi dung tra ve vao pcBody. 1: HTTP 200 */
static int prvHttpGet(const char *pcUrl, char *pcBody, size_t uxCap)
{
    WiFiClientSecure xClient;
    HTTPClient       xHttp;
    int              iCode;
    int              ok = 0;

    pcBody[0] = '\0';
    xClient.setInsecure();      /* khong kiem tra chung chi (demo) - van ma hoa TLS */
    xHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    xHttp.setTimeout(CLOUD_HTTP_TIMEOUT);
    if (!xHttp.begin(xClient, pcUrl)) {
        return 0;
    }
    iCode = xHttp.GET();
    if (iCode == 200) {
        strncpy(pcBody, xHttp.getString().c_str(), uxCap - 1U);
        pcBody[uxCap - 1U] = '\0';
        ok = 1;
    } else {
        Serial.printf("[CLOUD] HTTP loi %d\n", iCode);
    }
    xHttp.end();
    return ok;
}

static int prvSendLogs(void)
{
    static const char cHex[] = "0123456789abcdef";
    uint16_t n, i, k;
    uint8_t  ucRec[REC_BYTES];
    size_t   pos;

    n = log_store_peek(xBatch, CLOUD_BATCH);
    if (n == 0U) {
        return 0;
    }

    pos = (size_t)snprintf(cUrl, sizeof(cUrl), "%s?k=%s&a=log&d=", CLOUD_URL, CLOUD_KEY);
    for (i = 0; i < n; i++) {
        memcpy(ucRec, xBatch[i].raw, LOG_RAW_BYTES);
        Comm_PutLe(ucRec + LOG_RAW_BYTES, xBatch[i].rx_epoch, 4);
        for (k = 0; k < REC_BYTES; k++) {
            cUrl[pos++] = cHex[ucRec[k] >> 4];
            cUrl[pos++] = cHex[ucRec[k] & 0x0FU];
        }
    }
    cUrl[pos] = '\0';

    if (!prvHttpGet(cUrl, cBody, sizeof(cBody)) || strncmp(cBody, "OK", 2) != 0) {
        if (cBody[0] != '\0') {
            Serial.printf("[CLOUD] Sheet tra loi: %.60s\n", cBody);
        }
        ucState = CLOUD_STATE_ERROR;
        return 0;
    }
    log_store_drop(n);              /* chi xoa sau khi Sheet xac nhan */
    ucState = CLOUD_STATE_OK;
    Serial.printf("[CLOUD] Da gui %u log len Sheet, con %u\n", (unsigned)n, (unsigned)log_store_count());
    return 1;
}

static int prvFetchUsers(void)
{
    static CloudUsers xNew;         /* static: tranh 600 byte tren stack task */
    uint8_t ucForce = ucForceUsers;
    uint8_t ucChanged;

    snprintf(cUrl, sizeof(cUrl), "%s?k=%s&a=users", CLOUD_URL, CLOUD_KEY);
    if (!prvHttpGet(cUrl, cBody, sizeof(cBody)) || !prvParseUsers(cBody, &xNew)) {
        if (cBody[0] != '\0') {
            Serial.printf("[CLOUD] Bang ten loi: %.60s\n", cBody);
        }
        ucForceUsers = 0;           /* bam nut ma loi: khong lap lai lien tuc, cho chu ky sau */
        ucState = CLOUD_STATE_ERROR;
        return 0;
    }
    ucState = CLOUD_STATE_OK;

    ucForceUsers = 0;

    xSemaphoreTake(xUsersMutex, portMAX_DELAY);
    ucChanged = (uint8_t)(!ucUsersValid || xUsers.version != xNew.version);
    if (ucChanged) {
        Serial.printf("[CLOUD] Bang ten moi: %u nguoi\n", (unsigned)xNew.count);
    }
    memcpy(&xUsers, &xNew, sizeof(xNew));
    ucUsersValid = 1;
    ulUsersOkMs  = millis();
    /* STM32 khong bao da nhan bang ten: gui khi doi, khi bam nut, va dinh ky 10 phut cho chac */
    if (ucChanged || ucForce || millis() - ulUsersPushMs >= USERS_RESEND_MS) {
        ulUsersPushMs = millis();
        ucUsersNew    = 1;
    }
    xSemaphoreGive(xUsersMutex);
    return 1;
}

/* "OK\n<id>|<TEN>\n..." -> CloudUsers. Dong sai thi bo qua dong do. */
static int prvParseUsers(const char *pcBody, CloudUsers *pxOut)
{
    const char *p;
    const char *pcBar;
    const char *pcEnd;
    unsigned long ulId;
    size_t   uxLen;
    uint16_t i;
    char    *pcStop;

    memset(pxOut, 0, sizeof(*pxOut));
    if (strncmp(pcBody, "OK", 2) != 0) {
        return 0;
    }
    p = strchr(pcBody, '\n');
    while (p != NULL && pxOut->count < COMM_USER_MAX) {
        p++;
        pcEnd = strchr(p, '\n');
        if (pcEnd == NULL) {
            pcEnd = p + strlen(p);
        }
        pcBar = strchr(p, '|');
        if (pcBar != NULL && pcBar < pcEnd) {
            ulId  = strtoul(p, &pcStop, 10);
            uxLen = (size_t)(pcEnd - pcBar - 1);
            if (uxLen > 0U && pcBar[uxLen] == '\r') {
                uxLen--;                            /* phong khi co \r\n */
            }
            if (pcStop == pcBar && ulId < 0xFFFFUL && uxLen > 0U && uxLen <= COMM_NAME_MAX) {
                for (i = 0; i < pxOut->count && pxOut->id[i] != (uint16_t)ulId; i++) {
                }
                if (i == pxOut->count) {            /* ID trung: STM32 tu choi ca bang -> bo dong sau */
                    pxOut->id[i]  = (uint16_t)ulId;
                    pxOut->len[i] = (uint8_t)uxLen;
                    memcpy(pxOut->name[i], pcBar + 1, uxLen);
                    pxOut->name[i][uxLen] = '\0';
                    pxOut->count++;
                }
            }
        }
        p = (*pcEnd == '\0') ? NULL : pcEnd;
    }
    pxOut->version = prvUsersCrc(pxOut);
    return 1;
}

/* Giong het cach Comm_UsersAccept phia STM32 kiem tra: tung hang index(2) id(2) len(1) + ten */
static uint32_t prvUsersCrc(const CloudUsers *pxUsers)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t  prefix[5];
    uint16_t i;

    for (i = 0; i < pxUsers->count; i++) {
        Comm_PutLe(prefix, i, 2);
        Comm_PutLe(prefix + 2, pxUsers->id[i], 2);
        prefix[4] = pxUsers->len[i];
        crc = Comm_Crc32Update(crc, prefix, sizeof(prefix));
        crc = Comm_Crc32Update(crc, (const uint8_t *)pxUsers->name[i], pxUsers->len[i]);
    }
    return ~crc;
}