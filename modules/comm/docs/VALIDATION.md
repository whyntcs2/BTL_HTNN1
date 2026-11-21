# Kết quả kiểm tra Task_Comm

Ngày 21/11/2026. Source Comm ghép với Development commit `01f3c718f9266b0d65fcc85408338a7e476103b8`.

## Kiểm thử trên máy tính

Biên dịch C11 với GCC, `-Wall -Wextra -Werror`; script bàn giao còn bật `-O2`.
`test.ps1` chạy **26 nhóm** dùng source thật của module:

- 8 nhóm protocol: CRC16/32 theo check vector, layout LOG, little-endian, CRC sai/nhiễu/phân mảnh,
  giới hạn khung và bộ đệm, phục hồi parser, commit bảng tên, CRC/thiếu hàng/ID trùng/bảng quá lớn/bảng rỗng.
- 18 nhóm task: retry giữ nguyên ID/sequence, ACK sai, UART bận, ALLOW đúng phiên và chờ hết log,
  từ chối/timeout OTA, queue log đầy, STATUS/NAMES khi queue Main đầy, giữ thứ tự hủy OTA,
  hủy thắng ALLOW, ESP REJECT, timeout quiet, ABORT sai, tham số lỗi, tạo task hai lần,
  thu hồi queue khi khởi tạo lỗi, đồng bộ giờ và mất liên kết.

Đã kiểm tra thêm **4 nhóm tương thích C/C++** với encoder Arduino ở workspace: payload/khung LOG giống từng byte,
parser hai phía đọc được nhau, snapshot tên đầy đủ/thiếu/quá lớn/rỗng. Source Arduino không kèm trong commit STM32.

Các test task dùng RTOS/UART giả để chạy lại những tình huống khó gây trên bàn thử.
Chúng không thay cho kiểm tra ngắt, heap/stack hay flash khi chạy trên vi điều khiển.

## Build toàn bộ source STM32 đã ghép

- GCC ARM Cortex-M3, C11, `-Os`; source ứng dụng bật `-Wall -Wextra -Werror`.
- SPL/CMSIS của STM32F1, FreeRTOS Kernel V11.3.3; linker giới hạn FLASH 60 KB, RAM 20 KB.
- Build và link thành công: **FLASH 28.916/61.440 byte**, **RAM 18.224/20.480 byte**.
- RAM trên gồm heap tĩnh 12 KB và vùng dự trữ stack khởi động 1 KB; chưa phải số heap còn trống lúc chạy.
- Patch ghép đã qua `git apply --check` trên đúng commit Development ghi ở trên.

Thư viện dùng để build đã có tại workspace, không được chép vào module hoặc nhận là code do người phụ trách viết.
Branch chứa module và patch tích hợp; project Keil của nhóm vẫn cung cấp startup/linker và thư viện nền.

## Phần cần thử trên board

Chưa kiểm tra trên board STM32/ESP32 và chưa dùng tài khoản Sheets thật. Khi ráp mạch cần kiểm tra chéo TX/RX,
NVIC/USART1 IRQ, thời gian mất/có lại kết nối, lưu flash journal qua reset và mức heap/stack còn lại.
Thử mất ACK và kiểm tra ID log không đổi; thử đầy queue và xác nhận Main nhận lỗi.

Với OTA cần kiểm tra PAUSE/CLOSE đã thực thi, timeout có phục hồi, BOOT0/NRST và đọc lại ảnh sau nạp.
Số liệu build không chứng minh cửa đã đóng vật lý hoặc hệ thống không mất log khi nguồn bị ngắt:
queue STM32 vẫn ở RAM và chỉ chứa 12 bản ghi.
