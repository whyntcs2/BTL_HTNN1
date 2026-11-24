#include "task_keypad.h"
#include "keypad.h"
#include "task.h"
#include "queue.h"

/* ================== Cau hinh ================== */
#define KEYPAD_TASK_STACK       128     /* word */
#define KEYPAD_QUEUE_LEN        8       /* so phim cho task khac doc */
#define KEYPAD_SCAN_MS          20      /* chu ky quet ma tran */
#define KEYPAD_DEBOUNCE_MS      30      /* trang thai phai on dinh bao lau moi nhan */

/* ================== Bien noi bo ================== */
static QueueHandle_t xKeyQueue = NULL;

static void vTaskKeypad(void *pvParameters);

/* ================== Ham public ================== */

BaseType_t xTaskKeypadCreate(UBaseType_t uxPriority)
{
    /* Chi cau hinh GPIO, khong goi API FreeRTOS: goi truoc scheduler duoc */
    Keypad_Init();

    xKeyQueue = xQueueCreate(KEYPAD_QUEUE_LEN, sizeof(char));
    if (xKeyQueue == NULL) {
        return pdFAIL;                       /* het heap */
    }
    return xTaskCreate(vTaskKeypad, "KEYPAD", KEYPAD_TASK_STACK, NULL, uxPriority, NULL);
}

BaseType_t xTaskKeypadGetKey(char *pcKey, TickType_t xTicksToWait)
{
    return xQueueReceive(xKeyQueue, pcKey, xTicksToWait);
}

/* ================== Task ================== */

/* Thuat toan chong doi giu nguyen theo ban keypad_task.c cua nhom:
 * - Ket qua quet (cRaw) doi khac cCandidate thi bat dau do lai thoi gian on dinh.
 * - cCandidate giu du KEYPAD_DEBOUNCE_MS:
 *     = 0 (nha het)  -> cho phep nhan lan bam tiep (xArmed = pdTRUE)
 *     = 1 phim       -> gui 1 lan vao queue roi khoa lai (giu phim khong lap)
 * - Nhan chong nhieu phim: khoa lai, phai nha het moi nhan tiep.
 * - Queue day: chua khoa, lan quet sau thu gui lai neu van con giu phim.
 * Chu ky quet dung vTaskDelayUntil() de khong troi (slide p1, Vi du 5). */
static void vTaskKeypad(void *pvParameters)
{
    char       cRaw;
    char       cCandidate = 0;              /* 0: chua co phim */
    BaseType_t xArmed     = pdTRUE;         /* pdTRUE: duoc gui phim cua lan bam nay */
    TickType_t xLastWakeTime;
    TickType_t xCandidateSince;
    TickType_t xNow;

    (void)pvParameters;

    xLastWakeTime   = xTaskGetTickCount();
    xCandidateSince = xLastWakeTime;

    for (;;)
    {
        cRaw = Keypad_Scan();               /* ky tu, 0 hoac KEYPAD_MULTIPLE_KEYS */
        xNow = xTaskGetTickCount();

        if (cRaw == KEYPAD_MULTIPLE_KEYS) {
            /* Nhan chong phim: bo, cho nha het */
            xArmed          = pdFALSE;
            cCandidate      = cRaw;
            xCandidateSince = xNow;
        } else {
            if (cRaw != cCandidate) {
                /* Trang thai vua doi (nhan, nha hoac doi tiep diem): do lai tu dau */
                cCandidate      = cRaw;
                xCandidateSince = xNow;
            }

            /* Phep tru khong dau van dung khi bo dem tick tran */
            if ((TickType_t)(xNow - xCandidateSince) >= (KEYPAD_DEBOUNCE_MS / portTICK_RATE_MS)) {
                if (cCandidate == 0) {
                    xArmed = pdTRUE;        /* da nha on dinh: cho lan bam tiep */
                } else if (xArmed == pdTRUE) {
                    /* Khong cho (0 tick): task keypad khong bao gio bi chan o day */
                    if (xQueueSendToBack(xKeyQueue, &cCandidate, 0) == pdPASS) {
                        xArmed = pdFALSE;   /* da gui: giu phim khong ra them */
                    }
                }
            }
        }

        vTaskDelayUntil(&xLastWakeTime, KEYPAD_SCAN_MS / portTICK_RATE_MS);
    }
}