#include "task_control.h"
#include "task.h"
#include "task_as608.h"
#include "task_keypad.h"
#include "task_servo.h"
#include "task_buzzer.h"
#include "task_oled.h"
#include "flash_store.h"
#include "task_comm.h"
#include "comm_config.h"

/* ================== Cau hinh ================== */
#define CTRL_TASK_STACK         256     /* word */
#define CTRL_LOOP_WAIT_MS       50      /* cho su kien AS608 toi da bao lau roi quay lai doc phim */
#define CTRL_OLED_WAIT_MS       100     /* cho toi da khi queue OLED day */
#define CTRL_MSG_MS             3000    /* thong bao tam (MO CUA, SAI...) hien bao lau roi ve man hinh chinh */

#define CTRL_MAX_FINGER_FAIL    3       /* sai van tay 3 lan -> NHAP PIN */
#define CTRL_MAX_PIN_FAIL       3       /* sai PIN 3 lan -> khoa 5 phut */
#define CTRL_PIN_LEN            FLASH_STORE_PIN_LEN /* PIN 4 chu so */
#define CTRL_PIN_IDLE_MS        30000   /* NHAP PIN: 30s khong bam phim -> ve quet van tay */
#define CTRL_RESUME_MS          30000   /* mo cua bang PIN: 30s sau van tay hoat dong lai */

#define CTRL_KEY_CLEAR          'C'     /* xoa PIN dang go */

/* Khoa 5 phut (buoc 8b) */
#define CTRL_LOCK_MS            300000UL /* 5 phut (test nhanh: 60000UL) */
#define CTRL_MAX_ADMIN_FAIL     3       /* sai PIN admin 3 lan -> dem lai thoi gian khoa tu dau */
#define CTRL_ADMIN_PIN          "*AB#"  /* PIN admin: mo khoa som + vao menu quan ly */

/* Menu quan ly (buoc 8d) */
#define CTRL_MENU_IDLE_MS       30000   /* menu: 30s khong bam phim -> thoat ve quet van tay */
#define CTRL_LIST_ROWS          3       /* danh sach ID: so dong hien cung luc (dong 3..5) */
#define CTRL_KEY_OK             '#'     /* xac nhan */
#define CTRL_KEY_BACK           'B'     /* quay lai / huy / thoat (o danh sach: cuon xuong) */
#define CTRL_KEY_UP             'A'     /* danh sach: len */
#define CTRL_KEY_DOWN           'B'     /* danh sach: xuong */

/* ================== Log len Google Sheet qua task Comm (buoc 9b) ==================
 * Bang ma thong nhat 24/11. method / result theo task_comm.h cua ban cung nhom.
 * Log KHONG BAO GIO chua PIN. */
#define LOG_M_FINGER            1U      /* method: van tay */
#define LOG_M_PIN               2U      /* method: PIN (ca PIN admin) */
#define LOG_M_SYSTEM            3U      /* method: su kien he thong */
#define LOG_RES_FAIL            0U      /* result: sai */
#define LOG_RES_OK              1U      /* result: dung / thanh cong */
#define LOG_RES_WARN            2U      /* result: canh bao */
#define LOG_DOOR_CLOSED         0U      /* door: lenh dong / khong mo */
#define LOG_DOOR_OPEN           1U      /* door: lenh mo cua */
#define LOG_USER_NONE           0xFFFFU /* khong xac dinh duoc nguoi */

#define LOG_R_OPEN              0U      /* mo cua thanh cong */
#define LOG_R_FINGER_FAIL       1U      /* van tay khong khop */
#define LOG_R_PIN_FAIL          2U      /* sai PIN */
#define LOG_R_TO_PIN            3U      /* sai van tay 3 lan -> chuyen nhap PIN */
#define LOG_R_LOCK              4U      /* khoa 5 phut (nghi dot nhap) */
#define LOG_R_LOCK_RESTART      5U      /* sai PIN admin 3 lan luc khoa -> dem lai */
#define LOG_R_LOCK_BOOT         6U      /* bat nguon khi co khoa con -> khoa tiep */
#define LOG_R_ADMIN_UNLOCK      7U      /* admin mo khoa som */
#define LOG_R_LOCK_TIMEOUT      8U      /* het 5 phut, tu mo khoa */
#define LOG_R_ADMIN_FAIL        9U      /* sai PIN admin (luc khoa hoac vao menu) */
#define LOG_R_ADD               10U     /* them nguoi moi */
#define LOG_R_DELETE            11U     /* xoa nguoi */
#define LOG_R_REENROLL          12U     /* lam lai van tay */
#define LOG_R_CHANGE_PIN        13U     /* doi PIN */
#define LOG_R_BOOT              14U     /* khoi dong he thong */

/* Bang PIN mac dinh: chi ghi vao flash LAN DAU (trang 62 chua tung duoc ghi).
 * Sau do PIN doc tu flash; doi PIN trong menu quan ly (buoc 8d).
 * Moi PIN phai KHAC NHAU vi PIN dung de biet ai vao nha. */
typedef struct {
    uint16_t usId;                      /* ID van tay cua nguoi do */
    char     cPin[CTRL_PIN_LEN + 1];
} Ctrl_PinEntry;

static const Ctrl_PinEntry xDefaultPins[] = {
    { 0, "1111" },
    { 1, "2222" },
    { 2, "3333" }
};
#define CTRL_DEFAULT_COUNT  (sizeof(xDefaultPins) / sizeof(xDefaultPins[0]))

/* Trang thai cua task */
typedef enum {
    CTRL_STATE_SCAN = 0,    /* mac dinh: quet van tay */
    CTRL_STATE_PIN,         /* van tay da PAUSE, nhap PIN tren numpad */
    CTRL_STATE_WAIT_RESUME, /* vua mo cua bang PIN, cho 30s roi RESUME van tay */
    CTRL_STATE_LOCKED,      /* sai PIN 3 lan: khoa CTRL_LOCK_MS, chi nhan PIN admin */
    CTRL_STATE_OTA,         /* buoc 9b: da dong y cap nhat firmware, cho ESP32 nap (khoa phim) */
    /* Menu quan ly (van tay PAUSE suot thoi gian o menu) */
    CTRL_STATE_MENU_AUTH,   /* vua bam D: nhap PIN admin */
    CTRL_STATE_MENU_MAIN,   /* menu chinh: chon 1..5 */
    CTRL_STATE_MENU_LIST,   /* chon ID tu danh sach (muc 2, 3, 5): A len, B xuong, # chon, C lai */
    CTRL_STATE_MENU_CONFIRM,/* xac nhan xoa (muc 2) */
    CTRL_STATE_MENU_PIN,    /* nhap PIN moi (muc 1 sau khi dang ky, muc 5) */
    CTRL_STATE_MENU_BUSY    /* dang cho AS608 lam xong (dang ky, lam lai, tra, xoa) */
} Ctrl_State;

/* Muc menu dang lam */
typedef enum {
    CTRL_ITEM_NONE = 0,
    CTRL_ITEM_ADD,          /* 1: them nguoi = dang ky van tay + dat PIN */
    CTRL_ITEM_DELETE,       /* 2: xoa nguoi (van tay + PIN) */
    CTRL_ITEM_REENROLL,     /* 3: lam lai van tay, giu ID va PIN */
    CTRL_ITEM_LOOKUP,       /* 4: tra van tay */
    CTRL_ITEM_CHANGE_PIN,   /* 5: doi PIN */
    CTRL_ITEM_UNDO_ADD      /* huy them nguoi: dang xoa van tay vua dang ky */
} Ctrl_MenuItem;

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

/* Ban sao bang PIN trong RAM (doc tu flash luc khoi dong): cPins[i] = PIN cua ID i */
static char       cPins[FLASH_STORE_MAX_PIN][FLASH_STORE_PIN_LEN];

/* Trang thai hien o dong 7 (buoc 9b) */
static uint8_t    ucSensorOk    = 2;    /* 0: loi, 1: AS608 giao tiep tot, 2: chua biet (vua bat) */
static uint16_t   usSensorCount = 0;    /* so mau van tay trong AS608 */
static uint8_t    ucLink        = 0;    /* 1: nhan duoc khung tu ESP32 */
static uint8_t    ucWifi        = 0;    /* 1: ESP32 bao da co WiFi */

/* Menu */
static Ctrl_MenuItem eMenuItem  = CTRL_ITEM_NONE;
static uint16_t   usMenuId      = 0;    /* ID dang thao tac */
static BaseType_t xWaitIndex    = pdFALSE; /* dang cho AS608 doc danh sach ID */
static uint8_t    ucIndexMap[AS608_INDEX_BYTES]; /* bit i = 1: ID i co van tay */
static uint16_t   usListCount   = 0;    /* so ID trong danh sach (muc 5: chi ID 0..31) */
static uint16_t   usListPos     = 0;    /* dong dang chon (thu tu trong danh sach, tu 0) */
static uint16_t   usListTop     = 0;    /* dong dau tien dang hien */

static void vTaskControl(void *pvParameters);
static void prvHandleKey(char cKey);
static void prvHandleEvent(const AS608_Event *pxEvent);
static void prvCheckTimers(void);
static void prvEnterPin(void);
static void prvBackToScan(void);
static void prvCheckPin(void);
static void prvHandleLockKey(char cKey);
static void prvEnterLock(uint8_t ucReason);
static void prvUnlock(uint8_t ucReason);
static void prvLoadPins(void);
static void prvLog(uint16_t usUser, uint8_t ucMethod, uint8_t ucResult, uint8_t ucDoor, uint8_t ucReason);
static void prvHandleComm(const Comm_Event *pxComm);
static void prvShowStatus(void);
static void prvShowWho(uint8_t ucLine, uint16_t usId);
static void prvShowDiag(void);
static uint16_t prvLockSecondsLeft(void);
static void prvShowLock(void);
static void prvShowCountdown(uint16_t usSeconds);
static BaseType_t prvFindPin(const char *pcPin, uint16_t *pusId);
static void prvShowState(void);
static void prvShowTemp(void);
static void prvShowPinEntry(void);
static BaseType_t prvIsMenuState(void);
static void prvEnterMenu(void);
static void prvExitMenu(void);
static void prvMenuMain(void);
static void prvHandleMenuKey(char cKey);
static void prvMenuKeyAuth(char cKey);
static void prvMenuKeyMain(char cKey);
static void prvMenuKeyList(char cKey);
static void prvMenuStartList(Ctrl_MenuItem eItem);
static uint16_t prvListLimit(void);
static uint16_t prvListCount(void);
static BaseType_t prvListIdAt(uint16_t usOrder, uint16_t *pusId);
static void prvShowList(void);
static uint8_t prvAppendText(char *pcBuf, uint8_t ucLen, const char *pcText);
static uint8_t prvAppendNumber(char *pcBuf, uint8_t ucLen, uint16_t usNumber);
static void prvMenuKeyConfirm(char cKey);
static void prvMenuKeyPin(char cKey);
static void prvMenuUndoAdd(void);
static void prvHandleMenuEvent(const AS608_Event *pxEvent);
static void prvMenuDone(void);
static void prvMenuStep(const AS608_Event *pxEvent);
static BaseType_t prvPinUsedByOther(const char *pcPin, uint16_t usId);
static void prvShowMenu(void);
static void prvShowMask(uint8_t ucLine, const char *pcPrefix, uint8_t ucLen);
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
    Comm_Event  xComm;
    char        cKey;

    (void)pvParameters;

    /* Tieu de co dinh (dong 0, 1 khong bao gio bi ghi de) */
    xTaskOledPrint(0, 1, "KHOA CUA VAN TAY", CTRL_OLED_WAIT_MS / portTICK_RATE_MS);
    prvLine(1, "---------------------");

    /* Doc bang PIN tu flash (lan dau: ghi bang mac dinh). AS608 dang cho 300ms
     * khoi dong nen ghi flash luc nay khong lam mat byte USART1. */
    prvLoadPins();

    prvShowStatus();
    prvLog(LOG_USER_NONE, LOG_M_SYSTEM, LOG_RES_OK, LOG_DOOR_CLOSED, LOG_R_BOOT);

    /* Truoc khi mat dien dang khoa: khoa lai du CTRL_LOCK_MS tinh tu luc bat (khong co RTC) */
    if (xFlashStoreIsLocked() == pdTRUE) {
        prvEnterLock(LOG_R_LOCK_BOOT);
    } else {
        prvShowState();
    }

    for (;;)
    {
        while (xTaskKeypadGetKey(&cKey, 0) == pdPASS) {
            prvHandleKey(cKey);
        }

        /* Su kien tu task Comm (ESP32): ket noi, WiFi, OTA (0 tick: khong cho) */
        while (xTaskCommGetEvent(&xComm, 0) == pdPASS) {
            prvHandleComm(&xComm);
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
    if (eState == CTRL_STATE_OTA) {
        return;                             /* dang cap nhat firmware: khoa phim */
    }
    if (eState == CTRL_STATE_LOCKED) {
        prvHandleLockKey(cKey);             /* dang khoa: chi nhan PIN admin, D bi khoa */
        return;
    }

    if (prvIsMenuState() == pdTRUE) {
        prvHandleMenuKey(cKey);
        return;
    }

    if (cKey == KEYPAD_KEY_MENU) {
        prvEnterMenu();                     /* tu QUET, NHAP PIN hoac cho RESUME */
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
        prvLog(usId, LOG_M_PIN, LOG_RES_OK, LOG_DOOR_OPEN, LOG_R_OPEN);
        prvLine(2, "MO CUA BANG PIN");
        prvShowWho(3, usId);
        prvLine(4, "");
        prvLine(6, "");
        prvShowTemp();
        return;
    }

    ucPinFail++;
    prvLog(LOG_USER_NONE, LOG_M_PIN, LOG_RES_FAIL, LOG_DOOR_CLOSED, LOG_R_PIN_FAIL);

    if (ucPinFail >= CTRL_MAX_PIN_FAIL) {
        prvEnterLock(LOG_R_LOCK);           /* nguoi la do PIN: khoa */
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
        xTaskBuzzerPlay(BUZZER_SOUND_GRANTED, 0);
        prvUnlock(LOG_R_ADMIN_UNLOCK);
        prvLine(2, "ADMIN DA MO KHOA");
        prvShowTemp();
        return;
    }

    ucAdminFail++;
    prvLog(LOG_USER_NONE, LOG_M_PIN, LOG_RES_FAIL, LOG_DOOR_CLOSED, LOG_R_ADMIN_FAIL);
    if (ucAdminFail >= CTRL_MAX_ADMIN_FAIL) {
        prvEnterLock(LOG_R_LOCK_RESTART);   /* do PIN admin: dem lai tu dau + canh bao them */
        return;
    }
    xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
    prvShowLock();
}

/* Vao khoa (hoac dem lai tu dau): van tay PAUSE, dong cua, canh bao, ghi co khoa vao flash,
 * gui canh bao len Google Sheet (ucReason: LOG_R_LOCK / LOCK_RESTART / LOCK_BOOT) */
static void prvEnterLock(uint8_t ucReason)
{
    /* Van tay da PAUSE tu luc NHAP PIN; gui lai cho chac (khoa ngay luc khoi dong) */
    xTaskAS608SendCommand(AS608_CMD_PAUSE, 0, portMAX_DELAY);
    xTaskServoSendCommand(SERVO_CMD_CLOSE, 0, portMAX_DELAY);
    xFlashStoreSetLocked(pdTRUE);           /* da khoa san (dem lai) thi khong ghi lai */
    xTaskBuzzerPlay(BUZZER_SOUND_WARNING, 0);
    prvLog(LOG_USER_NONE, LOG_M_SYSTEM, LOG_RES_WARN, LOG_DOOR_CLOSED, ucReason);

    eState         = CTRL_STATE_LOCKED;
    xLockSince     = xTaskGetTickCount();
    ucAdminFail    = 0;
    ucPinLen       = 0;
    xRedrawPending = pdFALSE;
    usLockShownSec = 0xFFFFU;               /* bat ve lai dong dem nguoc */
    prvShowLock();
}

/* Ra khoi khoa (admin mo som hoac het gio): xoa co khoa trong flash ROI moi RESUME van tay
 * (xoa trang ~20ms, CPU dung: luc nay AS608 dang PAUSE nen khong mat byte) */
static void prvUnlock(uint8_t ucReason)
{
    prvLog(LOG_USER_NONE, (ucReason == LOG_R_ADMIN_UNLOCK) ? LOG_M_PIN : LOG_M_SYSTEM,
           LOG_RES_OK, LOG_DOOR_CLOSED, ucReason);
    xFlashStoreSetLocked(pdFALSE);
    ucPinFail   = 0;
    ucAdminFail = 0;
    prvBackToScan();
}

/* Gui 1 ban ghi cho task Comm (queue day / dang OTA thi bo qua, khong chan task dieu khien) */
static void prvLog(uint16_t usUser, uint8_t ucMethod, uint8_t ucResult, uint8_t ucDoor, uint8_t ucReason)
{
    (void)xTaskCommLog(usUser, ucMethod, ucResult, ucDoor, ucReason);
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

/* Doc bang PIN tu flash vao RAM. Lan dau (trang PIN trong): ghi bang mac dinh. */
static void prvLoadPins(void)
{
    uint8_t i;
    uint8_t j;

    if (xFlashStoreLoadPins(cPins) == pdTRUE) {
        return;
    }
    for (i = 0; i < CTRL_DEFAULT_COUNT; i++) {
        if (xDefaultPins[i].usId < FLASH_STORE_MAX_PIN) {
            for (j = 0; j < CTRL_PIN_LEN; j++) {
                cPins[xDefaultPins[i].usId][j] = xDefaultPins[i].cPin[j];
            }
        }
    }
    xFlashStoreSavePins(cPins);
}

/* Tim PIN trong bang RAM. Tra ve pdTRUE va ID (= so o) neu co. */
static BaseType_t prvFindPin(const char *pcPin, uint16_t *pusId)
{
    uint16_t i;
    uint8_t  j;

    for (i = 0; i < FLASH_STORE_MAX_PIN; i++) {
        if (cPins[i][0] == FLASH_STORE_PIN_EMPTY) {
            continue;                       /* ID nay chua co PIN */
        }
        for (j = 0; j < CTRL_PIN_LEN; j++) {
            if (cPins[i][j] != pcPin[j]) {
                break;
            }
        }
        if (j == CTRL_PIN_LEN) {
            *pusId = i;
            return pdTRUE;
        }
    }
    return pdFALSE;
}

/* ================== Su kien AS608 ================== */

static void prvHandleEvent(const AS608_Event *pxEvent)
{
    if (eState == CTRL_STATE_OTA) {
        return;                             /* dang cap nhat firmware: bo qua (khong log) */
    }
    if (prvIsMenuState() == pdTRUE) {
        prvHandleMenuEvent(pxEvent);
        return;
    }

    switch (pxEvent->eType) {
    case AS608_EVT_READY:
        ucSensorOk    = 1;
        usSensorCount = pxEvent->usId;
        prvShowStatus();
        break;

    case AS608_EVT_ERROR:
        ucSensorOk = 0;
        prvShowStatus();
        break;

    case AS608_EVT_MATCH:
        if (eState != CTRL_STATE_SCAN) {
            break;                          /* chi mo cua bang van tay khi dang QUET */
        }
        ucFingerFail = 0;
        ucPinFail    = 0;                   /* chu nha da xac thuc */
        xTaskServoSendCommand(SERVO_CMD_OPEN, 0, portMAX_DELAY);
        xTaskBuzzerPlay(BUZZER_SOUND_GRANTED, 0);
        prvLog(pxEvent->usId, LOG_M_FINGER, LOG_RES_OK, LOG_DOOR_OPEN, LOG_R_OPEN);
        prvLine(2, "MO CUA");
        prvShowWho(3, pxEvent->usId);
        prvLine(4, "");                     /* bo hien diem (24/11) */
        prvShowTemp();
        break;

    case AS608_EVT_NO_MATCH:
        if (eState != CTRL_STATE_SCAN) {
            break;
        }
        ucFingerFail++;
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        prvLog(LOG_USER_NONE, LOG_M_FINGER, LOG_RES_FAIL, LOG_DOOR_CLOSED, LOG_R_FINGER_FAIL);
        if (ucFingerFail >= CTRL_MAX_FINGER_FAIL) {
            prvLog(LOG_USER_NONE, LOG_M_SYSTEM, LOG_RES_WARN, LOG_DOOR_CLOSED, LOG_R_TO_PIN);
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

    /* Cac su kien duoi day ngoai menu: tu lenh go tren terminal (vTaskKey) */
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
            prvUnlock(LOG_R_LOCK_TIMEOUT);  /* het thoi gian khoa: ve quet van tay, xoa dem sai */
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

    /* Menu: 30s khong bam phim -> thoat (luc dang cho AS608 thi khong tinh, AS608 co timeout rieng) */
    if (prvIsMenuState() == pdTRUE && eState != CTRL_STATE_MENU_BUSY &&
        (TickType_t)(xNow - xStateSince) >= (CTRL_MENU_IDLE_MS / portTICK_RATE_MS)) {
        if (eState == CTRL_STATE_MENU_PIN && eMenuItem == CTRL_ITEM_ADD) {
            /* Nguoi moi chua co PIN: xoa van tay vua dang ky (AS608 lam truoc RESUME) */
            xTaskAS608SendCommand(AS608_CMD_DELETE_ID, usMenuId, portMAX_DELAY);
        }
        prvExitMenu();
        return;
    }

    if (xRedrawPending == pdTRUE &&
        (TickType_t)(xNow - xRedrawSince) >= (CTRL_MSG_MS / portTICK_RATE_MS)) {
        xRedrawPending = pdFALSE;
        prvShowState();
    }
}

/* ================== Menu quan ly (buoc 8d) ================== */

static BaseType_t prvIsMenuState(void)
{
    return (eState >= CTRL_STATE_MENU_AUTH) ? pdTRUE : pdFALSE;
}

/* Bam D: dung van tay, hoi PIN admin */
static void prvEnterMenu(void)
{
    if (eState == CTRL_STATE_SCAN) {
        xTaskAS608SendCommand(AS608_CMD_PAUSE, 0, portMAX_DELAY);
    }
    xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
    eState         = CTRL_STATE_MENU_AUTH;
    eMenuItem      = CTRL_ITEM_NONE;
    ucPinLen       = 0;
    xStateSince    = xTaskGetTickCount();
    xRedrawPending = pdFALSE;
    prvShowMenu();
}

/* Thoat menu: ve quet van tay (giu so lan sai PIN) */
static void prvExitMenu(void)
{
    eMenuItem = CTRL_ITEM_NONE;
    prvBackToScan();
}

/* Ve menu chinh */
static void prvMenuMain(void)
{
    eState         = CTRL_STATE_MENU_MAIN;
    eMenuItem      = CTRL_ITEM_NONE;
    ucPinLen       = 0;
    xStateSince    = xTaskGetTickCount();
    xRedrawPending = pdFALSE;
    prvShowMenu();
}

static void prvHandleMenuKey(char cKey)
{
    if (cKey == KEYPAD_KEY_MENU) {
        if (eState == CTRL_STATE_MENU_AUTH) {
            prvExitMenu();                  /* D lan nua o man PIN admin: thoat */
        }
        return;                             /* cac man khac: D khong co tac dung */
    }
    xStateSince = xTaskGetTickCount();      /* co bam phim: tinh lai 30s */

    /* Sua 24/11: dang hien ket qua tam (DA HUY, DA XOA...) o menu chinh, man hinh chua co
     * menu. Phim dau tien chi tat thong bao va ve lai menu, KHONG tinh la lenh
     * (truoc day bam B lan nua o day = THOAT menu ngoai y muon). */
    if (eState == CTRL_STATE_MENU_MAIN && xRedrawPending == pdTRUE) {
        xRedrawPending = pdFALSE;
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        prvShowMenu();
        return;
    }

    switch (eState) {
    case CTRL_STATE_MENU_AUTH:    prvMenuKeyAuth(cKey);    break;
    case CTRL_STATE_MENU_MAIN:    prvMenuKeyMain(cKey);    break;
    case CTRL_STATE_MENU_LIST:    prvMenuKeyList(cKey);    break;
    case CTRL_STATE_MENU_CONFIRM: prvMenuKeyConfirm(cKey); break;
    case CTRL_STATE_MENU_PIN:     prvMenuKeyPin(cKey);     break;
    case CTRL_STATE_MENU_BUSY:
        if (cKey == CTRL_KEY_BACK && xWaitIndex == pdFALSE &&
            eMenuItem != CTRL_ITEM_DELETE && eMenuItem != CTRL_ITEM_UNDO_ADD) {
            /* Dang cho dat tay: huy (task AS608 gui CANCEL len dau queue) */
            xTaskAS608SendCommand(AS608_CMD_CANCEL, 0, portMAX_DELAY);
        }
        break;
    default:
        break;
    }
}

/* Nhap PIN admin: 4 phim bat ky tru C (xoa) va D (thoat).
 * B la 1 ky tu cua PIN admin *AB# nen o man nay B KHONG phai phim thoat.
 * Sai tinh chung voi so lan sai PIN. */
static void prvMenuKeyAuth(char cKey)
{
    uint8_t i;

    if (cKey == CTRL_KEY_CLEAR) {
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        ucPinLen = 0;
        prvShowMenu();
        return;
    }

    cPinBuf[ucPinLen++] = cKey;
    if (ucPinLen < CTRL_PIN_LEN) {
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        prvShowMenu();
        return;
    }

    ucPinLen = 0;
    for (i = 0; i < CTRL_PIN_LEN; i++) {
        if (cPinBuf[i] != CTRL_ADMIN_PIN[i]) {
            break;
        }
    }
    if (i == CTRL_PIN_LEN) {
        ucPinFail = 0;                      /* admin da xac thuc */
        xTaskBuzzerPlay(BUZZER_SOUND_GRANTED, 0);
        prvMenuMain();
        return;
    }

    ucPinFail++;
    prvLog(LOG_USER_NONE, LOG_M_PIN, LOG_RES_FAIL, LOG_DOOR_CLOSED, LOG_R_ADMIN_FAIL);
    if (ucPinFail >= CTRL_MAX_PIN_FAIL) {
        prvEnterLock(LOG_R_LOCK);           /* do PIN admin qua menu: khoa nhu do PIN */
        return;
    }
    xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
    prvShowMenu();
}

/* Menu chinh: chon muc */
static void prvMenuKeyMain(char cKey)
{
    if (cKey == CTRL_KEY_BACK) {
        prvExitMenu();
        return;
    }

    switch (cKey) {
    case '1':
        eMenuItem = CTRL_ITEM_ADD;
        eState    = CTRL_STATE_MENU_BUSY;
        xTaskAS608SendCommand(AS608_CMD_ENROLL, 0, portMAX_DELAY);
        break;
    case '4':
        eMenuItem = CTRL_ITEM_LOOKUP;
        eState    = CTRL_STATE_MENU_BUSY;
        xTaskAS608SendCommand(AS608_CMD_LOOKUP, 0, portMAX_DELAY);
        break;
    case '2':
        prvMenuStartList(CTRL_ITEM_DELETE);
        return;
    case '3':
        prvMenuStartList(CTRL_ITEM_REENROLL);
        return;
    case '5':
        prvMenuStartList(CTRL_ITEM_CHANGE_PIN);
        return;
    case '0':
        prvShowDiag();                      /* an: heap / stack de kiem tra RAM (khong ghi tren menu) */
        return;
    default:
        return;                             /* phim khac: bo qua */
    }

    xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
    xRedrawPending = pdFALSE;
    prvShowMenu();
}

/* Muc 2, 3, 5: nho task AS608 doc danh sach ID, cho AS608_EVT_INDEX_READY */
static void prvMenuStartList(Ctrl_MenuItem eItem)
{
    xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
    eMenuItem      = eItem;
    eState         = CTRL_STATE_MENU_BUSY;
    xWaitIndex     = pdTRUE;
    xRedrawPending = pdFALSE;
    xTaskAS608SendCommand(AS608_CMD_READ_INDEX, 0, portMAX_DELAY);
    prvShowMenu();
}

/* Danh sach: A len, B xuong, # chon, C ve menu chinh */
static void prvMenuKeyList(char cKey)
{
    if (cKey == CTRL_KEY_UP) {
        if (usListPos > 0) {
            usListPos--;
            if (usListPos < usListTop) {
                usListTop = usListPos;          /* cuon len */
            }
            xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
            prvShowList();
        }
        return;
    }
    if (cKey == CTRL_KEY_DOWN) {
        if (usListPos + 1U < usListCount) {
            usListPos++;
            if (usListPos >= usListTop + CTRL_LIST_ROWS) {
                usListTop = (uint16_t)(usListPos - (CTRL_LIST_ROWS - 1U));  /* cuon xuong */
            }
            xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
            prvShowList();
        }
        return;
    }
    if (cKey == CTRL_KEY_CLEAR) {
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        prvMenuMain();
        return;
    }
    if (cKey != CTRL_KEY_OK || prvListIdAt(usListPos, &usMenuId) != pdTRUE) {
        return;
    }

    /* # : chon usMenuId */
    xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
    if (eMenuItem == CTRL_ITEM_DELETE) {
        eState = CTRL_STATE_MENU_CONFIRM;
    } else if (eMenuItem == CTRL_ITEM_REENROLL) {
        eState = CTRL_STATE_MENU_BUSY;
        xTaskAS608SendCommand(AS608_CMD_REENROLL, usMenuId, portMAX_DELAY);
    } else {
        eState   = CTRL_STATE_MENU_PIN;     /* doi PIN */
        ucPinLen = 0;
    }
    prvShowMenu();
}

/* Muc 5 (doi PIN) chi liet ke ID co cho luu PIN (0..31); muc 2, 3: moi ID */
static uint16_t prvListLimit(void)
{
    return (eMenuItem == CTRL_ITEM_CHANGE_PIN) ? (uint16_t)FLASH_STORE_MAX_PIN : (uint16_t)AS608_MAX_ID;
}

/* So ID co van tay (trong gioi han cua muc dang lam) */
static uint16_t prvListCount(void)
{
    uint16_t usId;
    uint16_t usCount = 0;
    uint16_t usLimit = prvListLimit();

    for (usId = 0; usId < usLimit; usId++) {
        if ((ucIndexMap[usId / 8U] & (1U << (usId % 8U))) != 0U) {
            usCount++;
        }
    }
    return usCount;
}

/* ID o vi tri usOrder trong danh sach (dem tu 0). Tra ve pdFALSE neu vuot qua cuoi */
static BaseType_t prvListIdAt(uint16_t usOrder, uint16_t *pusId)
{
    uint16_t usId;
    uint16_t usLimit = prvListLimit();

    for (usId = 0; usId < usLimit; usId++) {
        if ((ucIndexMap[usId / 8U] & (1U << (usId % 8U))) != 0U) {
            if (usOrder == 0U) {
                *pusId = usId;
                return pdTRUE;
            }
            usOrder--;
        }
    }
    return pdFALSE;
}

/* Xac nhan xoa: # = dong y, B = huy */
static void prvMenuKeyConfirm(char cKey)
{
    if (cKey == CTRL_KEY_OK) {
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        eState = CTRL_STATE_MENU_BUSY;
        xTaskAS608SendCommand(AS608_CMD_DELETE_ID, usMenuId, portMAX_DELAY);
        prvShowMenu();
    } else if (cKey == CTRL_KEY_BACK) {
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        prvMenuMain();
    }
}

/* Nhap PIN moi cho usMenuId: 4 chu so, C = xoa, B = huy */
static void prvMenuKeyPin(char cKey)
{
    uint8_t i;

    if (cKey == CTRL_KEY_BACK) {
        if (eMenuItem == CTRL_ITEM_ADD) {
            prvMenuUndoAdd();               /* nguoi moi bat buoc co PIN: huy ca van tay */
        } else {
            xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
            prvMenuMain();
        }
        return;
    }
    if (cKey == CTRL_KEY_CLEAR) {
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        ucPinLen = 0;
        prvShowMenu();
        return;
    }
    if (cKey < '0' || cKey > '9') {
        return;                             /* PIN nguoi dung chi gom chu so */
    }

    cPinBuf[ucPinLen++] = cKey;
    if (ucPinLen < CTRL_PIN_LEN) {
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        prvShowMenu();
        return;
    }
    ucPinLen = 0;

    if (prvPinUsedByOther(cPinBuf, usMenuId) == pdTRUE) {
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        prvShowMenu();
        prvLine(4, "PIN DA CO NGUOI DUNG");
        return;
    }

    /* Luu flash (AS608 dang PAUSE va ranh nen xoa trang ~20ms khong anh huong) */
    for (i = 0; i < CTRL_PIN_LEN; i++) {
        cPins[usMenuId][i] = cPinBuf[i];
    }
    if (xFlashStoreSavePins(cPins) != pdPASS) {
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        prvMenuDone();
        prvLine(2, "LOI GHI FLASH");
        return;
    }

    xTaskBuzzerPlay(BUZZER_SOUND_GRANTED, 0);
    prvLog(usMenuId, LOG_M_SYSTEM, LOG_RES_OK, LOG_DOOR_CLOSED,
           (eMenuItem == CTRL_ITEM_ADD) ? LOG_R_ADD : LOG_R_CHANGE_PIN);
    prvMenuDone();
    prvLineNumber(2, "DA LUU PIN ID ", usMenuId, "");
}

/* Huy them nguoi: xoa van tay vua dang ky, cho DELETED roi ve menu chinh */
static void prvMenuUndoAdd(void)
{
    xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
    eMenuItem = CTRL_ITEM_UNDO_ADD;
    eState    = CTRL_STATE_MENU_BUSY;
    xTaskAS608SendCommand(AS608_CMD_DELETE_ID, usMenuId, portMAX_DELAY);
    prvShowMenu();
}

/* PIN da thuoc ID khac chua (PIN dung de biet ai vao nen phai khac nhau) */
static BaseType_t prvPinUsedByOther(const char *pcPin, uint16_t usId)
{
    uint16_t usOwner;

    if (prvFindPin(pcPin, &usOwner) == pdTRUE && usOwner != usId) {
        return pdTRUE;
    }
    return pdFALSE;
}

/* Su kien AS608 khi dang o menu */
static void prvHandleMenuEvent(const AS608_Event *pxEvent)
{
    uint8_t i;

    if (pxEvent->eType == AS608_EVT_READY) {
        ucSensorOk    = 1;
        usSensorCount = pxEvent->usId;
        prvShowStatus();
        return;
    }
    if (eState != CTRL_STATE_MENU_BUSY) {
        return;                             /* khong cho AS608 thi bo qua (PAUSED, RESUMED...) */
    }

    switch (pxEvent->eType) {
    case AS608_EVT_INDEX_READY:
        if (xWaitIndex == pdFALSE) {
            break;
        }
        xWaitIndex = pdFALSE;
        vTaskAS608GetIndex(ucIndexMap);
        usSensorCount = pxEvent->usId;      /* tien cap nhat dong trang thai */
        prvShowStatus();
        usListCount = prvListCount();
        if (usListCount == 0U) {
            xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
            prvMenuDone();
            prvLine(2, "CHUA CO AI");
            break;
        }
        eState      = CTRL_STATE_MENU_LIST;
        usListPos   = 0;
        usListTop   = 0;
        xStateSince = xTaskGetTickCount();
        prvShowMenu();
        break;

    case AS608_EVT_ENROLL_STEP:
        prvMenuStep(pxEvent);
        break;

    case AS608_EVT_BAD_IMAGE:
        prvLine(4, "ANH XAU - DAT LAI");
        break;

    case AS608_EVT_ENROLL_OK:
        if (eMenuItem == CTRL_ITEM_ADD) {
            usMenuId = pxEvent->usId;
            usSensorCount++;                /* AS608 da luu mau moi (huy thi tru lai) */
            prvShowStatus();
            if (usMenuId >= FLASH_STORE_MAX_PIN) {
                /* Het cho PIN: nguoi moi bat buoc co PIN nen xoa van tay vua dang ky */
                prvMenuUndoAdd();
                return;
            }
            xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
            eState   = CTRL_STATE_MENU_PIN;
            ucPinLen = 0;
            xStateSince = xTaskGetTickCount();
            prvShowMenu();
        } else {
            xTaskBuzzerPlay(BUZZER_SOUND_GRANTED, 0);
            prvLog(pxEvent->usId, LOG_M_SYSTEM, LOG_RES_OK, LOG_DOOR_CLOSED, LOG_R_REENROLL);
            prvMenuDone();
            prvLineNumber(2, "XONG VAN TAY ID ", pxEvent->usId, "");
        }
        break;

    case AS608_EVT_ENROLL_FAIL:
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        prvMenuDone();
        if (pxEvent->usValue == AS608_FAIL_FULL) {
            prvLine(2, "BO NHO VAN TAY DAY");
        } else if (pxEvent->usValue == AS608_FAIL_MISMATCH) {
            prvLine(2, "2 LAN DAT KHONG KHOP");
        } else {
            prvLine(2, "DANG KY LOI");
        }
        break;

    case AS608_EVT_DUPLICATE:
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        prvMenuDone();
        prvLineNumber(2, "TRUNG VOI ID ", pxEvent->usId, "");
        break;

    case AS608_EVT_LOOKUP_RESULT:
        xTaskBuzzerPlay(BUZZER_SOUND_GRANTED, 0);
        prvMenuDone();
        prvLineNumber(2, "TRA: ID ", pxEvent->usId, "");
        if (pxEvent->usId < FLASH_STORE_MAX_PIN && cPins[pxEvent->usId][0] != FLASH_STORE_PIN_EMPTY) {
            prvLine(3, "DA CO PIN");
        } else {
            prvLine(3, "CHUA CO PIN");
        }
        prvShowWho(4, pxEvent->usId);       /* ten tu Google Sheet (chua co: "ID n") */
        break;

    case AS608_EVT_NOT_FOUND:
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        prvMenuDone();
        prvLine(2, "KHONG CO TRONG MAY");
        break;

    case AS608_EVT_DELETED:
        if (usSensorCount > 0U) {
            usSensorCount--;
        }
        prvShowStatus();
        if (eMenuItem == CTRL_ITEM_UNDO_ADD) {
            prvMenuDone();
            prvLine(2, (usMenuId >= FLASH_STORE_MAX_PIN) ? "DA DU 32 NGUOI" : "DA HUY THEM NGUOI");
            break;
        }
        /* Xoa nguoi: xoa ca PIN cua ID do */
        if (pxEvent->usId < FLASH_STORE_MAX_PIN && cPins[pxEvent->usId][0] != FLASH_STORE_PIN_EMPTY) {
            for (i = 0; i < CTRL_PIN_LEN; i++) {
                cPins[pxEvent->usId][i] = FLASH_STORE_PIN_EMPTY;
            }
            xFlashStoreSavePins(cPins);
        }
        xTaskBuzzerPlay(BUZZER_SOUND_GRANTED, 0);
        prvLog(pxEvent->usId, LOG_M_SYSTEM, LOG_RES_OK, LOG_DOOR_CLOSED, LOG_R_DELETE);
        prvMenuDone();
        prvLineNumber(2, "DA XOA ID ", pxEvent->usId, "");
        break;

    case AS608_EVT_CANCELLED:
        xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
        prvMenuDone();
        prvLine(2, "DA HUY");
        break;

    case AS608_EVT_TIMEOUT:
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        prvMenuDone();
        prvLine(2, "HET GIO DAT TAY");
        break;

    case AS608_EVT_ERROR:
        ucSensorOk = 0;
        prvShowStatus();
        xWaitIndex = pdFALSE;
        xTaskBuzzerPlay(BUZZER_SOUND_DENIED, 0);
        prvMenuDone();
        prvLine(2, "LOI CAM BIEN");
        break;

    default:
        break;
    }
}

/* Xong 1 viec: ve trang thai menu chinh nhung CHUA ve menu; nguoi goi ghi ket qua o dong 2,
 * 3s sau prvShowState() ve menu chinh (khong ve 2 lan lam day queue OLED) */
static void prvMenuDone(void)
{
    eState      = CTRL_STATE_MENU_MAIN;
    eMenuItem   = CTRL_ITEM_NONE;
    ucPinLen    = 0;
    xStateSince = xTaskGetTickCount();
    prvLine(3, "");
    prvLine(4, "");
    prvLine(5, "");
    prvLine(6, "PHIM BAT KY: VE MENU");
    prvShowTemp();
}

/* Buoc dang ky (them nguoi / lam lai van tay) */
static void prvMenuStep(const AS608_Event *pxEvent)
{
    xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
    prvLineNumber(2, (eMenuItem == CTRL_ITEM_ADD) ? "THEM NGUOI ID " : "LAM LAI ID ", pxEvent->usId, "");
    if (pxEvent->usValue == AS608_STEP_PLACE_1) {
        prvLine(3, "DAT NGON TAY");
    } else if (pxEvent->usValue == AS608_STEP_REMOVE) {
        prvLine(3, "NHAC TAY RA");
    } else {
        prvLine(3, "DAT LAI NGON DO");
    }
    prvLine(4, "");
}

/* Ve man hinh cua trang thai menu hien tai (dong 2..6) */
static void prvShowMenu(void)
{
    switch (eState) {
    case CTRL_STATE_MENU_AUTH:
        prvLine(2, "MENU QUAN LY");
        prvShowMask(3, "PIN ADMIN: ", ucPinLen);
        if (ucPinFail > 0) {
            prvLineNumber(4, "SAI PIN ", ucPinFail, "/3");
        } else {
            prvLine(4, "");
        }
        prvLine(5, "");
        prvLine(6, "C: XOA  D: THOAT");
        break;

    case CTRL_STATE_MENU_MAIN:
        prvLine(2, "MENU QUAN LY");
        prvLine(3, "1 THEM    2 XOA");
        prvLine(4, "3 LAM LAI VAN TAY");
        prvLine(5, "4 TRA     5 DOI PIN");
        prvLine(6, "B: THOAT");
        break;

    case CTRL_STATE_MENU_LIST:
        prvShowList();
        break;

    case CTRL_STATE_MENU_CONFIRM:
        prvLine(2, "XOA NGUOI");
        prvLineNumber(3, "XOA ID ", usMenuId, " ?");
        prvLine(4, "XOA VAN TAY VA PIN");
        prvLine(5, "");
        prvLine(6, "#: DONG Y  B: HUY");
        break;

    case CTRL_STATE_MENU_PIN:
        prvLineNumber(2, (eMenuItem == CTRL_ITEM_ADD) ? "DAT PIN ID " : "DOI PIN ID ", usMenuId, "");
        prvShowMask(3, "PIN MOI: ", ucPinLen);
        prvLine(4, "");
        prvLine(5, "");
        prvLine(6, "C: XOA  B: HUY");
        break;

    case CTRL_STATE_MENU_BUSY:
        if (xWaitIndex == pdTRUE) {
            prvLine(2, "DANG DOC DANH SACH");
            prvLine(3, "");
        } else if (eMenuItem == CTRL_ITEM_ADD) {
            prvLine(2, "THEM NGUOI MOI");
            prvLine(3, "CHO CAM BIEN...");
        } else if (eMenuItem == CTRL_ITEM_REENROLL) {
            prvLineNumber(2, "LAM LAI ID ", usMenuId, "");
            prvLine(3, "CHO CAM BIEN...");
        } else if (eMenuItem == CTRL_ITEM_LOOKUP) {
            prvLine(2, "TRA VAN TAY");
            prvLine(3, "DAT NGON TAY");
        } else if (eMenuItem == CTRL_ITEM_UNDO_ADD) {
            prvLine(2, "HUY THEM NGUOI");
            prvLine(3, "DANG XOA VAN TAY...");
        } else {
            prvLineNumber(2, "DANG XOA ID ", usMenuId, "");
            prvLine(3, "");
        }
        prvLine(4, "");
        prvLine(5, "");
        prvLine(6, (xWaitIndex == pdTRUE || eMenuItem == CTRL_ITEM_DELETE ||
                    eMenuItem == CTRL_ITEM_UNDO_ADD) ? "" : "B: HUY");
        break;

    default:
        break;
    }
}

/* Man danh sach: dong 2 tieu de + vi tri, dong 3..5 ba ID, dong 6 huong dan phim.
 *   CHON XOA 2/5
 *     ID 0    CO PIN
 *   > ID 1    CO PIN
 *     ID 3    CHUA PIN        (ID >= 32: "-", khong co cho luu PIN) */
static void prvShowList(void)
{
    char     cBuf[OLED_LINE_LEN + 1];
    uint8_t  ucLen;
    uint8_t  ucRow;
    uint16_t usId;

    if (eMenuItem == CTRL_ITEM_DELETE) {
        ucLen = prvAppendText(cBuf, 0, "CHON XOA ");
    } else if (eMenuItem == CTRL_ITEM_REENROLL) {
        ucLen = prvAppendText(cBuf, 0, "CHON LAM LAI ");
    } else {
        ucLen = prvAppendText(cBuf, 0, "CHON DOI PIN ");
    }
    ucLen = prvAppendNumber(cBuf, ucLen, (uint16_t)(usListPos + 1U));
    ucLen = prvAppendText(cBuf, ucLen, "/");
    ucLen = prvAppendNumber(cBuf, ucLen, usListCount);
    cBuf[ucLen] = '\0';
    prvLine(2, cBuf);

    for (ucRow = 0; ucRow < CTRL_LIST_ROWS; ucRow++) {
        if (prvListIdAt((uint16_t)(usListTop + ucRow), &usId) != pdTRUE) {
            prvLine((uint8_t)(3 + ucRow), "");
            continue;
        }
        ucLen = prvAppendText(cBuf, 0, ((uint16_t)(usListTop + ucRow) == usListPos) ? "> ID " : "  ID ");
        ucLen = prvAppendNumber(cBuf, ucLen, usId);
        while (ucLen < 10U) {
            cBuf[ucLen++] = ' ';            /* canh cot trang thai PIN */
        }
        if (usId >= FLASH_STORE_MAX_PIN) {
            ucLen = prvAppendText(cBuf, ucLen, "-");
        } else if (cPins[usId][0] != FLASH_STORE_PIN_EMPTY) {
            ucLen = prvAppendText(cBuf, ucLen, "CO PIN");
        } else {
            ucLen = prvAppendText(cBuf, ucLen, "CHUA PIN");
        }
        cBuf[ucLen] = '\0';
        prvLine((uint8_t)(3 + ucRow), cBuf);
    }

    prvLine(6, "A/B:CUON #:OK C:LAI");
}

/* Noi chuoi vao cBuf tu vi tri ucLen (toi da OLED_LINE_LEN), tra ve do dai moi */
static uint8_t prvAppendText(char *pcBuf, uint8_t ucLen, const char *pcText)
{
    while (*pcText != '\0' && ucLen < OLED_LINE_LEN) {
        pcBuf[ucLen++] = *pcText++;
    }
    return ucLen;
}

/* Noi so khong dau vao cBuf, tra ve do dai moi */
static uint8_t prvAppendNumber(char *pcBuf, uint8_t ucLen, uint16_t usNumber)
{
    char    cDigits[6];
    uint8_t ucDigits = 0;

    do {
        cDigits[ucDigits++] = (char)('0' + (usNumber % 10U));
        usNumber = (uint16_t)(usNumber / 10U);
    } while (usNumber != 0U);
    while (ucDigits > 0U && ucLen < OLED_LINE_LEN) {
        pcBuf[ucLen++] = cDigits[--ucDigits];
    }
    return ucLen;
}

/* "<pcPrefix>**--": ucLen ky tu da go hien '*', con lai '-' */
static void prvShowMask(uint8_t ucLine, const char *pcPrefix, uint8_t ucLen)
{
    char    cBuf[OLED_LINE_LEN + 1];
    uint8_t ucPos = 0;
    uint8_t i;

    while (*pcPrefix != '\0' && ucPos < OLED_LINE_LEN - CTRL_PIN_LEN) {
        cBuf[ucPos++] = *pcPrefix++;
    }
    for (i = 0; i < CTRL_PIN_LEN; i++) {
        cBuf[ucPos++] = (i < ucLen) ? '*' : '-';
    }
    cBuf[ucPos] = '\0';
    prvLine(ucLine, cBuf);
}

/* ================== Task Comm / ESP32 (buoc 9b) ================== */

static void prvHandleComm(const Comm_Event *pxComm)
{
    switch (pxComm->type) {
    case COMM_EVT_LINK:
        ucLink = pxComm->link;
        if (ucLink == 0U) {
            ucWifi = 0;
        }
        prvShowStatus();
        break;

    case COMM_EVT_STATUS:
        ucWifi = pxComm->wifi;
        prvShowStatus();
        break;

    case COMM_EVT_OTA_PREPARE:
        /* Chi cho cap nhat khi dang ranh: man quet van tay, khong menu / khoa / nhap PIN */
        if (eState == CTRL_STATE_SCAN && pxComm->value > 0UL && pxComm->value <= COMM_APP_BYTES) {
            xTaskAS608SendCommand(AS608_CMD_PAUSE, 0, portMAX_DELAY);
            xTaskServoSendCommand(SERVO_CMD_CLOSE, 0, portMAX_DELAY);
            xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
            eState         = CTRL_STATE_OTA;    /* tu day khong log, khong nhan phim */
            xRedrawPending = pdFALSE;
            prvShowState();
            if (xTaskCommOtaRespond(pxComm->sequence, pdTRUE) != pdPASS) {
                prvBackToScan();            /* khong gui duoc tra loi: hoat dong lai */
            }
        } else {
            (void)xTaskCommOtaRespond(pxComm->sequence, pdFALSE);
        }
        break;

    case COMM_EVT_OTA_ABORT:
        if (eState == CTRL_STATE_OTA) {
            prvBackToScan();                /* RESUME van tay */
            prvLine(2, "HUY CAP NHAT FW");
            prvShowTemp();
        }
        break;

    default:
        break;                              /* NAMES, REJECT, LOG_QUEUE: khong can hien */
    }
}

/* Dong 7: "WIFI OK  CB: 5 MAU" / "WIFI --  CB LOI" / "ESP32 -- CB: 5 MAU" */
static void prvShowStatus(void)
{
    char    cBuf[OLED_LINE_LEN + 1];
    uint8_t ucLen;

    if (ucLink == 0U) {
        ucLen = prvAppendText(cBuf, 0, "ESP32 -- ");
    } else if (ucWifi == 0U) {
        ucLen = prvAppendText(cBuf, 0, "WIFI --  ");
    } else {
        ucLen = prvAppendText(cBuf, 0, "WIFI OK  ");
    }
    if (ucSensorOk == 0U) {
        ucLen = prvAppendText(cBuf, ucLen, "CB LOI");
    } else if (ucSensorOk == 2U) {
        ucLen = prvAppendText(cBuf, ucLen, "CB ..");
    } else {
        ucLen = prvAppendText(cBuf, ucLen, "CB: ");
        ucLen = prvAppendNumber(cBuf, ucLen, usSensorCount);
        ucLen = prvAppendText(cBuf, ucLen, " MAU");
    }
    cBuf[ucLen] = '\0';
    prvLine(7, cBuf);
}

/* Ten nguoi (bang ten tu Google Sheet qua ESP32); chua co ten: "ID n" */
static void prvShowWho(uint8_t ucLine, uint16_t usId)
{
    char cName[COMM_NAME_MAX + 1];

    if (xTaskCommCopyName(usId, cName, sizeof(cName)) == pdTRUE && cName[0] != '\0') {
        prvLine(ucLine, cName);
    } else {
        prvLineNumber(ucLine, "ID ", usId, "");
    }
}

/* Phim 0 o menu chinh (an): heap trong / thap nhat (byte), stack con lai (word) cua vai task */
static void prvShowDiag(void)
{
    char         cBuf[OLED_LINE_LEN + 1];
    uint8_t      ucLen;
    TaskHandle_t xComm = xTaskGetHandle("COMM");
    TaskHandle_t xAs   = xTaskGetHandle("AS608");

    xTaskBuzzerPlay(BUZZER_SOUND_BEEP, 0);
    prvMenuDone();                          /* 3s / phim bat ky -> ve menu */

    ucLen = prvAppendText(cBuf, 0, "HEAP ");
    ucLen = prvAppendNumber(cBuf, ucLen, (uint16_t)xPortGetFreeHeapSize());
    ucLen = prvAppendText(cBuf, ucLen, " MIN ");
    ucLen = prvAppendNumber(cBuf, ucLen, (uint16_t)xPortGetMinimumEverFreeHeapSize());
    cBuf[ucLen] = '\0';
    prvLine(2, cBuf);
    prvLineNumber(3, "STACK CTRL ", (uint16_t)uxTaskGetStackHighWaterMark(NULL), "");
    if (xComm != NULL) {
        prvLineNumber(4, "STACK COMM ", (uint16_t)uxTaskGetStackHighWaterMark(xComm), "");
    } else {
        prvLine(4, "COMM KHONG CHAY");
    }
    if (xAs != NULL) {
        prvLineNumber(5, "STACK AS608 ", (uint16_t)uxTaskGetStackHighWaterMark(xAs), "");
    }
}

/* ================== Man hinh ================== */

/* Ve man hinh cua trang thai hien tai (dong 2..6) */
static void prvShowState(void)
{
    if (eState == CTRL_STATE_OTA) {
        prvLine(2, "DANG CAP NHAT FW");
        prvLine(3, "KHONG TAT NGUON");
        prvLine(4, "");
        prvLine(5, "");
        prvLine(6, "");
        return;
    }
    if (prvIsMenuState() == pdTRUE) {
        prvShowMenu();
        return;
    }
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
