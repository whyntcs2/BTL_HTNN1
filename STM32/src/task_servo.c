#include "task_servo.h"
#include "servo.h"
#include "task.h"
#include "queue.h"

/* ================== Cau hinh ================== */
#define SERVO_TASK_STACK        128     /* word */
#define SERVO_CMD_QUEUE_LEN     4       /* so lenh cho xu ly */

/* ================== Bien noi bo ================== */
static QueueHandle_t xServoQueue = NULL;

static void vTaskServo(void *pvParameters);

/* ================== Ham public ================== */

BaseType_t xTaskServoCreate(UBaseType_t uxPriority)
{
    /* Chi cau hinh GPIO + TIM1, khong goi API FreeRTOS: goi truoc scheduler duoc.
     * Servo_Init() phat san xung dong cua. */
    Servo_Init();

    xServoQueue = xQueueCreate(SERVO_CMD_QUEUE_LEN, sizeof(Servo_Command));
    if (xServoQueue == NULL) {
        return pdFAIL;                       /* het heap */
    }
    return xTaskCreate(vTaskServo, "SERVO", SERVO_TASK_STACK, NULL, uxPriority, NULL);
}

BaseType_t xTaskServoSendCommand(Servo_CmdType eCmd, uint16_t usHoldMs, TickType_t xTicksToWait)
{
    Servo_Command xCmd;

    xCmd.eCmd     = eCmd;
    xCmd.usHoldMs = usHoldMs;
    return xQueueSendToBack(xServoQueue, &xCmd, xTicksToWait);
}

/* ================== Task ================== */

/* Thuat toan giu nguyen theo ban servo_task.c cua nhom:
 * - Cua dong: xQueueReceive(lenh, portMAX_DELAY), task Blocked, khong ton CPU.
 * - Cua mo:   xQueueReceive(lenh, thoi gian giu con lai). Het gio (khong co lenh) thi dong cua.
 *   Dung timeout cua xQueueReceive nen khong can software timer (slide p1, phan queue).
 * - OPEN luc dang mo: tinh lai thoi gian giu tu luc nhan lenh.
 * - CLOSE: dong ngay. */
static void vTaskServo(void *pvParameters)
{
    Servo_Command xCmd;
    BaseType_t    xIsOpen     = pdFALSE;
    TickType_t    xOpenedAt   = 0;      /* tick luc mo cua */
    TickType_t    xHoldTicks  = 0;      /* thoi gian giu, don vi tick */
    TickType_t    xWait;
    TickType_t    xElapsed;
    uint16_t      usHoldMs;

    (void)pvParameters;

    for (;;)
    {
        xWait = portMAX_DELAY;

        if (xIsOpen == pdTRUE) {
            /* Phep tru khong dau van dung khi bo dem tick tran */
            xElapsed = (TickType_t)(xTaskGetTickCount() - xOpenedAt);
            if (xElapsed >= xHoldTicks) {
                Servo_Close();              /* het thoi gian giu: tu dong */
                xIsOpen = pdFALSE;
            } else {
                xWait = xHoldTicks - xElapsed;
            }
        }

        if (xQueueReceive(xServoQueue, &xCmd, xWait) != pdPASS) {
            continue;                       /* het gio: vong sau se dong cua */
        }

        if (xCmd.eCmd == SERVO_CMD_OPEN) {
            usHoldMs = xCmd.usHoldMs;
            if (usHoldMs == 0U) {
                usHoldMs = SERVO_HOLD_MS_DEFAULT;
            }
            xHoldTicks = (TickType_t)usHoldMs / portTICK_RATE_MS;
            if (xHoldTicks == 0U) {
                xHoldTicks = 1U;            /* giu toi thieu 1 tick */
            }
            Servo_Open();
            xOpenedAt = xTaskGetTickCount();
            xIsOpen   = pdTRUE;
        } else if (xCmd.eCmd == SERVO_CMD_CLOSE) {
            Servo_Close();
            xIsOpen = pdFALSE;
        }
    }
}