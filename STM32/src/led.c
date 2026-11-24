#include "led.h"
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define LED_PORT        GPIOC
#define LED_PIN         GPIO_Pin_13
#define LED_PORT_CLOCK  RCC_APB2Periph_GPIOC

void led_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(LED_PORT_CLOCK, ENABLE);
    gpio.GPIO_Pin   = LED_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(LED_PORT, &gpio);
    led_off();
}

void led_on(void)
{
    GPIO_ResetBits(LED_PORT, LED_PIN);      /* muc 0 = sang */
}

void led_off(void)
{
    GPIO_SetBits(LED_PORT, LED_PIN);
}

void led_toggle(void)
{
    LED_PORT->ODR ^= LED_PIN;
}
