/* Buoc 8a: task dieu khien - luong mac dinh (quet van tay -> sai 3 lan -> nhap PIN).
 *
 * Uu tien:
 *   3: vTaskControl (dieu khien, KHONG in USART3), vTaskKeypad, vTaskServo, vTaskBuzzer
 *   2: vTaskAS608
 *   1: vTaskOled (cho ban I2C lau), vTaskKey, vTaskLed
 * Task in USART3 phai thap hon task AS608: chi con main() in truoc khi scheduler chay.
 *
 * - vTaskKey (uu tien 1): lenh tu terminal USART3 trong luc chua co menu (buoc 8d)
 *       (ngat RXNE + queue, giong Vi du 14 "Queue trong ISR" trong slide p2):
 *       e = dang ky, c = huy, l = tra, d + so 0..9 = xoa ID do,
 *       f + so 0..9 = lam lai van tay ID do, X (chu hoa) = xoa toan bo.
 * - vTaskLed (uu tien 1): LED PC13 nhay 1s de biet he thong con chay. */
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"
#include "misc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timer.h"
#include "uart.h"
#include "task_as608.h"
#include "task_keypad.h"
#include "task_servo.h"
#include "task_buzzer.h"
#include "task_oled.h"
#include "task_control.h"

#define DEBUG_UART      USART3
#define KEY_QUEUE_LEN   16          /* so phim terminal cho xu ly */
#define USART3_IRQ_PRIO 6           /* >= 5 (configMAX_SYSCALL...): duoc goi ...FromISR */

static QueueHandle_t xDebugKeyQueue = NULL;

void vTaskLed(void *pvParameters);
void vTaskKey(void *pvParameters);
static void prvKeyIrqInit(void);
static void prvCheck(BaseType_t xResult, const char *pcName);

int main(void)
{
    GPIO_InitTypeDef gpio;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* Phan cung dung chung: TIM2 cho timeout UART, USART1 (AS608) + USART3 (debug) */
    tim2_cfg();
    uart_cfg();

    /* LED PC13 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    gpio.GPIO_Pin   = GPIO_Pin_13;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &gpio);

    uart_send_string(DEBUG_UART, "\r\n=== Buoc 8a: task dieu khien ===\r\n");
    uart_send_string(DEBUG_UART, "Terminal: e dang ky | c huy | l tra | d<so> xoa ID | f<so> lam lai ID | X xoa toan bo\r\n");
    uart_send_string(DEBUG_UART, "PIN test: ID0 = 1111, ID1 = 2222, ID2 = 3333\r\n");

    /* Tao cac task linh kien truoc, task dieu khien sau cung (dung queue cua chung) */
    prvCheck(xTaskAS608Create(2),  "AS608");
    prvCheck(xTaskKeypadCreate(3), "keypad");
    prvCheck(xTaskServoCreate(3),  "servo");
    prvCheck(xTaskBuzzerCreate(3), "buzzer");
    prvCheck(xTaskOledCreate(1),   "OLED");
    prvCheck(xTaskControlCreate(3), "dieu khien");

    /* Queue phim terminal: ISR USART3 gui vao, vTaskKey lay ra */
    xDebugKeyQueue = xQueueCreate(KEY_QUEUE_LEN, sizeof(uint8_t));

    xTaskCreate(vTaskKey,  "KEY",  128, NULL, 1, NULL);
    xTaskCreate(vTaskLed,  "LED",  128, NULL, 1, NULL);

    vTaskStartScheduler();

    for (;;);
}

/* In loi tao task (chi goi trong main, truoc khi scheduler chay) */
static void prvCheck(BaseType_t xResult, const char *pcName)
{
    if (xResult != pdPASS) {
        uart_send_string(DEBUG_UART, "Khong tao duoc task ");
        uart_send_string(DEBUG_UART, pcName);
        uart_send_string(DEBUG_UART, "\r\n");
    }
}

/* Bat ngat nhan USART3. Goi trong vTaskKey (scheduler da chay),
 * de ngat khong goi API FreeRTOS truoc khi scheduler khoi dong. */
static void prvKeyIrqInit(void)
{
    NVIC_InitTypeDef nvic;

    nvic.NVIC_IRQChannel                   = USART3_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = USART3_IRQ_PRIO;  /* slide p2 trang 57 */
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    USART_ITConfig(DEBUG_UART, USART_IT_RXNE, ENABLE);
}

/* ISR USART3: moi byte toi la dua ngay vao queue, khong mat byte du gui lien nhau */
void USART3_IRQHandler(void)
{
    uint8_t    ucByte;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (USART_GetFlagStatus(DEBUG_UART, USART_FLAG_RXNE) != RESET) {
        ucByte = (uint8_t)USART_ReceiveData(DEBUG_UART);      /* doc DR: xoa co RXNE */
        xQueueSendToBackFromISR(xDebugKeyQueue, &ucByte, &xHigherPriorityTaskWoken);
    } else if (USART_GetFlagStatus(DEBUG_UART, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(DEBUG_UART);                  /* tran: doc SR roi DR de xoa ORE */
    }

    /* vTaskKey vua duoc danh thuc thi chuyen sang no ngay khi thoat ISR */
    portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

/* Nhan phim tu queue (Blocked toi khi co phim, khong ton CPU).
 * 'd' va 'f' can them 1 phim so (0..9) la ID.
 * Task nay khong in gi: ket qua hien tren OLED (task dieu khien).
 * Khong con lenh p / r: PAUSE / RESUME van tay do task dieu khien quyet dinh. */
void vTaskKey(void *pvParameters)
{
    uint8_t ucKey;
    uint8_t ucPending = 0;          /* 'd' hoac 'f' dang cho phim so */

    (void)pvParameters;

    prvKeyIrqInit();

    for (;;)
    {
        if (xQueueReceive(xDebugKeyQueue, &ucKey, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (ucPending != 0) {
            if (ucKey >= '0' && ucKey <= '9') {
                if (ucPending == 'd') {
                    xTaskAS608SendCommand(AS608_CMD_DELETE_ID, (uint16_t)(ucKey - '0'), portMAX_DELAY);
                } else {
                    xTaskAS608SendCommand(AS608_CMD_REENROLL, (uint16_t)(ucKey - '0'), portMAX_DELAY);
                }
            }
            ucPending = 0;                  /* phim khac so: bo lenh */
        } else {
            switch (ucKey) {
            case 'e': xTaskAS608SendCommand(AS608_CMD_ENROLL, 0, portMAX_DELAY); break;
            case 'c': xTaskAS608SendCommand(AS608_CMD_CANCEL, 0, portMAX_DELAY); break;
            case 'l': xTaskAS608SendCommand(AS608_CMD_LOOKUP, 0, portMAX_DELAY); break;
            case 'X': xTaskAS608SendCommand(AS608_CMD_EMPTY,  0, portMAX_DELAY); break;
            case 'd':
            case 'f': ucPending = ucKey; break;
            default:  break;                /* ky tu khac (vd \r \n): bo qua */
            }
        }
    }
}

void vTaskLed(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        GPIOC->ODR ^= GPIO_Pin_13;
        vTaskDelay(500 / portTICK_RATE_MS);
    }
}