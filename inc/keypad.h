/**
 * @file    keypad.h
 * @brief   Giao diện driver GPIO và quét thô keypad 4x4.
 */

/*
 * Chỉ biên dịch nội dung header khi KEYPAD_H chưa được định nghĩa, tránh khai báo lặp khi include
 * nhiều lần.
 */
#ifndef KEYPAD_H
#define KEYPAD_H /* Đặt dấu đã nạp header; các lần include tiếp theo sẽ bỏ qua phần bên trong. */

/*
 * Định nghĩa mã char 0x7f, tức 127, để báo nhiều vị trí phím; giá trị này khác mọi ký tự có trên
 * keypad.
 */
#define KEYPAD_MULTIPLE_KEYS   ((char)0x7f)

/*
 * Khai báo hàm cấu hình GPIO, không nhận tham số và không trả kết quả; phần định nghĩa nằm trong
 * keypad.c.
 */
void Keypad_Init(void);

/*
 * Khai báo hàm quét trả về char: ký tự phím, 0 khi nhả hết, hoặc mã nhiều phím; cần gọi Keypad_Init
 * trước.
 */
char Keypad_Scan(void);

#endif /* Kết thúc phần nội dung được bảo vệ bởi KEYPAD_H. */
