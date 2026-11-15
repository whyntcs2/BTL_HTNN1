#include "buzzer.h"
#include "oled.h"

volatile uint32_t g_ms_ticks = 0U;

#define LED_GPIO_PORT          GPIOC
#define LED_GPIO_CLK           RCC_APB2Periph_GPIOC
#define LED_GPIO_PIN           GPIO_Pin_13
#define UART_GPIO_PORT         GPIOA
#define UART_GPIO_CLK          RCC_APB2Periph_GPIOA
#define UART_TX_PIN            GPIO_Pin_9
#define UART_RX_PIN            GPIO_Pin_10

static void serial_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;

    RCC_APB2PeriphClockCmd(UART_GPIO_CLK | RCC_APB2Periph_USART1, ENABLE);
    gpio.GPIO_Pin = UART_TX_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(UART_GPIO_PORT, &gpio);
    gpio.GPIO_Pin = UART_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(UART_GPIO_PORT, &gpio);

    USART_StructInit(&uart);
    uart.USART_BaudRate = 9600U;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &uart);
    USART_Cmd(USART1, ENABLE);
}

static void serial_write_char(char c)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) { }
    USART_SendData(USART1, (uint16_t)c);
}

static void serial_write(const char *text)
{
    while (*text != '\0') serial_write_char(*text++);
}

static void serial_handle_command(char command)
{
    /* One-byte UART protocol: each digit is handled immediately, no CR/LF. */
    if (command == '0') {
        oled_show_wifi_connecting();
        serial_write("ACK,0\r\n");
    } else if (command == '1') {
        oled_show_wifi_connected("08:30:15", "26/11/2026");
        serial_write("ACK,1\r\n");
    } else if (command == '2') {
        buzzer_access_granted();
        GPIO_ResetBits(LED_GPIO_PORT, LED_GPIO_PIN);
        oled_show_accepted("012", "Nhan Vien A", "08:30:18", "VAO CA");
        serial_write("ACK,2\r\n");
    } else if (command == '3') {
        buzzer_access_denied();
        GPIO_SetBits(LED_GPIO_PORT, LED_GPIO_PIN);
        oled_show_denied();
        serial_write("ACK,3\r\n");
    } else if (command == '4') {
        buzzer_enrollment();
        oled_show_enrollment("015", "LAN 1");
        serial_write("ACK,4\r\n");
    } else if (command == '5') {
        buzzer_enrollment();
        oled_show_deletion("015");
        serial_write("ACK,5\r\n");
    } else if (command == '6') {
        buzzer_security_warning();
        GPIO_SetBits(LED_GPIO_PORT, LED_GPIO_PIN);
        oled_show_security_warning();
        serial_write("ACK,6\r\n");
    } else {
        serial_write("ERR,BAD_COMMAND\r\n");
    }
}

static void io_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(LED_GPIO_CLK, ENABLE);

    gpio.GPIO_Pin = LED_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(LED_GPIO_PORT, &gpio);
    /* Blue Pill onboard LED is active-low. */
    GPIO_SetBits(LED_GPIO_PORT, LED_GPIO_PIN);
}

void SysTick_Handler(void)
{
    ++g_ms_ticks;
}

int main(void)
{
    SystemInit();
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000U);
    buzzer_init();
    io_init();
    serial_init();
    oled_init();
    oled_show_wifi_connecting();
    serial_write("STM32,READY\r\n");

    for (;;) {
        buzzer_process();
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) {
            char c = (char)USART_ReceiveData(USART1);
            if (c >= '0' && c <= '6') serial_handle_command(c);
            else if (c != '\r' && c != '\n') serial_write("ERR,BAD_COMMAND\r\n");
        }
    }
}
