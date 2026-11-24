#include "task_oled.h"
#include "oled.h"
#include "task.h"
#include "queue.h"

/* ================== Cau hinh ================== */
#define OLED_TASK_STACK         128     /* word */
#define OLED_QUEUE_LEN          8       /* du cho 1 man hinh 8 dong */
#define OLED_BOOT_DELAY_MS      100     /* SSD1306 can thoi gian on dinh nguon truoc khi khoi tao */

/* ================== Bien noi bo ================== */
static QueueHandle_t xOledQueue = NULL;

static void vTaskOled(void *pvParameters);

/* ================== Ham public ================== */

BaseType_t xTaskOledCreate(UBaseType_t uxPriority)
{
    xOledQueue = xQueueCreate(OLED_QUEUE_LEN, sizeof(Oled_Message));
    if (xOledQueue == NULL) {
        return pdFAIL;                       /* het heap */
    }
    return xTaskCreate(vTaskOled, "OLED", OLED_TASK_STACK, NULL, uxPriority, NULL);
}

BaseType_t xTaskOledPrint(uint8_t ucLine, uint8_t ucInverse, const char *pcText, TickType_t xTicksToWait)
{
    Oled_Message xMsg;
    uint8_t      ucLen = 0;

    if (ucLine >= OLED_LINES) {
        return pdFAIL;                       /* dong khong hop le */
    }

    xMsg.ucLine    = ucLine;
    xMsg.ucInverse = ucInverse;
    while (pcText[ucLen] != '\0' && ucLen < OLED_LINE_LEN) {
        xMsg.cText[ucLen] = pcText[ucLen];
        ucLen++;
    }
    xMsg.cText[ucLen] = '\0';

    return xQueueSendToBack(xOledQueue, &xMsg, xTicksToWait);
}

BaseType_t xTaskOledClear(TickType_t xTicksToWait)
{
    Oled_Message xMsg;

    xMsg.ucLine    = OLED_LINE_CLEAR;
    xMsg.ucInverse = 0;
    xMsg.cText[0]  = '\0';

    return xQueueSendToBack(xOledQueue, &xMsg, xTicksToWait);
}

/* ================== Task ================== */

/* Gatekeeper (slide p2, Vi du 16): phan lon thoi gian Blocked cho queue,
 * nhan duoc dong nao thi ve dong do. Uu tien thap nhat: luc ve (cho ban I2C)
 * cac task AS608 / keypad / servo / buzzer van chen ngang duoc; master I2C chi
 * giu SCL muc thap cho toi khi task ve tiep nen bi chen ngang khong hong du lieu. */
static void vTaskOled(void *pvParameters)
{
    Oled_Message xMsg;

    (void)pvParameters;

    vTaskDelay(OLED_BOOT_DELAY_MS / portTICK_RATE_MS);
    oled_init();                            /* cau hinh I2C1 + SSD1306, xoa man hinh */

    for (;;)
    {
        if (xQueueReceive(xOledQueue, &xMsg, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (xMsg.ucLine == OLED_LINE_CLEAR) {
            oled_clear();
        } else {
            oled_write_line(xMsg.ucLine, xMsg.cText, xMsg.ucInverse);
        }
    }
}