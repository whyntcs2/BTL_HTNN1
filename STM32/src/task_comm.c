/**
 * @file task_comm.c
 * @brief Task truyền thông STM32: giữ log tới ACK, nhận bảng tên và bắt tay OTA.
 * Main là nơi xác thực và điều khiển cửa; file này chỉ quản lý đường truyền.
 */
#include "task_comm.h"     /* API để Main gửi log và nhận sự kiện. */
#include "comm_config.h"   /* Thời gian chờ, dung lượng queue và mã thiết bị. */
#include "comm_identity.h" /* Cấp boot ID đã lưu vào flash trước khi chạy task. */
#include "comm_uart.h"     /* Driver USART1 có bộ đệm RX/TX và ngắt. */
#include "task.h"          /* Task, tick, notification và vùng tới hạn của FreeRTOS. */
#include "queue.h"         /* Queue do FreeRTOS cung cấp. */
#include <string.h>        /* memset và memcpy để chuẩn bị dữ liệu. */

typedef struct {
    uint16_t sequence;     /* Ghép quyết định của Main với đúng yêu cầu OTA. */
    BaseType_t allow;      /* pdTRUE: cho phép; pdFALSE: từ chối hoặc hủy. */
} Comm_OtaReply;

typedef enum {
    OTA_IDLE,             /* Chưa có phiên OTA. */
    OTA_WAIT_MAIN,        /* Chờ Main đóng cửa, dừng cảm biến và trả lời. */
    OTA_QUIET,            /* Đã gửi READY; ngừng log/HELLO để ESP chuyển sang ROM. */
    OTA_CANCELLING        /* Giữ thông báo hủy đến khi chuyển được cho Main. */
} Comm_OtaPhase;

typedef struct {
    Comm_OtaPhase phase;  /* Trạng thái của một phiên OTA duy nhất. */
    uint16_t sequence;    /* Sequence do ESP cấp, giữ nguyên khi READY/ABORT. */
    uint32_t size;        /* Kích thước ảnh để Main kiểm tra giới hạn flash. */
    TickType_t since;     /* Mốc tính timeout của trạng thái hiện tại. */
    BaseType_t decision;  /* -1: chưa trả lời; 0: từ chối; 1: cho phép. */
    BaseType_t prepare;   /* Còn phải chuyển OTA_PREPARE cho Main. */
    BaseType_t abort;     /* Còn phải chuyển OTA_ABORT cho Main. */
    BaseType_t wire_abort;/* Còn phải gửi ABORT về ESP khi UART hết bận. */
} Comm_Ota;

typedef struct {
    Comm_Event status;    /* Trạng thái mạng mới nhất khi queue Main đầy. */
    Comm_Event names;     /* Phiên bản bảng tên mới nhất đã áp dụng. */
    Comm_Event reject;    /* Mã từ chối mới nhất của log đang chờ ACK. */
    BaseType_t has_status, has_names, has_reject; /* Cờ cần gửi lại sự kiện. */
} Comm_Notices;

static QueueHandle_t log_queue, event_queue, reply_queue; /* Ba chiều trao đổi riêng. */
static Comm_Users names;                       /* Bảng tên đang dùng và bảng nhận dở. */
static uint32_t boot_id, event_counter, rejected_logs; /* Định danh và thống kê log lỗi. */
static UBaseType_t staged_logs;                /* Bản Main đang giữ bằng Comm_LogBuffer. */
static uint32_t clock_epoch;                   /* Giờ UTC ở thời điểm clock_tick. */
static TickType_t clock_tick;                  /* Tick tương ứng với clock_epoch. */
static BaseType_t clock_valid, ota_quiet;       /* Cờ đồng bộ giờ và cờ ngừng truyền OTA. */
static void vTaskComm(void *argument);         /* Hàm chạy task, tạo qua API bên dưới. */

static TickType_t prvTicks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms); /* Đổi ms theo tần số tick của dự án. */
    return ticks != 0 ? ticks : 1;       /* Không biến thời gian chờ ngắn thành 0. */
}

void vTaskCommInitHardware(void)
{
    CommUart_Init(); /* Chỉ cấu hình GPIO/USART; chưa bật ISR gọi FreeRTOS. */
}

static void prvDeleteQueues(void)
{
    if (reply_queue != NULL) { vQueueDelete(reply_queue); } /* Thu hồi queue đã tạo. */
    if (event_queue != NULL) { vQueueDelete(event_queue); }
    if (log_queue != NULL) { vQueueDelete(log_queue); }
    reply_queue = event_queue = log_queue = NULL; /* Có thể thử lại sau lỗi khởi tạo. */
}

BaseType_t xTaskCommCreate(UBaseType_t priority)
{
    if (log_queue != NULL) { return pdFAIL; } /* Không tạo hai task cùng giữ USART1. */
    log_queue = xQueueCreate(COMM_LOG_QUEUE_LEN, sizeof(Comm_Log));
    event_queue = xQueueCreate(COMM_EVENT_QUEUE_LEN, sizeof(Comm_Event));
    reply_queue = xQueueCreate(2, sizeof(Comm_OtaReply)); /* Quyết định của Main. */
    if (log_queue == NULL || event_queue == NULL || reply_queue == NULL ||
        !CommIdentity_ReserveBoot(&boot_id)) {
        prvDeleteQueues(); /* Không rò heap nếu tạo thiếu queue hoặc flash lỗi. */
        return pdFAIL;
    }
    if (xTaskCreate(vTaskComm, "COMM", COMM_TASK_STACK, NULL, priority, NULL) != pdPASS) {
        prvDeleteQueues(); /* Task chưa chạy nên có thể thu hồi queue vừa cấp. */
        return pdFAIL;
    }
    return pdPASS; /* Phải gọi API này trước khi scheduler bắt đầu. */
}

static BaseType_t prvMakeLog(Comm_Log *record, uint16_t user, uint8_t method,
                            uint8_t result, uint8_t door, uint8_t reason)
{
    if (log_queue == NULL || ota_quiet || event_counter == UINT32_MAX ||
        method < 1U || method > 3U || result > 2U || door > 2U) {
        rejected_logs++; /* Main nhận pdFAIL để biết log chưa được tiếp nhận. */
        return pdFAIL;
    }
    memset(record, 0, sizeof(*record)); /* Không có giờ thì epoch giữ bằng 0. */
    record->device = COMM_DEVICE_ID;
    taskENTER_CRITICAL(); /* Comm có thể cập nhật đồng hồ khi Main tạo log. */
    record->event = ((uint64_t)boot_id << 32) | ++event_counter;
    record->time_quality = clock_valid ? 1U : 0U;
    if (clock_valid) {
        record->epoch = clock_epoch +
            (uint32_t)((TickType_t)(xTaskGetTickCount() - clock_tick) / configTICK_RATE_HZ);
    }
    taskEXIT_CRITICAL();
    record->user = user;       /* FFFF nếu không xác định được người dùng. */
    record->method = method;   /* 1: vân tay; 2: PIN; 3: hệ thống. */
    record->result = result;   /* 0: thất bại; 1: thành công; 2: cảnh báo. */
    record->door = door;       /* Trạng thái lệnh cửa, không phải số đo cảm biến. */
    record->reason = reason;   /* Mã lý do do nhóm thống nhất, không chứa PIN. */
    return pdPASS;
}

BaseType_t xTaskCommLog(uint16_t user, uint8_t method, uint8_t result,
                       uint8_t door, uint8_t reason)
{
    Comm_Log record;
    BaseType_t sent;
    /* Không cho API cũ đưa bản mới vượt trước bản Main đang giữ để thử lại. */
    if (staged_logs != 0) { rejected_logs++; return pdFAIL; }
    if (prvMakeLog(&record, user, method, result, door, reason) != pdPASS) { return pdFAIL; }
    sent = xQueueSendToBack(log_queue, &record, 0); /* Không chặn Main khi queue đầy. */
    if (sent != pdPASS) { rejected_logs++; }
    return sent;
}

BaseType_t xTaskCommLogPrepare(Comm_LogBuffer *buffer, uint16_t user, uint8_t method,
                             uint8_t result, uint8_t door, uint8_t reason)
{
    if (buffer == NULL || buffer->pending) {
        rejected_logs++; /* Không ghi đè bản Main chưa chuyển được cho Comm. */
        return pdFAIL;
    }
    if (prvMakeLog(&buffer->record, user, method, result, door, reason) != pdPASS) { return pdFAIL; }
    taskENTER_CRITICAL();
    buffer->pending = pdTRUE;
    staged_logs++; /* Tính cả bản này khi xét còn log trước OTA. */
    taskEXIT_CRITICAL();
    return pdPASS;
}

BaseType_t xTaskCommLogFlush(Comm_LogBuffer *buffer)
{
    BaseType_t sent;
    if (buffer == NULL) { rejected_logs++; return pdFAIL; }
    if (!buffer->pending) { return pdPASS; } /* Gọi lại sau thành công không tạo bản trùng. */
    if (log_queue == NULL || ota_quiet) { rejected_logs++; return pdFAIL; }
    taskENTER_CRITICAL(); /* Chuyển quyền giữ và cập nhật tổng số log trong cùng một lượt. */
    sent = xQueueSendToBack(log_queue, &buffer->record, 0);
    if (sent == pdPASS) {
        buffer->pending = pdFALSE;
        staged_logs--;
    } else {
        rejected_logs++; /* Queue đầy: giữ nguyên record và pending để Main thử lại. */
    }
    taskEXIT_CRITICAL();
    return sent;
}

uint32_t ulTaskCommRejectedLogs(void)
{
    return rejected_logs; /* Queue đầy, tham số sai, hết ID hoặc đang quiet. */
}

UBaseType_t uxTaskCommPendingLogs(void)
{
    UBaseType_t count;
    taskENTER_CRITICAL();
    count = staged_logs + (log_queue != NULL ? uxQueueMessagesWaiting(log_queue) : 0);
    taskEXIT_CRITICAL();
    return count;
}

BaseType_t xTaskCommGetEvent(Comm_Event *event, TickType_t wait)
{
    if (event_queue == NULL || event == NULL) { return pdFAIL; }
    return xQueueReceive(event_queue, event, wait); /* Main lấy sự kiện qua đây. */
}

BaseType_t xTaskCommOtaRespond(uint16_t sequence, BaseType_t allow)
{
    Comm_OtaReply reply;
    if (reply_queue == NULL) { return pdFAIL; }
    reply.sequence = sequence;
    reply.allow = allow != pdFALSE; /* Chuẩn hóa về 0/1 trước khi vào queue. */
    return xQueueSendToBack(reply_queue, &reply, 0); /* Main thử lại nếu pdFAIL. */
}

BaseType_t xTaskCommCopyName(uint16_t user, char *out, size_t capacity)
{
    uint16_t i;
    size_t count;
    BaseType_t found = pdFALSE;
    if (out == NULL || capacity == 0) { return pdFALSE; }
    out[0] = '\0'; /* Không trả chuỗi rác khi ID chưa có trong bảng. */
    taskENTER_CRITICAL(); /* Không đọc đúng lúc Comm thay bảng đang dùng. */
    for (i = 0; i < names.active_count; ++i) {
        if (names.active[i].id == user) {
            count = names.active[i].length < capacity - 1U ? names.active[i].length : capacity - 1U;
            memcpy(out, names.active[i].name, count);
            out[count] = '\0'; /* Chừa byte kết thúc kể cả khi phải cắt tên. */
            found = pdTRUE;
            break;
        }
    }
    taskEXIT_CRITICAL();
    return found;
}

static int prvSend(const Comm_Frame *frame)
{
    uint8_t bytes[COMM_FRAME_MAX]; /* Driver chép dữ liệu này vào bộ đệm TX. */
    size_t count = Comm_Encode(frame, bytes, sizeof(bytes));
    return count != 0 && CommUart_Send(bytes, count); /* Bận thì thử lại ở vòng sau. */
}

static void prvCancelOta(Comm_Ota *ota, BaseType_t send_abort)
{
    ota->phase = OTA_CANCELLING; /* Chưa nhận phiên mới khi chưa báo hủy cho Main. */
    ota->prepare = pdFALSE;
    ota->abort = pdTRUE;
    ota->wire_abort = send_abort;
    ota_quiet = pdFALSE; /* Cho log/HELLO chạy lại trong khi chuyển thông báo hủy. */
}

static void prvPublish(Comm_Ota *ota, Comm_Notices *notices, BaseType_t link,
                       BaseType_t *reported_link, BaseType_t *reported_full)
{
    Comm_Event event = {0};
    event.sequence = ota->sequence;
    if (ota->abort || ota->prepare) { /* Thử báo OTA trước thông báo mạng. */
        event.type = ota->abort ? COMM_EVT_OTA_ABORT : COMM_EVT_OTA_PREPARE;
        event.value = ota->size;
        if (xQueueSendToBack(event_queue, &event, 0) != pdPASS) { return; }
        ota->abort = ota->prepare = pdFALSE;
    }
    if (link != *reported_link) {
        event.type = COMM_EVT_LINK;
        event.link = link;
        event.value = 0;
        event.sequence = 0;
        if (xQueueSendToBack(event_queue, &event, 0) != pdPASS) { return; }
        *reported_link = link; /* Chỉ ghi đã báo khi queue nhận thành công. */
    }
    {
        UBaseType_t count = uxQueueMessagesWaiting(log_queue);
        BaseType_t full = count >= COMM_LOG_QUEUE_LEN;
        if (full != *reported_full) {
            memset(&event, 0, sizeof(event));
            event.type = COMM_EVT_LOG_QUEUE;
            event.value = count; /* Main được báo khi queue đầy và khi có chỗ trở lại. */
            if (xQueueSendToBack(event_queue, &event, 0) != pdPASS) { return; }
            *reported_full = full;
        }
    }
    if (notices->has_status &&
        xQueueSendToBack(event_queue, &notices->status, 0) == pdPASS) {
        notices->has_status = pdFALSE;
    }
    if (notices->has_names &&
        xQueueSendToBack(event_queue, &notices->names, 0) == pdPASS) {
        notices->has_names = pdFALSE;
    }
    if (notices->has_reject &&
        xQueueSendToBack(event_queue, &notices->reject, 0) == pdPASS) {
        notices->has_reject = pdFALSE;
    }
}

static void vTaskComm(void *argument)
{
    Comm_Parser parser = {{0}, 0}; /* Ghép byte thành khung và kiểm tra CRC. */
    Comm_Frame frame, outgoing;
    Comm_Log pending, removed; /* pending vẫn nằm ở đầu queue tới khi có ACK. */
    Comm_Ota ota = {0};
    Comm_Notices notices = {0};
    Comm_OtaReply reply;
    TickType_t now, last_peer = 0, last_byte = 0, last_send = 0, last_hello = 0;
    uint32_t dropped = 0, seconds;
    uint16_t sequence = 0, pending_sequence = 0;
    uint8_t byte, attempts = 0;
    unsigned budget;
    BaseType_t has_pending = pdFALSE, reported_link = pdFALSE, seen_peer = pdFALSE, link;
    BaseType_t reported_full = pdFALSE;
    (void)argument; /* Task không cần tham số khởi động. */
    CommUart_Start(xTaskGetCurrentTaskHandle()); /* Scheduler đã chạy, giờ mới bật IRQ. */
    for (;;) {
        now = xTaskGetTickCount();
        taskENTER_CRITICAL();
        if (clock_valid) { /* Dời mốc mỗi giây để giờ không quay lui sau một vòng tick. */
            seconds = (uint32_t)((TickType_t)(now - clock_tick) / configTICK_RATE_HZ);
            clock_epoch += seconds;
            clock_tick += (TickType_t)(seconds * configTICK_RATE_HZ);
        }
        taskEXIT_CRITICAL();
        if (CommUart_Dropped() != dropped) {
            dropped = CommUart_Dropped();
            parser.used = 0; /* Bỏ khung thiếu byte sau lỗi UART hoặc tràn RX. */
            names.collecting = 0; /* Không áp dụng bảng tên nhận thiếu hàng. */
        }
        if ((TickType_t)(now - last_byte) >= prvTicks(250)) { parser.used = 0; }
        for (budget = 0; budget < 256 && CommUart_Read(&byte); ++budget) {
            last_byte = xTaskGetTickCount();
            Comm_ParserFeed(&parser, byte);
            while (Comm_ParserNext(&parser, &frame)) {
                last_peer = xTaskGetTickCount(); /* Chỉ khung hợp lệ mới chứng tỏ peer còn sống. */
                seen_peer = pdTRUE;
                if (frame.type == COMM_ACK_STORED && has_pending && frame.length == 12 &&
                    frame.sequence == pending_sequence &&
                    Comm_GetLe(frame.payload, 4) == pending.device &&
                    Comm_GetLe(frame.payload + 4, 8) == pending.event) {
                    xQueueReceive(log_queue, &removed, 0); /* Chỗ duy nhất xóa log. */
                    has_pending = pdFALSE;
                    attempts = 0;
                } else if (frame.type == COMM_REJECT && has_pending && frame.length == 1 &&
                           frame.sequence == pending_sequence) {
                    notices.reject.type = COMM_EVT_REJECT;
                    notices.reject.sequence = frame.sequence;
                    notices.reject.value = frame.payload[0]; /* Báo lỗi, vẫn giữ log. */
                    notices.has_reject = pdTRUE;
                } else if (frame.type == COMM_TIME && frame.length == 4 &&
                           Comm_GetLe(frame.payload, 4) >= 1577836800UL) {
                    taskENTER_CRITICAL();
                    clock_epoch = (uint32_t)Comm_GetLe(frame.payload, 4);
                    clock_tick = xTaskGetTickCount();
                    clock_valid = pdTRUE; /* Không sửa thời gian của log cũ đang chờ gửi. */
                    taskEXIT_CRITICAL();
                } else if (frame.type == COMM_STATUS && frame.length == 4) {
                    notices.status.type = COMM_EVT_STATUS;
                    notices.status.sequence = frame.sequence;
                    notices.status.wifi = frame.payload[0] != 0;
                    notices.status.cloud = frame.payload[1] != 0;
                    notices.status.value = (uint32_t)Comm_GetLe(frame.payload + 2, 2);
                    notices.has_status = pdTRUE; /* Queue đầy: giữ trạng thái mới nhất. */
                } else if (frame.type >= COMM_USERS_BEGIN && frame.type <= COMM_USERS_END) {
                    int applied;
                    /* Sua 24/11: kiem tra + CRC32 NGOAI vung gang (staging chi task nay dung);
                     * chi khoa luc chep sang bang active ma xTaskCommCopyName doc. */
                    applied = Comm_UsersAccept(&names, &frame);
                    if (applied) {
                        taskENTER_CRITICAL();
                        Comm_UsersCommit(&names);
                        taskEXIT_CRITICAL();
                    }
                    if (applied) {
                        notices.names.type = COMM_EVT_NAMES;
                        notices.names.sequence = frame.sequence;
                        notices.names.value = names.version;
                        notices.has_names = pdTRUE;
                    }
                } else if (frame.type == COMM_OTA_PREPARE && frame.length == 36 && ota.phase == OTA_IDLE) {
                    ota.phase = OTA_WAIT_MAIN;
                    ota.sequence = frame.sequence;
                    ota.size = (uint32_t)Comm_GetLe(frame.payload, 4);
                    ota.since = last_peer;
                    ota.decision = -1;
                    ota.prepare = pdTRUE; /* Main xét cửa/menu; Comm không tự quyết định. */
                } else if (frame.type == COMM_OTA_ABORT && frame.length == 0 &&
                           (ota.phase == OTA_WAIT_MAIN || ota.phase == OTA_QUIET) &&
                           frame.sequence == ota.sequence) {
                    prvCancelOta(&ota, pdFALSE); /* ESP đã hủy, không cần echo ABORT. */
                }
            }
        }
        now = xTaskGetTickCount();
        while (xQueueReceive(reply_queue, &reply, 0) == pdPASS) {
            if (ota.phase == OTA_WAIT_MAIN && reply.sequence == ota.sequence) {
                if (!reply.allow || ota.decision != pdFALSE) {
                    ota.decision = reply.allow; /* Hủy luôn thắng ALLOW còn chờ TX. */
                }
            } else if (ota.phase == OTA_QUIET && reply.sequence == ota.sequence && !reply.allow) {
                prvCancelOta(&ota, pdTRUE); /* Main vẫn có thể hủy sau READY. */
            }
        }
        if (ota.phase == OTA_WAIT_MAIN &&
            (ota.decision == pdFALSE || (TickType_t)(now - ota.since) >= prvTicks(COMM_OTA_MAIN_MS))) {
            prvCancelOta(&ota, pdTRUE); /* Quá hạn cũng báo cả Main và ESP. */
        }
        if (ota.phase == OTA_QUIET && (TickType_t)(now - ota.since) >= prvTicks(COMM_OTA_HOLD_MS)) {
            prvCancelOta(&ota, pdTRUE); /* ESP chưa reset MCU: Main khôi phục hoạt động. */
        }
        if (ota.phase == OTA_WAIT_MAIN && ota.decision == pdTRUE && !has_pending &&
            uxTaskCommPendingLogs() == 0) {
            memset(&outgoing, 0, sizeof(outgoing));
            outgoing.type = COMM_OTA_READY;
            outgoing.sequence = ota.sequence;
            if (prvSend(&outgoing)) {
                ota.phase = OTA_QUIET;
                ota.since = now;
                ota_quiet = pdTRUE; /* Main không được tạo log mới ở giai đoạn này. */
            }
        }
        if (ota.phase == OTA_CANCELLING && ota.wire_abort) {
            memset(&outgoing, 0, sizeof(outgoing));
            outgoing.type = COMM_OTA_ABORT;
            outgoing.sequence = ota.sequence;
            if (prvSend(&outgoing)) { ota.wire_abort = pdFALSE; }
        }
        link = seen_peer && (TickType_t)(now - last_peer) < prvTicks(COMM_LINK_TIMEOUT_MS);
        prvPublish(&ota, &notices, link, &reported_link, &reported_full);
        if (ota.phase == OTA_CANCELLING && !ota.abort && !ota.wire_abort) {
            ota.phase = OTA_IDLE; /* Hủy cũ đã vào queue trước mọi PREPARE mới. */
        }
        if (!ota_quiet) { /* Vẫn gửi log khi chuẩn bị OTA để Main có thể chờ queue rỗng. */
            if ((TickType_t)(now - last_hello) >= prvTicks(COMM_HEARTBEAT_MS)) {
                memset(&outgoing, 0, sizeof(outgoing));
                outgoing.type = COMM_HELLO;
                outgoing.sequence = sequence + 1U;
                outgoing.length = 4;
                Comm_PutLe(outgoing.payload, COMM_FIRMWARE_VERSION, 4);
                if (prvSend(&outgoing)) { sequence++; last_hello = now; }
            } else {
                if (!has_pending && xQueuePeek(log_queue, &pending, 0) == pdPASS) {
                    has_pending = pdTRUE;
                    pending_sequence = ++sequence; /* Cấp một lần cho bản ghi đầu queue. */
                    attempts = 0;
                }
                if (has_pending && (attempts == 0 || (TickType_t)(now - last_send) >=
                    prvTicks(attempts < 3 ? COMM_ACK_MS : COMM_RETRY_SLOW_MS))) {
                    memset(&outgoing, 0, sizeof(outgoing));
                    outgoing.type = COMM_LOG;
                    outgoing.sequence = pending_sequence;
                    outgoing.length = COMM_LOG_BYTES;
                    Comm_EncodeLog(&pending, outgoing.payload);
                    if (prvSend(&outgoing)) {
                        if (attempts < 255) { attempts++; } /* Không cho quay về 0. */
                        last_send = now; /* Tính timeout sau khi driver nhận khung. */
                    }
                }
            }
        }
        ulTaskNotifyTake(pdTRUE, prvTicks(20)); /* ISR đánh thức sớm; timeout phục vụ retry/OTA. */
    }
}
