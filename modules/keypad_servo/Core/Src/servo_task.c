/**
 * @file servo_task.c
 * @brief Nhận lệnh cửa, tự đóng và phản hồi CLOSE_ACK theo mã yêu cầu của Main.
 */
#include "servo_task.h" /* Lệnh và callback phản hồi của thiết bị servo. */
#include "servo.h"      /* Driver PWM PA8/TIM1_CH1. */
#include "FreeRTOS.h"   /* Kiểu dữ liệu và cấu hình kernel. */
#include "queue.h"      /* Queue lệnh do phía ghép Main cấp. */
#include "task.h"       /* Tick và thời gian chờ khi thử lại phản hồi. */

#define SERVO_DEFAULT_HOLD_MS 3000U /* OPEN với hold_ms bằng 0 dùng giá trị này. */
#define SERVO_REPLY_RETRY_MS  20U   /* Không quay vòng liên tục khi queue phản hồi đầy. */

static ServoSendCloseReply_t close_reply_handler; /* Cấu hình trước scheduler. */
static void *close_reply_context; /* Dữ liệu của callback, thường là queue sự kiện Main. */

void ServoTask_SetCloseReplyHandler(ServoSendCloseReply_t handler, void *context)
{
    close_reply_handler = handler; /* Chỉ cấu hình trước khi ServoTask bắt đầu. */
    close_reply_context = context;
}

void ServoTask(void *argument)
{
    QueueHandle_t command_queue = (QueueHandle_t)argument; /* Giữ cách truyền queue cũ. */
    ServoSendCloseReply_t send_reply = close_reply_handler;
    void *reply_context = close_reply_context;
    ServoCommand_t command;
    ServoCloseReply_t reply = {0}; /* Giữ phản hồi tới khi Main đã tiếp nhận. */
    BaseType_t reply_pending = pdFALSE;
    BaseType_t is_open = pdFALSE; /* Servo_Init đã đặt xung đóng trước khi tạo task. */
    TickType_t opened_at = 0;
    TickType_t hold_ticks = 0;
    TickType_t retry_ticks = pdMS_TO_TICKS(SERVO_REPLY_RETRY_MS);
    if (retry_ticks == 0U) { retry_ticks = 1U; }

    for (;;) {
        TickType_t wait = portMAX_DELAY; /* Cửa đóng và không chờ reply thì ngủ tại queue. */
        if (reply_pending) {
            if (!send_reply(&reply, reply_context)) {
                /* Đang đóng: giữ nguyên ID phản hồi, chưa nhận lệnh kế tiếp để tránh mất ACK. */
                vTaskDelay(retry_ticks);
                continue;
            }
            reply_pending = pdFALSE; /* Callback xác nhận đã đưa phản hồi cho Main. */
        }
        if (is_open != pdFALSE) {
            TickType_t elapsed = (TickType_t)(xTaskGetTickCount() - opened_at);
            if (elapsed >= hold_ticks) {
                Servo_Close(); /* Tự đóng không được giả làm phản hồi cho CLOSE_ACK. */
                is_open = pdFALSE;
            } else {
                wait = hold_ticks - elapsed; /* Có lệnh CLOSE thì queue đánh thức sớm. */
            }
        }
        if (xQueueReceive(command_queue, &command, wait) != pdPASS) { continue; }
        if (command.type == SERVO_CMD_OPEN) {
            uint32_t hold_ms = command.hold_ms;
            if (hold_ms == 0U) { hold_ms = SERVO_DEFAULT_HOLD_MS; }
            hold_ticks = pdMS_TO_TICKS(hold_ms);
            if (hold_ticks == 0U) { hold_ticks = 1U; }
            Servo_Open();
            opened_at = xTaskGetTickCount(); /* OPEN tiếp theo đặt lại hạn tự đóng. */
            is_open = pdTRUE;
        } else if (command.type == SERVO_CMD_CLOSE || command.type == SERVO_CMD_CLOSE_ACK) {
            Servo_Close(); /* Ghi độ rộng xung đóng; chuyển động cơ khí diễn ra sau đó. */
            is_open = pdFALSE;
            if (command.type == SERVO_CMD_CLOSE_ACK && command.request_id != 0U && send_reply != NULL) {
                reply.request_id = command.request_id; /* Chỉ tạo ACK sau khi gọi driver đóng. */
                reply_pending = pdTRUE;
            }
        }
    }
}
