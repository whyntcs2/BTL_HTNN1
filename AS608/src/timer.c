#include "timer.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

void tim2_cfg(void)
{
    TIM_TimeBaseInitTypeDef tim;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    tim.TIM_CounterMode   = TIM_CounterMode_Up;
    tim.TIM_Period        = 0xFFFF;
    tim.TIM_Prescaler     = 72 - 1;          /* 72MHz / 72 = 1MHz */
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &tim);
    TIM_Cmd(TIM2, ENABLE);
}

uint16_t timer_now_us(void)
{
    return (uint16_t)TIM_GetCounter(TIM2);
}
