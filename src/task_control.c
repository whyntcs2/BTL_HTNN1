#include "task_control.h"
#include "task.h"
#include "task_as608.h"
#include "task_keypad.h"
#include "task_servo.h"
#include "task_buzzer.h"
#include "task_oled.h"

/* ================== Cau hinh ================== */
#define CTRL_TASK_STACK         256     /* word */
#define CTRL_LOOP_WAIT_MS       50      /* cho su kien AS608 toi da bao lau roi quay lai doc phim */
#define CTRL_OLED_WAIT_MS       100     /* cho toi da khi queue OLED day */
#define CTRL_MSG_MS             3000    /* thong bao tam (MO CUA, SAI...) hien bao lau roi ve man hinh chinh */

#define CTRL_MAX_FINGER_FAIL    3       /* sai van tay 3 lan -> NHAP PIN */
#define CTRL_MAX_PIN_FAIL       3       /* sai PIN 3 lan -> khoa 5 phut */
#define CTRL_PIN_LEN            4       /* PIN 4 chu so */
#define CTRL_PIN_IDLE_MS        30000   /* NHAP PIN: 30s khong bam phim -> ve quet van tay */
#define CTRL_RESUME_MS          30000   /* mo cua bang PIN: 30s sau van tay hoat dong lai */

#define CTRL_KEY_CLEAR          'C'     /* xoa PIN dang go */

/* Khoa 5 phut (buoc 8b) */
#define CTRL_LOCK_MS            60000UL /* TEST 1 phut. Test xong doi thanh 300000UL (5 phut) */
#define CTRL_MAX_ADMIN_FAIL     3       /* sai PIN admin 3 lan -> dem lai thoi gian khoa tu dau */
#define CTRL_ADMIN_PIN          "*AB#"  /* PIN admin: chi PIN nay mo khoa som duoc */

/* Bang PIN tam cho buoc 8a (buoc 8c chuyen vao flash trang 62).
 * Moi PIN phai KHAC NHAU vi PIN dung de biet ai vao nha. */
typedef struct {
    uint16_t usId;                      /* ID van tay cua nguoi do */
    char     cPin[CTRL_PIN_LEN + 1];
} Ctrl_PinEntry;

static const Ctrl_PinEntry xPinTable[] = {
    { 0, "1111" },
    { 1, "2222" },
    { 2, "3333" }
};
#define CTRL_PIN_COUNT  (sizeof(xPinTable) / sizeof(xPinTable[0]))

/* Trang thai cua task */
typedef enum {
    CTRL_STATE_SCAN = 0,    /* mac dinh: quet van tay */
    CTRL_STATE_PIN,         /* van tay da PAUSE, nhap PIN tren numpad */
    CTRL_STATE_WAIT_RESUME, /* vua mo cua bang PIN, cho 30s roi RESUME van tay */
    CTRL_STATE_LOCKED       /* sai PIN 3 lan: khoa CTRL_LOCK_MS, chi nhan PIN admin */
} Ctrl_State;

/* ================== Bien noi bo (chi task dieu khien dung) ================== */
static Ctrl_State eState        = CTRL_STATE_SCAN;
static uint8_t    ucFingerFail  = 0;    /* so lan sai van tay lien tiep */
static uint8_t    ucPinFail     = 0;    /* so lan sai PIN lien tiep (giu khi het gio nhap PIN) */
static char       cPinBuf[CTRL_PIN_LEN + 1];
static uint8_t    ucPinLen      = 0;
static TickType_t xStateSince   = 0;    /* moc tinh 30s (phim cuoi o NHAP PIN, luc mo cua bang PIN) */
static BaseType_t xRedrawPending = pdFALSE;
static TickType_t xRedrawSince  = 0;    /* luc ve thong bao tam; CTRL_MSG_MS sau ve lai man hinh chinh */
static uint8_t    ucAdminFail   = 0;    /* so lan sai PIN admin trong lan khoa nay */
static TickType_t xLockSince    = 0;    /* luc bat dau (hoac dem lai) khoa */
static uint16_t   usLockShownSec = 0xFFFFU; /* so giay con lai dang hien tren OLED */

static void vTaskControl(void *pvParameters);
static void prvHandleKey(char cKey);
static void prvHandleEvent(const AS608_Event *pxEvent);
static void prvCheckTimers(void);
static void prvEnterPin(void);
static void prvBackToScan(void);
static void prvCheckPin(void);
static void prvHandleLockKey(char cKey);
static void prvEnterLock(void);
static void prvSendAlert(void);
static uint16_t prvLockSecondsLeft(void);
static void prvShowLock(void);
static void prvShowCountdown(uint16_t usSeconds);
static BaseType_t prvFindPin(const char *pcPin, uint16_t *pusId);
static void prvShowState(void);
static void prvShowTemp(void);
static void prvShowPinEntry(void);
static void prvLine(uint8_t ucLine, const char *pcText);
static void prvLineNumber(uint8_t ucLine, const char *pcPrefix, uint16_t usNumber, const char *pcSuffix);

/* ================== Ham public ================== */

BaseType_t xTaskControlCreate(UBaseType_t uxPriority)
{
    return xTaskCreate(vTaskControl, "CTRL", CTRL_TASK_STACK, NULL, uxPriority, NULL);
}

/* ================== Task ================== */

/* Moi vong (giong vTaskTest buoc 5-7 da chay on):
 * 1. Lay het phim dang cho (0 tick: khong cho).
 * 2. Kiem tra cac moc thoi gian (30s, ve lai man hinh).
 * 3. Cho su kien AS608 toi da 50ms (Blocked) -> task uu tien thap hon duoc chay. */
static void vTaskControl(void *pvParameters)
{
    AS608_Event xEvent;
    char        cKey;

    (void)pvParameters;

    /* Tieu de co dinh (dong 0, 1 khong bao gio bi ghi de) */
    xTaskOledPrint(0, 1, "KHOA CUA VAN TAY", CTRL_OLED_WAIT_MS / portTICK_RATE_MS);
    prvLine(1, "---------------------");
    prvShowState();

    for (;;)
    {
        while (xTaskKeypadGetKey(&cKey, 0) == pdPASS) {
            prvHandleKey(cKey);
        }

        prvCheckTimers();

        if (xTaskAS608GetEvent(&xEvent, CTRL_LOOP_WAIT_MS / portTICK_RATE_MS) == pdPASS) {
            prvHandleEvent(&xEvent);
        }
    }
}

/* ================== Phim ================== */

static void prvHandleKey(char cKey)
{
    if (eState == CTRL_STATE_LOCKED) {
        prvHandleLockKey(cKey);             /* dang khoa: chi nhan PIN admin, D bi khoa */
        return;
    }

    if (cKey == KEYPAD_KEY_MENU) {
        /* Buoc 8d moi lam menu */
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        prvLine(2, "MENU - CHUA LAM");
        prvLine(3, "");
        prvLine(4, "");
        prvShowTemp();
        return;
    }

    if (eState != CTRL_STATE_PIN) {
        return;                             /* QUET VAN TAY / cho RESUME: bo qua phim khac */
    }

    xStateSince = xTaskGetTickCount();      /* co bam phim: tinh lai 30s */

    if (cKey == CTRL_KEY_CLEAR) {
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        ucPinLen = 0;
        prvShowPinEntry();
    } else if (cKey >= '0' && cKey <= '9') {
        cPinBuf[ucPinLen++] = cKey;
        if (ucPinLen >= CTRL_PIN_LEN) {
            cPinBuf[CTRL_PIN_LEN] = '\0';
            prvCheckPin();                  /* phim thu 4: am dung / sai thay cho tieng bip */
        } else {
            xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
            prvShowPinEntry();
        }
    }
    /* '*', '#', 'A', 'B': khong dung trong PIN nguoi dung, bo qua */
}

/* Du 4 so: so voi bang PIN */
static void prvCheckPin(void)
{
    uint16_t usId;

    ucPinLen = 0;

    if (prvFindPin(cPinBuf, &usId) == pdTRUE) {
        /* PIN dung: mo cua, 30s sau van tay hoat dong lai */
        ucPinFail    = 0;
        ucFingerFail = 0;
        xTaskServoSendCommand(SERVO_CMD_OPEN, 0, portMAX_DELAY);
        xTaskBuzzerPlay(BUZZER_SOUND_GRANTED, 0);
        eState      = CTRL_STATE_WAIT_RESUME;
        xStateSince = xTaskGetTickCount();
        prvLine(2, "MO CUA BANG PIN");
        prvLineNumber(3, "ID ", usId, "");
        prvLine(4, "");
        prvLine(6, "");
        prvShowTemp();
        return;
    }

    ucPinFail++;

    if (ucPinFail >= CTRL_MAX_PIN_FAIL) {
        prvEnterLock();                     /* nguoi la do PIN: khoa */
        return;
    }

    xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
    prvShowPinEntry();
}

/* ================== Khoa 5 phut ================== */

/* Dang khoa: go 4 phim bat ky (tru C, D) roi so voi PIN admin */
static void prvHandleLockKey(char cKey)
{
    uint8_t i;

    if (cKey == KEYPAD_KEY_MENU) {
        return;                             /* menu bi khoa */
    }

    if (cKey == CTRL_KEY_CLEAR) {
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        ucPinLen = 0;
        prvShowLock();
        return;
    }

    cPinBuf[ucPinLen++] = cKey;
    if (ucPinLen < CTRL_PIN_LEN) {
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        prvShowLock();
        return;
    }

    /* Du 4 phim: so voi PIN admin */
    cPinBuf[CTRL_PIN_LEN] = '\0';
    ucPinLen = 0;
    for (i = 0; i < CTRL_PIN_LEN; i++) {
        if (cPinBuf[i] != CTRL_ADMIN_PIN[i]) {
            break;
        }
    }

    if (i == CTRL_PIN_LEN) {
        /* Admin mo khoa som: chi mo khoa he thong, KHONG mo cua */
        ucPinFail   = 0;
        ucAdminFail = 0;
        xTaskBuzzerPlay(BUZZER_SOUND_GRANTED, 0);
        prvBackToScan();
        prvLine(2, "ADMIN DA MO KHOA");
        prvShowTemp();
        return;
    }

    ucAdminFail++;
    if (ucAdminFail >= CTRL_MAX_ADMIN_FAIL) {
        prvEnterLock();                     /* do PIN admin: dem lai tu dau + canh bao them */
        return;
    }
    xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
    prvShowLock();
}

/* Vao khoa (hoac dem lai tu dau): van tay PAUSE, dong cua, canh bao */
static void prvEnterLock(void)
{
    /* Van tay da PAUSE tu luc NHAP PIN; gui lai cho chac (buoc 8c khoa ngay luc khoi dong) */
    xTaskAS608SendCommand(AS608_CMD_PAUSE, 0, portMAX_DELAY);
    xTaskServoSendCommand(SERVO_CMD_CLOSE, 0, portMAX_DELAY);
    xTaskBuzzerPlay(BUZZER_SOUND_WARNING, 0);
    prvSendAlert();

    eState         = CTRL_STATE_LOCKED;
    xLockSince     = xTaskGetTickCount();
    ucAdminFail    = 0;
    ucPinLen       = 0;
    xRedrawPending = pdFALSE;
    usLockShownSec = 0xFFFFU;               /* bat ve lai dong dem nguoc */
    prvShowLock();
}

/* Gui canh bao dot nhap len Google Sheet qua ESP32: lam o buoc task ESP32 */
static void prvSendAlert(void)
{
}

/* So giay con lai cua lan khoa (lam tron len: 59.1s hien 60) */
static uint16_t prvLockSecondsLeft(void)
{
    TickType_t xElapsed = (TickType_t)(xTaskGetTickCount() - xLockSince);
    TickType_t xTotal   = (TickType_t)(CTRL_LOCK_MS / portTICK_RATE_MS);
    TickType_t xLeft;

    if (xElapsed >= xTotal) {
        return 0;
    }
    xLeft = xTotal - xElapsed;
    return (uint16_t)((xLeft + (1000 / portTICK_RATE_MS) - 1) / (1000 / portTICK_RATE_MS));
}

/* Tim PIN trong bang. Tra ve pdTRUE va ID neu co. */
static BaseType_t prvFindPin(const char *pcPin, uint16_t *pusId)
{
    uint8_t i;
    uint8_t j;

    for (i = 0; i < CTRL_PIN_COUNT; i++) {
        for (j = 0; j < CTRL_PIN_LEN; j++) {
            if (xPinTable[i].cPin[j] != pcPin[j]) {
                break;
            }
        }
        if (j == CTRL_PIN_LEN) {
            *pusId = xPinTable[i].usId;
            return pdTRUE;
        }
    }
    return pdFALSE;
}

/* ================== Su kien AS608 ================== */

static void prvHandleEvent(const AS608_Event *pxEvent)
{
    switch (pxEvent->eType) {
    case AS608_EVT_READY:
        prvLineNumber(7, "CAM BIEN: ", pxEvent->usId, " MAU");
        break;

    case AS608_EVT_ERROR:
        prvLine(7, "LOI CAM BIEN VAN TAY");
        break;

    case AS608_EVT_MATCH:
        if (eState != CTRL_STATE_SCAN) {
            break;                          /* chi mo cua bang van tay khi dang QUET */
        }
        ucFingerFail = 0;
        ucPinFail    = 0;                   /* chu nha da xac thuc */
        xTaskServoSendCommand(SERVO_CMD_OPEN, 0, portMAX_DELAY);
        xTaskBuzzerPlay(BUZZER_SOUND_GRANTED, 0);
        prvLine(2, "MO CUA");
        prvLineNumber(3, "ID ", pxEvent->usId, "");
        prvLineNumber(4, "DIEM ", pxEvent->usValue, "");
        prvShowTemp();
        break;

    case AS608_EVT_NO_MATCH:
        if (eState != CTRL_STATE_SCAN) {
            break;
        }
        ucFingerFail++;
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        if (ucFingerFail >= CTRL_MAX_FINGER_FAIL) {
            prvEnterPin();
        } else {
            prvLine(2, "SAI VAN TAY");
            prvLineNumber(3, "LAN ", ucFingerFail, "/3");
            prvLine(4, "");
            prvShowTemp();
        }
        break;

    case AS608_EVT_BAD_IMAGE:
        prvLine(2, "ANH XAU");
        prvLine(3, "DAT LAI NGON TAY");
        prvLine(4, "");
        prvShowTemp();
        break;

    /* Cac su kien duoi day den tu lenh go tren terminal (vTaskKey) trong luc chua co menu */
    case AS608_EVT_ENROLL_STEP:
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        xRedrawPending = pdFALSE;
        prvLineNumber(2, "DANG KY ID ", pxEvent->usId, "");
        if (pxEvent->usValue == AS608_STEP_PLACE_1) {
            prvLine(3, "DAT TAY LAN 1");
        } else if (pxEvent->usValue == AS608_STEP_REMOVE) {
            prvLine(3, "NHAC TAY RA");
        } else {
            prvLine(3, "DAT LAI NGON DO");
        }
        prvLine(4, "");
        break;

    case AS608_EVT_ENROLL_OK:
        xTaskBuzzerPlay(BUZZER_SOUND_GRANTED, 0);
        prvLineNumber(2, "DANG KY XONG ID ", pxEvent->usId, "");
        prvLine(3, "");
        prvShowTemp();
        break;

    case AS608_EVT_ENROLL_FAIL:
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        prvLine(2, "DANG KY LOI");
        prvLine(3, "");
        prvShowTemp();
        break;

    case AS608_EVT_DUPLICATE:
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        prvLineNumber(2, "TRUNG VOI ID ", pxEvent->usId, "");
        prvLine(3, "");
        prvShowTemp();
        break;

    case AS608_EVT_DELETED:
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        if (pxEvent->usId == AS608_ID_ALL) {
            prvLine(2, "DA XOA TOAN BO");
        } else {
            prvLineNumber(2, "DA XOA ID ", pxEvent->usId, "");
        }
        prvLine(3, "");
        prvShowTemp();
        break;

    case AS608_EVT_LOOKUP_RESULT:
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        prvLineNumber(2, "TRA: ID ", pxEvent->usId, "");
        prvLine(3, "");
        prvShowTemp();
        break;

    case AS608_EVT_NOT_FOUND:
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        prvLine(2, "TRA: KHONG CO");
        prvLine(3, "");
        prvShowTemp();
        break;

    default:
        break;                              /* PAUSED, RESUMED, CANCELLED, TIMEOUT: khong can hien */
    }
}

/* ================== Chuyen trang thai + moc thoi gian ================== */

/* Sai van tay 3 lan: dung van tay, sang nhap PIN */
static void prvEnterPin(void)
{
    xTaskAS608SendCommand(AS608_CMD_PAUSE, 0, portMAX_DELAY);
    eState         = CTRL_STATE_PIN;
    ucFingerFail   = 0;
    ucPinLen       = 0;
    xStateSince    = xTaskGetTickCount();
    xRedrawPending = pdFALSE;
    prvShowPinEntry();
}

/* Ve QUET VAN TAY (het gio nhap PIN hoac het 30s sau khi mo bang PIN).
 * ucPinFail GIU NGUYEN: dung cho 30s khong lam lai duoc so lan thu PIN. */
static void prvBackToScan(void)
{
    xTaskAS608SendCommand(AS608_CMD_RESUME, 0, portMAX_DELAY);
    eState         = CTRL_STATE_SCAN;
    ucFingerFail   = 0;
    ucPinLen       = 0;
    xRedrawPending = pdFALSE;
    prvShowState();
}

/* Phep tru khong dau van dung khi bo dem tick tran */
static void prvCheckTimers(void)
{
    TickType_t xNow = xTaskGetTickCount();
    uint16_t   usLeft;

    if (eState == CTRL_STATE_LOCKED) {
        usLeft = prvLockSecondsLeft();
        if (usLeft == 0) {
            /* Het thoi gian khoa: ve quet van tay, xoa dem sai */
            ucPinFail   = 0;
            ucAdminFail = 0;
            prvBackToScan();
        } else if (usLeft != usLockShownSec) {
            prvShowCountdown(usLeft);       /* moi giay ve lai 1 dong */
        }
        return;
    }

    if (eState == CTRL_STATE_PIN &&
        (TickType_t)(xNow - xStateSince) >= (CTRL_PIN_IDLE_MS / portTICK_RATE_MS)) {
        prvBackToScan();                    /* 30s khong bam phim */
        return;
    }

    if (eState == CTRL_STATE_WAIT_RESUME &&
        (TickType_t)(xNow - xStateSince) >= (CTRL_RESUME_MS / portTICK_RATE_MS)) {
        prvBackToScan();                    /* 30s sau khi mo cua bang PIN */
        return;
    }

    if (xRedrawPending == pdTRUE &&
        (TickType_t)(xNow - xRedrawSince) >= (CTRL_MSG_MS / portTICK_RATE_MS)) {
        xRedrawPending = pdFALSE;
        prvShowState();
    }
}

/* ================== Man hinh ================== */

/* Ve man hinh cua trang thai hien tai (dong 2..6) */
static void prvShowState(void)
{
    if (eState == CTRL_STATE_LOCKED) {
        usLockShownSec = 0xFFFFU;
        prvShowLock();
        return;
    }
    if (eState == CTRL_STATE_PIN) {
        prvShowPinEntry();
        return;
    }
    if (eState == CTRL_STATE_WAIT_RESUME) {
        prvLine(2, "CUA DA MO BANG PIN");
        prvLine(3, "");
        prvLine(4, "VAN TAY BAT LAI 30S");
        prvLine(5, "");
        prvLine(6, "");
        return;
    }
    prvLine(2, "MOI DAT NGON TAY");
    prvLine(3, "");
    prvLine(4, "");
    prvLine(5, "");
    prvLine(6, "");
}

/* Man hinh KHOA: dem nguoc, PIN admin dang go (che ****), so lan sai admin */
static void prvShowLock(void)
{
    char    cBuf[16];                       /* "PIN ADMIN: " + 4 ky tu */
    uint8_t i;
    const char *pcPrefix = "PIN ADMIN: ";

    for (i = 0; pcPrefix[i] != '\0'; i++) {
        cBuf[i] = pcPrefix[i];
    }
    for (i = 0; i < CTRL_PIN_LEN; i++) {
        cBuf[11 + i] = (i < ucPinLen) ? '*' : '-';
    }
    cBuf[11 + CTRL_PIN_LEN] = '\0';

    prvLine(2, "HE THONG TAM KHOA");
    prvShowCountdown(prvLockSecondsLeft());
    prvLine(4, cBuf);
    if (ucAdminFail > 0) {
        prvLineNumber(5, "SAI ADMIN ", ucAdminFail, "/3");
    } else {
        prvLine(5, "");
    }
    prvLine(6, "C: XOA");
}

/* Dong 3: "CON m:ss" */
static void prvShowCountdown(uint16_t usSeconds)
{
    char cBuf[12];

    usLockShownSec = usSeconds;
    cBuf[0] = 'C'; cBuf[1] = 'O'; cBuf[2] = 'N'; cBuf[3] = ' ';
    cBuf[4] = (char)('0' + (usSeconds / 60U) % 10U);
    cBuf[5] = ':';
    cBuf[6] = (char)('0' + (usSeconds % 60U) / 10U);
    cBuf[7] = (char)('0' + (usSeconds % 10U));
    cBuf[8] = '\0';
    prvLine(3, cBuf);
}

/* Thong bao tam vua ve xong: CTRL_MSG_MS sau ve lai man hinh chinh */
static void prvShowTemp(void)
{
    xRedrawSince   = xTaskGetTickCount();
    xRedrawPending = pdTRUE;
}

/* Man hinh NHAP PIN: "PIN: **--" theo so phim da go, kem so lan sai */
static void prvShowPinEntry(void)
{
    char    cBuf[10];                       /* "PIN: " + 4 ky tu */
    uint8_t i;

    cBuf[0] = 'P'; cBuf[1] = 'I'; cBuf[2] = 'N'; cBuf[3] = ':'; cBuf[4] = ' ';
    for (i = 0; i < CTRL_PIN_LEN; i++) {
        cBuf[5 + i] = (i < ucPinLen) ? '*' : '-';
    }
    cBuf[5 + CTRL_PIN_LEN] = '\0';

    prvLine(2, "NHAP PIN 4 SO");
    prvLine(3, cBuf);
    if (ucPinFail > 0) {
        prvLineNumber(4, "SAI PIN ", ucPinFail, "/3");
    } else {
        prvLine(4, "");
    }
    prvLine(5, "");
    prvLine(6, "C: XOA  D: MENU");
}

/* Gui 1 dong len OLED; queue day qua 100ms thi bo dong do */
static void prvLine(uint8_t ucLine, const char *pcText)
{
    xTaskOledPrint(ucLine, 0, pcText, CTRL_OLED_WAIT_MS / portTICK_RATE_MS);
}

/* Ghep "<pcPrefix><so><pcSuffix>" (toi da 21 ky tu) roi gui len dong ucLine */
static void prvLineNumber(uint8_t ucLine, const char *pcPrefix, uint16_t usNumber, const char *pcSuffix)
{
    char    cBuf[OLED_LINE_LEN + 1];
    char    cDigits[6];
    uint8_t ucLen = 0;
    uint8_t ucDigits = 0;

    while (*pcPrefix != '\0' && ucLen < OLED_LINE_LEN) {
        cBuf[ucLen++] = *pcPrefix++;
    }
    do {
        cDigits[ucDigits++] = (char)('0' + (usNumber % 10U));
        usNumber = (uint16_t)(usNumber / 10U);
    } while (usNumber != 0U);
    while (ucDigits > 0U && ucLen < OLED_LINE_LEN) {
        cBuf[ucLen++] = cDigits[--ucDigits];
    }
    while (*pcSuffix != '\0' && ucLen < OLED_LINE_LEN) {
        cBuf[ucLen++] = *pcSuffix++;
    }
    cBuf[ucLen] = '\0';

    prvLine(ucLine, cBuf);
}