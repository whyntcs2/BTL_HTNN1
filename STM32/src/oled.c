#include "oled.h"
#include "i2c.h"     /* Sua 24/11: giao tiep I2C tach sang thu vien i2c.c/.h */

/* Sua 24/11: oled.c chi con phan rieng cua SSD1306 (lenh khoi tao, font, ve dong).
 * Moi thao tac bus goi qua i2c.h. Gui loi giua chung thi BO phan con lai cua dong,
 * i2c_recover() (go ket bus, SSD1306 van giu noi dung) roi ve lai dong do 1 lan
 * -> nhieu I2C (vd luc servo quay) chi ton vai ms thay vi vai giay. */

/* Byte dieu khien SSD1306 dung ngay sau dia chi */
#define OLED_CTRL_COMMAND        0x00U
#define OLED_CTRL_DATA           0x40U

/* 1 lenh SSD1306 trong 1 giao dich. Tra 1 neu gui duoc */
static uint8_t oled_command(uint8_t command)
{
    uint8_t ok = (uint8_t)(i2c_start(OLED_I2C_ADDRESS) == I2C_OK &&
                           i2c_write(OLED_CTRL_COMMAND) == I2C_OK &&
                           i2c_write(command) == I2C_OK);
    i2c_stop();
    return ok;
}

/* Bat dau giao dich ghi du lieu man hinh (goi i2c_write tung byte, xong goi i2c_stop) */
static uint8_t oled_data_begin(void)
{
    return (uint8_t)(i2c_start(OLED_I2C_ADDRESS) == I2C_OK &&
                     i2c_write(OLED_CTRL_DATA) == I2C_OK);
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

static uint8_t oled_set_page(uint8_t page)
{
    return (uint8_t)(oled_command((uint8_t)(0xB0U | page)) &&
                     oled_command(0x00U) &&
                     oled_command(0x10U));
}

/* Ve 1 dong (21 o x 6 cot = 126 byte). Loi o byte nao thi dung luon, tra 0 */
static uint8_t oled_line_once(uint8_t page, const char *text, uint8_t inverse)
{
    static const uint8_t blank[5] = {0,0,0,0,0};
    const uint8_t *columns;
    uint8_t i;
    uint8_t col;
    uint8_t value;
    uint8_t ok;

    if (!oled_set_page(page) || !oled_data_begin()) {
        i2c_stop();
        return 0U;
    }
    ok = 1U;
    for (i = 0U; ok != 0U && i < 21U; ++i) {
        columns = (*text != '\0') ? font(*text++) : blank;     /* het chu: dien trang */
        for (col = 0U; ok != 0U && col < 6U; ++col) {
            value = (col < 5U) ? columns[col] : 0x00U;         /* cot 6: khoang cach */
            if (inverse) {
                value = (uint8_t)(value ^ 0x7FU);
            }
            ok = (uint8_t)(i2c_write(value) == I2C_OK);
        }
    }
    i2c_stop();
    return ok;
}

static uint8_t oled_clear_page(uint8_t page)
{
    uint8_t col;
    uint8_t ok;

    if (!oled_set_page(page) || !oled_data_begin()) {
        i2c_stop();
        return 0U;
    }
    ok = 1U;
    for (col = 0U; ok != 0U && col < 128U; ++col) {
        ok = (uint8_t)(i2c_write(0x00U) == I2C_OK);
    }
    i2c_stop();
    return ok;
}

/* Sua 20/11 (khoa cua): bo static de task_oled.c goi duoc, khai bao trong oled.h
 * Sua 24/11: loi thi go ket bus roi ve lai 1 lan */
void oled_write_line(uint8_t page, const char *text, uint8_t inverse)
{
    if (!oled_line_once(page, text, inverse)) {
        i2c_recover();
        (void)oled_line_once(page, text, inverse);
    }
}

void oled_clear(void)
{
    uint8_t page;
    for (page = 0U; page < 8U; ++page) {
        if (!oled_clear_page(page)) {
            i2c_recover();
            (void)oled_clear_page(page);
        }
    }
}

void oled_init(void)
{
    i2c_init(100000U);          /* Sua 24/11: go ket bus + cau hinh I2C1 nam trong i2c.c */

    (void)oled_command(0xAEU); /* Display off */
    (void)oled_command(0xD5U); (void)oled_command(0x80U);
    (void)oled_command(0xA8U); (void)oled_command(0x3FU);
    (void)oled_command(0xD3U); (void)oled_command(0x00U);
    (void)oled_command(0x40U); (void)oled_command(0x8DU); (void)oled_command(0x14U);
    (void)oled_command(0x20U); (void)oled_command(0x02U); /* Page addressing mode */
    (void)oled_command(0xA1U); (void)oled_command(0xC8U);
    (void)oled_command(0xDAU); (void)oled_command(0x12U);
    (void)oled_command(0x81U); (void)oled_command(0x7FU);
    (void)oled_command(0xD9U); (void)oled_command(0xF1U);
    (void)oled_command(0xDBU); (void)oled_command(0x40U);
    (void)oled_command(0xA4U); (void)oled_command(0xA6U);
    oled_clear();
    (void)oled_command(0xAFU); /* Display on */
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