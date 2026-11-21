/**
 * @file comm_uart.c
 * @brief USART1 cho ESP32. Ngắt chỉ chuyển byte và đánh thức Task_Comm.
 */
#include "comm_uart.h"          /* API driver và kiểu TaskHandle_t. */
#include "comm_protocol.h"      /* Giới hạn kích thước một khung TX. */
#include "stm32f10x_gpio.h"      /* Cấu hình PA9/PA10. */
#include "stm32f10x_rcc.h"       /* Cấp clock GPIOA và USART1. */
#include "stm32f10x_usart.h"     /* Hàm SPL điều khiển USART1. */
#include "misc.h"               /* Cấu hình ưu tiên ngắt NVIC. */
#include <string.h>             /* memcpy chép khung sang bộ đệm TX. */

#define RX_SIZE 512U            /* Ring chừa một ô trống, chứa tối đa 511 byte. */
static uint8_t rx[RX_SIZE];      /* ISR ghi, Task_Comm đọc. */
static uint8_t tx[COMM_FRAME_MAX];/* Task_Comm ghi, ISR gửi lần lượt. */
static volatile uint16_t head, tail; /* Vị trí ghi/đọc trong ring RX. */
static volatile uint16_t tx_pos, tx_length; /* Byte TX kế tiếp và tổng chiều dài. */
static volatile uint32_t dropped; /* Số lỗi UART hoặc byte bị bỏ vì RX đầy. */
static TaskHandle_t owner_task;  /* Chỉ đánh thức task sở hữu USART1. */

void CommUart_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);
    GPIO_StructInit(&gpio);      /* Điền các giá trị GPIO mặc định của SPL. */
    gpio.GPIO_Pin = GPIO_Pin_9;  /* PA9 là TX mặc định của USART1. */
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_10; /* PA10 là RX mặc định của USART1. */
    gpio.GPIO_Mode = GPIO_Mode_IPU; /* Giữ RX ở mức nghỉ cao khi đầu kia chưa phát. */
    GPIO_Init(GPIOA, &gpio);
    USART_StructInit(&uart);     /* Mặc định 8 bit, một stop bit, không parity, TX/RX. */
    uart.USART_BaudRate = 115200;
    USART_Init(USART1, &uart);
    USART_Cmd(USART1, ENABLE);   /* Chưa bật IRQ để tránh gọi RTOS trước scheduler. */
}

void CommUart_Start(TaskHandle_t owner)
{
    NVIC_InitTypeDef nvic;
    owner_task = owner;         /* Lưu handle trước khi có thể phát sinh ngắt RX. */
    nvic.NVIC_IRQChannel = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 6; /* Số 6 >= mức syscall 5 của dự án. */
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE); /* Có byte mới thì vào ISR. */
    USART_ITConfig(USART1, USART_IT_ERR, ENABLE);  /* Cho phép xử lý ORE/NE/FE. */
}

int CommUart_Read(uint8_t *byte)
{
    if (byte == NULL || head == tail) { return 0; } /* RX rỗng thì không chờ. */
    *byte = rx[tail];           /* Chép byte trước khi trả ô cho ISR ghi tiếp. */
    __DMB();                   /* Hoàn tất đọc dữ liệu trước khi cập nhật tail. */
    tail = (uint16_t)((tail + 1U) % RX_SIZE);
    return 1;
}

int CommUart_Send(const uint8_t *data, size_t length)
{
    int sent = 0;
    if (data == NULL || length == 0 || length > sizeof(tx)) { return 0; }
    taskENTER_CRITICAL();      /* TXE ISR không được thay trạng thái trong lúc chép. */
    if (tx_length == 0) {      /* Chỉ nhận nguyên khung khi bộ đệm TX đang rảnh. */
        memcpy(tx, data, length);
        tx_pos = 0;
        tx_length = (uint16_t)length;
        USART_ITConfig(USART1, USART_IT_TXE, ENABLE);
        sent = 1;             /* Xác nhận đã nhận khung, chưa phải đã gửi hết trên dây. */
    }
    taskEXIT_CRITICAL();
    return sent;
}

uint32_t CommUart_Dropped(void)
{
    return dropped;           /* Task dùng thay đổi bộ đếm để bỏ parser đang dở. */
}

void USART1_IRQHandler(void)
{
    uint32_t status = USART1->SR; /* Đọc SR trước DR để xóa các cờ lỗi đúng trình tự. */
    uint16_t next;
    uint8_t byte;
    BaseType_t wake = pdFALSE; /* FreeRTOS đặt cờ nếu task cần chạy ngay sau ISR. */
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) {
        byte = (uint8_t)USART1->DR;
        if (status & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) {
            dropped++;       /* Byte có lỗi bị bỏ, không chuyển thành lệnh ứng dụng. */
        } else if (status & USART_SR_RXNE) {
            next = (uint16_t)((head + 1U) % RX_SIZE);
            if (next == tail) {
                dropped++;   /* Không ghi đè byte mà Task_Comm chưa đọc. */
            } else {
                rx[head] = byte;
                __DMB();     /* Công bố byte trước khi công bố head mới. */
                head = next;
            }
        }
        if (owner_task != NULL) { vTaskNotifyGiveFromISR(owner_task, &wake); }
    }
    if (USART_GetITStatus(USART1, USART_IT_TXE) != RESET) {
        if (tx_pos < tx_length) {
            USART_SendData(USART1, tx[tx_pos++]); /* Mỗi ngắt chỉ ghi một byte vào DR. */
        } else {
            USART_ITConfig(USART1, USART_IT_TXE, DISABLE);
            tx_length = 0;   /* DR đã nhận byte cuối; cho phép chép khung kế tiếp. */
            if (owner_task != NULL) { vTaskNotifyGiveFromISR(owner_task, &wake); }
        }
    }
    portEND_SWITCHING_ISR(wake); /* Nhường CPU nếu notification đánh thức task ưu tiên cao. */
}
