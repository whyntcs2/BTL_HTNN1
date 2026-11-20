#include "delay.h"
#include "timer.h"

void delay_us(uint16_t us)
{
    uint16_t start = timer_now_us();
    while ((uint16_t)(timer_now_us() - start) < us) {}
}

void delay_ms(uint32_t ms)
{
    while (ms) {
        delay_us(1000U);
        ms--;
    }
}
