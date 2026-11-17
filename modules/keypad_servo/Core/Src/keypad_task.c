/**
 * @file    keypad_task.c
 * @brief   Task đọc keypad: chống dội, chống lặp và gửi phím cho Main.
 * @note    Driver GPIO/quét thô được tách trong keypad.c.
 */

/* Sử dụng nguyên mẫu KeypadTask để phần định nghĩa trong file khớp với khai báo ở header. */
#include "keypad_task.h"

/* Sử dụng hàm quét thô Keypad_Scan và mã KEYPAD_MULTIPLE_KEYS của driver keypad. */
#include "keypad.h"
/* Nạp cấu hình và kiểu dữ liệu nền của FreeRTOS trước header API task. */
#include "FreeRTOS.h"
/* Sử dụng API thời gian và kiểu dữ liệu task của FreeRTOS; đây là task.h của kernel. */
#include "task.h"

/* Đặt chu kỳ lấy mẫu keypad là 20 ms, không phải 20 tick ở mọi cấu hình kernel. */
#define SCAN_PERIOD_MS   20U
/*
 * Đặt thời gian ổn định tối thiểu để chống dội là 30 ms; với mẫu 20 ms, thường xác nhận ở mốc 40
 * ms.
 */
#define DEBOUNCE_MS      30U

/*
 * Định nghĩa entry function của task. argument trỏ tới cấu hình hàm nhận phím do phía Main
 * chuẩn bị trước khi tạo task.
 */
void KeypadTask(void *argument)
{ /* Bắt đầu phần khai báo trạng thái và vòng xử lý keypad. */
    /* Đọc cấu hình qua con trỏ chỉ đọc; task không sở hữu hoặc thay đổi dữ liệu của phía nhận. */
    const KeypadTaskConfig_t *config = (const KeypadTaskConfig_t *)argument;
    /*
     * Cho phép nhận một phím mới. armed sẽ bị xóa sau khi gửi thành công và chỉ đặt lại khi đã nhả
     * ổn định.
     */
    BaseType_t armed = pdTRUE;
    char candidate = 0; /* Đặt trạng thái đang theo dõi là 0, tương ứng chưa có phím nhấn. */
    /* Lấy tick hiện tại làm mốc thức dậy ban đầu cho vTaskDelayUntil. */
    TickType_t last_wake = xTaskGetTickCount();
    /* Khởi tạo mốc bắt đầu theo dõi trạng thái candidate bằng tick vừa lấy. */
    TickType_t candidate_since = last_wake;
    /* Đổi chu kỳ quét từ mili giây sang tick theo configTICK_RATE_HZ của project. */
    TickType_t period = pdMS_TO_TICKS(SCAN_PERIOD_MS);
    /* Đổi thời gian chống dội từ mili giây sang tick để so sánh cùng đơn vị với bộ đếm hệ thống. */
    TickType_t debounce = pdMS_TO_TICKS(DEBOUNCE_MS);

    /* Kiểm tra phép đổi thời gian có làm chu kỳ quét bị làm tròn xuống 0 tick hay không. */
    if (period == 0U)
    { /* Xử lý trường hợp tick rate thấp khiến chu kỳ sau quy đổi bằng 0. */
        period = 1U; /* Dùng tối thiểu 1 tick để task vẫn có khoảng chờ giữa các lượt quét. */
    } /* Kết thúc hiệu chỉnh chu kỳ quét. */

    if (debounce == 0U) /* Kiểm tra thời gian chống dội sau quy đổi có bằng 0 tick hay không. */
    { /* Xử lý trường hợp ngưỡng chống dội bị làm tròn xuống 0. */
        debounce = 1U; /* Dùng tối thiểu 1 tick, tránh chấp nhận ngay trạng thái vừa thay đổi. */
    } /* Kết thúc hiệu chỉnh ngưỡng chống dội. */

    /* Duy trì vòng lặp trong suốt thời gian task hoạt động; việc nhường CPU nằm ở cuối vòng. */
    for (;;)
    { /* Bắt đầu một lượt lấy mẫu và xử lý phím. */
        /* Đọc ma trận từ driver; raw có thể là ký tự, 0 hoặc mã báo nhiều phím. */
        char raw = Keypad_Scan();
        /* Lấy tick sau lượt quét để xác định trạng thái đã giữ ổn định bao lâu. */
        TickType_t now = xTaskGetTickCount();

        /* Kiểm tra lượt quét có phát hiện nhiều vị trí phím hay không. */
        if (raw == KEYPAD_MULTIPLE_KEYS)
        { /* Xử lý trường hợp nhấn chồng phím. */
            /* Ngừng cho phép phát ký tự; phải nhả hết ổn định mới nhận lần nhấn tiếp theo. */
            armed = pdFALSE;
            /* Ghi nhận trạng thái nhiều phím làm candidate để theo dõi lần thay đổi sau. */
            candidate = raw;
            /*
             * Đặt lại mốc theo dõi tại thời điểm hiện tại; thời gian nhấn chồng không được tính là
             * đã nhả ổn định.
             */
            candidate_since = now;
        } /* Kết thúc nhánh xử lý nhiều phím. */
        else /* Chuyển sang trường hợp có một phím hoặc đã nhả toàn bộ. */
        { /* Bắt đầu kiểm tra sự ổn định của trạng thái đơn phím/nhả phím. */

            /* So sánh kết quả mới với candidate; khác nhau nghĩa là tín hiệu vừa đổi trạng thái. */
            if (raw != candidate)
            { /* Xử lý trạng thái mới, có thể do nhấn, nhả hoặc dội tiếp điểm. */
                candidate = raw; /* Cập nhật candidate bằng trạng thái vừa đọc. */
                /*
                 * Đo lại thời gian ổn định từ thời điểm này; khoảng trước khi thay đổi không được
                 * cộng dồn.
                 */
                candidate_since = now;
            } /* Kết thúc cập nhật trạng thái đang theo dõi. */

            /*
             * Lấy hiệu tick không dấu và so với ngưỡng chống dội; cách trừ này xử lý rollover với
             * khoảng ngắn đang dùng.
             */
            if ((TickType_t)(now - candidate_since) >= debounce)
            { /* Xử lý candidate đã giữ đủ thời gian ổn định. */
                /* Kiểm tra candidate có bằng 0, nghĩa là đã nhả hết phím, hay không. */
                if (candidate == 0)
                { /* Xử lý trạng thái nhả phím đã ổn định. */
                    armed = pdTRUE; /* Cho phép phát một ký tự trong lần nhấn kế tiếp. */
                } /* Kết thúc nhánh nhả phím. */
                /* Nếu vẫn có phím, chỉ tiếp tục khi lần nhấn hiện tại chưa được gửi thành công. */
                else if (armed != pdFALSE)
                { /* Chuyển ký tự đã chống dội cho hàm do phía Main cung cấp. */
                    /*
                     * Gọi hàm nhận phím với ký tự và dữ liệu riêng của bên nhận. Hàm trả true khi
                     * đã nhận thành công; keypad không biết cấu trúc sự kiện hoặc tên queue.
                     */
                    if (config->send_key(candidate, config->context))
                    { /* Xử lý trường hợp bên nhận đã tiếp nhận ký tự. */
                        /*
                         * Khóa lần nhấn hiện tại để giữ phím không tạo thêm ký tự; bên nhận từ
                         * chối thì cờ này chưa bị xóa và lượt sau sẽ thử lại khi còn giữ phím.
                         */
                        armed = pdFALSE;
                    } /* Kết thúc nhánh gửi thành công. */
                } /* Kết thúc phần chuyển ký tự của lần nhấn. */
            } /* Kết thúc xử lý trạng thái đã đủ thời gian chống dội. */
        } /* Kết thúc nhánh xử lý phím đơn hoặc nhả phím. */

        /*
         * Chờ tới mốc last_wake cộng period khi chưa trễ lịch; truyền địa chỉ để FreeRTOS cập nhật
         * mốc, hạn chế trôi chu kỳ.
         */
        vTaskDelayUntil(&last_wake, period);
    } /* Kết thúc một lượt của task; sau khi được đánh thức, bắt đầu lượt quét tiếp theo. */
/* Kết thúc phần định nghĩa KeypadTask; vòng lặp được thiết kế không trả về khi chạy bình thường. */
}
