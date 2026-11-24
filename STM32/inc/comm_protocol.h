#ifndef COMM_PROTOCOL_H
#define COMM_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>


#define COMM_PAYLOAD_MAX 192U
#define COMM_FRAME_MAX   (COMM_PAYLOAD_MAX + 10U)
#define COMM_LOG_BYTES   24U
#define COMM_USER_MAX    32U
#define COMM_NAME_MAX    16U /* Sua 24/11 (48 -> 16): OLED 21 ky tu, chu HOA khong dau; tiet kiem ~2KB RAM */

typedef enum {
    COMM_HELLO = 1, COMM_LOG = 2, COMM_ACK_STORED = 3, COMM_REJECT = 4,
    COMM_STATUS = 5, COMM_TIME = 6, COMM_USERS_BEGIN = 7, COMM_USER = 8,
    COMM_USERS_END = 9, COMM_OTA_PREPARE = 10, COMM_OTA_READY = 11, COMM_OTA_ABORT = 12
} Comm_Message;

typedef struct {
    uint8_t type;
    uint16_t sequence;
    uint16_t length;
    uint8_t payload[COMM_PAYLOAD_MAX];
} Comm_Frame;

typedef struct {
    uint32_t device;
    uint64_t event;
    uint32_t epoch;
    uint16_t user;       /* FFFF: kh�ng x�c d?nh du?c ID, v� d? v�n tay kh�ng h?p l?. */
    uint8_t method;      /* 1: v�n tay, 2: PIN, 3: s? ki?n h? th?ng. */
    uint8_t result;      /* 0: sai, 1: d�ng, 2: c?nh b�o. */
    uint8_t door;        /* 0: l?nh d�ng, 1: l?nh m?, 2: chua r�; kh�ng ph?i c?m bi?n c?a. */
    uint8_t reason;
    uint8_t time_quality; /* 0: chua c� gi? UTC, 1: d� d?ng b?. */
} Comm_Log;

typedef struct {
    uint8_t bytes[COMM_FRAME_MAX];
    uint16_t used;
} Comm_Parser;

typedef struct {
    uint16_t id;
    uint8_t length;
    char name[COMM_NAME_MAX + 1U];
} Comm_User;

typedef struct {
    Comm_User active[COMM_USER_MAX];
    Comm_User staging[COMM_USER_MAX];
    uint32_t version;         /* Phi�n b?n b?ng dang d�ng; ch? d?i sau CRC cu?i h?p l?. */
    uint32_t staging_version; /* Phi�n b?n b?ng dang nh?n, c� th? b? h?y gi?a ch?ng. */
    uint16_t count;
    uint16_t received;
    uint16_t active_count;
    uint8_t collecting;
} Comm_Users;

#ifdef __cplusplus
extern "C" {
#endif
void Comm_PutLe(uint8_t *out, uint64_t value, size_t bytes);
uint64_t Comm_GetLe(const uint8_t *in, size_t bytes);
uint16_t Comm_Crc16(const uint8_t *data, size_t length);
uint32_t Comm_Crc32Update(uint32_t crc, const uint8_t *data, size_t length);
size_t Comm_Encode(const Comm_Frame *frame, uint8_t *out, size_t capacity);
void Comm_ParserFeed(Comm_Parser *parser, uint8_t byte);
int Comm_ParserNext(Comm_Parser *parser, Comm_Frame *frame);
void Comm_EncodeLog(const Comm_Log *record, uint8_t out[COMM_LOG_BYTES]);

int Comm_UsersAccept(Comm_Users *users, const Comm_Frame *frame);
/* Sua 24/11: Accept khong tu chep bang nua. Khi Accept tra 1, ben goi chep staging -> active
 * bang ham nay (task_comm goi trong critical section, chi ~5us), con CRC32 tinh ngoai vung gang. */
void Comm_UsersCommit(Comm_Users *users);
#ifdef __cplusplus
}
#endif
#endif
