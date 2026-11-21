#ifndef COMM_CONFIG_H
#define COMM_CONFIG_H

/* Chỉ STM32F103C8T6: ứng dụng 60 KB; trang 60/61 giữ boot ID, 62/63 giữ PIN/cờ khóa. */
#define COMM_DEVICE_ID          1UL /* ID cửa, khác 0 và giữ ổn định qua reset. */
#define COMM_FIRMWARE_VERSION   1UL /* Trường version firmware trong HELLO. */
#define COMM_TASK_STACK         384U /* Word trên port Cortex-M3, không phải byte như ESP32. */
#define COMM_LOG_QUEUE_LEN      12U /* Gồm cả bản đang chờ ACK, lưu trong RAM. */
#define COMM_EVENT_QUEUE_LEN    6U  /* Queue Comm -> Main, tách khỏi queue log. */
#define COMM_ACK_MS             1000UL /* Timeout ba lần gửi đầu. */
#define COMM_RETRY_SLOW_MS      5000UL /* Khoảng chờ sau lần gửi thứ ba, không bỏ bản ghi. */
#define COMM_HEARTBEAT_MS       2000UL /* Chu kỳ gửi HELLO cho ESP. */
#define COMM_LINK_TIMEOUT_MS    6000UL /* Quá lâu không có khung hợp lệ thì báo mất link. */
#define COMM_OTA_HOLD_MS        30000UL /* Giới hạn im lặng sau READY nếu ESP chưa reset MCU. */
#define COMM_OTA_MAIN_MS        12000UL /* Thời gian chờ Main chuẩn bị và duyệt OTA. */
#define COMM_APP_BYTES          (60UL * 1024UL) /* Không chồng ứng dụng lên bốn trang dữ liệu. */
#define COMM_DEBUG_TERMINAL     0 /* Bản vận hành không cho terminal gọi thẳng lệnh quản lý vân tay. */
#endif
