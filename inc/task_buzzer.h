#ifndef TASK_BUZZER_H
#define TASK_BUZZER_H

/* Task buzzer: coi chip thu dong (TIM3_CH1 remap PB4).
 * - Chi task nay duoc goi ham buzzer_... (thu vien buzzer.c/.h giu nguyen).
 * - Task khac gui am bang xTaskBuzzerPlay(), khong goi thang buzzer.h.
 * - Am moi thay am dang keu (am cu bi cat ngang). Khong chan task goi. */

#include "FreeRTOS.h"

typedef enum {
    BUZZER_SOUND_STOP = 0,  /* tat coi ngay */
    BUZZER_SOUND_GRANTED,   /* dung: 2 tieng bip cao (mo cua) */
    BUZZER_SOUND_DENIED,    /* sai: 1 tieng tram dai */
    BUZZER_SOUND_BEEP,      /* 1 tieng bip ngan 70ms (bam phim, buoc dang ky) */
    BUZZER_SOUND_WARNING    /* canh bao: 5 tieng (khoa 5 phut, dot nhap) */
} Buzzer_Sound;

/* Cau hinh TIM3 PWM PB4 (buzzer_init), tao queue va task buzzer.
 * Goi trong main(), truoc vTaskStartScheduler().
 * Tra ve pdPASS neu tao thanh cong. */
BaseType_t xTaskBuzzerCreate(UBaseType_t uxPriority);

/* Gui 1 am, cho toi da xTicksToWait tick neu queue day.
 * Tra ve pdPASS neu gui duoc. */
BaseType_t xTaskBuzzerPlay(Buzzer_Sound eSound, TickType_t xTicksToWait);

#endif /* TASK_BUZZER_H */