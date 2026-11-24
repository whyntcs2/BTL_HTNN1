/* Buoc 9b: ghep task Comm (ESP32, USART1 PA9 TX / PA10 RX 115200) cua ban cung nhom.
 * Buoc 9a: AS608 tren USART3 (PB10 TX, PB11 RX), bo terminal debug.
 *
 * Uu tien:
 *   3: vTaskControl, vTaskKeypad, vTaskServo, vTaskBuzzer
 *   2: vTaskAS608, vTaskComm (khong cho ban; ngat USART1 uu tien 6 + task notification)
 *   1: vTaskOled (cho ban I2C lau), vTaskLed
 *
 * - vTaskLed (uu tien 1, task_led.c): LED PC13 nhay 0,5s = binh thuong; nhay nhanh 0,1s = co task
 *   khong tao duoc (het heap) - khong con terminal de in loi.
 * Sua 24/11: LED tach sang led.c/.h + task_led.c/.h; I2C tach sang i2c.c/.h (oled.c goi). */
#include "stm32f10x.h"
#include "misc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "task_as608.h"
#include "task_keypad.h"
#include "task_servo.h"
#include "task_buzzer.h"
#include "task_oled.h"
#include "task_control.h"
#include "task_comm.h"
#include "task_led.h"

static void prvCheck(BaseType_t xResult);

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* Phan cung dung chung: TIM2 cho timeout UART, USART3 cho AS608, USART1 cho ESP32
     * (ngat USART1 chi bat trong task Comm, sau khi scheduler chay) */
    tim2_cfg();
    uart_cfg();
    vTaskCommInitHardware();

    /* Tao cac task linh kien truoc, task dieu khien sau cung (dung queue cua chung) */
    prvCheck(xTaskAS608Create(2));
    prvCheck(xTaskKeypadCreate(3));
    prvCheck(xTaskServoCreate(3));
    prvCheck(xTaskBuzzerCreate(3));
    prvCheck(xTaskOledCreate(1));
    prvCheck(xTaskCommCreate(2));           /* loi neu trang flash 60/61 (boot ID) co du lieu la */
    prvCheck(xTaskControlCreate(3));
    prvCheck(xTaskLedCreate(1));             /* led_init() nam trong ham nay */

    vTaskStartScheduler();

    for (;;);
}

/* Ghi nho loi tao task (het heap) de LED bao (goi duoc ca truoc khi tao task LED) */
static void prvCheck(BaseType_t xResult)
{
    if (xResult != pdPASS) {
        vTaskLedReportError();
    }
}
