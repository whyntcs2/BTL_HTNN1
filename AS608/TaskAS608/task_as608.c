#include "task_as608.h"
#include "as608.h"
#include "task.h"
#include "queue.h"

/* ================== Cau hinh ================== */
#define AS608_TASK_STACK        256     /* word */
#define AS608_CMD_QUEUE_LEN     4       /* so lenh cho xu ly */
#define AS608_EVT_QUEUE_LEN     8       /* so su kien toi da cho task khac doc */
#define AS608_BOOT_DELAY_MS     300     /* AS608 can khoang 200ms de khoi dong */
#define AS608_RETRY             3       /* so lan thu khi loi giao tiep */
#define AS608_RETRY_DELAY_MS    100
#define AS608_SCAN_PERIOD_MS    250     /* khong co tay: nghi bao lau roi quet lai */
#define AS608_POLL_MS           100     /* chu ky GenImg khi cho nhac tay */
#define AS608_REMOVE_TIMEOUT_MS 5000    /* cho nhac tay toi da */
#define AS608_ERROR_BACKOFF_MS  1000    /* mat giao tiep: nghi lau hon roi thu lai */
#define AS608_ENROLL_TIMEOUT_MS 10000   /* dang ky: moi buoc cho dat / nhac tay toi da */

/* 1: xoa TOAN BO thu vien van tay moi lan khoi dong.
 * Chi bat 1 lan de xoa mau trung, chay xong DOI LAI THANH 0. */
#define AS608_CLEAR_ON_BOOT     0

/* Trang thai cua task */
typedef enum {
    AS608_STATE_SCAN = 0,   /* mac dinh: quet van tay de mo cua */
    AS608_STATE_PAUSED      /* dung quet, chi cho lenh */
} AS608_State;

/* Ket qua cac ham cho trong luc dang ky (chi dung trong file nay) */
typedef enum {
    AS608_WAIT_OK = 0,      /* xong viec can cho */
    AS608_WAIT_TIMEOUT,     /* het gio */
    AS608_WAIT_CANCEL,      /* nhan lenh CANCEL */
    AS608_WAIT_COMM         /* loi giao tiep */
} AS608_WaitResult;

/* ================== Bien noi bo ================== */
static QueueHandle_t xCmdQueue   = NULL;
static QueueHandle_t xEventQueue = NULL;

static void          vTaskAS608(void *pvParameters);
static void          prvSendEvent(AS608_EventType eType, uint16_t usId, uint16_t usValue);
static void          prvStartup(void);
static IdxTbl_Status prvCountTemplates(uint16_t *pusCount);
static void          prvHandleCommand(const AS608_Command *pxCmd, AS608_State *peState);
static void          prvScanOnce(AS608_State *peState);
static void          prvWaitRemove(void);
static BaseType_t    prvCheckCancel(uint16_t usWaitMs);
static AS608_WaitResult prvCapture(uint8_t ucBuffer);
static AS608_WaitResult prvWaitRemoveCancel(void);
static void          prvEnrollEnd(AS608_WaitResult eWait);
static void          prvEnrollTo(uint16_t usTargetId, uint8_t ucIsReenroll);
static void          prvEnroll(void);
static void          prvReenroll(uint16_t usId);
static void          prvLookup(void);
static void          prvDeleteId(uint16_t usId);
static void          prvEmpty(void);

/* ================== Ham public ================== */

BaseType_t xTaskAS608Create(UBaseType_t uxPriority)
{
    xCmdQueue   = xQueueCreate(AS608_CMD_QUEUE_LEN, sizeof(AS608_Command));
    xEventQueue = xQueueCreate(AS608_EVT_QUEUE_LEN, sizeof(AS608_Event));
    if (xCmdQueue == NULL || xEventQueue == NULL) {
        return pdFAIL;                       /* het heap */
    }
    return xTaskCreate(vTaskAS608, "AS608", AS608_TASK_STACK, NULL, uxPriority, NULL);
}

BaseType_t xTaskAS608SendCommand(AS608_CmdType eCmd, uint16_t usId, TickType_t xTicksToWait)
{
    AS608_Command xCmd;

    xCmd.eCmd = eCmd;
    xCmd.usId = usId;
    if (eCmd == AS608_CMD_CANCEL) {
        /* Huy can xu ly ngay: chen len dau queue, truoc cac lenh dang cho */
        return xQueueSendToFront(xCmdQueue, &xCmd, xTicksToWait);
    }
    return xQueueSendToBack(xCmdQueue, &xCmd, xTicksToWait);
}

BaseType_t xTaskAS608GetEvent(AS608_Event *pxEvent, TickType_t xTicksToWait)
{
    return xQueueReceive(xEventQueue, pxEvent, xTicksToWait);
}

/* ================== Ham noi bo ================== */

/* Dong goi su kien vao struct roi gui vao cuoi queue.
 * Queue day thi bo su kien (khong cho), de task AS608 khong bi treo. */
static void prvSendEvent(AS608_EventType eType, uint16_t usId, uint16_t usValue)
{
    AS608_Event xEvent;

    xEvent.eType   = eType;
    xEvent.usId    = usId;
    xEvent.usValue = usValue;
    xQueueSendToBack(xEventQueue, &xEvent, 0);
}

/* Doc bang chi muc tung trang, cong so bit 1 = so mau dang luu */
static IdxTbl_Status prvCountTemplates(uint16_t *pusCount)
{
    uint8_t       ucTable[32];
    uint8_t       ucPage;
    uint16_t      usTotal = 0;
    IdxTbl_Status eStatus;

    for (ucPage = 0; ucPage < AS608_IDX_PAGES; ucPage++) {
        eStatus = as608_ReadIndexTable(ucPage, ucTable);
        if (eStatus != IdxTbl_OK) {
            return eStatus;
        }
        usTotal = (uint16_t)(usTotal + idx_count(ucTable));
    }
    *pusCount = usTotal;
    return IdxTbl_OK;
}

/* Khoi dong module (buoc 1): cho, chon UART, [xoa thu vien], dem mau, bao READY/ERROR */
static void prvStartup(void)
{
    IdxTbl_Status eIdx = IdxTbl_COMM_ERR;
    uint16_t      usCount = 0;
    uint8_t       ucTry;
#if AS608_CLEAR_ON_BOOT
    Empty_Status  eEmpty = Empty_COMM_ERR;
#endif

    vTaskDelay(AS608_BOOT_DELAY_MS / portTICK_RATE_MS);
    as608_init(USART1);

#if AS608_CLEAR_ON_BOOT
    for (ucTry = 0; ucTry < AS608_RETRY; ucTry++) {
        eEmpty = as608_Empty();
        if (eEmpty == Empty_OK) {
            break;
        }
        vTaskDelay(AS608_RETRY_DELAY_MS / portTICK_RATE_MS);
    }
    if (eEmpty != Empty_OK) {
        prvSendEvent(AS608_EVT_ERROR, 0, (uint16_t)eEmpty);
    }
#endif

    for (ucTry = 0; ucTry < AS608_RETRY; ucTry++) {
        eIdx = prvCountTemplates(&usCount);
        if (eIdx == IdxTbl_OK) {
            break;
        }
        vTaskDelay(AS608_RETRY_DELAY_MS / portTICK_RATE_MS);
    }

    if (eIdx == IdxTbl_OK) {
        prvSendEvent(AS608_EVT_READY, usCount, 0);
    } else {
        prvSendEvent(AS608_EVT_ERROR, 0, (uint16_t)eIdx);
    }
}

/* Xu ly 1 lenh nhan tu queue, doi trang thai va bao lai */
static void prvHandleCommand(const AS608_Command *pxCmd, AS608_State *peState)
{
    switch (pxCmd->eCmd) {
    case AS608_CMD_PAUSE:
        *peState = AS608_STATE_PAUSED;
        prvSendEvent(AS608_EVT_PAUSED, 0, 0);
        break;
    case AS608_CMD_RESUME:
        *peState = AS608_STATE_SCAN;
        prvSendEvent(AS608_EVT_RESUMED, 0, 0);
        break;
    case AS608_CMD_ENROLL:
        prvEnroll();                         /* xong thi quay ve trang thai cu */
        break;
    case AS608_CMD_CANCEL:
        break;                               /* khong co gi dang chay de huy */
    case AS608_CMD_REENROLL:
        prvReenroll(pxCmd->usId);
        break;
    case AS608_CMD_DELETE_ID:
        prvDeleteId(pxCmd->usId);
        break;
    case AS608_CMD_LOOKUP:
        prvLookup();
        break;
    case AS608_CMD_EMPTY:
        prvEmpty();
        break;
    default:
        break;                               /* lenh la: bo qua */
    }
}

/* Cho nhac tay: GenImg moi 100ms, toi da 5s. Trong luc cho task Blocked, khong ton CPU */
static void prvWaitRemove(void)
{
    uint16_t      usElapsed = 0;
    GenImg_Status eImg;

    while (usElapsed < AS608_REMOVE_TIMEOUT_MS) {
        eImg = as608_GenImg();
        if (eImg == GenImg_NO_FINGER || eImg == GenImg_COMM_ERR || eImg == GenImg_PKT_ERR) {
            return;                          /* da nhac tay, hoac loi thi thoi cho */
        }
        vTaskDelay(AS608_POLL_MS / portTICK_RATE_MS);
        usElapsed = (uint16_t)(usElapsed + AS608_POLL_MS);
    }
}

/* Quet 1 lan o trang thai SCAN */
static void prvScanOnce(AS608_State *peState)
{
    static uint8_t  ucCommErr  = 0;          /* so lan loi giao tiep lien tiep */
    static uint8_t  ucReported = 0;          /* da bao ERROR cho dot loi nay chua */
    AS608_Command   xCmd;
    Identify_Status eId;
    uint16_t        usId = 0;
    uint16_t        usScore = 0;

    eId = as608_identify(&usId, &usScore);

    if (eId != ID_COMM_ERR) {
        ucCommErr  = 0;                      /* module tra loi binh thuong */
        ucReported = 0;
    }

    switch (eId) {
    case ID_NO_FINGER:
        /* Khong co tay: nghi 250ms, nhung co lenh thi xu ly ngay */
        if (xQueueReceive(xCmdQueue, &xCmd, AS608_SCAN_PERIOD_MS / portTICK_RATE_MS) == pdPASS) {
            prvHandleCommand(&xCmd, peState);
        }
        break;

    case ID_OK:
        prvSendEvent(AS608_EVT_MATCH, usId, usScore);
        prvWaitRemove();
        break;

    case ID_NOT_FOUND:
        prvSendEvent(AS608_EVT_NO_MATCH, 0, 0);
        prvWaitRemove();
        break;

    case ID_BAD_IMAGE:
        prvSendEvent(AS608_EVT_BAD_IMAGE, 0, 0);
        prvWaitRemove();
        break;

    default:                                 /* ID_COMM_ERR */
        ucCommErr++;
        if (ucCommErr >= AS608_RETRY && ucReported == 0) {
            prvSendEvent(AS608_EVT_ERROR, 0, 0xFF);
            ucReported = 1;                  /* chi bao 1 lan cho moi dot loi */
        }
        vTaskDelay(((ucCommErr >= AS608_RETRY) ? AS608_ERROR_BACKOFF_MS : AS608_RETRY_DELAY_MS)
                   / portTICK_RATE_MS);
        break;
    }
}

/* ================== Lenh quan ly: dang ky, tra, xoa ================== */

/* Cho toi da usWaitMs, trong luc cho xem co CANCEL khong (xQueuePeek: xem ma khong lay).
 * - CANCEL o dau queue: lay ra, tra ve pdTRUE.
 * - Lenh khac: de nguyen trong queue (xu ly sau khi dang ky xong), nghi het usWaitMs.
 * - Khong co lenh: xQueuePeek tu Blocked het usWaitMs. */
static BaseType_t prvCheckCancel(uint16_t usWaitMs)
{
    AS608_Command xCmd;

    if (xQueuePeek(xCmdQueue, &xCmd, usWaitMs / portTICK_RATE_MS) == pdPASS) {
        if (xCmd.eCmd == AS608_CMD_CANCEL) {
            xQueueReceive(xCmdQueue, &xCmd, 0);
            return pdTRUE;
        }
        vTaskDelay(usWaitMs / portTICK_RATE_MS);
    }
    return pdFALSE;
}

/* Cho dat tay, chup anh va tao dac trung vao ucBuffer (1 hoac 2).
 * Anh xau: bao BAD_IMAGE 1 lan roi cho dat lai, trong cung khoang thoi gian. */
static AS608_WaitResult prvCapture(uint8_t ucBuffer)
{
    uint16_t      usElapsed = 0;           /* thoi gian xap xi, tinh theo chu ky 100ms */
    uint8_t       ucBadSent = 0;
    GenImg_Status eImg;
    Img2Tz_Status eTz;

    for (;;) {
        eImg = as608_GenImg();
        if (eImg == GenImg_OK) {
            eTz = as608_Img2Tz(ucBuffer);
            if (eTz == Img2Tz_OK) {
                return AS608_WAIT_OK;
            }
            if (eTz == Img2Tz_COMM_ERR || eTz == Img2Tz_PKT_ERR) {
                return AS608_WAIT_COMM;
            }
            eImg = GenImg_IMG_FAIL;            /* dac trung xau: coi nhu anh xau */
        }

        if (eImg == GenImg_IMG_FAIL) {
            if (ucBadSent == 0) {
                prvSendEvent(AS608_EVT_BAD_IMAGE, 0, 0);
                ucBadSent = 1;                 /* khong bao lien tuc moi 100ms */
            }
        } else if (eImg != GenImg_NO_FINGER) {
            return AS608_WAIT_COMM;            /* PKT_ERR hoac COMM_ERR */
        }

        if (usElapsed >= AS608_ENROLL_TIMEOUT_MS) {
            return AS608_WAIT_TIMEOUT;
        }
        if (prvCheckCancel(AS608_POLL_MS) == pdTRUE) {
            return AS608_WAIT_CANCEL;
        }
        usElapsed = (uint16_t)(usElapsed + AS608_POLL_MS);
    }
}

/* Cho nhac tay giua 2 lan chup, co timeout va nghe CANCEL */
static AS608_WaitResult prvWaitRemoveCancel(void)
{
    uint16_t      usElapsed = 0;
    GenImg_Status eImg;

    for (;;) {
        eImg = as608_GenImg();
        if (eImg == GenImg_NO_FINGER) {
            return AS608_WAIT_OK;
        }
        if (eImg == GenImg_COMM_ERR || eImg == GenImg_PKT_ERR) {
            return AS608_WAIT_COMM;
        }
        if (usElapsed >= AS608_ENROLL_TIMEOUT_MS) {
            return AS608_WAIT_TIMEOUT;
        }
        if (prvCheckCancel(AS608_POLL_MS) == pdTRUE) {
            return AS608_WAIT_CANCEL;
        }
        usElapsed = (uint16_t)(usElapsed + AS608_POLL_MS);
    }
}

/* Ket thuc dang ky giua chung: bao ly do roi cho nhac tay */
static void prvEnrollEnd(AS608_WaitResult eWait)
{
    if (eWait == AS608_WAIT_CANCEL) {
        prvSendEvent(AS608_EVT_CANCELLED, 0, 0);
    } else if (eWait == AS608_WAIT_TIMEOUT) {
        prvSendEvent(AS608_EVT_ENROLL_FAIL, 0, AS608_FAIL_TIMEOUT);
    } else {
        prvSendEvent(AS608_EVT_ENROLL_FAIL, 0, AS608_FAIL_COMM);
    }
    prvWaitRemove();
}

/* Dang ky vao usTargetId, co kiem tra trung.
 * chup lan 1 (buffer 1) -> Search trung -> nhac tay -> chup lan 2 (buffer 2)
 * -> ghep mau + luu (as608_enroll_save).
 * ucIsReenroll = 1 (lam lai van tay): ngon khop CHINH usTargetId thi cho qua,
 * khop ID khac moi bao DUPLICATE. Store ghi de o cu nen khong can xoa truoc:
 * dang ky hong thi mau cu van con nguyen. */
static void prvEnrollTo(uint16_t usTargetId, uint8_t ucIsReenroll)
{
    Search_Status    eSearch;
    Enroll_Status    eSave;
    AS608_WaitResult eWait;
    uint16_t         usDupId = 0;
    uint16_t         usScore = 0;

    /* Buoc 1: chup lan 1 vao buffer 1 */
    prvSendEvent(AS608_EVT_ENROLL_STEP, usTargetId, AS608_STEP_PLACE_1);
    eWait = prvCapture(1);
    if (eWait != AS608_WAIT_OK) {
        prvEnrollEnd(eWait);
        return;
    }

    /* Buoc 2: kiem tra trung - tim mau buffer 1 trong thu vien */
    eSearch = as608_Search(1, 0, AS608_LIB_SIZE, &usDupId, &usScore);
    if (eSearch == Search_OK) {
        if (!(ucIsReenroll && usDupId == usTargetId)) {
            prvSendEvent(AS608_EVT_DUPLICATE, usDupId, usScore);
            prvWaitRemove();
            return;
        }
        /* lam lai van tay va ngon khop chinh ID do: cho qua */
    } else if (eSearch != Search_NO_MATCH) {
        prvEnrollEnd(AS608_WAIT_COMM);
        return;
    }

    /* Buoc 3: cho nhac tay */
    prvSendEvent(AS608_EVT_ENROLL_STEP, usTargetId, AS608_STEP_REMOVE);
    eWait = prvWaitRemoveCancel();
    if (eWait != AS608_WAIT_OK) {
        prvEnrollEnd(eWait);
        return;
    }

    /* Buoc 4: chup lan 2 vao buffer 2 */
    prvSendEvent(AS608_EVT_ENROLL_STEP, usTargetId, AS608_STEP_PLACE_2);
    eWait = prvCapture(2);
    if (eWait != AS608_WAIT_OK) {
        prvEnrollEnd(eWait);
        return;
    }

    /* Buoc 5: ghep 2 lan chup thanh 1 mau va luu vao usTargetId */
    eSave = as608_enroll_save(usTargetId);
    if (eSave == ENROLL_OK) {
        prvSendEvent(AS608_EVT_ENROLL_OK, usTargetId, 0);
    } else if (eSave == ENROLL_MISMATCH) {
        prvSendEvent(AS608_EVT_ENROLL_FAIL, 0, AS608_FAIL_MISMATCH);
    } else if (eSave == ENROLL_ID_INVALID) {
        prvSendEvent(AS608_EVT_ENROLL_FAIL, 0, AS608_FAIL_INVALID_ID);
    } else if (eSave == ENROLL_FLASH_ERR) {
        prvSendEvent(AS608_EVT_ENROLL_FAIL, 0, AS608_FAIL_FLASH);
    } else {
        prvSendEvent(AS608_EVT_ENROLL_FAIL, 0, AS608_FAIL_COMM);
    }

    /* Cho nhac tay de vong quet khong MATCH (mo cua) ngay sau khi dang ky */
    prvWaitRemove();
}

/* ENROLL: dang ky vao ID trong dau tien */
static void prvEnroll(void)
{
    FreeId_Status eFree;
    uint16_t      usNewId = 0;

    eFree = as608_find_free_id(&usNewId);
    if (eFree == FREE_FULL) {
        prvSendEvent(AS608_EVT_ENROLL_FAIL, 0, AS608_FAIL_FULL);
        return;
    }
    if (eFree != FREE_OK) {
        prvSendEvent(AS608_EVT_ENROLL_FAIL, 0, AS608_FAIL_COMM);
        return;
    }
    prvEnrollTo(usNewId, 0);
}

/* REENROLL: xoa van tay de lam lai = dang ky lai vao dung ID cu */
static void prvReenroll(uint16_t usId)
{
    if (usId >= AS608_LIB_SIZE) {
        prvSendEvent(AS608_EVT_ENROLL_FAIL, usId, AS608_FAIL_INVALID_ID);
        return;
    }
    prvEnrollTo(usId, 1);
}

/* LOOKUP: dat tay, bao ngon nay la ID may. Chi de tra cuu, khong mo cua */
static void prvLookup(void)
{
    AS608_WaitResult eWait;
    Search_Status    eSearch;
    uint16_t         usId = 0;
    uint16_t         usScore = 0;

    eWait = prvCapture(1);
    if (eWait == AS608_WAIT_CANCEL) {
        prvSendEvent(AS608_EVT_CANCELLED, 0, 0);
        return;
    }
    if (eWait == AS608_WAIT_TIMEOUT) {
        prvSendEvent(AS608_EVT_TIMEOUT, 0, 0);
        return;
    }
    if (eWait != AS608_WAIT_OK) {
        prvSendEvent(AS608_EVT_ERROR, 0, 0xFF);
        return;
    }

    eSearch = as608_Search(1, 0, AS608_LIB_SIZE, &usId, &usScore);
    if (eSearch == Search_OK) {
        prvSendEvent(AS608_EVT_LOOKUP_RESULT, usId, usScore);
    } else if (eSearch == Search_NO_MATCH) {
        prvSendEvent(AS608_EVT_NOT_FOUND, 0, 0);
    } else {
        prvSendEvent(AS608_EVT_ERROR, 0, (uint16_t)eSearch);
    }
    prvWaitRemove();
}

/* DELETE_ID: xoa nguoi, khong can dat tay */
static void prvDeleteId(uint16_t usId)
{
    Delete_Status eDel;

    if (usId >= AS608_LIB_SIZE) {
        prvSendEvent(AS608_EVT_ERROR, usId, (uint16_t)Delete_FAIL);
        return;
    }
    eDel = as608_DeletChar(usId, 1);
    if (eDel == Delete_OK) {
        prvSendEvent(AS608_EVT_DELETED, usId, 0);
    } else {
        prvSendEvent(AS608_EVT_ERROR, usId, (uint16_t)eDel);
    }
}

/* EMPTY: xoa toan bo thu vien */
static void prvEmpty(void)
{
    Empty_Status eEmpty;

    eEmpty = as608_Empty();
    if (eEmpty == Empty_OK) {
        prvSendEvent(AS608_EVT_DELETED, AS608_ID_ALL, 0);
    } else {
        prvSendEvent(AS608_EVT_ERROR, AS608_ID_ALL, (uint16_t)eEmpty);
    }
}

/* ================== Than task ================== */

static void vTaskAS608(void *pvParameters)
{
    AS608_State   eState = AS608_STATE_SCAN;
    AS608_Command xCmd;

    (void)pvParameters;

    prvStartup();

    for (;;)
    {
        if (eState == AS608_STATE_PAUSED) {
            /* Dang dung: Blocked cho toi khi co lenh, khong goi AS608 */
            if (xQueueReceive(xCmdQueue, &xCmd, portMAX_DELAY) == pdPASS) {
                prvHandleCommand(&xCmd, &eState);
            }
        } else {
            /* Dang quet: xu ly lenh dang cho (neu co) TRUOC khi quet,
             * de PAUSE gui ngay sau NO_MATCH lan 3 khong bi lo them 1 lan quet */
            if (xQueueReceive(xCmdQueue, &xCmd, 0) == pdPASS) {
                prvHandleCommand(&xCmd, &eState);
            } else {
                prvScanOnce(&eState);
            }
        }
    }
}