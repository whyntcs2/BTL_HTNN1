#ifndef DELAY_H
#define DELAY_H

#include "stm32f10x.h"

/* Can goi tim2_cfg() truoc khi dung */
void delay_us(uint16_t us);
void delay_ms(uint32_t ms);

#endif /* DELAY_H */
