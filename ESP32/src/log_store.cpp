/* Hang doi log trong LittleFS.
 *   /q.bin  : cac ban ghi StoredLog noi tiep nhau (chi ghi them vao cuoi)
 *   /q.head : so ban ghi dau file da gui len Sheet (bo qua khi doc)
 * Khi da gui het: xoa ca 2 file de file khong lon mai.
 * Buoc 9c-2: task cloud (doc + xoa) va loop() (ghi them) cung dung -> moi ham cong khai
 * giu mutex xLogMutex trong luc dung file (slide p2: mutex bao ve tai nguyen chia se).
 * File .cpp vi LittleFS cua Arduino la C++; code viet theo kieu C (ham + bien static). */
#include <Arduino.h>
#include <LittleFS.h>
#include "log_store.h"
#include "config.h"

#define Q_FILE      "/q.bin"
#define HEAD_FILE   "/q.head"
#define SEEN_SIZE   32U         /* nho 32 log moi nhan de chong trung */

static SemaphoreHandle_t xLogMutex = NULL;

static uint32_t ulTotal = 0;    /* so ban ghi trong q.bin */
static uint32_t ulHead  = 0;    /* so ban ghi dau file da gui xong */

static uint32_t ulSeenDev[SEEN_SIZE];
static uint64_t ullSeenEvt[SEEN_SIZE];
static uint8_t  ucSeenPos = 0;
static uint8_t  ucSeenUsed = 0;

#define LOCK()      xSemaphoreTake(xLogMutex, portMAX_DELAY)
#define UNLOCK()    xSemaphoreGive(xLogMutex)

static uint16_t prvCount(void)
{
    return (uint16_t)(ulTotal - ulHead);
}

static void prvSaveHead(void)
{
    File f = LittleFS.open(HEAD_FILE, "w");
    if (f) {
        f.write((const uint8_t *)&ulHead, sizeof(ulHead));
        f.close();
    }
}

int log_store_init(void)
{
    File f;

    xLogMutex = xSemaphoreCreateMutex();
    if (xLogMutex == NULL) {
        Serial.println("[LOG] Khong tao duoc mutex");
        return 0;
    }
    if (!LittleFS.begin(true)) {                /* true: lan dau tu format */
        Serial.println("[LOG] Khong mo duoc LittleFS");
        return 0;
    }

    ulTotal = 0;
    ulHead  = 0;
    f = LittleFS.open(Q_FILE, "r");
    if (f) {
        ulTotal = f.size() / sizeof(StoredLog);
        f.close();
    }
    f = LittleFS.open(HEAD_FILE, "r");
    if (f) {
        if (f.read((uint8_t *)&ulHead, sizeof(ulHead)) != sizeof(ulHead) || ulHead > ulTotal) {
            ulHead = 0;
        }
        f.close();
    }
    Serial.printf("[LOG] LittleFS OK, %u log cho gui\n", (unsigned)prvCount());
    return 1;
}

uint16_t log_store_count(void)
{
    uint16_t n;

    LOCK();
    n = prvCount();
    UNLOCK();
    return n;
}

int log_store_append(const uint8_t *raw, uint32_t rx_epoch)
{
    StoredLog rec;
    File      f;
    size_t    n;

    memcpy(rec.raw, raw, LOG_RAW_BYTES);
    rec.rx_epoch = rx_epoch;
    rec.reserved = 0;

    LOCK();
    if (prvCount() >= LOG_STORE_MAX) {
        UNLOCK();
        return 0;                               /* day: STM32 se giu log va gui lai sau */
    }
    f = LittleFS.open(Q_FILE, "a");
    if (!f) {
        UNLOCK();
        return 0;
    }
    n = f.write((const uint8_t *)&rec, sizeof(rec));
    f.close();
    if (n == sizeof(rec)) {
        ulTotal++;
    }
    UNLOCK();
    return (n == sizeof(rec)) ? 1 : 0;
}

uint16_t log_store_peek(StoredLog *out, uint16_t max)
{
    File     f;
    uint16_t n = 0;

    LOCK();
    if (max > prvCount()) {
        max = prvCount();
    }
    if (max > 0U) {
        f = LittleFS.open(Q_FILE, "r");
        if (f) {
            if (f.seek(ulHead * sizeof(StoredLog))) {
                while (n < max &&
                       f.read((uint8_t *)&out[n], sizeof(StoredLog)) == sizeof(StoredLog)) {
                    n++;
                }
            }
            f.close();
        }
    }
    UNLOCK();
    return n;
}

void log_store_drop(uint16_t count)
{
    LOCK();
    if (count > prvCount()) {
        count = prvCount();
    }
    ulHead += count;
    if (ulHead >= ulTotal) {
        /* Gui het: xoa file cho gon */
        LittleFS.remove(Q_FILE);
        LittleFS.remove(HEAD_FILE);
        ulTotal = 0;
        ulHead  = 0;
    } else {
        prvSaveHead();
    }
    UNLOCK();
}

/* seen / mark_seen chi loop() dung (vong RAM), khong can mutex */
int log_store_seen(uint32_t device, uint64_t event)
{
    uint8_t i;

    for (i = 0; i < ucSeenUsed; i++) {
        if (ulSeenDev[i] == device && ullSeenEvt[i] == event) {
            return 1;
        }
    }
    return 0;
}

void log_store_mark_seen(uint32_t device, uint64_t event)
{
    ulSeenDev[ucSeenPos]  = device;
    ullSeenEvt[ucSeenPos] = event;
    ucSeenPos = (uint8_t)((ucSeenPos + 1U) % SEEN_SIZE);
    if (ucSeenUsed < SEEN_SIZE) {
        ucSeenUsed++;
    }
}