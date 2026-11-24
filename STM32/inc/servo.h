/**
 * @file    servo.h
 * @brief   Giao diện servo PA8/TIM1_CH1 và thông số vị trí mở/đóng.
 */

#ifndef SERVO_H 
#define SERVO_H 

#define SERVO_CLOSED_PULSE_US   1000U
#define SERVO_OPEN_PULSE_US     2000U

void Servo_Init(void); 
void Servo_Open(void);
void Servo_Close(void);

#endif 
