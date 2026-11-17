#include "uart.h"
#include "timer.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

void uart_cfg(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;

    /* ---- USART1: AS608 ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_9;               /* PA9: TX */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin   = GPIO_Pin_10;              /* PA10: RX, keo len chong byte rac */
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = 57600;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &usart);

    /* ---- USART3: debug ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_10;              /* PB10: TX */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin   = GPIO_Pin_11;              /* PB11: RX */
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);

    usart.USART_BaudRate = 115200;              /* cac thong so khac giu nguyen */
    USART_Init(USART3, &usart);

    USART_Cmd(USART1, ENABLE);
    USART_Cmd(USART3, ENABLE);
}

void uart_send_byte(USART_TypeDef *usart, uint8_t data)
{
    while (USART_GetFlagStatus(usart, USART_FLAG_TXE) == RESET) {}
    USART_SendData(usart, data);
}

void uart_send_string(USART_TypeDef *usart, const char *s)
{
    while (*s) {
        uart_send_byte(usart, (uint8_t)*s++);
    }
}

void uart_send_hex(USART_TypeDef *usart, uint8_t b)
{
    const char hex[] = "0123456789ABCDEF";
    uart_send_byte(usart, (uint8_t)hex[b >> 4]);
    uart_send_byte(usart, (uint8_t)hex[b & 0x0F]);
}

UART_Status uart_recv_byte(USART_TypeDef *usart, uint8_t *data, uint32_t timeout_ms)
{
    uint16_t start = timer_now_us();
    uint32_t elapsed_ms = 0;

    while (1) {
        if (USART_GetFlagStatus(usart, USART_FLAG_RXNE) != RESET) {
            *data = (uint8_t)USART_ReceiveData(usart);
            return UART_OKE;
        }

        if ((uint16_t)(timer_now_us() - start) >= 1000U) {
            start = (uint16_t)(start + 1000U);
            elapsed_ms++;
            if (elapsed_ms >= timeout_ms) {
                return UART_TIMEOUT;
            }
        }
    }
}

void uart_flush_rx(USART_TypeDef *usart)
{
    while (USART_GetFlagStatus(usart, USART_FLAG_RXNE) != RESET) {
        (void)USART_ReceiveData(usart);       /* doc de xoa co, bo du lieu */
    }
}
