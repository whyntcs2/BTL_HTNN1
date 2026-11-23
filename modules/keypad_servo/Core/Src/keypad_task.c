/**
 * @file keypad_task.c
 * @brief Quét, chống dội và giữ FIFO phím cho tới khi phía Main nhận được.
 */
#include "keypad_task.h" /* Giao diện callback và dung lượng FIFO cục bộ. */
#include "keypad.h"      /* Driver quét ma trận, không chứa xử lý PIN. */
#include "FreeRTOS.h"    /* Kiểu tick và phép đổi mili giây sang tick. */
#include "task.h"        /* API thời gian của task. */

#define SCAN_PERIOD_MS 20U /* Chu kỳ lấy mẫu keypad. */
#define DEBOUNCE_MS    30U /* Với mẫu 20 ms, thường xác nhận sau 40 ms. */

static uint32_t dropped_keys; /* Chỉ một KeypadTask được phép cập nhật bộ đếm. */

uint32_t KeypadTask_GetDroppedKeys(void)
{
    uint32_t count;
    taskENTER_CRITICAL(); /* Chép nhất quán khi Main đọc thống kê của task. */
    count = dropped_keys;
    taskEXIT_CRITICAL();
    return count;
}

void KeypadTask(void *argument)
{
    const KeypadTaskConfig_t *config = (const KeypadTaskConfig_t *)argument;
    char pending[KEYPAD_PENDING_LENGTH]; /* FIFO giữ cả phím đã nhả nhưng chưa gửi được. */
    unsigned int head = 0; /* Vị trí ký tự cũ nhất chưa được bên nhận tiếp nhận. */
    unsigned int count = 0; /* Số ký tự hiện đang nằm trong FIFO. */
    BaseType_t armed = pdTRUE; /* Mỗi lần nhấn ổn định chỉ được ghi FIFO một lần. */
    char candidate = 0; /* Trạng thái đang được kiểm tra chống dội. */
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t candidate_since = last_wake;
    TickType_t period = pdMS_TO_TICKS(SCAN_PERIOD_MS);
    TickType_t debounce = pdMS_TO_TICKS(DEBOUNCE_MS);
    if (period == 0U) { period = 1U; } /* Không cho vòng quét chạy không nghỉ. */
    if (debounce == 0U) { debounce = 1U; }

    for (;;) {
        char raw = Keypad_Scan(); /* Vẫn quét/nhả/chống dội khi queue Main đang đầy. */
        TickType_t now = xTaskGetTickCount();
        if (raw == KEYPAD_MULTIPLE_KEYS) {
            armed = pdFALSE; /* Nhấn chồng: phải nhả toàn bộ trước lần nhận mới. */
            candidate = raw;
            candidate_since = now;
        } else {
            if (raw != candidate) {
                candidate = raw;
                candidate_since = now; /* Chỉ tính thời gian kể từ trạng thái mới nhất. */
            }
            if ((TickType_t)(now - candidate_since) >= debounce) {
                if (candidate == 0) {
                    armed = pdTRUE; /* Nhả ổn định cho phép lần nhấn tiếp theo. */
                } else if (armed != pdFALSE) {
                    if (count < KEYPAD_PENDING_LENGTH) {
                        pending[(head + count) % KEYPAD_PENDING_LENGTH] = candidate;
                        count++; /* Nhận ký tự vào FIFO trước, không phụ thuộc queue Main. */
                    } else {
                        taskENTER_CRITICAL();
                        dropped_keys++; /* FIFO hữu hạn: bỏ phím mới, giữ nguyên các phím cũ. */
                        taskEXIT_CRITICAL();
                    }
                    armed = pdFALSE; /* Giữ phím không được tạo thêm bản sao hoặc đếm tràn lặp. */
                }
            }
        }
        if (count != 0U && config->send_key(pending[head], config->context)) {
            head = (head + 1U) % KEYPAD_PENDING_LENGTH;
            count--; /* Chỉ bỏ ký tự khi callback xác nhận đã tiếp nhận. */
        }
        /* Callback trả false: giữ nguyên đầu FIFO, thử lại ở lượt sau kể cả đã nhả phím. */
        vTaskDelayUntil(&last_wake, period);
    }
}
