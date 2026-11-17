/**
 * @file    servo.h
 * @brief   Giao diện servo PA8/TIM1_CH1 và thông số vị trí mở/đóng.
 */

#ifndef SERVO_H /* Kiểm tra header servo đã được nạp hay chưa. */
#define SERVO_H /* Đánh dấu SERVO_H đã được định nghĩa để bảo vệ nội dung khỏi include lặp. */

/*
 * Chọn xung cao đóng cửa là 1000 us, tương đương 1 ms; cần hiệu chỉnh theo vị trí cơ cấu thực tế.
 */
#define SERVO_CLOSED_PULSE_US   1000U
/*
 * Chọn xung cao mở cửa là 2000 us, tương đương 2 ms; đây là độ rộng xung, không phải số đo góc
 * servo.
 */
#define SERVO_OPEN_PULSE_US     2000U

void Servo_Init(void); /* Khai báo hàm cấu hình GPIO/TIM1; phải gọi trước khi chạy ServoTask. */

/* Khai báo hàm đổi PWM sang xung mở; thời gian giữ và tự đóng được quản lý trong ServoTask. */
void Servo_Open(void);

/* Khai báo hàm đổi PWM sang xung đóng; hàm không chờ chuyển động cơ khí hoàn tất. */
void Servo_Close(void);

#endif /* Kết thúc phạm vi bảo vệ header servo.h. */
