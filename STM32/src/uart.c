#include "uart.h"
#include "timer.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

void uart_cfg(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;

    /* ---- USART3: AS608 (sua 24/11: truoc day la USART1 PA9/PA10) ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_10;              /* PB10: TX -> chan RX cua AS608 */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin   = GPIO_Pin_11;              /* PB11: RX <- chan TX cua AS608, keo len chong byte rac */
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &gpio);

    usart.USART_BaudRate            = 57600;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &usart);

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

/* Sua 24/11 (loi "CB LOI sau mot thoi gian"): doc SR 1 lan roi moi quyet dinh.
 * Ban cu: doc SR (thay RXNE) -> [task bi chen ngang, them 1 byte toi -> co ORE bat]
 * -> doc DR: RXNE xoa nhung ORE KHONG xoa (vi lan doc SR truoc do ORE chua bat).
 * STM32F1 dang ORE = 1 thi bo moi byte nhan tiep, RXNE khong bao gio len lai -> USART3 "diec"
 * vinh vien, AS608 van chay binh thuong, chi RESET STM32 moi het.
 * Ban moi: co ORE / NE / FE / PE ma khong co RXNE thi doc DR ngay sau lan doc SR do de xoa co. */
#define UART_ERR_FLAGS  (USART_FLAG_ORE | USART_FLAG_NE | USART_FLAG_FE | USART_FLAG_PE)

UART_Status uart_recv_byte(USART_TypeDef *usart, uint8_t *data, uint32_t timeout_ms)
{
    uint16_t start = timer_now_us();
    uint32_t elapsed_ms = 0;
    uint16_t sr;

    while (1) {
        sr = usart->SR;                                 /* doc SR dung 1 lan cho moi vong */
        if ((sr & USART_FLAG_RXNE) != 0U) {
            *data = (uint8_t)usart->DR;                 /* SR roi DR: xoa ca RXNE lan ORE neu co */
            return UART_OKE;
        }
        if ((sr & UART_ERR_FLAGS) != 0U) {
            (void)usart->DR;                            /* SR (co loi) roi DR: xoa co loi */
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
    uint16_t sr = usart->SR;

    /* Sua 24/11: bo ca byte con sot lan co loi (ORE / NE / FE / PE) */
    while ((sr & (USART_FLAG_RXNE | UART_ERR_FLAGS)) != 0U) {
        (void)usart->DR;                                /* doc de xoa co, bo du lieu */
        sr = usart->SR;
    }
}