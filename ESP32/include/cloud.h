#ifndef CLOUD_H
#define CLOUD_H

/* Gui log len Google Sheets + lay bang ten (Apps Script Web App), chay trong task FreeRTOS rieng
 * de loop() (lien ket STM32) khong bi dung trong luc cho HTTPS (1-3s/lan). */

#include <stdint.h>
#include "comm_protocol.h"      /* COMM_USER_MAX, COMM_NAME_MAX */

#define CLOUD_STATE_UNKNOWN 0U  /* chua gui lan nao */
#define CLOUD_STATE_OK      1U  /* yeu cau gan nhat thanh cong */
#define CLOUD_STATE_ERROR   2U  /* yeu cau gan nhat loi */

typedef struct {
    uint16_t count;
    uint32_t version;                               /* = CRC32 cua bang (giong cach STM32 tinh) */
    uint16_t id[COMM_USER_MAX];
    uint8_t  len[COMM_USER_MAX];
    char     name[COMM_USER_MAX][COMM_NAME_MAX + 1U];
} CloudUsers;

int     cloud_init(void);                   /* tao mutex + task cloud. 1: OK */
uint8_t cloud_state(void);                  /* CLOUD_STATE_... */

/* Bang ten moi lay ve tu Sheet (moi 5 phut): tra 1 va chep ra neu co ban moi tu lan goi truoc */
int     cloud_take_users(CloudUsers *out);
/* Chep bang ten gan nhat (khong danh dau da lay). 0: chua lay duoc lan nao */
int     cloud_copy_users(CloudUsers *out);

/* Nut "Cap nhat ten ngay" tren trang web: lay bang ten o vong ke tiep cua task cloud
 * va gui xuong STM32 ke ca khi bang khong doi */
void    cloud_request_users(void);
/* So nguoi + so giay tu lan lay thanh cong gan nhat. 0: chua lay duoc lan nao */
int     cloud_users_info(uint16_t *pusCount, uint32_t *pulAgeSec);

#endif /* CLOUD_H */