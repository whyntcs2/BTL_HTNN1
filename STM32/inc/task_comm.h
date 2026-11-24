#ifndef TASK_COMM_H
#define TASK_COMM_H
#include "FreeRTOS.h"
#include "comm_protocol.h"

typedef enum {
    COMM_EVT_LINK,        /* link: USART còn nh?n du?c khung h?p l? hay dã quá h?n. */
    COMM_EVT_STATUS,      /* wifi/cloud/value: k?t n?i và s? log ESP còn luu. */
    COMM_EVT_NAMES,       /* value: phiên b?n b?ng ID -> tên v?a du?c áp d?ng. */
    COMM_EVT_OTA_PREPARE, /* sequence/value: phiên OTA và kích thu?c ?nh; Main xét duy?t. */
    COMM_EVT_OTA_ABORT,   /* sequence: phiên b? h?y; Main ti?p t?c ho?t d?ng. */
    COMM_EVT_REJECT,      /* value: 1 sai d? li?u, 2 ESP d?y, 3 l?i luu, 4 trùng ID khác n?i dung. */
    COMM_EVT_LOG_QUEUE    /* value: s? ô queue dang dùng; b?ng COMM_LOG_QUEUE_LEN khi d?y. */
} Comm_EventType;
typedef struct {
    Comm_EventType type;
    uint32_t value; /* OTA: kích thu?c file; NAMES: version; STATUS: s? log; REJECT: mã l?i. */
    uint16_t sequence;
    uint8_t link, wifi, cloud;
} Comm_Event;

/* Main c?p buffer static ho?c toàn c?c, kh?i t?o b?ng {0}.
 * Khi pending khác 0: không s?a, sao chép, h?y ho?c dùng l?i buffer cho s? ki?n khác.
 * Prepare gi? b?n ghi t?i Main; Flush chuy?n quy?n gi? sang queue Comm khi thành công.
 * N?u dùng nhi?u buffer, Main ph?i Flush theo th? t? Prepare d? gi? th? t? s? ki?n.
 * Buffer v?n ? RAM, không luu du?c qua m?t di?n. Ch? m?t Task_Main g?i các API log. */
typedef struct {
    Comm_Log record;     /* ID/th?i gian du?c ch?t m?t l?n khi Prepare thành công. */
    BaseType_t pending; /* Flush t? xóa c? này sau khi queue dã nh?n b?n sao. */
} Comm_LogBuffer;

void vTaskCommInitHardware(void); /* C?u hình USART1 riêng, tru?c scheduler. */
BaseType_t xTaskCommCreate(UBaseType_t priority); /* T?o queue/task; d? tr? boot ID tru?c scheduler. */
/* Ch? Main g?i Log; tr? pdFAIL n?u chua ti?p nh?n. Không g?i các API này t? ISR.
 * T?o task dúng m?t l?n tru?c scheduler. Main ng?ng phát sinh log tru?c khi ALLOW OTA.
 * pdPASS c?a Log là dã vào RAM STM32; ACK_STORED m?i xác nh?n luu ? ESP, chua ph?i Sheets. */
BaseType_t xTaskCommLog(uint16_t user, uint8_t method, uint8_t result, uint8_t door, uint8_t reason);
/* Prepare tr? pdFAIL n?u buffer còn pending: s? ki?n m?i CHUA du?c ti?p nh?n.
 * Không g?i Prepare l?n n?a ch? d? th? l?i b?n cu; g?i Flush ? các vòng Main sau.
 * Flush không ch?; queue d?y thì tr? pdFAIL và gi? nguyên buffer. Buffer r?ng tr? pdPASS.
 * Khi h?t ch? ? c? queue l?n các buffer Main c?p, Main ph?i báo d?y/h?n ch? nh?n s? ki?n m?i.
 * API Log cu v?n tr? pdFAIL khi queue d?y và không gi? b?n th?t b?i d? th? l?i.
 * Ví d?: static Comm_LogBuffer retry = {0};
 *   Khi có s? ki?n: xTaskCommLogPrepare(&retry, id, method, result, door, reason);
 *   M?i vòng Main: xTaskCommLogFlush(&retry); // Ki?m tra k?t qu? Prepare, không b? qua pdFAIL.
 */
BaseType_t xTaskCommLogPrepare(Comm_LogBuffer *buffer, uint16_t user, uint8_t method,
                             uint8_t result, uint8_t door, uint8_t reason);
BaseType_t xTaskCommLogFlush(Comm_LogBuffer *buffer);
BaseType_t xTaskCommGetEvent(Comm_Event *event, TickType_t wait);
BaseType_t xTaskCommOtaRespond(uint16_t sequence, BaseType_t allow);
BaseType_t xTaskCommCopyName(uint16_t user, char *out, size_t capacity);
uint32_t ulTaskCommRejectedLogs(void); /* S? l?n API t? ch?i, g?m c? retry; không ph?i s? b?n ghi dã m?t. */
UBaseType_t uxTaskCommPendingLogs(void); /* G?m queue ch? ACK và buffer dã Prepare nhung chua Flush. */
#endif
