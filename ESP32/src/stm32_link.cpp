/* Lien ket UART voi STM32 (task Comm cua ban cung nhom).
 * Dung chung comm_protocol.c voi STM32 nen cach dong khung + CRC giong het 2 ben.
 *
 * STM32 -> ESP32: HELLO (1), LOG (2), OTA_READY (11), OTA_ABORT (12)
 * ESP32 -> STM32: ACK_STORED (3), REJECT (4), STATUS (5), TIME (6),
 *                 USERS_BEGIN/USER/USERS_END (7/8/9, buoc 9c-2), OTA_PREPARE (10, buoc 9c-3)
 * File .cpp vi Serial1 cua Arduino la C++; code viet theo kieu C. */
#include <Arduino.h>
#include "stm32_link.h"
#include "comm_protocol.h"
#include "log_store.h"
#include "net.h"
#include "config.h"
#include "cloud.h"

/* Ma REJECT (giong task_comm.h phia STM32) */
#define REJECT_BAD_DATA     1U
#define REJECT_FULL         2U
#define REJECT_STORE_ERROR  3U

static Comm_Parser xParser;
static uint16_t    usTxSeq = 0;         /* sequence cho khung ESP32 tu phat (STATUS, TIME) */
static uint32_t    ulLastRx = 0;        /* millis() luc nhan khung hop le gan nhat */
static uint8_t     ucEverRx = 0;
static uint32_t    ulRxBytes = 0;       /* tong so byte nhan tu STM32 (de chan doan day noi) */
static uint32_t    ulRxFrames = 0;      /* so khung hop le (dung CRC) */
static uint8_t     ucNeedUsers = 0;     /* STM32 vua bat / vua noi lai: bang ten trong RAM STM32 da mat */
static uint8_t     ucWasAlive = 0;
static uint8_t     ucSuspended = 0;     /* 1: UART dang dung de nap firmware (bootloader 8E1) */
static uint8_t     ucHelloShown = 0;
static uint32_t    ulStmVersion = 0;    /* version firmware STM32 trong HELLO */
static uint8_t     ucOtaReplyType = 0;  /* OTA_READY / OTA_ABORT vua nhan (0: chua co) */
static uint16_t    usOtaReplySeq = 0;

static void prvSend(uint8_t ucType, uint16_t usSeq, const uint8_t *pucPayload, uint16_t usLen);
static void prvHandleFrame(const Comm_Frame *pxFrame);
static void prvHandleLog(const Comm_Frame *pxFrame);
static void prvPrintLog(const uint8_t *raw);

void link_init(void)
{
    memset(&xParser, 0, sizeof(xParser));
    Serial1.setRxBufferSize(1024);      /* goi truoc begin() */
    Serial1.begin(STM_BAUD, SERIAL_8N1, STM_UART_RX_PIN, STM_UART_TX_PIN);
    Serial.printf("[STM] UART1 %d baud, TX GPIO%d -> PA10, RX GPIO%d <- PA9\n",
                  STM_BAUD, STM_UART_TX_PIN, STM_UART_RX_PIN);
}

void link_poll(void)
{
    Comm_Frame xFrame;

    if (ucSuspended) {
        return;                             /* Serial1 dang thuoc bootloader */
    }
    while (Serial1.available() > 0) {
        Comm_ParserFeed(&xParser, (uint8_t)Serial1.read());
        ulRxBytes++;
        while (Comm_ParserNext(&xParser, &xFrame)) {
            ulRxFrames++;
            ulLastRx = millis();
            ucEverRx = 1;
            prvHandleFrame(&xFrame);
        }
    }

    /* Vua thay STM32 (bat nguon / noi lai): gui lai bang ten */
    if (link_stm32_alive() && !ucWasAlive) {
        ucNeedUsers = 1;
    }
    ucWasAlive = link_stm32_alive();
}

/* Bat dau nap: ngung doc / gui tren Serial1 (stm32_boot.cpp doi sang 8E1) */
void link_suspend(void)
{
    ucSuspended = 1;
}

/* Nap xong: tra Serial1 ve 8N1 cho giao thuc Comm, STM32 vua reset nen coi nhu chua thay */
void link_resume(void)
{
    Serial1.end();
    Serial1.setRxBufferSize(1024);
    Serial1.begin(STM_BAUD, SERIAL_8N1, STM_UART_RX_PIN, STM_UART_TX_PIN);
    memset(&xParser, 0, sizeof(xParser));
    ucEverRx      = 0;
    ucWasAlive    = 0;
    ucHelloShown  = 0;
    ulStmVersion  = 0;
    ucOtaReplyType = 0;
    ucSuspended   = 0;
}

uint32_t link_stm32_version(void)
{
    return ulStmVersion;
}

/* OTA_PREPARE: size (4) + 32 byte (STM32 chua dung; o day: CRC32 cua file + 0). Tra seq da gui */
uint16_t link_send_ota_prepare(uint32_t ulSize, uint32_t ulCrc)
{
    uint8_t p[36];

    memset(p, 0, sizeof(p));
    Comm_PutLe(p, ulSize, 4);
    Comm_PutLe(p + 4, ulCrc, 4);
    ucOtaReplyType = 0;
    ++usTxSeq;
    prvSend(COMM_OTA_PREPARE, usTxSeq, p, 36);
    return usTxSeq;
}

void link_send_ota_abort(uint16_t usSeq)
{
    prvSend(COMM_OTA_ABORT, usSeq, NULL, 0);
}

/* 1 (roi tu xoa) neu vua nhan OTA_READY / OTA_ABORT tu STM32 */
int link_take_ota_reply(uint8_t *pucType, uint16_t *pusSeq)
{
    if (ucOtaReplyType == 0U) {
        return 0;
    }
    *pucType = ucOtaReplyType;
    *pusSeq  = usOtaReplySeq;
    ucOtaReplyType = 0;
    return 1;
}

uint8_t link_take_need_users(void)
{
    uint8_t r = ucNeedUsers;

    ucNeedUsers = 0;
    return r;
}

/* USERS_BEGIN (version, count) -> USER x count (version, index, id, len, ten) -> USERS_END (version, count, CRC32).
 * Nghi 5ms giua cac khung: ring RX 512 byte phia STM32 khong bi tran. */
void link_send_users(const CloudUsers *pxUsers)
{
    uint8_t  p[9U + COMM_NAME_MAX];
    uint16_t i;

    if (ucSuspended) {
        return;
    }
    Comm_PutLe(p, pxUsers->version, 4);
    Comm_PutLe(p + 4, pxUsers->count, 2);
    prvSend(COMM_USERS_BEGIN, ++usTxSeq, p, 6);
    delay(5);

    for (i = 0; i < pxUsers->count; i++) {
        Comm_PutLe(p, pxUsers->version, 4);
        Comm_PutLe(p + 4, i, 2);
        Comm_PutLe(p + 6, pxUsers->id[i], 2);
        p[8] = pxUsers->len[i];
        memcpy(p + 9, pxUsers->name[i], pxUsers->len[i]);
        prvSend(COMM_USER, ++usTxSeq, p, (uint16_t)(9U + pxUsers->len[i]));
        delay(5);
    }

    Comm_PutLe(p, pxUsers->version, 4);
    Comm_PutLe(p + 4, pxUsers->count, 2);
    Comm_PutLe(p + 6, pxUsers->version, 4);     /* version chinh la CRC32 cua bang */
    prvSend(COMM_USERS_END, ++usTxSeq, p, 10);
    Serial.printf("[STM] Da gui bang ten %u nguoi xuong STM32\n", (unsigned)pxUsers->count);
}

uint32_t link_rx_bytes(void)
{
    return ulRxBytes;
}

uint32_t link_rx_frames(void)
{
    return ulRxFrames;
}

uint8_t link_stm32_alive(void)
{
    return (ucEverRx && (millis() - ulLastRx) < STM_ALIVE_MS) ? 1 : 0;
}

void link_send_status(uint8_t wifi, uint8_t cloud, uint16_t pending)
{
    uint8_t p[4];

    if (ucSuspended) {
        return;
    }
    p[0] = wifi;
    p[1] = cloud;
    Comm_PutLe(p + 2, pending, 2);
    prvSend(COMM_STATUS, ++usTxSeq, p, 4);
}

void link_send_time(uint32_t epoch)
{
    uint8_t p[4];

    if (ucSuspended) {
        return;
    }
    Comm_PutLe(p, epoch, 4);
    prvSend(COMM_TIME, ++usTxSeq, p, 4);
}

/* ================== Noi bo ================== */

static void prvSend(uint8_t ucType, uint16_t usSeq, const uint8_t *pucPayload, uint16_t usLen)
{
    Comm_Frame xFrame;
    uint8_t    ucBytes[COMM_FRAME_MAX];
    size_t     n;

    xFrame.type     = ucType;
    xFrame.sequence = usSeq;
    xFrame.length   = usLen;
    if (usLen > 0U && pucPayload != NULL) {
        memcpy(xFrame.payload, pucPayload, usLen);
    }
    n = Comm_Encode(&xFrame, ucBytes, sizeof(ucBytes));
    if (n > 0U) {
        Serial1.write(ucBytes, n);
    }
}

static void prvHandleFrame(const Comm_Frame *pxFrame)
{
    switch (pxFrame->type) {
    case COMM_HELLO:
        if (pxFrame->length == 4U) {
            ulStmVersion = (uint32_t)Comm_GetLe(pxFrame->payload, 4);
            if (!ucHelloShown) {
                ucHelloShown = 1;
                Serial.printf("[STM] Da nhan HELLO, firmware STM32 v%lu\n", (unsigned long)ulStmVersion);
            }
        }
        break;
    case COMM_LOG:
        prvHandleLog(pxFrame);
        break;
    case COMM_OTA_READY:
    case COMM_OTA_ABORT:
        ucOtaReplyType = pxFrame->type;
        usOtaReplySeq  = pxFrame->sequence;
        Serial.printf("[STM] %s (seq %u)\n",
                      (pxFrame->type == COMM_OTA_READY) ? "OTA_READY" : "OTA_ABORT",
                      (unsigned)pxFrame->sequence);
        break;
    default:
        break;
    }
}

/* LOG: ghi vao LittleFS ROI moi ACK (STM32 chi xoa log khi nhan ACK).
 * Log gui lai (ACK truoc bi mat) thi khong ghi lan nua, chi ACK lai. */
static void prvHandleLog(const Comm_Frame *pxFrame)
{
    const uint8_t *p = pxFrame->payload;
    uint32_t       ulDevice;
    uint64_t       ullEvent;
    uint8_t        ucAck[12];
    uint8_t        ucCode;

    if (pxFrame->length != LOG_RAW_BYTES || p[23] != 0U ||
        p[18] < 1U || p[18] > 3U || p[19] > 2U || p[20] > 2U) {
        ucCode = REJECT_BAD_DATA;
        prvSend(COMM_REJECT, pxFrame->sequence, &ucCode, 1);
        return;
    }

    ulDevice = (uint32_t)Comm_GetLe(p, 4);
    ullEvent = Comm_GetLe(p + 4, 8);

    if (!log_store_seen(ulDevice, ullEvent)) {
        if (log_store_count() >= LOG_STORE_MAX) {
            ucCode = REJECT_FULL;
            prvSend(COMM_REJECT, pxFrame->sequence, &ucCode, 1);
            return;
        }
        if (!log_store_append(p, net_epoch())) {
            ucCode = REJECT_STORE_ERROR;
            prvSend(COMM_REJECT, pxFrame->sequence, &ucCode, 1);
            return;
        }
        log_store_mark_seen(ulDevice, ullEvent);
        prvPrintLog(p);
        if (p[21] == 14U) {
            ucNeedUsers = 1;                    /* log KHOI DONG: STM32 vua reset, bang ten da mat */
        }
    }

    Comm_PutLe(ucAck, ulDevice, 4);
    Comm_PutLe(ucAck + 4, ullEvent, 8);
    prvSend(COMM_ACK_STORED, pxFrame->sequence, ucAck, 12);
}

/* In log ra Serial Monitor de kiem tra (bang ma ly do thong nhat 24/11) */
static void prvPrintLog(const uint8_t *raw)
{
    static const char *pcReason[] = {
        "Mo cua", "Sai van tay", "Sai PIN", "Sai van tay 3 lan -> nhap PIN",
        "KHOA 5 PHUT (nghi dot nhap)", "Sai PIN admin 3 lan -> dem lai",
        "Bat nguon khi dang khoa", "Admin mo khoa", "Het 5 phut, tu mo khoa",
        "Sai PIN admin", "Them nguoi", "Xoa nguoi", "Lam lai van tay", "Doi PIN", "Khoi dong"
    };
    static const char *pcMethod[] = { "?", "Van tay", "PIN", "He thong" };
    uint16_t usUser   = (uint16_t)Comm_GetLe(raw + 16, 2);
    uint32_t ulEpoch  = (uint32_t)Comm_GetLe(raw + 12, 4);
    uint8_t  ucReason = raw[21];
    char     cUser[8];

    if (usUser == 0xFFFFU) {
        strcpy(cUser, "--");                    /* khong xac dinh duoc nguoi */
    } else {
        snprintf(cUser, sizeof(cUser), "%u", usUser);
    }
    Serial.printf("[LOG] su kien %lu/%lu | ID %s | %s | %s | %s | gio %lu (%s)\n",
                  (unsigned long)Comm_GetLe(raw + 8, 4), (unsigned long)Comm_GetLe(raw + 4, 4),
                  cUser,
                  pcMethod[raw[18] & 3U],
                  (raw[19] == 1U) ? "DUNG" : ((raw[19] == 2U) ? "CANH BAO" : "SAI"),
                  (ucReason < 15U) ? pcReason[ucReason] : "?",
                  (unsigned long)ulEpoch, raw[22] ? "STM32 co gio" : "chua co gio");
}
