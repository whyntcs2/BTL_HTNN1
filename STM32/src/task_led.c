#include "task_led.h"
#include "led.h"

/* ================== Cau hinh ================== */
#define LED_TASK_STACK      128     /* word */
#define LED_OK_MS           500     /* nua chu ky nhay khi binh thuong */
#define LED_ERROR_MS        100     /* nua chu ky nhay khi co task tao loi */

/* ================== Bien noi bo ================== */
static volatile BaseType_t xError = pdFALSE;

static void vTaskLed(void *pvParameters);

/* ================== Ham public ================== */

BaseType_t xTaskLedCreate(UBaseType_t uxPriority)
{
    led_init();
    return xTaskCreate(vTaskLed, "LED", LED_TASK_STACK, NULL, uxPriority, NULL);
}

void vTaskLedReportError(void)
{
    xError = pdTRUE;
}

/* ================== Task ================== */

static void vTaskLed(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        led_toggle();
        vTaskDelay(((xError == pdTRUE) ? LED_ERROR_MS : LED_OK_MS) / portTICK_RATE_MS);
    }
}
