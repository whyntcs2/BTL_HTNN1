/* Nap firmware STM32F103 qua bootloader ROM (ST AN3155 "USART protocol used in the STM32 bootloader").
 *
 * Trinh tu:  BOOT0 = 1, reset -> gui 0x7F (bootloader tu do baud) -> ACK 0x79
 *            Get (0x00): lay danh sach lenh, chon Erase 0x43 hoac Extended Erase 0x44
 *            Get ID (0x02): kiem tra chip
 *            Erase cac trang 0..N-1 (chi trang firmware moi chiem, giu trang 60-63)
 *            Write Memory (0x31) tung khoi 256 byte
 *            Read Memory (0x11) doc lai so sanh
 *            BOOT0 = 0, reset -> STM32 chay firmware moi
 * Bootloader dung 8E1 (chan le Even), khac 8N1 cua giao thuc task Comm.
 * Chay trong task rieng (ota.cpp tao), nen cho bang vTaskDelay de nhuong CPU.
 * File .cpp vi Serial1 cua Arduino la C++; code viet theo kieu C. */
#include <Arduino.h>
#include "stm32_boot.h"
#include "config.h"

#define BL_ACK          0x79U
#define BL_NACK         0x1FU
#define BL_SYNC         0x7FU
#define BL_CMD_GET      0x00U
#define BL_CMD_GET_ID   0x02U
#define BL_CMD_READ     0x11U
#define BL_CMD_WRITE    0x31U
#define BL_CMD_ERASE    0x43U
#define BL_CMD_EXT_ERASE 0x44U

#define FLASH_BASE_ADDR 0x08000000UL
#define PAGE_SIZE       1024UL
#define BLOCK           256U            /* toi da 256 byte moi lenh Write / Read */

static uint16_t usPid = 0;

static void prvResetPulse(void);
static int  prvRead(uint32_t ulTimeoutMs);
static void prvFlushRx(void);
static int  prvWaitAck(uint32_t ulTimeoutMs);
static int  prvCommand(uint8_t ucCmd);
static int  prvSendAddress(uint32_t ulAddr);
static int  prvSync(void);
static int  prvGet(uint8_t *pucEraseCmd);
static int  prvGetId(void);
static int  prvErase(uint8_t ucEraseCmd, uint16_t usPages);
static int  prvWriteBlock(uint32_t ulAddr, const uint8_t *pucData, uint16_t usLen);
static int  prvReadBlock(uint32_t ulAddr, uint8_t *pucOut, uint16_t usLen);
static void prvUart(uint32_t ulConfig);

void boot_init(void)
{
    pinMode(STM_BOOT0_PIN, OUTPUT);
    digitalWrite(STM_BOOT0_PIN, LOW);           /* chay app (dien tro 10k cung giu muc 0) */
    pinMode(STM_NRST_PIN, OUTPUT_OPEN_DRAIN);
    digitalWrite(STM_NRST_PIN, HIGH);           /* open-drain muc 1 = tha, Blue Pill tu keo len */
}

void boot_run_app(void)
{
    digitalWrite(STM_BOOT0_PIN, LOW);
    prvResetPulse();
}

uint16_t boot_last_pid(void)
{
    return usPid;
}

const char *boot_error_text(int iErr)
{
    switch (iErr) {
    case BOOT_OK:          return "Nap xong";
    case BOOT_ERR_SYNC:    return "Khong vao duoc bootloader STM32 (kiem tra day GPIO7-BOOT0, GPIO10-R, da rut jumper BOOT0, jumper BOOT1 o 0)";
    case BOOT_ERR_ID:      return "Chip khong phai STM32F103 trang 1KB";
    case BOOT_ERR_CMD:     return "Bootloader khong co lenh xoa";
    case BOOT_ERR_ERASE:   return "Loi xoa flash";
    case BOOT_ERR_WRITE:   return "Loi ghi flash";
    case BOOT_ERR_VERIFY:  return "Doc lai khong khop du lieu da ghi";
    case BOOT_ERR_SIZE:    return "Kich thuoc firmware khong hop le";
    default:               return "Loi khong ro";
    }
}

int boot_flash(const uint8_t *pucData, uint32_t ulLen, BootProgress pfProgress)
{
    static uint8_t ucCheck[BLOCK];
    uint8_t  ucEraseCmd = 0;
    uint16_t usPages;
    uint16_t usChunk;
    uint32_t ulOff;
    int      iErr = BOOT_OK;

    if (ulLen == 0U || ulLen > STM_APP_BYTES || (ulLen & 3U) != 0U) {
        return BOOT_ERR_SIZE;                   /* Write Memory can do dai boi 4 */
    }
    usPages = (uint16_t)((ulLen + PAGE_SIZE - 1UL) / PAGE_SIZE);

    pfProgress(2, "VAO BOOTLOADER");
    /* Sua 24/11: STM32 vua nhan OTA dang ve man "DANG CAP NHAT FW" len OLED (I2C, ~100ms).
     * Reset ngay luc do thi SSD1306 giu SDA -> bus I2C ket -> OLED tat sau khi nap.
     * Cho 600ms cho ve xong roi moi reset (link da ngung gui STATUS tu luc prvStartFlash). */
    vTaskDelay(600 / portTICK_PERIOD_MS);
    prvUart(SERIAL_8E1);
    digitalWrite(STM_BOOT0_PIN, HIGH);
    prvResetPulse();
    vTaskDelay(100 / portTICK_PERIOD_MS);       /* cho bootloader ROM khoi dong */

    if (!prvSync()) {
        iErr = BOOT_ERR_SYNC;
    } else if (!prvGet(&ucEraseCmd)) {
        iErr = BOOT_ERR_CMD;
    } else if (!prvGetId()) {
        iErr = BOOT_ERR_ID;
    }

    if (iErr == BOOT_OK) {
        pfProgress(8, "XOA FLASH");
        if (!prvErase(ucEraseCmd, usPages)) {
            iErr = BOOT_ERR_ERASE;
        }
    }

    /* Ghi: 10% -> 60% */
    for (ulOff = 0; iErr == BOOT_OK && ulOff < ulLen; ulOff += usChunk) {
        usChunk = (uint16_t)(((ulLen - ulOff) > BLOCK) ? BLOCK : (ulLen - ulOff));
        if (!prvWriteBlock(FLASH_BASE_ADDR + ulOff, pucData + ulOff, usChunk)) {
            iErr = BOOT_ERR_WRITE;
        } else {
            pfProgress((uint8_t)(10UL + (ulOff + usChunk) * 50UL / ulLen), "GHI FLASH");
        }
    }

    /* Doc lai kiem tra: 60% -> 98% */
    for (ulOff = 0; iErr == BOOT_OK && ulOff < ulLen; ulOff += usChunk) {
        usChunk = (uint16_t)(((ulLen - ulOff) > BLOCK) ? BLOCK : (ulLen - ulOff));
        if (!prvReadBlock(FLASH_BASE_ADDR + ulOff, ucCheck, usChunk) ||
            memcmp(ucCheck, pucData + ulOff, usChunk) != 0) {
            iErr = BOOT_ERR_VERIFY;
        } else {
            pfProgress((uint8_t)(60UL + (ulOff + usChunk) * 38UL / ulLen), "KIEM TRA");
        }
    }

    /* Thanh cong hay loi deu tra BOOT0 = 0 va reset (loi truoc buoc xoa: app cu van chay) */
    boot_run_app();
    prvUart(SERIAL_8N1);
    if (iErr == BOOT_OK) {
        pfProgress(100, "XONG");
    }
    return iErr;
}

/* ================== Tang thap ================== */

static void prvUart(uint32_t ulConfig)
{
    Serial1.end();
    Serial1.setRxBufferSize(1024);
    Serial1.begin(STM_BAUD, ulConfig, STM_UART_RX_PIN, STM_UART_TX_PIN);
}

static void prvResetPulse(void)
{
    digitalWrite(STM_NRST_PIN, LOW);
    vTaskDelay(20 / portTICK_PERIOD_MS);
    digitalWrite(STM_NRST_PIN, HIGH);           /* tha (open-drain) */
}

static int prvRead(uint32_t ulTimeoutMs)
{
    uint32_t ulStart = millis();

    while (millis() - ulStart < ulTimeoutMs) {
        if (Serial1.available() > 0) {
            return Serial1.read();
        }
        vTaskDelay(1);
    }
    return -1;
}

static void prvFlushRx(void)
{
    while (Serial1.available() > 0) {
        (void)Serial1.read();
    }
}

static int prvWaitAck(uint32_t ulTimeoutMs)
{
    return (prvRead(ulTimeoutMs) == (int)BL_ACK) ? 1 : 0;
}

/* Lenh = 1 byte + byte bu (XOR 0xFF) */
static int prvCommand(uint8_t ucCmd)
{
    uint8_t b[2];

    b[0] = ucCmd;
    b[1] = (uint8_t)(ucCmd ^ 0xFFU);
    prvFlushRx();
    Serial1.write(b, 2);
    return prvWaitAck(1000);
}

/* Dia chi 4 byte big-endian + checksum XOR */
static int prvSendAddress(uint32_t ulAddr)
{
    uint8_t b[5];

    b[0] = (uint8_t)(ulAddr >> 24);
    b[1] = (uint8_t)(ulAddr >> 16);
    b[2] = (uint8_t)(ulAddr >> 8);
    b[3] = (uint8_t)ulAddr;
    b[4] = (uint8_t)(b[0] ^ b[1] ^ b[2] ^ b[3]);
    Serial1.write(b, 5);
    return prvWaitAck(1000);
}

static int prvSync(void)
{
    uint8_t ucTry;
    int     r;

    for (ucTry = 0; ucTry < 5U; ucTry++) {
        prvFlushRx();
        Serial1.write((uint8_t)BL_SYNC);
        r = prvRead(300);
        if (r == (int)BL_ACK || r == (int)BL_NACK) {    /* NACK: bootloader da dong bo tu truoc */
            return 1;
        }
    }
    return 0;
}

static int prvGet(uint8_t *pucEraseCmd)
{
    int n, i, c;

    if (!prvCommand(BL_CMD_GET)) {
        return 0;
    }
    n = prvRead(500);                           /* so byte tiep theo - 1 */
    if (n < 0) {
        return 0;
    }
    (void)prvRead(500);                         /* phien ban bootloader */
    for (i = 0; i < n; i++) {
        c = prvRead(500);
        if (c == (int)BL_CMD_ERASE || c == (int)BL_CMD_EXT_ERASE) {
            *pucEraseCmd = (uint8_t)c;
        }
    }
    return (prvWaitAck(500) && *pucEraseCmd != 0U) ? 1 : 0;
}

static int prvGetId(void)
{
    int n, hi, lo;

    if (!prvCommand(BL_CMD_GET_ID)) {
        return 0;
    }
    n  = prvRead(500);                          /* = 1 */
    hi = prvRead(500);
    lo = prvRead(500);
    if (n != 1 || hi < 0 || lo < 0 || !prvWaitAck(500)) {
        return 0;
    }
    usPid = (uint16_t)((hi << 8) | lo);
    /* 0x410 F103 medium density (C8T6), 0x412 low density: deu trang 1KB */
    return (usPid == 0x410U || usPid == 0x412U) ? 1 : 0;
}

/* Xoa trang 0..usPages-1. 0x43: so trang 1 byte; 0x44: 2 byte big-endian */
static int prvErase(uint8_t ucEraseCmd, uint16_t usPages)
{
    uint8_t  ucCks = 0;
    uint8_t  b[2];
    uint16_t i;

    if (!prvCommand(ucEraseCmd)) {
        return 0;
    }
    if (ucEraseCmd == BL_CMD_ERASE) {
        b[0] = (uint8_t)(usPages - 1U);
        Serial1.write(b, 1);
        ucCks = b[0];
        for (i = 0; i < usPages; i++) {
            b[0] = (uint8_t)i;
            Serial1.write(b, 1);
            ucCks ^= b[0];
        }
        Serial1.write(&ucCks, 1);
    } else {
        b[0] = (uint8_t)((usPages - 1U) >> 8);
        b[1] = (uint8_t)(usPages - 1U);
        Serial1.write(b, 2);
        ucCks = (uint8_t)(b[0] ^ b[1]);
        for (i = 0; i < usPages; i++) {
            b[0] = (uint8_t)(i >> 8);
            b[1] = (uint8_t)i;
            Serial1.write(b, 2);
            ucCks ^= (uint8_t)(b[0] ^ b[1]);
        }
        Serial1.write(&ucCks, 1);
    }
    return prvWaitAck(10000);                   /* ~20-40ms moi trang */
}

static int prvWriteBlock(uint32_t ulAddr, const uint8_t *pucData, uint16_t usLen)
{
    uint8_t  ucN = (uint8_t)(usLen - 1U);
    uint8_t  ucCks = ucN;
    uint16_t i;

    if (!prvCommand(BL_CMD_WRITE) || !prvSendAddress(ulAddr)) {
        return 0;
    }
    for (i = 0; i < usLen; i++) {
        ucCks ^= pucData[i];
    }
    Serial1.write(&ucN, 1);
    Serial1.write(pucData, usLen);
    Serial1.write(&ucCks, 1);
    return prvWaitAck(2000);
}

static int prvReadBlock(uint32_t ulAddr, uint8_t *pucOut, uint16_t usLen)
{
    uint8_t  b[2];
    uint16_t i;
    int      c;

    if (!prvCommand(BL_CMD_READ) || !prvSendAddress(ulAddr)) {
        return 0;
    }
    b[0] = (uint8_t)(usLen - 1U);
    b[1] = (uint8_t)(b[0] ^ 0xFFU);
    Serial1.write(b, 2);
    if (!prvWaitAck(1000)) {
        return 0;
    }
    for (i = 0; i < usLen; i++) {
        c = prvRead(500);
        if (c < 0) {
            return 0;
        }
        pucOut[i] = (uint8_t)c;
    }
    return 1;
}