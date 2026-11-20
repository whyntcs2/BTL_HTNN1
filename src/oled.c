#include "oled.h"

#define OLED_I2C                 I2C1
#define OLED_I2C_CLOCK           RCC_APB1Periph_I2C1
#define OLED_GPIO_CLOCK          RCC_APB2Periph_GPIOB
#define OLED_SCL_PIN             GPIO_Pin_6
#define OLED_SDA_PIN             GPIO_Pin_7
#define OLED_I2C_ADDR_WRITE      ((uint8_t)(OLED_I2C_ADDRESS << 1))
#define OLED_TIMEOUT             100000UL

static uint8_t i2c_wait_event(uint32_t event)
{
    uint32_t timeout = OLED_TIMEOUT;
    while (I2C_CheckEvent(OLED_I2C, event) == ERROR) {
        if (timeout-- == 0U) return 0U;
    }
    return 1U;
}

static uint8_t i2c_begin(uint8_t control)
{
    uint32_t timeout = OLED_TIMEOUT;
    I2C_GenerateSTART(OLED_I2C, ENABLE);
    while (!I2C_GetFlagStatus(OLED_I2C, I2C_FLAG_SB)) {
        if (timeout-- == 0U) return 0U;
    }
    I2C_Send7bitAddress(OLED_I2C, OLED_I2C_ADDR_WRITE, I2C_Direction_Transmitter);
    if (!i2c_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return 0U;
    I2C_SendData(OLED_I2C, control);
    if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 0U;
    return 1U;
}

static void i2c_end(void)
{
    I2C_GenerateSTOP(OLED_I2C, ENABLE);
}

static void oled_command(uint8_t command)
{
    if (i2c_begin(0x00U)) {
        I2C_SendData(OLED_I2C, command);
        (void)i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED);
    }
    i2c_end();
}

static void oled_data_byte(uint8_t data)
{
    I2C_SendData(OLED_I2C, data);
    (void)i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED);
}

/* Five-column, 7-pixel ASCII font for the menu/status vocabulary. */
static const uint8_t *font(char c)
{
    static const uint8_t blank[5] = {0,0,0,0,0};
    static const uint8_t letters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
        {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}
    };
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c == '>') {
        static const uint8_t arrow[5] = {0x00,0x41,0x22,0x14,0x08};
        return arrow;
    }
    if (c == '-') { static const uint8_t g[5] = {8,8,8,8,8}; return g; }
    if (c == ':') { static const uint8_t g[5] = {0,0x36,0x36,0,0}; return g; }
    if (c == '/') { static const uint8_t g[5] = {0x20,0x10,8,4,2}; return g; }
    if (c == '#') { static const uint8_t g[5] = {0x14,0x7F,0x14,0x7F,0x14}; return g; }
    if (c == '!') { static const uint8_t g[5] = {0,0,0x5F,0,0}; return g; }
    if (c == '[') { static const uint8_t g[5] = {0,0x7F,0x41,0x41,0}; return g; }
    if (c == ']') { static const uint8_t g[5] = {0,0x41,0x41,0x7F,0}; return g; }
    if (c == '.') { static const uint8_t g[5] = {0,0x60,0x60,0,0}; return g; }
    /* Them 20/11 (khoa cua): '*' che PIN / PIN admin, '<' va '?' cho cac man hinh co san */
    if (c == '*') { static const uint8_t g[5] = {0x14,0x08,0x3E,0x08,0x14}; return g; }
    if (c == '<') { static const uint8_t g[5] = {0x08,0x14,0x22,0x41,0x00}; return g; }
    if (c == '?') { static const uint8_t g[5] = {0x02,0x01,0x51,0x09,0x06}; return g; }
    return blank;
}

static void oled_set_page(uint8_t page)
{
    oled_command((uint8_t)(0xB0U | page));
    oled_command(0x00U);
    oled_command(0x10U);
}

/* Sua 20/11 (khoa cua): bo static de task_oled.c goi duoc, khai bao trong oled.h */
void oled_write_line(uint8_t page, const char *text, uint8_t inverse)
{
    uint8_t i = 0U;
    oled_set_page(page);
    if (!i2c_begin(0x40U)) { i2c_end(); return; }
    while (*text != '\0' && i < 21U) {
        const uint8_t *columns = font(*text++);
        uint8_t col;
        for (col = 0U; col < 5U; ++col) {
            uint8_t value = columns[col];
            oled_data_byte(inverse ? (uint8_t)(value ^ 0x7FU) : value);
        }
        oled_data_byte(inverse ? 0x7FU : 0x00U);
        ++i;
    }
    while (i++ < 21U) {
        uint8_t col;
        for (col = 0U; col < 6U; ++col) oled_data_byte(inverse ? 0x7FU : 0x00U);
    }
    i2c_end();
}

void oled_clear(void)
{
    uint8_t page;
    for (page = 0U; page < 8U; ++page) {
        uint8_t col;
        oled_set_page(page);
        if (!i2c_begin(0x40U)) { i2c_end(); continue; }
        for (col = 0U; col < 128U; ++col) oled_data_byte(0x00U);
        i2c_end();
    }
}

void oled_init(void)
{
    GPIO_InitTypeDef gpio;
    I2C_InitTypeDef i2c;
    RCC_APB2PeriphClockCmd(OLED_GPIO_CLOCK, ENABLE);
    RCC_APB1PeriphClockCmd(OLED_I2C_CLOCK, ENABLE);
    gpio.GPIO_Pin = OLED_SCL_PIN | OLED_SDA_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_Init(GPIOB, &gpio);
    I2C_DeInit(OLED_I2C);
    I2C_StructInit(&i2c);
    i2c.I2C_ClockSpeed = 100000U;
    i2c.I2C_Mode = I2C_Mode_I2C;
    i2c.I2C_DutyCycle = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1 = 0x00U;
    i2c.I2C_Ack = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(OLED_I2C, &i2c);
    I2C_Cmd(OLED_I2C, ENABLE);

    oled_command(0xAEU); /* Display off */
    oled_command(0xD5U); oled_command(0x80U);
    oled_command(0xA8U); oled_command(0x3FU);
    oled_command(0xD3U); oled_command(0x00U);
    oled_command(0x40U); oled_command(0x8DU); oled_command(0x14U);
    oled_command(0x20U); oled_command(0x02U); /* Page addressing mode */
    oled_command(0xA1U); oled_command(0xC8U);
    oled_command(0xDAU); oled_command(0x12U);
    oled_command(0x81U); oled_command(0x7FU);
    oled_command(0xD9U); oled_command(0xF1U);
    oled_command(0xDBU); oled_command(0x40U);
    oled_command(0xA4U); oled_command(0xA6U);
    oled_clear();
    oled_command(0xAFU); /* Display on */
}

static void oled_frame(const char *header)
{
    uint8_t page;
    for (page = 0U; page < 8U; ++page) oled_write_line(page, "", 0U);
    oled_write_line(0U, header, 1U);
    oled_write_line(1U, "---------------------", 0U);
}

void oled_show_wifi_connecting(void)
{
    oled_frame("CHAM CONG VAN TAY");
    oled_write_line(2U, "   00:00:00", 0U);
    oled_write_line(4U, "   --/--/2026", 0U);
    oled_write_line(5U, "[DANG KET NOI WIFI]", 0U);
    oled_write_line(6U, "[NTP SYNC PENDING]", 0U);
    oled_write_line(7U, ">> VUI LONG CHO... <<", 0U);
}

void oled_show_wifi_connected(const char *time, const char *date)
{
    char line[22];
    uint8_t i;
    oled_frame("CHAM CONG VAN TAY");
    line[0] = ' '; line[1] = ' '; line[2] = ' ';
    for (i = 0U; time[i] != '\0' && i < 17U; ++i) line[3U + i] = time[i];
    line[3U + i] = '\0'; oled_write_line(2U, line, 0U);
    line[0] = ' '; line[1] = ' '; line[2] = ' ';
    for (i = 0U; date[i] != '\0' && i < 17U; ++i) line[3U + i] = date[i];
    line[3U + i] = '\0'; oled_write_line(4U, line, 0U);
    oled_write_line(5U, "[WIFI: DA KET NOI]", 0U);
    oled_write_line(6U, "[NTP SYNCED / READY]", 0U);
    oled_write_line(7U, ">> QUET VAN TAY <<", 0U);
}

void oled_show_accepted(const char *id, const char *name, const char *time, const char *status)
{
    char line[22];
    uint8_t i;
    oled_frame("XAC NHAN THANH CONG");
    line[0]='I'; line[1]='D'; line[2]=':'; line[3]=' '; line[4]='#';
    for (i=0U; id[i]!='\0' && i<16U; ++i) line[5U+i]=id[i];
    line[5U+i]='\0'; oled_write_line(2U,line,0U);
    line[0]='T'; line[1]='E'; line[2]='N'; line[3]=':'; line[4]=' ';
    for (i=0U; name[i]!='\0' && i<16U; ++i) line[5U+i]=name[i];
    line[5U+i]='\0'; oled_write_line(3U,line,0U);
    line[0]='T'; line[1]='H'; line[2]='O'; line[3]='I'; line[4]=' ';
    line[5]='G'; line[6]='I'; line[7]='A'; line[8]='N'; line[9]=':'; line[10]=' ';
    for (i=0U; time[i]!='\0' && i<10U; ++i) line[11U+i]=time[i];
    line[11U+i]='\0'; oled_write_line(4U,line,0U);
    line[0]='T'; line[1]='T'; line[2]=':'; line[3]=' ';
    for (i=0U; status[i]!='\0' && i<17U; ++i) line[4U+i]=status[i];
    line[4U+i]='\0'; oled_write_line(5U,line,0U);
    oled_write_line(6U,"---------------------",0U);
    oled_write_line(7U,">> XIN MOI VAO CUA <<",0U);
}

void oled_show_denied(void)
{
    oled_frame("KHONG HOP LE !");
    oled_write_line(2U,"LOI: VAN TAY LA",0U);
    oled_write_line(3U,"ID KHONG TON TAI",0U);
    oled_write_line(5U,"VUI LONG THU LAI!",0U);
    oled_write_line(6U,"---------------------",0U);
}

void oled_show_enrollment(const char *id, const char *step)
{
    char line[22];
    uint8_t i;
    oled_frame("DANG KY VAN TAY");
    line[0]='I'; line[1]='D'; line[2]=' '; line[3]='M'; line[4]='O'; line[5]='I';
    line[6]=':'; line[7]=' '; line[8]='#';
    for (i=0U; id[i]!='\0' && i<12U; ++i) line[9U+i]=id[i];
    line[9U+i]='\0'; oled_write_line(2U,line,0U);
    line[0]='D'; line[1]='A'; line[2]='T'; line[3]=' '; line[4]='N'; line[5]='G';
    line[6]='O'; line[7]='N'; line[8]=' '; line[9]='T'; line[10]='A'; line[11]='Y'; line[12]=' ';
    for (i=0U; step[i]!='\0' && i<8U; ++i) line[13U+i]=step[i];
    line[13U+i]='\0'; oled_write_line(4U,line,0U);
    oled_write_line(6U,"GIU YEN TAY...",0U);
    oled_write_line(7U,"[HUY: NHAN GIU NUT]",0U);
}

void oled_show_deletion(const char *id)
{
    char line[22];
    uint8_t i;
    oled_frame("XOA VAN TAY");
    oled_write_line(2U,"NHAP ID CAN XOA:",0U);
    line[0]='I'; line[1]='D'; line[2]=':'; line[3]=' '; line[4]='['; line[5]='#';
    for (i=0U; id[i]!='\0' && i<12U; ++i) line[6U+i]=id[i];
    line[6U+i]=']'; line[7U+i]='\0'; oled_write_line(4U,line,0U);
    oled_write_line(6U,"XAC NHAN XOA?",0U);
    oled_write_line(7U,"[OK: AN] [HUY: GIU]",0U);
}

void oled_show_security_warning(void)
{
    oled_frame("CANH BAO AN NINH");
    oled_write_line(3U,"HE THONG TAM KHOA!",0U);
    oled_write_line(4U,"SAI QUA 5 LAN",0U);
    oled_write_line(6U,"VUI LONG CHO...",0U);
    oled_write_line(7U,"---------------------",0U);
}