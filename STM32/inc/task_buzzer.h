#ifndef TASK_BUZZER_H
#define TASK_BUZZER_H


#include "FreeRTOS.h"

typedef enum {
    BUZZER_SOUND_STOP = 0,  /* tat coi ngay */
    BUZZER_SOUND_GRANTED,   /* dung: 2 tieng bip cao (mo cua) */
    BUZZER_SOUND_DENIED,    /* sai: 1 tieng tram dai */
    BUZZER_SOUND_BEEP,      /* 1 tieng bip ngan 70ms (bam phim, buoc dang ky) */
    BUZZER_SOUND_WARNING    /* canh bao: 5 tieng (khoa 5 phut, dot nhap) */
} Buzzer_Sound;

BaseType_t xTaskBuzzerCreate(UBaseType_t uxPriority);
BaseType_t xTaskBuzzerPlay(Buzzer_Sound eSound, TickType_t xTicksToWait);

#endif 