/* Thu vien I2C1 master - tach tu oled.c (24/11).
 *
 * Timeout tinh bang TIM2 (1 nhip = 1us): moi lan cho toi da I2C_TIMEOUT_US.
 * 1 byte o 100kHz mat ~90us nen 2ms la du rong; ban cu dem 100000 vong lap (~50ms)
 * va loi giua chung van gui tiep ca dong -> 1 dong OLED loi mat toi vai giay.
 * Task goi co the bi task uu tien cao chen ngang lau: het gio thi kiem tra co them 1 lan
 * truoc khi bao loi (phan cung co the da xong trong luc task dang bi chen). */
#include "i2c.h"
#include "timer.h"
#include "delay.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_i2c.h"

#define I2C_PORT            I2C1
#define I2C_PORT_CLOCK      RCC_APB1Periph_I2C1
#define I2C_GPIO_CLOCK      RCC_APB2Periph_GPIOB
#define I2C_SCL_PIN         GPIO_Pin_6
#define I2C_SDA_PIN         GPIO_Pin_7
#define I2C_TIMEOUT_US      2000U

static uint32_t ulClock = 100000UL;

static void       prvBusRecover(void);
static void       prvConfig(void);
static I2C_Status prvWaitEvent(uint32_t ulEvent);
static I2C_Status prvWaitIdle(void);

void i2c_init(uint32_t ulClockHz)
{
    ulClock = ulClockHz;
    RCC_APB2PeriphClockCmd(I2C_GPIO_CLOCK, ENABLE);
    RCC_APB1PeriphClockCmd(I2C_PORT_CLOCK, ENABLE);
    prvBusRecover();                    /* STM32 vua reset giua giao dich: thiet bi co the dang giu SDA */
    prvConfig();
}

I2C_Status i2c_start(uint8_t ucAddr7)
{
    if (prvWaitIdle() != I2C_OK) {      /* STOP truoc chua xong / bus ket */
        return I2C_ERR_TIMEOUT;
    }
    I2C_GenerateSTART(I2C_PORT, ENABLE);
    if (prvWaitEvent(I2C_EVENT_MASTER_MODE_SELECT) != I2C_OK) {
        return I2C_ERR_TIMEOUT;
    }
    I2C_Send7bitAddress(I2C_PORT, (uint8_t)(ucAddr7 << 1), I2C_Direction_Transmitter);
    return prvWaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);
}

I2C_Status i2c_write(uint8_t ucData)
{
    I2C_SendData(I2C_PORT, ucData);
    return prvWaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED);
}

void i2c_stop(void)
{
    I2C_GenerateSTOP(I2C_PORT, ENABLE);
}

void i2c_recover(void)
{
    I2C_GenerateSTOP(I2C_PORT, ENABLE);
    I2C_Cmd(I2C_PORT, DISABLE);
    prvBusRecover();
    prvConfig();
}

/* ================== Noi bo ================== */

/* Cho su kien; NACK (co AF) thi bao loi ngay, khong cho het gio */
static I2C_Status prvWaitEvent(uint32_t ulEvent)
{
    uint16_t usStart = timer_now_us();

    while (I2C_CheckEvent(I2C_PORT, ulEvent) != SUCCESS) {
        if (I2C_GetFlagStatus(I2C_PORT, I2C_FLAG_AF) != RESET) {
            I2C_ClearFlag(I2C_PORT, I2C_FLAG_AF);
            return I2C_ERR_TIMEOUT;     /* thiet bi khong nhan (NACK) */
        }
        if ((uint16_t)(timer_now_us() - usStart) >= I2C_TIMEOUT_US) {
            return (I2C_CheckEvent(I2C_PORT, ulEvent) == SUCCESS) ? I2C_OK : I2C_ERR_TIMEOUT;
        }
    }
    return I2C_OK;
}

static I2C_Status prvWaitIdle(void)
{
    uint16_t usStart = timer_now_us();

    while (I2C_GetFlagStatus(I2C_PORT, I2C_FLAG_BUSY) != RESET) {
        if ((uint16_t)(timer_now_us() - usStart) >= I2C_TIMEOUT_US) {
            return (I2C_GetFlagStatus(I2C_PORT, I2C_FLAG_BUSY) == RESET) ? I2C_OK : I2C_ERR_TIMEOUT;
        }
    }
    return I2C_OK;
}

/* Go ket bus (chuan I2C): thiet bi bi ngat giua byte van giu SDA = 0 cho clock tiep theo.
 * Dieu khien SCL bang GPIO, phat toi da 9 xung cho thiet bi nha SDA, roi tao STOP. */
static void prvBusRecover(void)
{
    GPIO_InitTypeDef gpio;
    uint8_t i;

    gpio.GPIO_Pin   = I2C_SCL_PIN | I2C_SDA_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_Out_OD;     /* GPIO thuong, open-drain (dien tro keo len tren module) */
    GPIO_Init(GPIOB, &gpio);
    GPIO_SetBits(GPIOB, I2C_SCL_PIN | I2C_SDA_PIN);
    delay_us(10);

    for (i = 0; i < 9U; i++) {
        if (GPIO_ReadInputDataBit(GPIOB, I2C_SDA_PIN) != 0U) {
            break;                          /* thiet bi da nha SDA */
        }
        GPIO_ResetBits(GPIOB, I2C_SCL_PIN);
        delay_us(5);
        GPIO_SetBits(GPIOB, I2C_SCL_PIN);
        delay_us(5);
    }

    /* STOP: SDA 0 -> 1 trong luc SCL = 1 */
    GPIO_ResetBits(GPIOB, I2C_SCL_PIN);
    delay_us(5);
    GPIO_ResetBits(GPIOB, I2C_SDA_PIN);
    delay_us(5);
    GPIO_SetBits(GPIOB, I2C_SCL_PIN);
    delay_us(5);
    GPIO_SetBits(GPIOB, I2C_SDA_PIN);
    delay_us(5);
}

/* Giao chan cho I2C1 roi khoi tao lai khoi I2C1 tu dau (I2C_DeInit = reset qua RCC, xoa BUSY ket) */
static void prvConfig(void)
{
    GPIO_InitTypeDef gpio;
    I2C_InitTypeDef  i2c;

    gpio.GPIO_Pin   = I2C_SCL_PIN | I2C_SDA_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_AF_OD;
    GPIO_Init(GPIOB, &gpio);

    I2C_DeInit(I2C_PORT);
    I2C_StructInit(&i2c);
    i2c.I2C_ClockSpeed          = ulClock;
    i2c.I2C_Mode                = I2C_Mode_I2C;
    i2c.I2C_DutyCycle           = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1         = 0x00U;
    i2c.I2C_Ack                 = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C_PORT, &i2c);
    I2C_Cmd(I2C_PORT, ENABLE);
}
