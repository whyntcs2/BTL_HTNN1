#ifndef BUZZER_H
#define BUZZER_H

#include "stm32f10x_conf.h"


#ifndef BUZZER_GPIO_PORT
#define BUZZER_GPIO_PORT       GPIOB
#define BUZZER_GPIO_CLK        RCC_APB2Periph_GPIOB
#define BUZZER_GPIO_PIN        GPIO_Pin_4
#endif

typedef enum {
    BUZZER_IDLE = 0,
    BUZZER_ACCESS_GRANTED,
    BUZZER_ACCESS_DENIED,
    BUZZER_ENROLLMENT,
    BUZZER_SECURITY_WARNING
} BuzzerState;

void buzzer_init(void);
void buzzer_idle(void);
void buzzer_access_granted(void);
void buzzer_access_denied(void);
void buzzer_enrollment(void);
void buzzer_security_warning(void);
void buzzer_set_state(BuzzerState state);
void buzzer_process(void);

#endif /* BUZZER_H */
