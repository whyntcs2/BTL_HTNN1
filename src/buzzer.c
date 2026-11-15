#include "buzzer.h"

extern volatile uint32_t g_ms_ticks;

typedef struct {
    uint16_t frequency;
    uint16_t on_ms;
    uint16_t gap_ms;
} BuzzerBeep;

static const BuzzerBeep granted_pattern[] = {{2400U, 90U, 70U}, {3000U, 120U, 0U}};
static const BuzzerBeep denied_pattern[] = {{700U, 450U, 80U}};
static const BuzzerBeep enrollment_pattern[] = {{2100U, 70U, 0U}};
static const BuzzerBeep warning_pattern[] = {
    {900U, 180U, 100U}, {900U, 180U, 100U}, {900U, 180U, 100U},
    {900U, 180U, 100U}, {900U, 180U, 0U}
};

static const BuzzerBeep *active_pattern;
static uint8_t active_pattern_length;
static uint8_t pattern_index;
static uint8_t tone_is_on;
static uint8_t pattern_is_active;
static uint32_t phase_deadline;

static void buzzer_tone_start(uint16_t frequency_hz)
{
    RCC_ClocksTypeDef clocks;
    uint32_t timer_clock;
    uint32_t prescaler;
    uint32_t period;

    RCC_GetClocksFreq(&clocks);
    timer_clock = clocks.PCLK1_Frequency;
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) timer_clock *= 2U;
    prescaler = (timer_clock / 1000000U) - 1U; /* 1 MHz timer counter */
    period = (1000000U / frequency_hz) - 1U;
    if (period > 0xFFFFU) period = 0xFFFFU;

    TIM_Cmd(TIM3, DISABLE);
    TIM_PrescalerConfig(TIM3, (uint16_t)prescaler, TIM_PSCReloadMode_Immediate);
    TIM_SetAutoreload(TIM3, (uint16_t)period);
    TIM_SetCompare1(TIM3, (uint16_t)((period + 1U) / 2U));
    TIM_GenerateEvent(TIM3, TIM_EventSource_Update);
    TIM_Cmd(TIM3, ENABLE);
}

static void buzzer_tone_stop(void)
{
    TIM_SetCompare1(TIM3, 0U);
}

static void buzzer_start_pattern(const BuzzerBeep *pattern, uint8_t length)
{
    active_pattern = pattern;
    active_pattern_length = length;
    pattern_index = 0U;
    tone_is_on = 1U;
    pattern_is_active = 1U;
    buzzer_tone_start(active_pattern[0].frequency);
    phase_deadline = g_ms_ticks + active_pattern[0].on_ms;
}

void buzzer_init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timebase;
    TIM_OCInitTypeDef output_compare;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | BUZZER_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE); /* Keep SWD, free PB4 */
    GPIO_PinRemapConfig(GPIO_PartialRemap_TIM3, ENABLE);    /* TIM3_CH1 -> PB4 */

    gpio.GPIO_Pin = BUZZER_GPIO_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(BUZZER_GPIO_PORT, &gpio);

    timebase.TIM_Prescaler = 71U;
    timebase.TIM_CounterMode = TIM_CounterMode_Up;
    timebase.TIM_Period = 999U;
    timebase.TIM_ClockDivision = TIM_CKD_DIV1;
    timebase.TIM_RepetitionCounter = 0U;
    TIM_TimeBaseInit(TIM3, &timebase);

    output_compare.TIM_OCMode = TIM_OCMode_PWM1;
    output_compare.TIM_OutputState = TIM_OutputState_Enable;
    output_compare.TIM_Pulse = 0U;
    output_compare.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM3, &output_compare);
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
}

void buzzer_idle(void)
{
    pattern_is_active = 0U;
    tone_is_on = 0U;
    active_pattern = 0;
    buzzer_tone_stop();
}

void buzzer_process(void)
{
    uint32_t now = g_ms_ticks;
    const BuzzerBeep *beep;

    if (!pattern_is_active || (int32_t)(now - phase_deadline) < 0) return;
    beep = &active_pattern[pattern_index];

    if (tone_is_on) {
        buzzer_tone_stop();
        tone_is_on = 0U;
        if (beep->gap_ms != 0U) {
            phase_deadline = now + beep->gap_ms;
            return;
        }
    }

    ++pattern_index;
    if (pattern_index >= active_pattern_length) {
        buzzer_idle();
        return;
    }

    beep = &active_pattern[pattern_index];
    buzzer_tone_start(beep->frequency);
    tone_is_on = 1U;
    phase_deadline = now + beep->on_ms;
}

void buzzer_access_granted(void)
{
    buzzer_start_pattern(granted_pattern, (uint8_t)(sizeof(granted_pattern) / sizeof(granted_pattern[0])));
}

void buzzer_access_denied(void)
{
    buzzer_start_pattern(denied_pattern, (uint8_t)(sizeof(denied_pattern) / sizeof(denied_pattern[0])));
}

void buzzer_enrollment(void)
{
    buzzer_start_pattern(enrollment_pattern, (uint8_t)(sizeof(enrollment_pattern) / sizeof(enrollment_pattern[0])));
}

void buzzer_security_warning(void)
{
    buzzer_start_pattern(warning_pattern, (uint8_t)(sizeof(warning_pattern) / sizeof(warning_pattern[0])));
}

void buzzer_set_state(BuzzerState state)
{
    switch (state) {
    case BUZZER_ACCESS_GRANTED: buzzer_access_granted(); break;
    case BUZZER_ACCESS_DENIED: buzzer_access_denied(); break;
    case BUZZER_ENROLLMENT: buzzer_enrollment(); break;
    case BUZZER_SECURITY_WARNING: buzzer_security_warning(); break;
    case BUZZER_IDLE:
    default: buzzer_idle(); break;
    }
}
