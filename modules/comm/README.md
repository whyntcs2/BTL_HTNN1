# Task_Comm trên STM32

Module này chạy trên **STM32F103C8T6**, ưu tiên đề xuất **2**. Task_Main giữ quyền xác thực,
điều khiển cửa và duyệt yêu cầu cập nhật. Comm chỉ giữ USART1, đóng gói dữ liệu và báo sự kiện.

## File đưa vào project

| File | Nhiệm vụ |
|---|---|
| Core/Inc/comm_config.h | Device ID, bộ nhớ, stack, queue và các timeout |
| Core/Inc + Core/Src/comm_protocol.h/.c | Khung CRC16, payload LOG và bảng tên có CRC32 |
| Core/Inc + Core/Src/comm_identity.h/.c | Dự trữ boot ID trong hai trang flash trước scheduler |
| Core/Inc + Core/Src/comm_uart.h/.c | Hàm cấu hình USART1, ring RX và ISR RX/TX |
| Core/Inc + Core/Src/task_comm.h/.c | Hàm tạo task, queue, retry/ACK và trạng thái OTA |

Có **5 header và 4 file C**. Thêm `Core/Inc` vào include path và bốn file trong `Core/Src`
vào build. Cần SPL GPIO/RCC/USART/FLASH/misc, CMSIS và FreeRTOS có sẵn của nhóm.

`Tests` chỉ chạy trên máy tính. Các header trong `Tests/mocks` là mô phỏng RTOS tối thiểu,
không phải thư viện FreeRTOS để nạp vào STM32. Không thêm thư mục này vào include path của firmware.

## Luồng hoạt động

1. Main gọi `xTaskCommLog()`, bản ghi được chép vào queue RAM, không chặn việc xử lý cửa.
2. Comm dùng `xQueuePeek()` lấy đầu queue và gửi LOG. Mất ACK thì gửi lại cùng ID/sequence.
3. Chỉ ACK khớp sequence, device và event mới làm Comm bỏ bản ghi bằng `xQueueReceive()`.
4. Comm nhận STATUS, TIME, bảng ID/tên và yêu cầu OTA từ ESP. Main lấy sự kiện bằng `xTaskCommGetEvent()`.
5. Main chuẩn bị thiết bị và trả lời OTA qua `xTaskCommOtaRespond()`. Comm chỉ gửi READY khi Main cho phép
   và queue log đã hết. Timeout hoặc từ chối sẽ gửi ABORT và báo Main khôi phục hoạt động.

ACK_STORED có nghĩa ESP đã lưu bản ghi; nó chưa xác nhận Google Sheets đã nhận.
ESP phải lưu bền vững và chống trùng trước khi trả ACK theo [giao thức](docs/PROTOCOL.md).

## Cấu hình phần cứng và khởi động

- USART1: **PA9 TX -> RX ESP**, **PA10 RX <- TX ESP**, chung GND, logic 3,3 V; 115200 8N1.
- Gọi riêng `vTaskCommInitHardware()` trước scheduler; `xTaskCommCreate(2)` cũng gọi trước scheduler.
- USART1_IRQHandler do `comm_uart.c` sở hữu. AS608 phải dùng UART khác, bản vá Development chọn USART2.
- Ứng dụng giới hạn **60 KB**, journal boot ID dùng trang flash 60/61. Xem bảng bộ nhớ trong hướng dẫn tích hợp.

```c
NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
vTaskCommInitHardware();              /* Cấu hình phần cứng, chưa bật IRQ. */
if (xTaskCommCreate(2) != pdPASS) {
    for (;;) { }                     /* Không chạy thiếu task truyền thông. */
}
/* Tạo Main và các task của nhóm rồi mới gọi vTaskStartScheduler(). */
```

## Ghép vào code nhóm

[INTEGRATION.md](docs/INTEGRATION.md) có API, bản đồ chân và cách áp dụng
[development.patch](Integration/development.patch) lên commit Development `01f3c718`.
Bản vá chỉ thêm các điểm ghép cần thiết vào code nhóm: tạo Comm, ghi log, xử lý OTA và chuyển UART.
Module không cần tầng `app_*` hoặc một Task_Main mới.

Bản Development dùng tên `task_keypad`/`task_servo` khác module keypad/servo bàn giao trước.
Chọn một bộ implementation khi ghép; không biên dịch đồng thời hai file cùng định nghĩa `Keypad_Init` hoặc `Servo_Init`.

## Kiểm tra

Có GCC trên máy tính thì chạy từ thư mục module:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\test.ps1
```

Script chạy 8 nhóm protocol và 18 nhóm task. Kết quả build STM32 và kiểm tra tương thích nằm ở
[VALIDATION.md](docs/VALIDATION.md). Chưa chạy trên board thật.

Queue STM32 chứa 12 bản ghi RAM: đầy thì Main nhận `pdFAIL`; mất nguồn STM32 thì bản chưa ACK có thể mất.
Boot ID bền vững giúp tránh trùng ID qua reset, không làm queue RAM lưu được qua mất điện.
Firmware ESP32 và phía Google Sheets được phát triển riêng theo giao thức, không thuộc module STM32 này.
