#include "as608.h"
#include "uart.h"
#include "delay.h"



#define AS608_ADDRESS      0xFFFFFFFFUL
#define AS608_BUFFER_SIZE  48U        

/* Loi giao tiep MCU <-> AS608 */
typedef enum {
    AS608_OK = 0,
    AS608_ERR_TIMEOUT,
    AS608_ERR_LENGTH,
    AS608_ERR_HEADER,
    AS608_ERR_PID,
    AS608_ERR_CHECKSUM
} AS608_Status;

/* Cong UART dang noi voi AS608, dat qua as608_init() */
static USART_TypeDef *as608_uart = AS608_UART;

static void         as608_send_cmd(uint8_t cmd, const uint8_t *params, uint16_t param_len);
static AS608_Status as608_recv_ack(uint8_t *rx, uint16_t *rx_len);

void as608_init(USART_TypeDef *usart)
{
    as608_uart = usart;
    uart_flush_rx(as608_uart);
}

static void as608_send_cmd(uint8_t cmd, const uint8_t *params, uint16_t param_len)
{
    uint8_t  buffer[32];
    uint16_t idx = 0;
    uint16_t i;
    uint16_t length = (uint16_t)(param_len + 3U);
    uint16_t sum = 0;

    if (param_len > (32U - 12U)) {
        return;
    }

    /* Header */
    buffer[idx++] = 0xEF;
    buffer[idx++] = 0x01;

    /* Dia chi */
    buffer[idx++] = (uint8_t)(AS608_ADDRESS >> 24);
    buffer[idx++] = (uint8_t)(AS608_ADDRESS >> 16);
    buffer[idx++] = (uint8_t)(AS608_ADDRESS >> 8);
    buffer[idx++] = (uint8_t)(AS608_ADDRESS);

    /* Ma goi: 0x01 = goi lenh */
    buffer[idx++] = 0x01;

    /* Do dai = tham so + ma lenh + 2 byte checksum */
    buffer[idx++] = (uint8_t)(length >> 8);
    buffer[idx++] = (uint8_t)(length);

    /* Ma lenh */
    buffer[idx++] = cmd;

    /* Tham so */
    for (i = 0; i < param_len; i++) {
        buffer[idx++] = params[i];
    }

    /* Checksum: cong tu ma goi den het tham so */
    for (i = 6; i < idx; i++) {
        sum += buffer[i];
    }
    buffer[idx++] = (uint8_t)(sum >> 8);
    buffer[idx++] = (uint8_t)(sum);

    uart_flush_rx(as608_uart);
    for (i = 0; i < idx; i++) {
        uart_send_byte(as608_uart, buffer[i]);
    }
}

static AS608_Status as608_recv_ack(uint8_t *rx, uint16_t *rx_len)
{
    uint16_t i;
    uint32_t t;
    uint16_t len;
    uint16_t total;
    uint16_t sum = 0;
    uint16_t recv_sum;

    *rx_len = 0;

    /* Dot 1: 9 byte dau (header, dia chi, ma goi, do dai) */
    for (i = 0; i < 9U; i++) {
        t = (i == 0U) ? 1000U : 50U;    /* byte dau cho lau vi GenImg co the mat vai tram ms */
        if (uart_recv_byte(as608_uart, &rx[i], t) != UART_OKE) {
            return AS608_ERR_TIMEOUT;
        }
        (*rx_len)++;
    }

    /* Kiem tra header */
    if (rx[0] != 0xEF || rx[1] != 0x01) {
        return AS608_ERR_HEADER;
    }

    /* Kiem tra dia chi */
    if (rx[2] != (uint8_t)(AS608_ADDRESS >> 24) ||
        rx[3] != (uint8_t)(AS608_ADDRESS >> 16) ||
        rx[4] != (uint8_t)(AS608_ADDRESS >> 8)  ||
        rx[5] != (uint8_t)(AS608_ADDRESS)) {
        return AS608_ERR_HEADER;
    }

    /* Kiem tra ma goi: 0x07 la goi tra loi */
    if (rx[6] != 0x07) {
        return AS608_ERR_PID;
    }

    /* Kiem tra do dai hop ly truoc khi doc tiep */
    len = (uint16_t)(((uint16_t)rx[7] << 8) | rx[8]);
    if (len < 3U || (9U + len) > AS608_BUFFER_SIZE) {
        return AS608_ERR_LENGTH;
    }

    /* Dot 2: doc them dung len byte */
    for (i = 9; i < (uint16_t)(9U + len); i++) {
        if (uart_recv_byte(as608_uart, &rx[i], 50U) != UART_OKE) {
            return AS608_ERR_TIMEOUT;
        }
        (*rx_len)++;
    }
    total = (uint16_t)(9U + len);

    /* Checksum: cong tu ma goi den het noi dung (bo 2 byte checksum cuoi) */
    for (i = 6; i < (uint16_t)(total - 2U); i++) {
        sum += rx[i];
    }
    recv_sum = (uint16_t)(((uint16_t)rx[total - 2U] << 8) | rx[total - 1U]);

    if (sum != recv_sum) {
        return AS608_ERR_CHECKSUM;
    }
    return AS608_OK;
}

/* ================== Lenh co ban ================== */

GenImg_Status as608_GenImg(void)
{
    uint8_t  rx[AS608_BUFFER_SIZE];
    uint16_t n;

    as608_send_cmd(0x01, 0, 0);
    if (as608_recv_ack(rx, &n) != AS608_OK) {
        return GenImg_COMM_ERR;
    }
    return (GenImg_Status)rx[9];
}

const char *GenImg_str(GenImg_Status s)
{
    switch (s) {
    case GenImg_OK:        return "chup anh thanh cong";
    case GenImg_PKT_ERR:   return "module nhan goi loi";
    case GenImg_NO_FINGER: return "khong co ngon tay";
    case GenImg_IMG_FAIL:  return "chup anh that bai";
    case GenImg_COMM_ERR:  return "loi giao tiep";
    default:               return "ma khong xac dinh";
    }
}

Img2Tz_Status as608_Img2Tz(uint8_t buffer_id)
{
    uint8_t  rx[AS608_BUFFER_SIZE];
    uint16_t n;
    uint8_t  p[1];

    p[0] = buffer_id;                     /* 0x01: CharBuffer1, 0x02: CharBuffer2 */
    as608_send_cmd(0x02, p, 1);
    if (as608_recv_ack(rx, &n) != AS608_OK) {
        return Img2Tz_COMM_ERR;
    }
    return (Img2Tz_Status)rx[9];
}

const char *Img2Tz_str(Img2Tz_Status s)
{
    switch (s) {
    case Img2Tz_OK:        return "tao dac trung OK";
    case Img2Tz_PKT_ERR:   return "module nhan goi loi";
    case Img2Tz_DISORDER:  return "anh qua lon xon";
    case Img2Tz_FEW_POINT: return "qua it diem dac trung";
    case Img2Tz_NO_IMAGE:  return "chua co anh, goi GenImg truoc";
    case Img2Tz_COMM_ERR:  return "loi giao tiep";
    default:               return "ma khong xac dinh";
    }
}

RegModel_Status as608_RegModel(void)
{
    uint8_t  rx[AS608_BUFFER_SIZE];
    uint16_t n;

    as608_send_cmd(0x05, 0, 0);
    if (as608_recv_ack(rx, &n) != AS608_OK) {
        return RegModel_COMM_ERR;
    }
    return (RegModel_Status)rx[9];
}

const char *RegModel_str(RegModel_Status s)
{
    switch (s) {
    case RegModel_OK:       return "ghep mau OK";
    case RegModel_PKT_ERR:  return "module nhan goi loi";
    case RegModel_Fail_CB:  return "2 lan dat khong cung 1 ngon";
    case RegModel_COMM_ERR: return "loi giao tiep";
    default:                return "ma khong xac dinh";
    }
}

Store_Status as608_Store(uint8_t buffer_id, uint16_t page_id)
{
    uint8_t  rx[AS608_BUFFER_SIZE];
    uint16_t n;
    uint8_t  p[3];

    p[0] = buffer_id;
    p[1] = (uint8_t)(page_id >> 8);       /* byte cao */
    p[2] = (uint8_t)(page_id);            /* byte thap */

    as608_send_cmd(0x06, p, 3);
    if (as608_recv_ack(rx, &n) != AS608_OK) {
        return Store_COMM_ERR;
    }
    return (Store_Status)rx[9];
}

const char *Store_str(Store_Status s)
{
    switch (s) {
    case Store_OK:        return "luu mau OK";
    case Store_PKT_ERR:   return "module nhan goi loi";
    case Store_BEYOND:    return "ID vuot qua dung luong";
    case Store_ERR_FLASH: return "loi ghi flash";
    case Store_COMM_ERR:  return "loi giao tiep";
    default:              return "ma khong xac dinh";
    }
}

Search_Status as608_Search(uint8_t buffer_id, uint16_t start_page, uint16_t page_num,
                           uint16_t *page_id, uint16_t *score)
{
    uint8_t  rx[AS608_BUFFER_SIZE];
    uint16_t n;
    uint8_t  p[5];

    p[0] = buffer_id;
    p[1] = (uint8_t)(start_page >> 8);
    p[2] = (uint8_t)(start_page);
    p[3] = (uint8_t)(page_num >> 8);
    p[4] = (uint8_t)(page_num);

    as608_send_cmd(0x04, p, 5);
    if (as608_recv_ack(rx, &n) != AS608_OK) {
        return Search_COMM_ERR;
    }

    /* Tim thay thi moi doc PageID va Score */
    if (rx[9] == Search_OK) {
        *page_id = (uint16_t)(((uint16_t)rx[10] << 8) | rx[11]);
        *score   = (uint16_t)(((uint16_t)rx[12] << 8) | rx[13]);
    }
    return (Search_Status)rx[9];
}

const char *Search_str(Search_Status s)
{
    switch (s) {
    case Search_OK:       return "tim thay";
    case Search_PKT_ERR:  return "module nhan goi loi";
    case Search_NO_MATCH: return "khong phu hop";
    case Search_COMM_ERR: return "loi giao tiep";
    default:              return "ma khong xac dinh";
    }
}

Delete_Status as608_DeletChar(uint16_t page_id, uint16_t count)
{
    uint8_t  rx[AS608_BUFFER_SIZE];
    uint16_t n;
    uint8_t  p[4];

    p[0] = (uint8_t)(page_id >> 8);       /* ID bat dau, byte cao */
    p[1] = (uint8_t)(page_id);            /* ID bat dau, byte thap */
    p[2] = (uint8_t)(count >> 8);         /* so mau can xoa, byte cao */
    p[3] = (uint8_t)(count);              /* so mau can xoa, byte thap */

    as608_send_cmd(0x0C, p, 4);
    if (as608_recv_ack(rx, &n) != AS608_OK) {
        return Delete_COMM_ERR;
    }
    return (Delete_Status)rx[9];
}

const char *Delete_str(Delete_Status s)
{
    switch (s) {
    case Delete_OK:       return "xoa mau OK";
    case Delete_PKT_ERR:  return "module nhan goi loi";
    case Delete_FAIL:     return "xoa mau that bai";
    case Delete_COMM_ERR: return "loi giao tiep";
    default:              return "ma khong xac dinh";
    }
}

Empty_Status as608_Empty(void)
{
    uint8_t  rx[AS608_BUFFER_SIZE];
    uint16_t n;

    as608_send_cmd(0x0D, 0, 0);
    if (as608_recv_ack(rx, &n) != AS608_OK) {
        return Empty_COMM_ERR;
    }
    return (Empty_Status)rx[9];
}

const char *Empty_str(Empty_Status s)
{
    switch (s) {
    case Empty_OK:       return "xoa toan bo thu vien OK";
    case Empty_PKT_ERR:  return "module nhan goi loi";
    case Empty_FAIL:     return "xoa thu vien that bai";
    case Empty_COMM_ERR: return "loi giao tiep";
    default:             return "ma khong xac dinh";
    }
}

IdxTbl_Status as608_ReadIndexTable(uint8_t page, uint8_t *table)
{
    uint8_t  rx[AS608_BUFFER_SIZE];
    uint16_t n;
    uint8_t  p[1];
    uint8_t  i;

    p[0] = page;                          /* trang 0: ID 0-255, trang 1: ID 256-511 */
    as608_send_cmd(0x1F, p, 1);
    if (as608_recv_ack(rx, &n) != AS608_OK) {
        return IdxTbl_COMM_ERR;
    }

    /* Module bao loi thi tra ma loi ve luon */
    if (rx[9] != IdxTbl_OK) {
        return (IdxTbl_Status)rx[9];
    }

    /* Bao OK nhung goi khong du 44 byte thi coi la loi */
    if (n != 44U) {
        return IdxTbl_COMM_ERR;
    }

    /* Chep 32 byte chi muc ra mang cua nguoi goi */
    for (i = 0; i < 32U; i++) {
        table[i] = rx[10U + i];
    }
    return IdxTbl_OK;
}

uint8_t idx_is_used(const uint8_t *table, uint8_t pos)
{
    uint8_t byte_idx = (uint8_t)(pos >> 3);   /* = pos / 8: nam o byte nao */
    uint8_t bit_idx  = (uint8_t)(pos & 7U);   /* = pos % 8: bit thu may trong byte */

    return (uint8_t)((table[byte_idx] >> bit_idx) & 1U);
}

uint16_t idx_count(const uint8_t *table)
{
    uint16_t count = 0;
    uint8_t  i, b;

    for (i = 0; i < 32U; i++) {
        b = table[i];
        while (b != 0U) {
            b &= (uint8_t)(b - 1U);           /* xoa bit 1 thap nhat */
            count++;
        }
    }
    return count;
}

/* ================== Ham cap cao ================== */

GenImg_Status as608_wait_finger(uint32_t timeout_ms)
{
    GenImg_Status g;
    uint32_t tries = timeout_ms / 100U;

    if (tries == 0U) {
        tries = 1U;
    }

    while (tries > 0U) {
        g = as608_GenImg();
        if (g != GenImg_NO_FINGER) {
            return g;
        }
        delay_ms(100U);
        tries--;
    }
    return GenImg_NO_FINGER;              /* het gio */
}

GenImg_Status as608_wait_remove(uint32_t timeout_ms)
{
    GenImg_Status g;
    uint32_t tries = timeout_ms / 100U;

    if (tries == 0U) {
        tries = 1U;
    }

    while (tries > 0U) {
        g = as608_GenImg();
        if (g == GenImg_NO_FINGER) {
            return GenImg_NO_FINGER;      /* da nhac tay */
        }
        if (g == GenImg_PKT_ERR || g == GenImg_COMM_ERR) {
            return g;                     /* loi: bao ngay */
        }
        /* con lai la OK hoac IMG_FAIL: tay van con, thu lai */
        delay_ms(100U);
        tries--;
    }
    return GenImg_OK;                     /* het gio, tay van con */
}

Identify_Status as608_identify(uint16_t *id, uint16_t *score)
{
    GenImg_Status g;
    Img2Tz_Status t;
    Search_Status s;

    /* Buoc 1: chup anh */
    g = as608_GenImg();
    if (g == GenImg_NO_FINGER) return ID_NO_FINGER;
    if (g != GenImg_OK) {
        return (g == GenImg_IMG_FAIL) ? ID_BAD_IMAGE : ID_COMM_ERR;
    }

    /* Buoc 2: tao dac trung vao buffer 1 */
    t = as608_Img2Tz(1);
    if (t != Img2Tz_OK) {
        return (t == Img2Tz_COMM_ERR || t == Img2Tz_PKT_ERR) ? ID_COMM_ERR : ID_BAD_IMAGE;
    }

    /* Buoc 3: tim trong thu vien */
    s = as608_Search(1, 0, AS608_LIB_SIZE, id, score);
    if (s == Search_OK)       return ID_OK;
    if (s == Search_NO_MATCH) return ID_NOT_FOUND;
    return ID_COMM_ERR;
}

FreeId_Status as608_find_free_id(uint16_t *free_id)
{
    uint8_t  tbl[32];
    uint8_t  page;
    uint16_t pos;                         /* uint16_t, KHONG duoc dung uint8_t */
    uint16_t id;

    for (page = 0; page < AS608_IDX_PAGES; page++) {
        /* Buoc 1: doc bang chi muc cua trang nay */
        if (as608_ReadIndexTable(page, tbl) != IdxTbl_OK) {
            return FREE_COMM_ERR;
        }

        /* Buoc 2: xet tung vi tri trong trang */
        for (pos = 0; pos < 256U; pos++) {
            id = (uint16_t)(page * 256U + pos);

            if (id >= AS608_LIB_SIZE) {
                return FREE_FULL;         /* da di qua o cuoi cung */
            }
            if (idx_is_used(tbl, (uint8_t)pos) == 0U) {
                *free_id = id;            /* o trong dau tien */
                return FREE_OK;
            }
        }
    }
    return FREE_FULL;
}

Enroll_Status as608_enroll_capture(uint8_t buffer_id, uint32_t timeout_ms)
{
    GenImg_Status g;
    Img2Tz_Status t;

    /* Buoc 1: cho co tay va chup anh */
    g = as608_wait_finger(timeout_ms);
    if (g == GenImg_NO_FINGER) return ENROLL_TIMEOUT;
    if (g != GenImg_OK) {
        return (g == GenImg_IMG_FAIL) ? ENROLL_BAD_IMAGE : ENROLL_COMM_ERR;
    }

    /* Buoc 2: tao dac trung vao buffer duoc chon */
    t = as608_Img2Tz(buffer_id);
    if (t != Img2Tz_OK) {
        return (t == Img2Tz_COMM_ERR || t == Img2Tz_PKT_ERR) ? ENROLL_COMM_ERR : ENROLL_BAD_IMAGE;
    }
    return ENROLL_OK;
}

Enroll_Status as608_enroll_save(uint16_t page_id)
{
    RegModel_Status r;
    Store_Status    s;

    /* Buoc 1: ghep CharBuffer1 + CharBuffer2 thanh 1 mau */
    r = as608_RegModel();
    if (r == RegModel_Fail_CB) return ENROLL_MISMATCH;
    if (r != RegModel_OK)      return ENROLL_COMM_ERR;

    /* Buoc 2: luu mau tu CharBuffer1 vao flash tai page_id */
    s = as608_Store(1, page_id);
    if (s == Store_OK)        return ENROLL_OK;
    if (s == Store_BEYOND)    return ENROLL_ID_INVALID;
    if (s == Store_ERR_FLASH) return ENROLL_FLASH_ERR;
    return ENROLL_COMM_ERR;               /* PKT_ERR, COMM_ERR hoac ma la */
}

const char *Enroll_str(Enroll_Status s)
{
    switch (s) {
    case ENROLL_OK:         return "dang ky thanh cong";
    case ENROLL_TIMEOUT:    return "het gio";
    case ENROLL_BAD_IMAGE:  return "anh xau, dat lai tay";
    case ENROLL_MISMATCH:   return "2 lan khong cung 1 ngon";
    case ENROLL_ID_INVALID: return "ID vuot qua dung luong";
    case ENROLL_FLASH_ERR:  return "loi ghi flash";
    case ENROLL_COMM_ERR:   return "loi giao tiep";
    case ENROLL_LIB_FULL:   return "thu vien da day";
    default:                return "ma khong xac dinh";
    }
}

Enroll_Status as608_enroll(uint16_t page_id, uint32_t timeout_ms, Enroll_Callback cb)
{
    Enroll_Status e;
    GenImg_Status g;

    /* Buoc 1: chup lan 1 vao buffer 1 */
    if (cb != 0) {
        cb(ENROLL_STEP_PLACE_1);
    }
    e = as608_enroll_capture(1, timeout_ms);
    if (e != ENROLL_OK) {
        return e;
    }

    /* Buoc 2: cho nhac tay */
    if (cb != 0) {
        cb(ENROLL_STEP_REMOVE);
    }
    g = as608_wait_remove(timeout_ms);
    if (g == GenImg_OK) {
        return ENROLL_TIMEOUT;            /* het gio ma tay van con */
    }
    if (g != GenImg_NO_FINGER) {
        return ENROLL_COMM_ERR;           /* PKT_ERR hoac COMM_ERR */
    }

    /* Buoc 3: chup lan 2 vao buffer 2 */
    if (cb != 0) {
        cb(ENROLL_STEP_PLACE_2);
    }
    e = as608_enroll_capture(2, timeout_ms);
    if (e != ENROLL_OK) {
        return e;
    }

    /* Buoc 4: ghep mau va luu */
    return as608_enroll_save(page_id);
}

Enroll_Status as608_enroll_auto(uint16_t *new_id, uint32_t timeout_ms, Enroll_Callback cb)
{
    FreeId_Status fs;
    Enroll_Status e;
    uint16_t      id;

    /* Buoc 1: tim o trong dau tien */
    fs = as608_find_free_id(&id);
    if (fs == FREE_FULL) return ENROLL_LIB_FULL;
    if (fs != FREE_OK)   return ENROLL_COMM_ERR;

    /* Buoc 2: dang ky vao dung o vua tim duoc */
    e = as608_enroll(id, timeout_ms, cb);

    /* Buoc 3: chi bao ID ra ngoai khi dang ky thanh cong */
    if (e == ENROLL_OK) {
        *new_id = id;
    }
    return e;
}
