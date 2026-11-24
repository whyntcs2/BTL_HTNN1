#ifndef TASK_SERVO_H
#define TASK_SERVO_H

/* Task servo: dieu khien servo SG90 mo / dong cua.
 * - Chi task nay duoc goi Servo_Open() / Servo_Close() (thu vien servo.c/.h giu nguyen).
 * - Task khac gui lenh bang xTaskServoSendCommand(), khong goi thang servo.h.
 * - Task nay KHONG quyet dinh mo cua (viec cua task dieu khien) va khong gui su kien nguoc lai.
 * Mo cua xong tu dong sau thoi gian giu; nhan OPEN luc dang mo thi tinh lai thoi gian giu tu dau. */

#include "FreeRTOS.h"
#include <stdint.h>

/* Thoi gian giu cua mac dinh khi usHoldMs = 0 */
#define SERVO_HOLD_MS_DEFAULT   3000U

typedef enum {
    SERVO_CMD_OPEN = 0,     /* mo cua, tu dong sau usHoldMs (0 = SERVO_HOLD_MS_DEFAULT) */
    SERVO_CMD_CLOSE         /* dong ngay */
} Servo_CmdType;

/* Phan tu cua queue lenh (gui theo gia tri, giong Vi du 11 trong slide) */
typedef struct {
    Servo_CmdType eCmd;
    uint16_t      usHoldMs; /* chi dung voi SERVO_CMD_OPEN: 1..65535 ms, 0 = mac dinh */
} Servo_Command;

/* Cau hinh TIM1 PWM PA8 (Servo_Init, cua o vi tri dong), tao queue lenh va task servo.
 * Goi trong main(), truoc vTaskStartScheduler().
 * Tra ve pdPASS neu tao thanh cong. */
BaseType_t xTaskServoCreate(UBaseType_t uxPriority);

/* Gui 1 lenh, cho toi da xTicksToWait tick neu queue lenh day.
 * Tra ve pdPASS neu gui duoc. */
BaseType_t xTaskServoSendCommand(Servo_CmdType eCmd, uint16_t usHoldMs, TickType_t xTicksToWait);

#endif /* TASK_SERVO_H */