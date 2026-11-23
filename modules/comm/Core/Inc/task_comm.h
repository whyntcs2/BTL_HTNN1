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
    COMM_EVT_REJECT,      /* value: 1 sai dữ liệu, 2 ESP đầy, 3 lỗi lưu, 4 trùng ID khác nội dung. */
    COMM_EVT_LOG_QUEUE    /* value: số ô queue đang dùng; bằng COMM_LOG_QUEUE_LEN khi đầy. */
} Comm_EventType;
typedef struct {
    Comm_EventType type;
    uint32_t value; /* OTA: kích thước file; NAMES: version; STATUS: số log; REJECT: mã lỗi. */
    uint16_t sequence;
    uint8_t link, wifi, cloud;
} Comm_Event;

/* Main cấp buffer static hoặc toàn cục, khởi tạo bằng {0}.
 * Khi pending khác 0: không sửa, sao chép, hủy hoặc dùng lại buffer cho sự kiện khác.
 * Prepare giữ bản ghi tại Main; Flush chuyển quyền giữ sang queue Comm khi thành công.
 * Nếu dùng nhiều buffer, Main phải Flush theo thứ tự Prepare để giữ thứ tự sự kiện.
 * Buffer vẫn ở RAM, không lưu được qua mất điện. Chỉ một Task_Main gọi các API log. */
typedef struct {
    Comm_Log record;     /* ID/thời gian được chốt một lần khi Prepare thành công. */
    BaseType_t pending; /* Flush tự xóa cờ này sau khi queue đã nhận bản sao. */
} Comm_LogBuffer;

void vTaskCommInitHardware(void); /* Cấu hình USART1 riêng, trước scheduler. */
BaseType_t xTaskCommCreate(UBaseType_t priority); /* Tạo queue/task; dự trữ boot ID trước scheduler. */
/* Chỉ Main gọi Log; trả pdFAIL nếu chưa tiếp nhận. Không gọi các API này từ ISR.
 * Tạo task đúng một lần trước scheduler. Main ngừng phát sinh log trước khi ALLOW OTA.
 * pdPASS của Log là đã vào RAM STM32; ACK_STORED mới xác nhận lưu ở ESP, chưa phải Sheets. */
BaseType_t xTaskCommLog(uint16_t user, uint8_t method, uint8_t result, uint8_t door, uint8_t reason);
/* Prepare trả pdFAIL nếu buffer còn pending: sự kiện mới CHƯA được tiếp nhận.
 * Không gọi Prepare lần nữa chỉ để thử lại bản cũ; gọi Flush ở các vòng Main sau.
 * Flush không chờ; queue đầy thì trả pdFAIL và giữ nguyên buffer. Buffer rỗng trả pdPASS.
 * Khi hết chỗ ở cả queue lẫn các buffer Main cấp, Main phải báo đầy/hạn chế nhận sự kiện mới.
 * API Log cũ vẫn trả pdFAIL khi queue đầy và không giữ bản thất bại để thử lại.
 * Ví dụ: static Comm_LogBuffer retry = {0};
 *   Khi có sự kiện: xTaskCommLogPrepare(&retry, id, method, result, door, reason);
 *   Mỗi vòng Main: xTaskCommLogFlush(&retry); // Kiểm tra kết quả Prepare, không bỏ qua pdFAIL.
 */
BaseType_t xTaskCommLogPrepare(Comm_LogBuffer *buffer, uint16_t user, uint8_t method,
                             uint8_t result, uint8_t door, uint8_t reason);
BaseType_t xTaskCommLogFlush(Comm_LogBuffer *buffer);
BaseType_t xTaskCommGetEvent(Comm_Event *event, TickType_t wait);
BaseType_t xTaskCommOtaRespond(uint16_t sequence, BaseType_t allow);
BaseType_t xTaskCommCopyName(uint16_t user, char *out, size_t capacity);
uint32_t ulTaskCommRejectedLogs(void); /* Số lần API từ chối, gồm cả retry; không phải số bản ghi đã mất. */
UBaseType_t uxTaskCommPendingLogs(void); /* Gồm queue chờ ACK và buffer đã Prepare nhưng chưa Flush. */
#endif
