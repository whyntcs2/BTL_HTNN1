/**
 * @file    servo.c
 * @brief   Driver servo: cấu hình TIM1/PA8 và thay đổi độ rộng xung PWM.
 *
 * Timer tạo xung bằng phần cứng. File này không phụ thuộc FreeRTOS.
 * Nhận lệnh và quản lý thời hạn tự đóng được tách trong servo_task.c.
 */

#include "servo.h" /* Sử dụng nguyên mẫu các hàm servo và hai hằng độ rộng xung mở/đóng. */

#include "stm32f10x_gpio.h" /* Sử dụng các kiểu dữ liệu và hàm cấu hình GPIO của SPL. */
#include "stm32f10x_rcc.h" /* Sử dụng API cấp clock và đọc tần số clock hệ thống, APB2. */
/* Sử dụng kiểu cấu hình timebase, output compare và API điều khiển TIM1. */
#include "stm32f10x_tim.h"

/* Đặt tần số đếm mong muốn là 1 MHz, tương ứng mỗi lần tăng bộ đếm cách nhau 1 us. */
#define SERVO_COUNTER_HZ        1000000U
#define SERVO_FRAME_PERIOD_US   20000U /* Đặt chu kỳ PWM là 20000 us, tức 20 ms hoặc 50 Hz. */

/* Định nghĩa hàm cấu hình servo; gọi một lần sau khi clock hệ thống đã được thiết lập. */
void Servo_Init(void)
{ /* Bắt đầu phần chuẩn bị GPIO và bộ định thời. */
    GPIO_InitTypeDef gpio; /* Khai báo cấu trúc chứa thông số cấu hình chân PA8. */
    /* Khai báo cấu trúc chứa prescaler, chu kỳ và các thông số timebase của TIM1. */
    TIM_TimeBaseInitTypeDef time_base;
    TIM_OCInitTypeDef output; /* Khai báo cấu trúc cấu hình kênh output compare để phát PWM. */
    /* Khai báo cấu trúc nhận các tần số clock do RCC_GetClocksFreq trả về. */
    RCC_ClocksTypeDef clocks;
    /* Khai báo biến 32 bit không dấu để giữ tần số clock đưa vào TIM1, đơn vị Hz. */
    uint32_t timer_clock;

    /* Cấp clock đồng thời cho GPIOA và TIM1 trên APB2; phép OR ghép hai mặt nạ ngoại vi. */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE);

    /* Khởi tạo các trường GPIO về mặc định trước khi chọn chế độ cho PA8. */
    GPIO_StructInit(&gpio);
    /* Chọn duy nhất chân PA8, nơi TIM1_CH1 xuất tín hiệu servo ở ánh xạ mặc định. */
    gpio.GPIO_Pin = GPIO_Pin_8;
    /* Đặt PA8 ở chế độ chức năng thay thế push-pull để timer điều khiển mức cao/thấp của chân. */
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    /* Chọn mức tốc độ chuyển mạch GPIO 2 MHz; tần số PWM vẫn do TIM1 quyết định. */
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio); /* Áp dụng cấu hình của biến gpio cho chân PA8 trên GPIOA. */

    /* Đọc các tần số clock hiện tại; dấu & cho phép hàm ghi kết quả vào cấu trúc clocks. */
    RCC_GetClocksFreq(&clocks);
    /* Ban đầu lấy clock APB2 làm clock timer, đúng với trường hợp bộ chia APB2 bằng 1. */
    timer_clock = clocks.PCLK2_Frequency;

    /*
     * Kiểm tra PCLK2 có khác HCLK hay không; trong cấu hình này điều đó cho biết APB2 đã dùng bộ
     * chia.
     */
    if (clocks.PCLK2_Frequency != clocks.HCLK_Frequency)
    { /* Xử lý trường hợp clock APB2 bị chia. */
        /* Nhân đôi PCLK2 để lấy clock TIM1 theo quy tắc cấp clock timer trên STM32F1. */
        timer_clock *= 2U;
    } /* Kết thúc hiệu chỉnh clock timer. */

    /* Khởi tạo thông số timebase mặc định để các trường chưa gán không chứa giá trị rác. */
    TIM_TimeBaseStructInit(&time_base);
    /* Tính PSC = clock_timer / 1000000 - 1; thanh ghi chia theo PSC + 1. Ở 72 MHz, PSC bằng 71. */
    time_base.TIM_Prescaler = (uint16_t)((timer_clock / SERVO_COUNTER_HZ) - 1U);
    /* Tính ARR = 20000 - 1 = 19999; bộ đếm từ 0 đến ARR có tổng cộng 20000 count. */
    time_base.TIM_Period = SERVO_FRAME_PERIOD_US - 1U;
    /* Chọn chế độ đếm tăng: CNT tăng dần rồi quay về 0 khi hết chu kỳ. */
    time_base.TIM_CounterMode = TIM_CounterMode_Up;
    /*
     * Chọn tDTS bằng clock timer; trường CKD dùng cho lấy mẫu/lọc, không thay thế prescaler của bộ
     * đếm.
     */
    time_base.TIM_ClockDivision = TIM_CKD_DIV1;
    /* Nạp các thông số timebase vào TIM1; truyền địa chỉ cấu trúc time_base. */
    TIM_TimeBaseInit(TIM1, &time_base);

    /* Khởi tạo cấu trúc output compare về mặc định trước khi chọn chế độ PWM. */
    TIM_OCStructInit(&output);
    /*
     * Chọn PWM mode 1: khi đếm tăng, đầu ra có trạng thái tích cực trong khoảng CNT nhỏ hơn CCR1.
     */
    output.TIM_OCMode = TIM_OCMode_PWM1;
    /* Cho phép kênh output compare xuất tín hiệu; TIM1 còn cần bật ngõ ra chính MOE ở bước sau. */
    output.TIM_OutputState = TIM_OutputState_Enable;
    /* Đặt CCR1 ban đầu bằng độ rộng xung đóng; với bộ đếm 1 MHz, số count cũng là số micro giây. */
    output.TIM_Pulse = SERVO_CLOSED_PULSE_US;
    /* Chọn trạng thái tích cực ở mức cao, tạo xung cao trong thời gian đã đặt tại CCR1. */
    output.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM1, &output); /* Áp dụng cấu hình output compare cho kênh 1 của TIM1. */

    /*
     * Bật preload của CCR1 để giá trị so sánh mới được áp dụng tại sự kiện cập nhật, hạn chế đổi
     * giữa xung.
     */
    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    /* Bật preload của ARR để thông số chu kỳ cũng được nạp theo cơ chế cập nhật của timer. */
    TIM_ARRPreloadConfig(TIM1, ENABLE);

    /* Bật bit MOE cho phép ngõ ra chính của advanced timer TIM1 hoạt động. */
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    /* Bật bộ đếm TIM1; sau bước này phần cứng phát PWM mà không cần task kéo chân liên tục. */
    TIM_Cmd(TIM1, ENABLE);
} /* Hoàn tất cấu hình; tín hiệu bắt đầu ở độ rộng xung đóng. */

/* Định nghĩa hàm đổi PWM sang vị trí mở; hàm này không tự quản lý thời gian giữ cửa. */
void Servo_Open(void)
{ /* Bắt đầu thao tác đổi độ rộng xung. */
    /*
     * Ghi độ rộng xung mở vào CCR1 của TIM1; vị trí cơ khí tương ứng cần hiệu chỉnh theo servo thực
     * tế.
     */
    TIM_SetCompare1(TIM1, SERVO_OPEN_PULSE_US);
} /* Kết thúc Servo_Open; ServoTask chịu trách nhiệm theo dõi thời hạn tự đóng. */

void Servo_Close(void) /* Định nghĩa hàm đổi PWM sang vị trí đóng. */
{ /* Bắt đầu thao tác đưa PWM về giá trị đóng. */
    /* Ghi độ rộng xung đóng vào CCR1; hàm không chờ servo hoàn tất chuyển động cơ khí. */
    TIM_SetCompare1(TIM1, SERVO_CLOSED_PULSE_US);
} /* Kết thúc Servo_Close. */
