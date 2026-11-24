/* Nap firmware STM32 qua WiFi (buoc 9c-3).
 *
 *   Trinh duyet --(POST /upload, file .bin)--> ESP32 (giu trong RAM, toi da 60KB)
 *   kiem tra bang vector (SP trong RAM, Reset_Handler trong flash) -> dem them 0xFF cho du boi 4
 *   STM32 con song: gui OTA_PREPARE -> STM32 chi dong y khi dang o man quet van tay
 *                   -> dong cua, PAUSE van tay, gui het log -> OTA_READY
 *   -> task "stm32boot" nap qua bootloader ROM (Serial1 8E1) -> reset chay firmware moi
 *   Trang web hoi GET /status moi 0.5s de hien tien do.
 *
 * WebServer (dong bo) chay trong loop(); qua trinh nap chay trong task rieng nen trang web
 * van tra loi duoc trong luc nap. File .cpp vi WebServer cua Arduino la C++; viet kieu C. */
#include <Arduino.h>
#include <WebServer.h>
#include <stdlib.h>
#include "ota.h"
#include "config.h"
#include "stm32_boot.h"
#include "stm32_link.h"
#include "comm_protocol.h"
#include "cloud.h"

#define OTA_TASK_STACK      4096U       /* byte */
#define OTA_SCREEN_HOLD_MS  15000UL     /* OLED giu man ket qua 15s */
#define MSG_MAX             160U

static WebServer xServer(80);

static volatile uint8_t ucState = OTA_ST_IDLE;
static volatile uint8_t ucPercent = 0;
static char     cStage[24] = "";                /* chu ngan cho OLED */
static char     cMsg[MSG_MAX] = "San sang";     /* cau day du cho trang web */

static uint8_t *pucImage = NULL;                /* firmware trong RAM (malloc luc tai len) */
static uint32_t ulImageLen = 0;
static uint8_t  ucUploadErr = 0;
static uint8_t  ucUploadAuth = 0;
static uint16_t usOtaSeq = 0;
static uint32_t ulStateSince = 0;
static volatile int iFlashResult = BOOT_OK;
static volatile uint8_t ucFlashEnded = 0;

static void prvPage(void);
static void prvStatus(void);
static void prvUploadDone(void);
static void prvUsersNow(void);
static void prvUploadChunk(void);
static int  prvAuth(void);
static int  prvCheckImage(char *pcWhy, size_t uxCap);
static void prvStartFlash(void);
static void vTaskFlash(void *pvParameters);
static void prvProgress(uint8_t ucPct, const char *pcStage);
static void prvSetState(uint8_t ucNew, const char *pcStage, const char *pcMsg);
static void prvFreeImage(void);

/* Trang web: 1 file HTML nho, JS gui file va hoi /status */
static const char cPageHtml[] =
"<!DOCTYPE html><html lang='vi'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Nạp firmware khóa cửa</title><style>"
"body{font-family:sans-serif;max-width:520px;margin:30px auto;padding:0 16px;color:#222}"
"h2{font-weight:500}.box{border:1px solid #ccc;border-radius:8px;padding:16px;margin:12px 0}"
"button{padding:8px 18px;font-size:15px}progress{width:100%;height:18px}"
"#msg{margin-top:10px;white-space:pre-wrap}.ok{color:#2a7a2a}.err{color:#b22}"
"</style></head><body><h2>Nạp firmware STM32 qua WiFi</h2>"
"<div class='box'>Firmware STM32 đang chạy: <b id='ver'>?</b><br>"
"Chọn file <b>.bin</b> build từ Keil (tối đa 60 KB). Khóa phải đang ở màn quét vân tay.</div>"
"<div class='box'>Bảng tên: <b id='un'>?</b><br><button onclick='usr()'>Cập nhật tên ngay</button>"
"<div id='um'></div></div>"
"<div class='box'><input type='file' id='f' accept='.bin'> <button onclick='up()'>Nạp</button>"
"<p><progress id='p' max='100' value='0'></progress></p><div id='msg'></div></div>"
"<script>"
"var busy=0;"
"function show(t,c){var m=document.getElementById('msg');m.textContent=t;m.className=c||'';}"
"function up(){var f=document.getElementById('f').files[0];"
"if(!f){show('Chưa chọn file','err');return;}"
"if(f.size>61440){show('File quá 60 KB','err');return;}"
"var x=new XMLHttpRequest(),d=new FormData();d.append('fw',f,f.name);"
"x.upload.onprogress=function(e){if(e.lengthComputable){document.getElementById('p').value=e.loaded*20/e.total;"
"show('Đang tải lên '+Math.round(e.loaded*100/e.total)+'%');}};"
"x.onload=function(){if(x.status==200){busy=1;poll();}else{show(x.responseText||('Lỗi '+x.status),'err');}};"
"x.onerror=function(){show('Mất kết nối tới ESP32','err');};"
"x.open('POST','/upload');x.send(d);}"
"function info(s){document.getElementById('ver').textContent=s.ver?('v'+s.ver):'chưa thấy STM32';"
"document.getElementById('un').textContent=(s.ua<0)?'chưa lấy được':(s.un+' người, lấy từ Sheet '+s.ua+' giây trước');}"
"function usr(){document.getElementById('um').textContent='Đang lấy bảng tên từ Google Sheet...';"
"fetch('/users',{method:'POST'}).then(function(r){if(r.status!=200){document.getElementById('um').textContent='Lỗi '+r.status;return;}"
"setTimeout(function(){fetch('/status').then(function(r){return r.json();}).then(function(s){info(s);"
"document.getElementById('um').textContent=(s.ua>=0&&s.ua<8)?'Đã cập nhật, đã gửi xuống STM32':'Chưa lấy được (kiểm tra WiFi / Sheet)';});},5000);});}"
"function poll(){fetch('/status').then(function(r){return r.json();}).then(function(s){"
"document.getElementById('ver').textContent=s.ver?('v'+s.ver):'chưa thấy STM32';"
"if(s.st==1||s.st==2){document.getElementById('p').value=20+s.pct*0.8;show(s.msg);}"
"else if(s.st==3){document.getElementById('p').value=100;show(s.msg,'ok');busy=0;}"
"else if(s.st==4){show(s.msg,'err');busy=0;}"
"if(busy)setTimeout(poll,500);}).catch(function(){if(busy)setTimeout(poll,1000);});}"
"busy=0;fetch('/status').then(function(r){return r.json();}).then(function(s){info(s);"
"if(s.st==1||s.st==2){busy=1;poll();}});"
"</script></body></html>";

void ota_init(void)
{
    xServer.on("/", HTTP_GET, prvPage);
    xServer.on("/status", HTTP_GET, prvStatus);
    xServer.on("/upload", HTTP_POST, prvUploadDone, prvUploadChunk);
    xServer.on("/users", HTTP_POST, prvUsersNow);
    xServer.begin();
    Serial.println("[OTA] Web server cong 80 da bat");
}

uint8_t ota_uart_busy(void)
{
    return (ucState == OTA_ST_FLASHING) ? 1U : 0U;
}

int ota_screen(char *pcStageOut, size_t uxCap, uint8_t *pucPct)
{
    if (ucState == OTA_ST_IDLE) {
        return 0;
    }
    if ((ucState == OTA_ST_DONE || ucState == OTA_ST_FAIL) &&
        millis() - ulStateSince >= OTA_SCREEN_HOLD_MS) {
        return 0;
    }
    strncpy(pcStageOut, cStage, uxCap - 1U);
    pcStageOut[uxCap - 1U] = '\0';
    *pucPct = ucPercent;
    return 1;
}

void ota_poll(void)
{
    uint8_t  ucType;
    uint16_t usSeq;

    xServer.handleClient();

    switch (ucState) {
    case OTA_ST_HANDSHAKE:
        if (link_take_ota_reply(&ucType, &usSeq) && usSeq == usOtaSeq) {
            if (ucType == COMM_OTA_READY) {
                prvStartFlash();
            } else {
                prvFreeImage();
                prvSetState(OTA_ST_FAIL, "STM32 TU CHOI",
                            "STM32 tu choi: chi nap duoc khi khoa dang o man quet van tay "
                            "(khong o menu, khong dang khoa, khong dang nhap PIN)");
            }
        } else if (millis() - ulStateSince >= OTA_READY_TIMEOUT_MS) {
            link_send_ota_abort(usOtaSeq);
            prvFreeImage();
            prvSetState(OTA_ST_FAIL, "HET THOI GIAN", "STM32 khong tra loi OTA_READY");
        }
        break;

    case OTA_ST_FLASHING:
        if (ucFlashEnded) {
            ucFlashEnded = 0;
            prvFreeImage();
            link_resume();                      /* Serial1 ve 8N1, cho HELLO cua firmware moi */
            if (iFlashResult == BOOT_OK) {
                prvSetState(OTA_ST_DONE, "XONG", "Nap xong, STM32 da khoi dong lai firmware moi");
            } else {
                snprintf(cMsg, sizeof(cMsg), "%s", boot_error_text(iFlashResult));
                prvSetState(OTA_ST_FAIL, "LOI NAP", cMsg);
            }
            Serial.printf("[OTA] %s\n", cMsg);
        }
        break;

    default:
        break;
    }
}

/* ================== Web ================== */

static int prvAuth(void)
{
    if (OTA_PASSWORD[0] == '\0') {
        return 1;
    }
    return xServer.authenticate(OTA_USER, OTA_PASSWORD) ? 1 : 0;
}

static void prvPage(void)
{
    if (!prvAuth()) {
        xServer.requestAuthentication();
        return;
    }
    xServer.send(200, "text/html; charset=utf-8", cPageHtml);
}

static void prvStatus(void)
{
    static char cJson[MSG_MAX + 96];
    char    cSafe[MSG_MAX];
    size_t  i;
    uint16_t usUsers = 0;
    uint32_t ulAge = 0;
    long    lAge;

    /* cMsg khong co dau ngoac kep / gach cheo, nhung van loc cho chac */
    for (i = 0; i < sizeof(cSafe) - 1U && cMsg[i] != '\0'; i++) {
        cSafe[i] = (cMsg[i] == '"' || cMsg[i] == '\\') ? '\'' : cMsg[i];
    }
    cSafe[i] = '\0';
    lAge = cloud_users_info(&usUsers, &ulAge) ? (long)ulAge : -1L;
    snprintf(cJson, sizeof(cJson), "{\"st\":%u,\"pct\":%u,\"ver\":%lu,\"un\":%u,\"ua\":%ld,\"msg\":\"%s\"}",
             (unsigned)ucState, (unsigned)ucPercent, (unsigned long)link_stm32_version(),
             (unsigned)usUsers, lAge, cSafe);
    xServer.send(200, "application/json", cJson);
}

/* Nut "Cap nhat ten ngay": task cloud lay bang ten o vong ke tiep (~0.2s) roi loop() gui xuong STM32 */
static void prvUsersNow(void)
{
    if (!prvAuth()) {
        xServer.requestAuthentication();
        return;
    }
    cloud_request_users();
    xServer.send(200, "text/plain; charset=utf-8", "OK");
}

/* Goi nhieu lan trong luc nhan file (START / WRITE / END), truoc prvUploadDone */
static void prvUploadChunk(void)
{
    HTTPUpload &xUp = xServer.upload();

    if (xUp.status == UPLOAD_FILE_START) {
        ucUploadErr  = 0;
        ucUploadAuth = (uint8_t)prvAuth();
        if (!ucUploadAuth || ucState == OTA_ST_HANDSHAKE || ucState == OTA_ST_FLASHING) {
            ucUploadErr = 1;
            return;
        }
        prvFreeImage();
        pucImage = (uint8_t *)malloc(STM_APP_BYTES + 4UL);
        ulImageLen = 0;
        if (pucImage == NULL) {
            ucUploadErr = 2;
        }
        Serial.printf("[OTA] Nhan file %s\n", xUp.filename.c_str());
    } else if (xUp.status == UPLOAD_FILE_WRITE) {
        if (ucUploadErr == 0U) {
            if (ulImageLen + xUp.currentSize > STM_APP_BYTES) {
                ucUploadErr = 3;                /* qua 60KB */
            } else {
                memcpy(pucImage + ulImageLen, xUp.buf, xUp.currentSize);
                ulImageLen += xUp.currentSize;
            }
        }
    } else if (xUp.status == UPLOAD_FILE_ABORTED) {
        ucUploadErr = 4;
    }
}

static void prvUploadDone(void)
{
    char cWhy[96];
    uint32_t ulCrc;

    if (!ucUploadAuth && !prvAuth()) {
        xServer.requestAuthentication();
        return;
    }
    if (ucUploadErr != 0U) {
        if (ucUploadErr != 1U) {
            prvFreeImage();                     /* 1: dang nap do -> khong dung vao anh dang nap */
        }
        xServer.send(400, "text/plain; charset=utf-8",
                     (ucUploadErr == 1U) ? "Đang nạp, chờ xong đã" :
                     (ucUploadErr == 2U) ? "ESP32 hết RAM" :
                     (ucUploadErr == 3U) ? "File quá 60 KB" : "Tải lên bị ngắt");
        return;
    }
    if (!prvCheckImage(cWhy, sizeof(cWhy))) {
        prvFreeImage();
        xServer.send(400, "text/plain; charset=utf-8", cWhy);
        return;
    }

    /* Dem 0xFF cho du boi 4 (lenh Write Memory cua bootloader) */
    while ((ulImageLen & 3U) != 0U) {
        pucImage[ulImageLen++] = 0xFFU;
    }
    ulCrc = ~Comm_Crc32Update(0xFFFFFFFFUL, pucImage, ulImageLen);
    Serial.printf("[OTA] File hop le: %lu byte, CRC32 %08lx\n", (unsigned long)ulImageLen, (unsigned long)ulCrc);

    if (link_stm32_alive()) {
        usOtaSeq = link_send_ota_prepare(ulImageLen, ulCrc);
        prvSetState(OTA_ST_HANDSHAKE, "HOI STM32", "Dang hoi STM32 (dong cua, dung quet van tay)...");
    } else {
        Serial.println("[OTA] Khong thay STM32 -> nap thang (nap cuu ho)");
        prvStartFlash();
    }
    xServer.send(200, "text/plain; charset=utf-8", "OK");
}

/* Bang vector o dau file: word 0 = dinh stack (trong RAM 20KB), word 1 = Reset_Handler
 * (trong vung app 60KB, bit 0 = 1 vi Thumb). Chan nham file .hex, file ESP32, file rac. */
static int prvCheckImage(char *pcWhy, size_t uxCap)
{
    uint32_t ulSp, ulReset;

    if (ulImageLen < 8U) {
        snprintf(pcWhy, uxCap, "File quá nhỏ");
        return 0;
    }
    if (pucImage[0] == ':') {
        snprintf(pcWhy, uxCap, "Đây là file .hex, cần file .bin");
        return 0;
    }
    ulSp    = (uint32_t)Comm_GetLe(pucImage, 4);
    ulReset = (uint32_t)Comm_GetLe(pucImage + 4, 4);
    if (ulSp < 0x20000000UL || ulSp > 0x20005000UL ||
        ulReset < 0x08000000UL || ulReset >= 0x08000000UL + STM_APP_BYTES || (ulReset & 1UL) == 0UL) {
        snprintf(pcWhy, uxCap, "Không phải firmware STM32F103 (SP %08lx, Reset %08lx)",
                 (unsigned long)ulSp, (unsigned long)ulReset);
        return 0;
    }
    return 1;
}

/* ================== Nap ================== */

static void prvStartFlash(void)
{
    link_suspend();                             /* loop() thoi doc / gui tren Serial1 */
    ucFlashEnded = 0;
    prvSetState(OTA_ST_FLASHING, "VAO BOOTLOADER", "Dang nap...");
    if (xTaskCreate(vTaskFlash, "stm32boot", OTA_TASK_STACK, NULL, 2, NULL) != pdPASS) {
        link_resume();
        prvFreeImage();
        prvSetState(OTA_ST_FAIL, "LOI", "Khong tao duoc task nap");
    }
}

static void vTaskFlash(void *pvParameters)
{
    (void)pvParameters;
    iFlashResult = boot_flash(pucImage, ulImageLen, prvProgress);
    ucFlashEnded = 1;                           /* ota_poll() trong loop() xu ly tiep */
    vTaskDelete(NULL);
}

static void prvProgress(uint8_t ucPct, const char *pcStage)
{
    ucPercent = ucPct;
    strncpy(cStage, pcStage, sizeof(cStage) - 1U);
    cStage[sizeof(cStage) - 1U] = '\0';
    snprintf(cMsg, sizeof(cMsg), "%s %u%%", pcStage, (unsigned)ucPct);
}

static void prvSetState(uint8_t ucNew, const char *pcStage, const char *pcMsg)
{
    if (pcMsg != cMsg) {
        snprintf(cMsg, sizeof(cMsg), "%s", pcMsg);
    }
    strncpy(cStage, pcStage, sizeof(cStage) - 1U);
    cStage[sizeof(cStage) - 1U] = '\0';
    if (ucNew == OTA_ST_HANDSHAKE || ucNew == OTA_ST_FLASHING) {
        ucPercent = 0;
    }
    ulStateSince = millis();
    ucState = ucNew;
}

static void prvFreeImage(void)
{
    if (pucImage != NULL) {
        free(pucImage);
        pucImage = NULL;
    }
    ulImageLen = 0;
}