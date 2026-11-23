/**
 * @file    servo_task.h
 * @brief   Khai báo task điều khiển cửa, tách khỏi driver servo.h.
 */

#ifndef SERVO_TASK_H /* Kiểm tra header task servo đã được nạp hay chưa. */
#define SERVO_TASK_H /* Định nghĩa SERVO_TASK_H để ngăn include lặp trong cùng đơn vị biên dịch. */

/* Dùng uint16_t để quy định rõ độ rộng trường thời gian giữ cửa trong lệnh servo. */
#include <stdint.h>
#include <stdbool.h> /* Callback trả true khi Main đã tiếp nhận phản hồi. */

typedef enum /* Khai báo các lệnh thuộc giao diện của thiết bị servo. */
{ /* Bắt đầu danh sách lệnh. */
    SERVO_CMD_OPEN, /* Yêu cầu phát xung mở và tính lại thời hạn tự đóng. */
    SERVO_CMD_CLOSE, /* Đóng như cũ, không phát phản hồi theo ID. */
    SERVO_CMD_CLOSE_ACK /* Đóng rồi phản hồi request_id; dùng khi Main chuẩn bị OTA. */
} ServoCommandType_t; /* Đặt tên kiểu dữ liệu cho mã lệnh. */

typedef struct /* Khai báo nội dung một phần tử trong queue lệnh servo. */
{ /* Bắt đầu danh sách trường của lệnh. */
    ServoCommandType_t type; /* Chọn OPEN, CLOSE thường hoặc CLOSE_ACK có phản hồi. */
    uint16_t hold_ms; /* Thời gian giữ khi OPEN: 1..65535 ms; 0 chọn mặc định 3000 ms. */
    uint32_t request_id; /* CLOSE_ACK dùng ID khác 0, Main không tái dùng khi còn đang chờ. */
} ServoCommand_t; /* Main và ServoTask cùng dùng kiểu này khi tạo/gửi/nhận queue. */

typedef struct {
    uint32_t request_id; /* Trùng ID lệnh CLOSE_ACK đã gọi Servo_Close xong. */
} ServoCloseReply_t;

/* Chạy trong ServoTask, không chờ: true khi đã đưa bản sao vào queue Main;
 * false thì task giữ nguyên phản hồi và thử lại sau 20 ms, chưa nhận lệnh servo kế tiếp.
 * Callback phải chép dữ liệu, không giữ con trỏ reply để sử dụng về sau. */
typedef bool (*ServoSendCloseReply_t)(const ServoCloseReply_t *reply, void *context);

/* Gọi trước scheduler, context tồn tại suốt thời gian task chạy; không sửa khi task đang chạy.
 * CLOSE_ACK chỉ phản hồi khi handler hợp lệ và request_id khác 0. Thiếu cấu hình thì
 * vẫn đóng nhưng không phát ACK; Main phải có timeout và không coi đó là sẵn sàng OTA.
 * ACK xác nhận đã ghi yêu cầu PWM đóng, không phải xác nhận cảm biến cửa hoặc chờ cơ khí.
 * Main dừng gửi OPEN trước phiên OTA, nhận đúng ID rồi chờ thêm thời gian servo di chuyển.
 * Tạo queue theo sizeof(ServoCommand_t) của header mới, khởi tạo lệnh bằng {0} hoặc designated initializer.
 * Ví dụ: ServoCommand_t cmd = {.type = SERVO_CMD_CLOSE_ACK, .request_id = 1U};
 * Cấu hình handler đưa reply vào queue sự kiện sẵn có của Main, không tạo thêm Main/app.
 */
void ServoTask_SetCloseReplyHandler(ServoSendCloseReply_t handler, void *context);

/*
 * argument là QueueHandle_t đã tạo với sizeof(ServoCommand_t), truyền trực tiếp khi xTaskCreate;
 * không truyền địa chỉ biến handle. Gọi Servo_Init trước khi chạy task.
 */
void ServoTask(void *argument);

#endif /* Kết thúc phạm vi bảo vệ header servo_task.h. */
