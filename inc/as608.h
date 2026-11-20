#ifndef AS608_H
#define AS608_H

#include "stm32f10x.h"

/* ================== Cau hinh ================== */
#define AS608_LIB_SIZE   300U                                /* so o mau trong module */
#define AS608_IDX_PAGES  ((AS608_LIB_SIZE + 255U) / 256U)    /* so trang bang chi muc */

/* Chon cong UART noi voi AS608. Goi 1 lan sau uart_cfg(), truoc moi lenh khac.
 * Cong phai duoc cau hinh 57600 8N1 tu truoc. */
void as608_init(USART_TypeDef *usart);

/* ================== Lenh co ban (ma theo datasheet) ================== */

/* GenImg 0x01: chup anh van tay vao ImageBuffer */
typedef enum {
    GenImg_OK        = 0x00,   /* chup anh thanh cong */
    GenImg_PKT_ERR   = 0x01,   /* module nhan goi bi loi */
    GenImg_NO_FINGER = 0x02,   /* khong phat hien ngon tay */
    GenImg_IMG_FAIL  = 0x03,   /* chup anh that bai */
    GenImg_COMM_ERR  = 0xFF    /* loi giao tiep MCU <-> AS608 */
} GenImg_Status;
GenImg_Status as608_GenImg(void);
const char   *GenImg_str(GenImg_Status s);

/* Img2Tz 0x02: tao dac trung tu ImageBuffer vao CharBuffer1/2 */
typedef enum {
    Img2Tz_OK        = 0x00,   /* tao dac trung thanh cong */
    Img2Tz_PKT_ERR   = 0x01,   /* module nhan goi loi */
    Img2Tz_DISORDER  = 0x06,   /* anh qua lon xon */
    Img2Tz_FEW_POINT = 0x07,   /* qua it diem dac trung / anh qua nho */
    Img2Tz_NO_IMAGE  = 0x15,   /* khong co anh goc hop le */
    Img2Tz_COMM_ERR  = 0xFF    /* loi giao tiep MCU <-> AS608 */
} Img2Tz_Status;
Img2Tz_Status as608_Img2Tz(uint8_t buffer_id);
const char   *Img2Tz_str(Img2Tz_Status s);

/* RegModel 0x05: ghep CharBuffer1 + CharBuffer2 thanh 1 mau */
typedef enum {
    RegModel_OK       = 0x00,
    RegModel_PKT_ERR  = 0x01,
    RegModel_Fail_CB  = 0x0A,  /* 2 lan dat khong cung 1 ngon */
    RegModel_COMM_ERR = 0xFF
} RegModel_Status;
RegModel_Status as608_RegModel(void);
const char     *RegModel_str(RegModel_Status s);

/* Store 0x06: luu mau tu CharBuffer vao flash tai page_id */
typedef enum {
    Store_OK        = 0x00,
    Store_PKT_ERR   = 0x01,
    Store_BEYOND    = 0x0B,    /* ID vuot dung luong */
    Store_ERR_FLASH = 0x18,    /* loi ghi flash */
    Store_COMM_ERR  = 0xFF
} Store_Status;
Store_Status as608_Store(uint8_t buffer_id, uint16_t page_id);
const char  *Store_str(Store_Status s);

/* Search 0x04: tim mau trong CharBuffer trong thu vien */
typedef enum {
    Search_OK       = 0x00,
    Search_PKT_ERR  = 0x01,
    Search_NO_MATCH = 0x09,
    Search_COMM_ERR = 0xFF
} Search_Status;
Search_Status as608_Search(uint8_t buffer_id, uint16_t start_page, uint16_t page_num,
                           uint16_t *page_id, uint16_t *score);
const char   *Search_str(Search_Status s);

/* DeletChar 0x0C: xoa count mau lien tiep bat dau tu page_id */
typedef enum {
    Delete_OK       = 0x00,
    Delete_PKT_ERR  = 0x01,
    Delete_FAIL     = 0x10,
    Delete_COMM_ERR = 0xFF
} Delete_Status;
Delete_Status as608_DeletChar(uint16_t page_id, uint16_t count);
const char   *Delete_str(Delete_Status s);

/* Empty 0x0D: xoa TOAN BO thu vien, khong hoan tac duoc */
typedef enum {
    Empty_OK       = 0x00,
    Empty_PKT_ERR  = 0x01,
    Empty_FAIL     = 0x11,
    Empty_COMM_ERR = 0xFF
} Empty_Status;
Empty_Status as608_Empty(void);
const char  *Empty_str(Empty_Status s);

/* ReadIndexTable 0x1F: doc bang chi muc 1 trang (32 byte = 256 o).
 * Bit 0 cua byte 0 la o dau trang. ID = page * 256 + byte * 8 + bit. */
typedef enum {
    IdxTbl_OK       = 0x00,
    IdxTbl_PKT_ERR  = 0x01,
    IdxTbl_COMM_ERR = 0xFF
} IdxTbl_Status;
IdxTbl_Status as608_ReadIndexTable(uint8_t page, uint8_t *table);   /* table: mang 32 byte */

/* Thao tac tren bang chi muc da doc ve (khong gui lenh) */
uint8_t  idx_is_used(const uint8_t *table, uint8_t pos);   /* 1: co mau, 0: trong */
uint16_t idx_count(const uint8_t *table);                   /* so mau trong 1 trang */

/* ================== Ham cap cao (ma tu dat) ================== */

/* Cho dat tay / nhac tay, co timeout */
GenImg_Status as608_wait_finger(uint32_t timeout_ms);   /* NO_FINGER = het gio */
GenImg_Status as608_wait_remove(uint32_t timeout_ms);   /* NO_FINGER = da nhac, OK = het gio */

/* Nhan dien: thu 1 lan, khong dung cho */
typedef enum {
    ID_OK = 0,
    ID_NO_FINGER,
    ID_BAD_IMAGE,
    ID_NOT_FOUND,
    ID_COMM_ERR
} Identify_Status;
Identify_Status as608_identify(uint16_t *id, uint16_t *score);

/* Tim o trong dau tien */
typedef enum {
    FREE_OK = 0,      /* tim thay, ID nam trong *free_id */
    FREE_FULL,        /* thu vien day */
    FREE_COMM_ERR     /* doc bang chi muc bi loi */
} FreeId_Status;
FreeId_Status as608_find_free_id(uint16_t *free_id);

/* Dang ky */
typedef enum {
    ENROLL_OK = 0,
    ENROLL_TIMEOUT,      /* het gio cho dat tay hoac nhac tay */
    ENROLL_BAD_IMAGE,    /* anh xau: chup loi, lon xon, it diem */
    ENROLL_MISMATCH,     /* 2 lan khong cung 1 ngon */
    ENROLL_ID_INVALID,   /* ID vuot dung luong */
    ENROLL_FLASH_ERR,    /* loi ghi flash */
    ENROLL_COMM_ERR,     /* loi giao tiep hoac loi goi */
    ENROLL_LIB_FULL      /* thu vien day, khong con ID trong */
} Enroll_Status;
const char *Enroll_str(Enroll_Status s);

/* Cac buoc driver bao ra ngoai trong luc dang ky */
typedef enum {
    ENROLL_STEP_PLACE_1,   /* moi dat tay lan 1 */
    ENROLL_STEP_REMOVE,    /* moi nhac tay ra */
    ENROLL_STEP_PLACE_2    /* moi dat lai ngon do */
} Enroll_Step;

/* Con tro toi ham bao buoc; truyen 0 neu khong can */
typedef void (*Enroll_Callback)(Enroll_Step step);

Enroll_Status as608_enroll_capture(uint8_t buffer_id, uint32_t timeout_ms);
Enroll_Status as608_enroll_save(uint16_t page_id);
Enroll_Status as608_enroll(uint16_t page_id, uint32_t timeout_ms, Enroll_Callback cb);
Enroll_Status as608_enroll_auto(uint16_t *new_id, uint32_t timeout_ms, Enroll_Callback cb);

#endif /* AS608_H */