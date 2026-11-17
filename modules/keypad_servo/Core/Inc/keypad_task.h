/**
 * @file    keypad_task.h
 * @brief   Khai báo task đọc keypad 4x4, tách khỏi driver keypad.h.
 */

/* Kiểm tra header task keypad đã được nạp trong lần biên dịch này hay chưa. */
#ifndef KEYPAD_TASK_H
#define KEYPAD_TASK_H /* Định nghĩa dấu KEYPAD_TASK_H để tránh khai báo lặp do include nhiều lần. */

/* Dùng kiểu bool cùng hai giá trị true/false của thư viện chuẩn C. */
#include <stdbool.h>

/*
 * Khai báo kiểu con trỏ hàm chuyển một ký tự cho phía nhận. Hàm do người ghép Main cung cấp,
 * không chờ: trả true khi đã nhận phím, false để keypad thử lại khi còn giữ phím.
 */
typedef bool (*KeypadSendKey_t)(char key, void *context);

typedef struct /* Bắt đầu cấu trúc tham số, chỉ mô tả cách bàn giao phím đã chống dội. */
{ /* Mở danh sách trường cấu hình của KeypadTask. */
    KeypadSendKey_t send_key; /* Lưu địa chỉ hàm nhận phím, phải khác NULL trước khi tạo task. */
    void *context; /* Dữ liệu riêng của phía nhận; keypad chỉ chuyển nguyên con trỏ cho send_key. */
} KeypadTaskConfig_t; /* Đặt tên kiểu cấu hình truyền cho task qua argument. */

/*
 * argument phải trỏ tới KeypadTaskConfig_t hợp lệ và không thay đổi trong suốt thời gian task
 * chạy; dùng biến static hoặc toàn cục. Gọi Keypad_Init trước khi chạy task.
 */
void KeypadTask(void *argument);

#endif /* Kết thúc phạm vi bảo vệ header keypad_task.h. */
