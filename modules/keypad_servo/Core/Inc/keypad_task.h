/**
 * @file    keypad_task.h
 * @brief   Khai báo task đọc keypad 4x4, tách khỏi driver keypad.h.
 */

/* Kiểm tra header task keypad đã được nạp trong lần biên dịch này hay chưa. */
#ifndef KEYPAD_TASK_H
#define KEYPAD_TASK_H /* Định nghĩa dấu KEYPAD_TASK_H để tránh khai báo lặp do include nhiều lần. */

/* Dùng kiểu bool cùng hai giá trị true/false của thư viện chuẩn C. */
#include <stdbool.h>
#include <stdint.h> /* Kiểu bộ đếm phím bị bỏ khi FIFO nội bộ cũng đầy. */

#define KEYPAD_PENDING_LENGTH 16U /* Số ký tự chờ gửi, lưu trong stack của KeypadTask. */

/*
 * Khai báo kiểu con trỏ hàm chuyển một ký tự cho phía nhận. Hàm do người ghép Main cung cấp,
 * không chờ: true nghĩa là đã nhận phím; false giữ nguyên đầu FIFO để thử lại cả sau khi nhả.
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

/* FIFO đầy thì bỏ ký tự mới, không ghi đè ký tự cũ. Main có thể đọc bộ đếm này
 * để báo quá tải/xóa chuỗi PIN đang nhập. Chỉ tạo một KeypadTask cho một ma trận. */
uint32_t KeypadTask_GetDroppedKeys(void);

#endif /* Kết thúc phạm vi bảo vệ header keypad_task.h. */
