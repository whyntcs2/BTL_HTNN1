#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); exit(1); } } while (0)
/* Chạy nguyên task với RTOS/UART giả; không biên dịch file này vào firmware. */
#include "../Core/Src/task_comm.c"

typedef struct { unsigned capacity, size, count, head; uint8_t data[16][64]; } FakeQueue;
static FakeQueue queues[3];
static unsigned queue_count, queue_calls, deleted, init_count, steps;
static unsigned sent_logs, sent_ready, sent_abort, busy_sends, identity_calls;
static unsigned fail_queue_at;
static int fail_boot, fail_task;
static uint16_t sequences[32];
static Comm_Log expected_log;
static TickType_t tick, stop_tick;
static TaskFunction_t task_entry;
static jmp_buf task_exit;
static uint8_t received[4096];
static size_t rx_head, rx_tail;
static uint32_t rx_dropped;
static int mode, phase;
enum { NO_ACK, ACK_TEST, MAIN_ALLOW, MAIN_DENY, NO_MAIN, DRAIN_EVENTS,
       CANCEL_FULL, CANCEL_ALLOW, PEER_REJECT, QUIET_TIMEOUT, BAD_ABORT, TIME_SYNC };

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t size)
{
    FakeQueue *queue;
    queue_calls++;
    if (queue_calls == fail_queue_at) { return NULL; }
    CHECK(queue_count < 3 && length <= 16 && size <= 64);
    queue = &queues[queue_count++];
    queue->capacity = length; queue->size = size;
    return queue;
}
void vQueueDelete(QueueHandle_t queue) { CHECK(queue != NULL); deleted++; }
BaseType_t xQueueSendToBack(QueueHandle_t handle, const void *item, TickType_t wait)
{
    FakeQueue *queue = handle; CHECK(queue != NULL && item != NULL && wait == 0);
    if (queue->count == queue->capacity) { return pdFAIL; }
    memcpy(queue->data[(queue->head + queue->count) % queue->capacity], item, queue->size);
    queue->count++; return pdPASS;
}
BaseType_t xQueuePeek(QueueHandle_t handle, void *item, TickType_t wait)
{
    FakeQueue *queue = handle; CHECK(queue != NULL && item != NULL && wait == 0);
    if (queue->count == 0) { return pdFAIL; }
    memcpy(item, queue->data[queue->head], queue->size); return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t handle, void *item, TickType_t wait)
{
    FakeQueue *queue = handle;
    if (xQueuePeek(handle, item, wait) != pdPASS) { return pdFAIL; }
    queue->head = (queue->head + 1) % queue->capacity; queue->count--; return pdPASS;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) { return ((FakeQueue *)queue)->count; }
TickType_t xTaskGetTickCount(void) { return tick; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, size_t stack, void *arg, UBaseType_t priority, TaskHandle_t *handle)
{
    CHECK(strcmp(name, "COMM") == 0 && stack == COMM_TASK_STACK && arg == NULL && priority == 2 && handle == NULL);
    task_entry = entry; return fail_task ? pdFAIL : pdPASS;
}
int CommIdentity_ReserveBoot(uint32_t *boot) { identity_calls++; *boot = 7; return !fail_boot; }
void CommUart_Init(void) { init_count++; }
void CommUart_Start(TaskHandle_t owner) { CHECK(owner == (void *)1); }
uint32_t CommUart_Dropped(void) { return rx_dropped; }
int CommUart_Read(uint8_t *byte)
{
    if (rx_tail == rx_head) { return 0; }
    *byte = received[rx_tail++]; return 1;
}
int CommUart_Send(const uint8_t *bytes, size_t size)
{
    Comm_Parser parser = {{0}, 0}; Comm_Frame frame; size_t i;
    if (busy_sends != 0) { busy_sends--; return 0; }
    for (i = 0; i < size; ++i) { Comm_ParserFeed(&parser, bytes[i]); }
    CHECK(Comm_ParserNext(&parser, &frame));
    if (frame.type == COMM_LOG) {
        CHECK(sent_logs < 32 && frame.length == COMM_LOG_BYTES);
        CHECK(Comm_GetLe(frame.payload, 4) == expected_log.device);
        CHECK(Comm_GetLe(frame.payload + 4, 8) == expected_log.event);
        sequences[sent_logs++] = frame.sequence;
    }
    if (frame.type == COMM_OTA_READY) {
        CHECK(uxTaskCommPendingLogs() == 0); /* Bất biến cần giữ trước khi ESP reset STM32. */
        sent_ready++;
    }
    if (frame.type == COMM_OTA_ABORT) { sent_abort++; }
    return 1;
}
static void inject(const Comm_Frame *frame)
{
    uint8_t bytes[COMM_FRAME_MAX]; size_t size = Comm_Encode(frame, bytes, sizeof(bytes));
    CHECK(size != 0 && rx_head + size <= sizeof(received));
    memcpy(received + rx_head, bytes, size); rx_head += size;
}
static void inject_ack(uint16_t sequence, uint64_t event)
{
    Comm_Frame frame = {0}; frame.type = COMM_ACK_STORED;
    frame.sequence = sequence; frame.length = 12;
    Comm_PutLe(frame.payload, expected_log.device, 4);
    Comm_PutLe(frame.payload + 4, event, 8); inject(&frame);
}
static void prepare_sequence(uint16_t sequence)
{
    Comm_Frame frame = {0}; frame.type = COMM_OTA_PREPARE;
    frame.sequence = sequence; frame.length = 36;
    Comm_PutLe(frame.payload, 1024, 4); inject(&frame);
}
static void fill_events(void)
{
    Comm_Event event = {0}; event.type = COMM_EVT_NAMES; event.value = 0xDEAD;
    while (xQueueSendToBack(event_queue, &event, 0) == pdPASS) { }
}
static void discard_events(void)
{
    Comm_Event event;
    while (xTaskCommGetEvent(&event, 0) == pdPASS) { }
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait)
{
    Comm_Event event; Comm_Frame frame = {0};
    CHECK(clear == pdTRUE && wait == 2); steps++; tick += wait;
    if (mode == ACK_TEST && sent_logs != 0) {
        if (phase == 0) { CHECK(uxTaskCommPendingLogs() == 1); inject_ack(sequences[0] + 1, expected_log.event); phase++; }
        else if (phase == 1) { CHECK(uxTaskCommPendingLogs() == 1); inject_ack(sequences[0], expected_log.event + 1); phase++; }
        else if (phase == 2) { CHECK(uxTaskCommPendingLogs() == 1); inject_ack(sequences[0], expected_log.event); phase++; }
    }
    if (mode == MAIN_ALLOW || mode == MAIN_DENY || mode == CANCEL_ALLOW || mode == QUIET_TIMEOUT || mode == BAD_ABORT) {
        while (xTaskCommGetEvent(&event, 0) == pdPASS) {
            if (event.type == COMM_EVT_OTA_PREPARE) {
                CHECK(sent_ready == 0);
                CHECK(xTaskCommOtaRespond(event.sequence + 1, pdTRUE) == pdPASS); phase = 1;
            }
        }
        if (phase == 1 && steps >= 3) {
            CHECK(sent_ready == 0);
            CHECK(xTaskCommOtaRespond(99, mode != MAIN_DENY) == pdPASS); phase++;
            if (mode == CANCEL_ALLOW) { CHECK(xTaskCommOtaRespond(99, pdFALSE) == pdPASS); busy_sends = 5; }
        }
        if (mode != MAIN_DENY && mode != CANCEL_ALLOW && tick < 30) {
            CHECK(sent_ready == 0); /* ALLOW sớm vẫn phải chờ ACK của log hiện tại. */
        }
        if (tick == 30 && sent_logs != 0 && mode != MAIN_DENY && mode != CANCEL_ALLOW) {
            inject_ack(sequences[0], expected_log.event); busy_sends = 5;
        }
        if (mode == BAD_ABORT && tick == 50) {
            frame.type = COMM_OTA_ABORT; frame.sequence = 98; inject(&frame);
            frame.sequence = 99; frame.length = 1; inject(&frame);
        }
    }
    if (mode == DRAIN_EVENTS && steps == 5) { discard_events(); }
    if (mode == CANCEL_FULL) {
        if (steps == 2) {
            frame.type = COMM_OTA_ABORT; frame.sequence = 99; inject(&frame);
            prepare_sequence(100); /* Không được ghi đè thông báo hủy 99 đang kẹt. */
        }
        if (steps == 4) { discard_events(); }
        if (steps == 6) { prepare_sequence(100); }
    }
    if (mode == PEER_REJECT && steps == 2) {
        frame.type = COMM_REJECT; frame.length = 1; frame.sequence = sequences[0];
        frame.payload[0] = 2; inject(&frame);
    }
    if (mode == TIME_SYNC && steps == 3) {
        CHECK(xTaskCommLog(4, 1, 1, 1, 0) == pdPASS);
    }
    if (tick >= stop_tick) { longjmp(task_exit, 1); }
    return 0;
}
static void clear_state(void)
{
    memset(queues, 0, sizeof(queues)); memset(&names, 0, sizeof(names));
    log_queue = event_queue = reply_queue = NULL;
    queue_count = queue_calls = deleted = init_count = steps = 0;
    sent_logs = sent_ready = sent_abort = busy_sends = identity_calls = 0;
    fail_queue_at = 0; fail_boot = fail_task = 0;
    tick = 0; mode = NO_ACK; phase = 0; rx_head = rx_tail = 0; rx_dropped = 0;
    clock_valid = ota_quiet = pdFALSE; clock_tick = clock_epoch = event_counter = rejected_logs = 0;
}
static void reset(int scenario, TickType_t duration)
{
    clear_state(); stop_tick = duration; mode = scenario;
    vTaskCommInitHardware(); CHECK(xTaskCommCreate(2) == pdPASS);
    CHECK(xTaskCommLog(3, 2, 1, 1, 0) == pdPASS);
    CHECK(xQueuePeek(log_queue, &expected_log, 0) == pdPASS);
    CHECK(expected_log.event == (((uint64_t)7 << 32) | 1));
}
static void run(void) { if (setjmp(task_exit) == 0) { task_entry(NULL); } CHECK(init_count == 1); }
static int find_event(Comm_EventType type, uint16_t sequence, uint32_t value)
{
    Comm_Event event;
    while (xTaskCommGetEvent(&event, 0) == pdPASS) {
        if (event.type == type && event.sequence == sequence && event.value == value) { return 1; }
    }
    return 0;
}
int main(void)
{
    Comm_Event event; Comm_Frame frame = {0}; Comm_Log record;
    unsigned saw_status, saw_names, saw_abort, saw_prepare;
    reset(NO_ACK, 810); run(); CHECK(uxTaskCommPendingLogs() == 1 && sent_logs >= 4);
    for (unsigned i = 1; i < sent_logs; ++i) { CHECK(sequences[i] == sequences[0]); }
    puts("PASS: retries retain record and sequence until ACK");
    reset(ACK_TEST, 20); run(); CHECK(phase == 3 && uxTaskCommPendingLogs() == 0);
    puts("PASS: wrong ACK sequence or event cannot remove a log");
    reset(NO_ACK, 20); busy_sends = 4; run(); CHECK(uxTaskCommPendingLogs() == 1 && sent_logs >= 1);
    puts("PASS: busy UART preserves pending log");
    reset(MAIN_ALLOW, 100); prepare_sequence(99); run(); CHECK(sent_ready == 1 && phase == 2);
    CHECK(ota_quiet && xTaskCommLog(1, 1, 1, 1, 0) == pdFAIL);
    puts("PASS: READY needs matching Main ALLOW, drained logs and available UART");
    reset(MAIN_DENY, 100); prepare_sequence(99); run(); CHECK(sent_ready == 0 && sent_abort == 1);
    puts("PASS: Main denial sends ABORT");
    reset(NO_MAIN, 1250); prepare_sequence(99); run();
    CHECK(sent_ready == 0 && sent_logs >= 2 && sent_abort == 1);
    CHECK(find_event(COMM_EVT_OTA_ABORT, 99, 1024));
    puts("PASS: Main timeout preserves logs and cancels both peers");
    reset(NO_ACK, 20);
    for (unsigned i = 1; i < COMM_LOG_QUEUE_LEN; ++i) { CHECK(xTaskCommLog(i, 1, 0, 2, 1) == pdPASS); }
    CHECK(xTaskCommLog(1, 1, 0, 2, 1) == pdFAIL && ulTaskCommRejectedLogs() == 1);
    puts("PASS: full log queue reports failure");
    reset(DRAIN_EVENTS, 30); fill_events();
    frame.type = COMM_STATUS; frame.sequence = 42; frame.length = 4;
    frame.payload[0] = frame.payload[1] = 1; Comm_PutLe(frame.payload + 2, 8, 2); inject(&frame);
    frame.type = COMM_USERS_BEGIN; frame.length = 6;
    Comm_PutLe(frame.payload, 5, 4); Comm_PutLe(frame.payload + 4, 0, 2); inject(&frame);
    frame.type = COMM_USERS_END; frame.length = 10; Comm_PutLe(frame.payload + 6, 0, 4); inject(&frame);
    run(); saw_status = saw_names = 0;
    while (xTaskCommGetEvent(&event, 0) == pdPASS) {
        if (event.type == COMM_EVT_STATUS) { CHECK(event.wifi && event.cloud && event.value == 8); saw_status++; }
        if (event.type == COMM_EVT_NAMES) { CHECK(event.value == 5); saw_names++; }
    }
    CHECK(saw_status == 1 && saw_names == 1);
    puts("PASS: full Main queue retries latest STATUS and NAMES");
    reset(CANCEL_FULL, 30); fill_events(); prepare_sequence(99); run(); saw_abort = saw_prepare = 0;
    while (xTaskCommGetEvent(&event, 0) == pdPASS) {
        if (event.type == COMM_EVT_OTA_ABORT) { CHECK(event.sequence == 99 && !saw_prepare); saw_abort++; }
        if (event.type == COMM_EVT_OTA_PREPARE) { CHECK(event.sequence == 100 && saw_abort == 1); saw_prepare++; }
    }
    CHECK(saw_abort == 1 && saw_prepare == 1 && sent_ready == 0);
    puts("PASS: queued OTA cancel cannot be overwritten by a new preparation");
    reset(CANCEL_ALLOW, 100); prepare_sequence(99); run(); CHECK(sent_ready == 0 && sent_abort == 1);
    puts("PASS: cancellation supersedes an ALLOW while TX is busy");
    reset(PEER_REJECT, 30); run(); CHECK(uxTaskCommPendingLogs() == 1);
    CHECK(find_event(COMM_EVT_REJECT, sequences[0], 2));
    puts("PASS: ESP REJECT is reported without deleting the log");
    reset(QUIET_TIMEOUT, 3100); prepare_sequence(99); run();
    CHECK(sent_ready == 1 && sent_abort == 1 && !ota_quiet);
    puts("PASS: failed OTA handoff leaves quiet state after timeout");
    reset(BAD_ABORT, 100); prepare_sequence(99); run();
    CHECK(sent_ready == 1 && sent_abort == 0 && ota_quiet);
    puts("PASS: wrong sequence and malformed ABORT cannot cancel a session");
    reset(NO_ACK, 20);
    CHECK(xTaskCommGetEvent(NULL, 0) == pdFAIL);
    CHECK(xTaskCommLog(1, 0, 1, 1, 0) == pdFAIL);
    CHECK(xTaskCommLog(1, 1, 3, 1, 0) == pdFAIL);
    CHECK(xTaskCommLog(1, 1, 1, 3, 0) == pdFAIL);
    event_counter = UINT32_MAX; CHECK(xTaskCommLog(1, 1, 1, 1, 0) == pdFAIL);
    CHECK(uxTaskCommPendingLogs() == 1 && rejected_logs == 4);
    puts("PASS: invalid log fields and exhausted event IDs are rejected locally");
    CHECK(xTaskCommCreate(2) == pdFAIL && identity_calls == 1);
    puts("PASS: repeated creation cannot create a second UART owner");
    for (unsigned i = 1; i <= 3; ++i) {
        clear_state(); fail_queue_at = i; CHECK(xTaskCommCreate(2) == pdFAIL);
        CHECK(deleted == 2 && log_queue == NULL && event_queue == NULL && reply_queue == NULL);
    }
    clear_state(); fail_boot = 1; CHECK(xTaskCommCreate(2) == pdFAIL && deleted == 3);
    clear_state(); fail_task = 1; CHECK(xTaskCommCreate(2) == pdFAIL && deleted == 3);
    CHECK(xTaskCommGetEvent(&event, 0) == pdFAIL && xTaskCommOtaRespond(1, pdTRUE) == pdFAIL);
    puts("PASS: queue, flash and task startup failures release allocated resources");
    reset(TIME_SYNC, 210);
    frame.type = COMM_TIME; frame.sequence = 1; frame.length = 4;
    Comm_PutLe(frame.payload, 1800000000UL, 4); inject(&frame); run();
    CHECK(xQueueReceive(log_queue, &record, 0) == pdPASS && record.time_quality == 0 && record.epoch == 0);
    CHECK(xQueueReceive(log_queue, &record, 0) == pdPASS && record.time_quality == 1 && record.epoch == 1800000000UL);
    CHECK(xTaskCommLog(4, 1, 1, 1, 0) == pdPASS);
    CHECK(xQueueReceive(log_queue, &record, 0) == pdPASS && record.epoch == 1800000002UL);
    puts("PASS: TIME applies to new logs while unsynchronized old logs stay unchanged");
    reset(NO_ACK, 700); frame.type = COMM_STATUS; frame.length = 4; inject(&frame); run();
    CHECK(find_event(COMM_EVT_LINK, 0, 0)); /* First UP has link=1; inspect final DOWN separately below. */
    CHECK(xTaskCommGetEvent(&event, 0) == pdPASS && event.type == COMM_EVT_STATUS);
    CHECK(xTaskCommGetEvent(&event, 0) == pdPASS && event.type == COMM_EVT_LINK && !event.link);
    puts("PASS: silent peer produces link down after timeout");
    puts("ALL 18 COMM TASK TEST GROUPS PASSED"); return 0;
}
