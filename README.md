# BTL_HTNN1

Phần bàn giao của Anh Tài, dùng với STM32F103 và FreeRTOS:

- [Keypad 4x4 và servo](modules/keypad_servo): 8 file driver/task đã bàn giao.
- [Task_Comm](modules/comm): USART1 nối ESP32, log có CRC/ACK, bảng tên và bắt tay OTA.

Cách ghép Comm vào source nhóm nằm trong [hướng dẫn tích hợp](modules/comm/docs/INTEGRATION.md).
Mỗi module sử dụng SPL/CMSIS/FreeRTOS sẵn có trong project của nhóm.
