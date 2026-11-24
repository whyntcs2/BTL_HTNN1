/**
 * @file    keypad.c
 * @brief   Driver keypad 4x4: cấu hình GPIO và quét thô ma trận.
 *
 * File này chỉ chứa thao tác phần cứng, không phụ thuộc FreeRTOS.
 * Chống dội và gửi sự kiện nằm trong keypad_task.c; xác thực thuộc Task_Main.
 */

/* Sử dụng các khai báo Keypad_Init, Keypad_Scan và mã báo nhiều phím trong keypad.h. */
#include "keypad.h"

/* Sử dụng kiểu GPIO_InitTypeDef, các chế độ GPIO và hàm đọc, ghi chân của thư viện SPL. */
#include "stm32f10x_gpio.h"
/* Sử dụng hàm cấp clock ngoại vi và hằng RCC_APB2Periph_GPIOA của SPL. */
#include "stm32f10x_rcc.h"

/* Quy định ma trận có 4 hàng; hậu tố U biểu thị hằng số nguyên không dấu. */
#define KEYPAD_ROW_COUNT         4U
/* Quy định ma trận có 4 cột, tương ứng bốn đầu vào cần đọc ở mỗi hàng. */
#define KEYPAD_COLUMN_COUNT      4U
/* Ghép mặt nạ PA0, PA1, PA2, PA3 bằng phép OR bit để cấu hình hoặc ghi cả bốn hàng. */
#define KEYPAD_ROWS              (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3)
/* Ghép mặt nạ PA4, PA5, PA6, PA7 để chọn đúng bốn chân đầu vào của cột. */
#define KEYPAD_COLS              (GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7)

/*
 * Khai báo bảng chân hàng kiểu uint16_t, có 4 phần tử. static giới hạn trong file; const ngăn sửa
 * bảng trong chương trình.
 */
static const uint16_t row_pins[KEYPAD_ROW_COUNT] = {
    /* Gán R1, R2, R3, R4 lần lượt cho PA0, PA1, PA2, PA3; chỉ số mảng bắt đầu từ 0. */
    GPIO_Pin_0, GPIO_Pin_1, GPIO_Pin_2, GPIO_Pin_3
}; /* Kết thúc danh sách phần tử và khai báo mảng row_pins. */

/* Khai báo bảng chân cột kiểu uint16_t, số phần tử lấy từ KEYPAD_COLUMN_COUNT. */
static const uint16_t col_pins[KEYPAD_COLUMN_COUNT] = {
    /* Gán C1, C2, C3, C4 lần lượt cho PA4, PA5, PA6, PA7. */
    GPIO_Pin_4, GPIO_Pin_5, GPIO_Pin_6, GPIO_Pin_7
}; /* Kết thúc khai báo mảng col_pins. */

/* Khai báo bảng ký tự 4 hàng, 4 cột; dùng cặp chỉ số hàng/cột để đổi vị trí phím thành ký tự. */
static const char key_map[KEYPAD_ROW_COUNT][KEYPAD_COLUMN_COUNT] = {
    {'1', '2', '3', 'A'}, /* Hàng R1 có các phím 1, 2, 3, A theo thứ tự C1 đến C4. */
    {'4', '5', '6', 'B'}, /* Hàng R2 có các phím 4, 5, 6, B theo thứ tự C1 đến C4. */
    {'7', '8', '9', 'C'}, /* Hàng R3 có các phím 7, 8, 9, C theo thứ tự C1 đến C4. */
    /*
     * Hàng R4 có các phím *, 0, #, D. Giá trị '0' là ký tự số 0, khác giá trị 0 báo không nhấn
     * phím.
     */
    {'*', '0', '#', 'D'}
}; /* Kết thúc bảng key_map; các ký tự trong bảng phải khớp thứ tự dây của keypad. */

/* Định nghĩa hàm cấu hình keypad; void trong danh sách tham số cho biết hàm không nhận đối số. */
void Keypad_Init(void)
{ /* Bắt đầu phần cấu hình clock và GPIO của keypad. */
    /* Khai báo biến gpio để chứa các thông số cấu hình trước khi truyền cho GPIO_Init. */
    GPIO_InitTypeDef gpio;

    /*
     * Cấp clock cho GPIOA trên bus APB2; cần thực hiện trước khi cấu hình và sử dụng các chân PA.
     */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /*
     * Đặt bit chốt đầu ra của bốn hàng lên 1 trước khi đổi chế độ; với open-drain, mức ghi 1 sẽ nhả
     * hàng.
     */
    GPIO_SetBits(GPIOA, KEYPAD_ROWS);

    /* Gán giá trị mặc định cho cấu trúc gpio, sau đó mới thay các trường phục vụ keypad. */
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = KEYPAD_ROWS; /* Chọn cả bốn chân hàng PA0 đến PA3 bằng mặt nạ KEYPAD_ROWS. */
    /* Đặt hàng ở chế độ output open-drain: ghi 0 để kéo thấp, ghi 1 để nhả ngõ ra. */
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    /* Chọn mức tốc độ chuyển mạch GPIO 2 MHz; đây không phải chu kỳ quét keypad. */
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    /* Áp dụng các thông số gpio cho những chân đã chọn của GPIOA; dấu & truyền địa chỉ cấu trúc. */
    GPIO_Init(GPIOA, &gpio);

    /* Đổi mặt nạ chân trong cấu trúc sang PA4 đến PA7 để cấu hình các cột. */
    gpio.GPIO_Pin = KEYPAD_COLS;
    /*
     * Đặt cột ở chế độ đầu vào có điện trở kéo lên; khi không có phím nối xuống hàng thấp, cột đọc
     * mức 1.
     */
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    /*
     * Áp dụng chế độ input pull-up cho bốn cột; trường tốc độ không quyết định tần số đọc đầu vào.
     */
    GPIO_Init(GPIOA, &gpio);
} /* Hoàn tất Keypad_Init; việc quét định kỳ sẽ do KeypadTask thực hiện sau khi scheduler chạy. */

/*
 * Định nghĩa hàm quét thô, trả về một giá trị kiểu char để biểu diễn phím hoặc trạng thái ma trận.
 */
char Keypad_Scan(void)
{ /* Bắt đầu một lượt quét toàn bộ hàng và cột. */
    /* Khởi tạo kết quả bằng 0, tức chưa phát hiện phím; không dùng ký tự '0' làm trạng thái này. */
    char key = 0;
    /* Khởi tạo bộ đếm số vị trí phím đang được đọc là nhấn trong lượt quét hiện tại. */
    unsigned int count = 0;

    /*
     * Nhả tất cả các hàng trước khi chọn lần lượt từng hàng để tránh còn hàng thấp từ lượt trước.
     */
    GPIO_SetBits(GPIOA, KEYPAD_ROWS);

    /* Duyệt chỉ số row từ 0 đến 3; sau mỗi lượt tăng một đơn vị để chuyển sang hàng kế tiếp. */
    for (unsigned int row = 0; row < KEYPAD_ROW_COUNT; ++row)
    { /* Các thao tác trong khối này áp dụng cho hàng đang được chọn. */
        /* Kéo thấp duy nhất chân hàng row; các hàng khác vẫn ở trạng thái nhả. */
        GPIO_ResetBits(GPIOA, row_pins[row]);

        /*
         * Thực hiện 128 lượt chờ ngắn sau khi đổi hàng. volatile giữ các truy cập biến settle; đây
         * không phải bộ định thời 30 ms.
         */
        for (volatile unsigned int settle = 0; settle < 128U; ++settle)
        { /* Bắt đầu phần chờ ngắn để tín hiệu điện trên hàng/cột có thời gian ổn định. */
            /*
             * Thực hiện lệnh NOP của CPU; thời gian toàn vòng còn phụ thuộc clock và mã lệnh do
             * compiler sinh.
             */
            __NOP();
        } /* Kết thúc phần chờ ổn định tín hiệu. */

        /* Duyệt chỉ số col từ 0 đến 3 để đọc đủ bốn cột của hàng hiện tại. */
        for (unsigned int col = 0; col < KEYPAD_COLUMN_COUNT; ++col)
        { /* Bắt đầu phần kiểm tra một cột. */
            /*
             * Đọc mức logic của chân cột; mức 0 cho biết cột có đường nối tới hàng đang kéo thấp.
             */
            if (GPIO_ReadInputDataBit(GPIOA, col_pins[col]) == Bit_RESET)
            { /* Xử lý vị trí phím được phát hiện tại cặp chỉ số row, col. */
                /*
                 * Lấy ký tự tương ứng trong key_map; nếu có phím khác, giá trị này có thể được ghi
                 * lại ở lượt sau.
                 */
                key = key_map[row][col];
                /*
                 * Tăng số vị trí được phát hiện; bộ đếm dùng để phân biệt một phím với nhiều phím.
                 */
                ++count;
            } /* Kết thúc xử lý vị trí phím tại cột hiện tại. */
        } /* Kết thúc một lần đọc cột; vòng lặp tiếp tục với cột còn lại. */

        /*
         * Nhả lại hàng vừa quét để không đồng thời kéo thấp hai hàng khi chuyển sang lượt kế tiếp.
         */
        GPIO_SetBits(GPIOA, row_pins[row]);
    } /* Kết thúc xử lý hàng hiện tại; sau bốn lượt, toàn bộ ma trận đã được đọc. */

    if (count > 1U) /* Kiểm tra có hơn một vị trí phím được phát hiện hay không. */
    { /* Xử lý trường hợp nhấn chồng phím. */
        /*
         * Trả mã KEYPAD_MULTIPLE_KEYS để task từ chối thao tác, thay vì dùng ký tự cuối cùng đã
         * đọc.
         */
        return KEYPAD_MULTIPLE_KEYS;
    } /* Kết thúc nhánh báo nhiều phím; các trường hợp còn lại có không quá một vị trí nhấn. */

    /* Trả ký tự của phím duy nhất, hoặc trả 0 nếu không phát hiện phím nào trong lượt quét. */
    return key;
} /* Kết thúc hàm Keypad_Scan. */
