#ifndef TIMER_H
#define TIMER_H

#include "stm32f10x.h"

/* TIM2 dem tu do 1MHz: 1 nhip = 1us, tran sau 65535us */
void     tim2_cfg(void);

/* Doc gia tri bo dem hien tai (don vi us).
 * Dung phep tru ep (uint16_t) de tinh khoang thoi gian, chiu duoc tran. */
uint16_t timer_now_us(void);

#endif /* TIMER_H */
