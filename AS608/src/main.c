#include "stm32f10x.h"
#include "timer.h"
#include "delay.h"
#include "uart.h"
#include "as608.h"

#define DEBUG_UART  USART3

/* In loi nhac dang ky ra cong debug. Ham nay thuoc ve ung dung,
 * khong nam trong thu vien: sau nay doi sang OLED chi can sua o day. */
static void enroll_prompt(Enroll_Step step)
{
    switch (step) {
    case ENROLL_STEP_PLACE_1: uart_send_string(DEBUG_UART, "Dat tay lan 1...\r\n");   break;
    case ENROLL_STEP_REMOVE:  uart_send_string(DEBUG_UART, "Nhac tay ra...\r\n");     break;
    case ENROLL_STEP_PLACE_2: uart_send_string(DEBUG_UART, "Dat lai ngon do...\r\n"); break;
    default: break;
    }
}

int main(void)
{
    Identify_Status r;
    IdxTbl_Status   it;
    Enroll_Status   e;
    uint8_t         tbl[32];
    uint8_t         pg, k;
    uint16_t        id, score;
    uint16_t        cnt, new_id;

    tim2_cfg();
    uart_cfg();
    delay_ms(300);                    /* AS608 can ~200ms de khoi dong */
    as608_init(USART1);               /* AS608 dang noi USART1 */

    /* ---- Doc bang chi muc ---- */
    for (pg = 0; pg < AS608_IDX_PAGES; pg++) {
        it = as608_ReadIndexTable(pg, tbl);
        uart_send_string(DEBUG_UART, "Trang ");
        uart_send_hex(DEBUG_UART, pg);
        uart_send_string(DEBUG_UART, ": ");
        if (it == IdxTbl_OK) {
            for (k = 0; k < 32U; k++) {
                uart_send_hex(DEBUG_UART, tbl[k]);
                uart_send_string(DEBUG_UART, " ");
            }
        } else {
            uart_send_string(DEBUG_UART, "loi, ma = 0x");
            uart_send_hex(DEBUG_UART, (uint8_t)it);
        }
        uart_send_string(DEBUG_UART, "\r\n");
    }

    /* ---- Dem so mau trang 0 ---- */
    it = as608_ReadIndexTable(0, tbl);
    if (it == IdxTbl_OK) {
        cnt = idx_count(tbl);
        uart_send_string(DEBUG_UART, "So mau trang 0 = 0x");
        uart_send_hex(DEBUG_UART, (uint8_t)(cnt >> 8));
        uart_send_hex(DEBUG_UART, (uint8_t)cnt);
        uart_send_string(DEBUG_UART, "\r\n");
    }

    /* ---- Dang ky tu dong vao o trong ---- */
    e = as608_enroll_auto(&new_id, 5000, enroll_prompt);
    uart_send_string(DEBUG_UART, Enroll_str(e));
    if (e == ENROLL_OK) {
        uart_send_string(DEBUG_UART, " -> ID moi = 0x");
        uart_send_hex(DEBUG_UART, (uint8_t)(new_id >> 8));
        uart_send_hex(DEBUG_UART, (uint8_t)new_id);
    }
    uart_send_string(DEBUG_UART, "\r\n");
    as608_wait_remove(5000);

    /* ---- Nhan dien ---- */
    uart_send_string(DEBUG_UART, "Dat tay de kiem tra...\r\n");
    while (1) {
        r = as608_identify(&id, &score);
        if (r == ID_NO_FINGER) {
            continue;
        }
        if (r == ID_OK) {
            uart_send_string(DEBUG_UART, "Nhan ra ID = 0x");
            uart_send_hex(DEBUG_UART, (uint8_t)(id >> 8));
            uart_send_hex(DEBUG_UART, (uint8_t)id);
        } else if (r == ID_NOT_FOUND) {
            uart_send_string(DEBUG_UART, "Khong nhan ra");
        } else {
            uart_send_string(DEBUG_UART, "Anh xau hoac loi giao tiep");
        }
        uart_send_string(DEBUG_UART, "\r\n");
        as608_wait_remove(5000);
    }
}