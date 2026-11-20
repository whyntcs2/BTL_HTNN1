#include "task_buzzer.h"
#include "buzzer.h"
#include "task.h"
#include "queue.h"

/* ================== Cau hinh ================== */
#define BUZZER_TASK_STACK       128     /* word */
#define BUZZER_QUEUE_LEN        4       /* so am cho xu ly */
#define BUZZER_PERIOD_MS        10      /* chu ky goi buzzer_process() (= 1 tick) */

/* ================== Bien noi bo ================== */

/* Thu vien buzzer.c can bien dem mili giay nay (extern volatile uint32_t g_ms_ticks).
 * Ban cu tang trong SysTick_Handler; o day SysTick thuoc FreeRTOS nen vTaskBuzzer
 * tu cap nhat tu so tick truoc moi lan goi thu vien. Chi task nay ghi va doc bien. */
volatile uint32_t g_ms_ticks = 0;

static QueueHandle_t xBuzzerQueue = NULL;

static void vTaskBuzzer(void *pvParameters);
static void prvUpdateMs(void);
static void prvStartSound(Buzzer_Sound eSound);

/* ================== Ham public ================== */

BaseType_t xTaskBuzzerCreate(UBaseType_t uxPriority)
{
    /* Chi cau hinh GPIO + AFIO + TIM3, khong goi API FreeRTOS: goi truoc scheduler duoc */
    buzzer_init();

    xBuzzerQueue = xQueueCreate(BUZZER_QUEUE_LEN, sizeof(Buzzer_Sound));
    if (xBuzzerQueue == NULL) {
        return pdFAIL;                       /* het heap */
    }
    return xTaskCreate(vTaskBuzzer, "BUZZER", BUZZER_TASK_STACK, NULL, uxPriority, NULL);
}

BaseType_t xTaskBuzzerPlay(Buzzer_Sound eSound, TickType_t xTicksToWait)
{
    return xQueueSendToBack(xBuzzerQueue, &eSound, xTicksToWait);
}

/* ================== Task ================== */

/* Moi vong: cho lenh toi da 10ms (Blocked), co lenh thi bat dau am moi,
 * roi goi buzzer_process() de chuyen tieng bip / khoang lang cua am dang keu.
 * buzzer_process() chi ghi vai thanh ghi TIM3 nen rat ngan. */
static void vTaskBuzzer(void *pvParameters)
{
    Buzzer_Sound eSound;

    (void)pvParameters;

    for (;;)
    {
        if (xQueueReceive(xBuzzerQueue, &eSound, BUZZER_PERIOD_MS / portTICK_RATE_MS) == pdPASS) {
            prvUpdateMs();                  /* thu vien tinh moc ket thuc tu g_ms_ticks */
            prvStartSound(eSound);
        }

        prvUpdateMs();
        buzzer_process();
    }
}

/* Doi so tick sang mili giay cho thu vien (tick 100Hz: moi tick 10ms) */
static void prvUpdateMs(void)
{
    g_ms_ticks = (uint32_t)xTaskGetTickCount() * (uint32_t)portTICK_RATE_MS;
}

static void prvStartSound(Buzzer_Sound eSound)
{
    switch (eSound) {
    case BUZZER_SOUND_GRANTED: buzzer_access_granted();   break;
    case BUZZER_SOUND_DENIED:  buzzer_access_denied();    break;
    case BUZZER_SOUND_BEEP:    buzzer_enrollment();       break;
    case BUZZER_SOUND_WARNING: buzzer_security_warning(); break;
    case BUZZER_SOUND_STOP:
    default:                   buzzer_idle();             break;
    }
}