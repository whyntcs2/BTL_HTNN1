#ifndef TIMER_H
#define TIMER_H

#include "stm32f10x.h"

void     tim2_cfg(void);
uint16_t timer_now_us(void);

#endif /* TIMER_H */
