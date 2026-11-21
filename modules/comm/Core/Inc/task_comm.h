#ifndef TASK_COMM_H
#define TASK_COMM_H
#include "FreeRTOS.h"
#include "comm_protocol.h"

typedef enum {
    COMM_EVT_LINK,        /* link: USART còn nhận được khung hợp lệ hay đã quá hạn. */
    COMM_EVT_STATUS,      /* wifi/cloud/value: kết nối và số log ESP còn lưu. */
    COMM_EVT_NAMES,       /* value: phiên bản bảng ID -> tên vừa được áp dụng. */
    COMM_EVT_OTA_PREPARE, /* sequence/value: phiên OTA và kích thước ảnh; Main xét duyệt. */
    COMM_EVT_OTA_ABORT,   /* sequence: phiên bị hủy; Main tiếp tục hoạt động. */
    COMM_EVT_REJECT       /* value: 1 sai dữ liệu, 2 ESP đầy, 3 lỗi lưu, 4 trùng ID khác nội dung. */
} Comm_EventType;
typedef struct {
    Comm_EventType type;
    uint32_t value; /* OTA: kích thước file; NAMES: version; STATUS: số log; REJECT: mã lỗi. */
    uint16_t sequence;
    uint8_t link, wifi, cloud;
} Comm_Event;

void vTaskCommInitHardware(void); /* Cấu hình USART1 riêng, trước scheduler. */
BaseType_t xTaskCommCreate(UBaseType_t priority); /* Tạo queue/task; dự trữ boot ID trước scheduler. */
/* Chỉ Main gọi Log; trả pdFAIL nếu chưa tiếp nhận. Không gọi các API này từ ISR.
 * Tạo task đúng một lần trước scheduler. Main ngừng phát sinh log trước khi ALLOW OTA.
 * pdPASS của Log là đã vào RAM STM32; ACK_STORED mới xác nhận lưu ở ESP, chưa phải Sheets. */
BaseType_t xTaskCommLog(uint16_t user, uint8_t method, uint8_t result, uint8_t door, uint8_t reason);
BaseType_t xTaskCommGetEvent(Comm_Event *event, TickType_t wait);
BaseType_t xTaskCommOtaRespond(uint16_t sequence, BaseType_t allow);
BaseType_t xTaskCommCopyName(uint16_t user, char *out, size_t capacity);
uint32_t ulTaskCommRejectedLogs(void); /* Main phải biết nếu hàng đợi RAM đã đầy, không báo gửi thành công giả. */
UBaseType_t uxTaskCommPendingLogs(void); /* Gồm cả bản đang chờ ACK, vì task dùng xQueuePeek. */
#endif
