/**
 * @file    servo_task.c
 * @brief   Task nhận lệnh cửa từ Main và quản lý thời hạn tự đóng.
 * @note    Cấu hình GPIO/TIM1 và API PWM được tách trong servo.c.
 */

/* Sử dụng nguyên mẫu ServoTask để thống nhất khai báo giữa file .h và .c. */
#include "servo_task.h"

#include "servo.h" /* Sử dụng hai hàm mức thấp Servo_Open và Servo_Close để thay đổi PWM. */
#include "FreeRTOS.h" /* Nạp cấu hình và kiểu dữ liệu nền của FreeRTOS. */
#include "queue.h" /* Sử dụng QueueHandle_t và API nhận lệnh, không phụ thuộc queue toàn cục. */
#include "task.h" /* Sử dụng kiểu dữ liệu tick, trạng thái và API task của FreeRTOS. */

/* Đặt thời gian giữ mặc định là 3000 ms, áp dụng khi lệnh OPEN gửi hold_ms bằng 0. */
#define SERVO_DEFAULT_HOLD_MS   3000U

/* Định nghĩa task thực hiện lệnh servo; argument chứa handle queue do phần khởi tạo truyền vào. */
void ServoTask(void *argument)
{ /* Bắt đầu phần khai báo trạng thái điều khiển cửa. */
    /* Lấy handle queue từ tham số task; bên tạo task chịu trách nhiệm tạo queue trước. */
    QueueHandle_t command_queue = (QueueHandle_t)argument;
    /* Khai báo biến nhận một bản sao lệnh từ queue; chỉ đọc các trường sau khi nhận thành công. */
    ServoCommand_t command;
    /* Đặt trạng thái lệnh ban đầu là đóng, phù hợp PWM đóng đã cấu hình trong Servo_Init. */
    BaseType_t is_open = pdFALSE;
    /* Khởi tạo biến lưu tick phát lệnh mở gần nhất; chỉ dùng khi is_open đang được đặt. */
    TickType_t opened_at = 0;
    /* Khởi tạo thời gian giữ tính theo tick; sẽ được gán khi nhận lệnh OPEN. */
    TickType_t hold_ticks = 0;

    for (;;) /* Duy trì vòng xử lý lệnh trong suốt thời gian task hoạt động. */
    { /* Bắt đầu một lượt kiểm tra thời hạn và nhận lệnh mới. */
        /*
         * Mặc định chờ lệnh mới vô thời hạn khi đang đóng; cấu hình hiện tại bật
         * INCLUDE_vTaskSuspend.
         */
        TickType_t wait = portMAX_DELAY;

        if (is_open != pdFALSE) /* Kiểm tra cửa có đang ở trạng thái lệnh mở hay không. */
        { /* Tính thời gian còn lại trước khi tự đóng. */
            /*
             * Lấy hiệu tick từ lúc phát lệnh mở; kiểu không dấu cho phép tính qua rollover trong
             * khoảng ngắn đang dùng.
             */
            TickType_t elapsed = (TickType_t)(xTaskGetTickCount() - opened_at);

            /* So sánh thời gian đã qua với thời gian giữ được yêu cầu. */
            if (elapsed >= hold_ticks)
            { /* Xử lý trường hợp đã tới hoặc vượt thời hạn. */
                Servo_Close(); /* Đổi PWM về độ rộng xung đóng thông qua driver. */
                /*
                 * Ghi nhận trạng thái lệnh đã đóng; đây không phải tín hiệu xác nhận từ cảm biến vị
                 * trí cửa.
                 */
                is_open = pdFALSE;
            } /* Kết thúc xử lý hết thời gian giữ. */
            else /* Chuyển sang trường hợp cửa vẫn còn trong thời gian mở. */
            { /* Xác định khoảng chờ tối đa tới thời điểm cần tự đóng. */
                /*
                 * Lấy thời gian giữ trừ thời gian đã qua; kết quả cùng đơn vị tick với timeout của
                 * queue.
                 */
                wait = hold_ticks - elapsed;
            } /* Kết thúc tính timeout khi đang mở. */
        } /* Kết thúc kiểm tra trạng thái mở và thời hạn. */

        /*
         * Nhận một lệnh từ command_queue trong khoảng wait tick; có dữ liệu thì trả sớm, hết
         * timeout thì không vào nhánh pdPASS.
         */
        if (xQueueReceive(command_queue, &command, wait) == pdPASS)
        { /* Xử lý lệnh đã được sao chép vào biến command. */
            if (command.type == SERVO_CMD_OPEN) /* Kiểm tra lệnh có yêu cầu mở cửa hay không. */
            { /* Xử lý lệnh mở, bao gồm cả lệnh mở mới trong lúc cửa đang mở. */
                /*
                 * Lấy thời gian giữ từ lệnh vào biến 32 bit không dấu để xử lý trước khi đổi sang
                 * tick.
                 */
                uint32_t hold_ms = command.hold_ms;

                /* Kiểm tra người gửi có chọn giá trị 0 để dùng thời gian mặc định hay không. */
                if (hold_ms == 0U)
                { /* Áp dụng thời gian giữ mặc định của module. */
                    /* Gán 3000 ms thay cho giá trị 0; không dùng 0 làm yêu cầu đóng ngay. */
                    hold_ms = SERVO_DEFAULT_HOLD_MS;
                } /* Kết thúc chọn thời gian giữ mặc định. */

                /* Đổi mili giây sang tick theo tick rate hiện tại của FreeRTOS. */
                hold_ticks = pdMS_TO_TICKS(hold_ms);

                /* Kiểm tra thời gian nhỏ có bị làm tròn xuống 0 tick hay không. */
                if (hold_ticks == 0U)
                { /* Xử lý trường hợp kết quả quy đổi bằng 0. */
                    hold_ticks = 1U; /* Giữ tối thiểu 1 tick trước khi xét tự đóng. */
                } /* Kết thúc hiệu chỉnh thời gian giữ theo tick. */

                /* Đổi PWM sang độ rộng xung mở; chuyển động thực tế của servo xảy ra sau đó. */
                Servo_Open();
                /*
                 * Ghi tick phát lệnh mở mới, làm mốc tính thời hạn; lệnh OPEN tiếp theo sẽ đặt lại
                 * mốc này.
                 */
                opened_at = xTaskGetTickCount();
                is_open = pdTRUE; /* Đặt trạng thái lệnh là mở để vòng tiếp theo quản lý tự đóng. */
            } /* Kết thúc xử lý lệnh OPEN. */
            /* Nếu không phải OPEN, kiểm tra có phải lệnh CLOSE hay không. */
            else if (command.type == SERVO_CMD_CLOSE)
            { /* Xử lý yêu cầu đóng trước khi hết thời gian giữ. */
                Servo_Close(); /* Đổi PWM về độ rộng xung đóng ngay khi task xử lý lệnh này. */
                /* Xóa trạng thái mở để không tiếp tục chờ thời hạn của lần OPEN trước. */
                is_open = pdFALSE;
            /* Kết thúc xử lý lệnh CLOSE; giá trị loại lệnh khác không làm thay đổi PWM tại đây. */
            }
        } /* Kết thúc phần xử lý nhận lệnh thành công. */

    /*
     * Kết thúc lượt xử lý; nếu lần nhận vừa hết timeout, vòng kế tiếp kiểm tra thời hạn và đóng
     * cửa.
     */
    }
} /* Kết thúc phần định nghĩa ServoTask; vòng lặp bình thường chạy liên tục và chờ tại queue. */
