# Ghép Task_Comm vào Development

Bản vá `Integration/development.patch` được tạo trên Development commit
[`01f3c718f9266b0d65fcc85408338a7e476103b8`](https://github.com/whyntcs2/BTL_HTNN1/commit/01f3c718f9266b0d65fcc85408338a7e476103b8).
Nó sửa 11 file có sẵn, không tạo một Main khác cho project.

## Thứ tự ghép

Thực hiện trong bản source Development đã có thư mục `modules/comm` này:

```powershell
git apply --check modules/comm/Integration/development.patch
git apply modules/comm/Integration/development.patch
Copy-Item modules/comm/Core/Inc/*.h inc/
Copy-Item modules/comm/Core/Src/*.c src/
```

Sau đó thêm bốn file C mới vào project Keil/GCC. Nếu code nhóm đã thay đổi so với commit trên,
đọc từng hunk của patch và ghép các điểm tương ứng; không ép áp dụng patch bị lỗi.
Không áp dụng patch này trên branch chỉ có hai thư mục module mà chưa có `inc/src` của Development.

## Các thay đổi của bản vá

- `main.c`: gọi hàm cấu hình Comm riêng, tạo Comm ưu tiên 2, Control ưu tiên 4; dừng khởi tạo khi thiếu tài nguyên.
- `uart.c/.h`, `as608.c`, `task_as608.c`: AS608 từ USART1 chuyển USART2 để nhường USART1 cho ESP và ROM bootloader.
- `keypad.c`: R3/R4 từ PA2/PA3 chuyển PB0/PB1 để nhường USART2.
- `task_control.c`: thêm log PIN/vân tay/cảnh báo khóa; đọc sự kiện Comm, bảng tên và duyệt OTA.
- `task_as608.h/.c`, `task_servo.h/.c`: thêm bộ đếm PAUSE/CLOSE đã thực thi, tránh trả READY khi lệnh còn nằm trong queue.
- `FreeRTOSConfig.h`: heap 12 KB; Control stack lên 384 word. Comm dùng 384 word, tức 1536 byte trên Cortex-M3.
- Terminal quản lý trực tiếp AS608 mặc định tắt qua `COMM_DEBUG_TERMINAL=0`, để lệnh quản lý đi qua Control.

Patch giữ cách chia task và chính sách xác thực hiện có của Development. Bản đó còn task Servo/Buzzer/LED riêng,
AS608 polling và ngưỡng 3 lần vân tay rồi 3 lần PIN; việc thêm Comm không chuyển toàn project thành đúng thiết kế 5 task.

## Chân của bản tích hợp

| Thiết bị | Chân STM32 |
|---|---|
| ESP32, USART1 | PA9 TX, PA10 RX |
| AS608, USART2 | PA2 TX, PA3 RX |
| Keypad R1/R2/R3/R4 | PA0, PA1, PB0, PB1 |
| Keypad C1/C2/C3/C4 | PA4, PA5, PA6, PA7 |
| Servo | PA8, TIM1_CH1 |
| OLED | PB6/PB7 |
| Còi | PB4 |
| Debug, USART3 | PB10/PB11 |

Đây là cách chia chân của source Development đang ghép. Nếu mạch thực tế đã giữ PB0/PB1 cho nút/WAK,
chọn chân hàng keypad khác và sửa `row_ports/row_pins` trước khi đấu. WAK/EXTI thuộc phần Finger,
chưa được bổ sung trong bản vá Comm. Bảng PA0..PA7 của module keypad cũ chưa bao gồm thay đổi dây này.

## Bộ nhớ bắt buộc

| Vùng | Địa chỉ |
|---|---|
| Ứng dụng 60 KB | 0x08000000..0x0800EFFF |
| Boot ID, trang 60/61 | 0x0800F000 và 0x0800F400, mỗi trang 1 KB |
| PIN, trang 62 | 0x0800F800 |
| Cờ khóa, trang 63 | 0x0800FC00 |

Keil đặt **IROM1 Size = 0xF000**; GCC giới hạn FLASH `LENGTH = 60K`. Phải đặt giới hạn này dù chưa dùng OTA,
vì Comm vẫn ghi boot ID. Không dùng mass erase khi cần giữ journal/PIN/cờ khóa.
Nếu xóa cả journal trong khi backend còn log của cùng device ID, phải cấp lại định danh thiết bị trước khi hoạt động,
tránh tái sử dụng event ID cũ. Không áp dụng địa chỉ flash này cho dòng STM32 khác mà chưa sửa thiết kế bộ nhớ.

Cấu hình cần có: cấp phát động, task notifications, `INCLUDE_xTaskGetCurrentTaskHandle=1`, tick 32 bit,
`configMAX_PRIORITIES >= 5`. Project cung cấp các vector SVC/PendSV/SysTick đúng port FreeRTOS.
Dùng NVIC Group 4; IRQ USART1 là mức 6 trong dự án có mức syscall 5. Không định nghĩa trùng USART1_IRQHandler.

## API Main sử dụng

| API | Cách dùng |
|---|---|
| `vTaskCommInitHardware()` | Cấu hình USART1 riêng, gọi trước scheduler |
| `xTaskCommCreate(2)` | Tạo queue/task và dự trữ boot ID; chỉ gọi một lần trước scheduler, kiểm tra pdPASS |
| `xTaskCommLog(id, method, result, door, reason)` | Chỉ Main gọi, không chờ; pdPASS là đã vào RAM, pdFAIL là chưa tiếp nhận |
| `xTaskCommGetEvent(&event, wait)` | Main đọc queue Comm; timeout 0 nếu ghép vòng lặp hiện có |
| `xTaskCommCopyName(id, out, capacity)` | Chép tên theo ID, luôn kết thúc chuỗi nếu capacity > 0; không dùng tên để xác thực |
| `xTaskCommOtaRespond(sequence, allow)` | Main duyệt/hủy đúng phiên; queue đầy thì Main phải thử lại |
| `uxTaskCommPendingLogs()` | Số bản trong RAM, gồm bản đang đợi ACK |
| `ulTaskCommRejectedLogs()` | Tổng bản bị API từ chối; dùng để theo dõi queue đầy/lỗi tham số |

`method`: 1 vân tay, 2 PIN, 3 hệ thống. `result`: 0 sai, 1 đúng, 2 cảnh báo.
`door`: 0 lệnh đóng, 1 lệnh mở, 2 chưa rõ. `user=0xFFFF` khi không biết ID.
Log không chứa PIN. Chỉ gọi API trong task, không gọi từ ISR.

Ví dụ log đăng nhập bằng PIN đúng:

```c
if (xTaskCommLog(user_id, 2, 1, 1, 0) != pdPASS) {
    /* Main báo chưa lưu được log, hoặc giữ lại để thử sau theo chính sách của nhóm. */
}
```

`COMM_EVT_LINK` báo liên kết UART; `COMM_EVT_STATUS` tách WiFi và cloud; `COMM_EVT_NAMES` báo version bảng mới.
`COMM_EVT_REJECT` mang mã lỗi từ ESP, log vẫn giữ ở đầu queue. STATUS/NAMES/REJECT được gộp theo bản mới nhất
nếu queue đầy; OTA_PREPARE/ABORT được giữ theo phiên và ưu tiên chuyển cho Main.

Nếu nhóm chuyển sang một `q_event` chung, nối các sự kiện trên vào kiểu event chung tại điểm tích hợp của Main;
không thêm lớp `app_*` để đổi quyền điều khiển thiết bị. Bản vá hiện tại dùng getter như các task sẵn có của Development.

## Điều kiện duyệt OTA

Main từ chối khi đang khóa/menu hoặc ảnh nằm ngoài 8..61440 byte. Khi bắt đầu chuẩn bị, Main ngừng nhận
lệnh mở mới, yêu cầu Finger PAUSE và Servo CLOSE. Chỉ khi các task xác nhận đã thực hiện, chờ thêm thời gian
servo quay và không còn log chờ ACK thì Main mới trả allow. Bộ đếm lệnh chỉ xác nhận PWM/PAUSE đã áp dụng,
không thay cảm biến cửa vật lý. Main phải giữ trạng thái này tới khi reset hoặc nhận OTA_ABORT.

Comm tự kiểm tra queue log rỗng trước READY; vẫn gửi log trong lúc chờ Main. Quyết định Main được giữ khi TX bận.
Hủy thắng một allow chưa gửi; timeout chờ Main 12 giây, timeout im lặng sau READY 30 giây.
Main nhận ABORT phải phục hồi task Finger; bản vá có thử lại lệnh RESUME nếu queue cảm biến đầy.

Task_Comm chỉ bắt tay trong ứng dụng. Phía ESP thực hiện tải ảnh, đưa BOOT0/NRST về mức thích hợp,
chuyển UART sang giao thức ROM và ghi/đọc kiểm tra flash. Do đó cần phần cứng BOOT0/NRST phù hợp và phía ESP
thực hiện đúng hợp đồng trước khi thử cập nhật thực tế. Task STM32 này không có mã tải HTTP hoặc ghi Sheets.

Tài liệu đối chiếu: [ST AN2606](https://www.st.com/resource/en/application_note/an2606-introduction-to-system-memory-boot-mode-stmicroelectronics.pdf)
về USART bootloader; [FreeRTOS Cortex-M](https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/04-Demos/ARM-Cortex/RTOS-Cortex-M3-M4)
về NVIC và API FromISR.
