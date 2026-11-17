/**
 * @file    servo_task.h
 * @brief   Khai báo task điều khiển cửa, tách khỏi driver servo.h.
 */

#ifndef SERVO_TASK_H /* Kiểm tra header task servo đã được nạp hay chưa. */
#define SERVO_TASK_H /* Định nghĩa SERVO_TASK_H để ngăn include lặp trong cùng đơn vị biên dịch. */

/* Dùng uint16_t để quy định rõ độ rộng trường thời gian giữ cửa trong lệnh servo. */
#include <stdint.h>

typedef enum /* Khai báo các lệnh thuộc giao diện của thiết bị servo. */
{ /* Bắt đầu danh sách lệnh. */
    SERVO_CMD_OPEN, /* Yêu cầu phát xung mở và tính lại thời hạn tự đóng. */
    SERVO_CMD_CLOSE /* Yêu cầu phát xung đóng ngay khi ServoTask nhận được lệnh. */
} ServoCommandType_t; /* Đặt tên kiểu dữ liệu cho mã lệnh. */

typedef struct /* Khai báo nội dung một phần tử trong queue lệnh servo. */
{ /* Bắt đầu danh sách trường của lệnh. */
    ServoCommandType_t type; /* Chọn một trong hai thao tác OPEN hoặc CLOSE. */
    uint16_t hold_ms; /* Thời gian giữ khi OPEN: 1..65535 ms; 0 chọn mặc định 3000 ms. */
} ServoCommand_t; /* Main và ServoTask cùng dùng kiểu này khi tạo/gửi/nhận queue. */

/*
 * argument là QueueHandle_t đã tạo với sizeof(ServoCommand_t), truyền trực tiếp khi xTaskCreate;
 * không truyền địa chỉ biến handle. Gọi Servo_Init trước khi chạy task.
 */
void ServoTask(void *argument);

#endif /* Kết thúc phạm vi bảo vệ header servo_task.h. */
