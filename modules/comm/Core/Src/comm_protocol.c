/**
 * @file comm_protocol.c
 * @brief Đóng gói dữ liệu UART và kiểm tra bảng tên, độc lập với STM32/FreeRTOS.
 * Số nhiều byte được tách tường minh để tránh padding của struct C.
 */
#include "comm_protocol.h" /* Kiểu khung, bản ghi, parser và giới hạn bộ đệm. */
#include <string.h>        /* Chép payload và dồn bộ đệm khi bỏ byte lỗi. */

void Comm_PutLe(uint8_t *out, uint64_t value, size_t bytes)
{
    size_t i;
    /* Bên gọi truyền bytes từ 1..8; byte có trọng số thấp được ghi trước. */
    for (i = 0; i < bytes; ++i) { out[i] = (uint8_t)(value >> (8U * i)); }
}
uint64_t Comm_GetLe(const uint8_t *in, size_t bytes)
{
    uint64_t value = 0;
    size_t i;
    /* Dựng lại số từ các byte little-endian, không ép con trỏ không căn hàng. */
    for (i = 0; i < bytes; ++i) { value |= (uint64_t)in[i] << (8U * i); }
    return value;
}
uint16_t Comm_Crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU; /* Giá trị khởi đầu của CRC-16/CCITT-FALSE. */
    size_t i;
    unsigned bit;
    for (i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8; /* Đưa byte mới vào tám bit cao của CRC. */
        for (bit = 0; bit < 8U; ++bit) {
            crc = (uint16_t)((crc & 0x8000U) ? ((uint32_t)crc << 1) ^ 0x1021U : (uint32_t)crc << 1); /* Chia đa thức theo từng bit. */
        }
    }
    return crc;
}
uint32_t Comm_Crc32Update(uint32_t crc, const uint8_t *data, size_t length)
{
    size_t i;
    unsigned bit;
    for (i = 0; i < length; ++i) {
        crc ^= data[i]; /* CRC32 phản chiếu: đưa byte mới vào phía bit thấp. */
        for (bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U); /* Đa thức phản chiếu của CRC-32/ISO-HDLC. */
        }
    }
    return crc; /* Bên gọi dùng init FFFFFFFF và đảo bit sau byte cuối cùng. */
}
size_t Comm_Encode(const Comm_Frame *frame, uint8_t *out, size_t capacity)
{
    if (frame == NULL || out == NULL || frame->length > COMM_PAYLOAD_MAX ||
        capacity < frame->length + 10U) { return 0; } /* Kiểm tra trước khi ghi bất kỳ byte nào. */
    out[0] = 0xA5U; /* Hai byte đầu giúp parser tìm vị trí bắt đầu khung. */
    out[1] = 0x5AU;
    out[2] = 1U;    /* Phiên bản giao thức ứng dụng. */
    out[3] = frame->type;
    Comm_PutLe(out + 4, frame->length, 2);
    Comm_PutLe(out + 6, frame->sequence, 2);
    memcpy(out + 8, frame->payload, frame->length);
    Comm_PutLe(out + 8 + frame->length, Comm_Crc16(out + 2, frame->length + 6U), 2); /* CRC không gồm SOF. */
    return frame->length + 10U; /* Trả tổng số byte để driver UART gửi. */
}
static void prvDiscard(Comm_Parser *parser, uint16_t count)
{
    memmove(parser->bytes, parser->bytes + count, parser->used - count); /* Nguồn/đích chồng nhau nên dùng memmove. */
    parser->used = (uint16_t)(parser->used - count);
}
void Comm_ParserFeed(Comm_Parser *parser, uint8_t byte)
{
    if (parser->used == COMM_FRAME_MAX) { prvDiscard(parser, 1); } /* Không ghi quá cuối mảng. */
    parser->bytes[parser->used++] = byte;
}
int Comm_ParserNext(Comm_Parser *parser, Comm_Frame *frame)
{
    uint16_t length;
    while (parser->used >= 2U) {
        if (parser->bytes[0] != 0xA5U || parser->bytes[1] != 0x5AU) { prvDiscard(parser, 1); continue; } /* Trượt một byte để tìm SOF. */
        if (parser->used < 8U) { return 0; } /* Chưa đủ header, giữ lại tới lần nhận sau. */
        length = (uint16_t)Comm_GetLe(parser->bytes + 4, 2); /* Đọc độ dài trước khi truy cập payload. */
        if (parser->bytes[2] != 1U || length > COMM_PAYLOAD_MAX) { prvDiscard(parser, 1); continue; }
        if (parser->used < length + 10U) { return 0; } /* Khung phân mảnh vẫn chưa nhận đủ. */
        if (Comm_Crc16(parser->bytes + 2, length + 6U) != Comm_GetLe(parser->bytes + 8 + length, 2)) {
            prvDiscard(parser, 1); continue; /* CRC sai: không chuyển dữ liệu lên task. */
        }
        frame->type = parser->bytes[3];
        frame->length = length;
        frame->sequence = (uint16_t)Comm_GetLe(parser->bytes + 6, 2);
        memcpy(frame->payload, parser->bytes + 8, length);
        prvDiscard(parser, (uint16_t)(length + 10U)); /* Chỉ bỏ các byte của khung vừa lấy. */
        return 1; /* Bên gọi có thể lấy tiếp nếu còn khung trong bộ đệm. */
    }
    return 0;
}
void Comm_EncodeLog(const Comm_Log *r, uint8_t out[COMM_LOG_BYTES])
{
    Comm_PutLe(out, r->device, 4);     /* Byte 0..3: mã cửa/thiết bị. */
    Comm_PutLe(out + 4, r->event, 8); /* Byte 4..11: ID chống trùng của bản ghi. */
    Comm_PutLe(out + 12, r->epoch, 4); /* Byte 12..15: giờ xảy ra sự kiện. */
    Comm_PutLe(out + 16, r->user, 2); /* Byte 16..17: ID người dùng. */
    out[18] = r->method;
    out[19] = r->result;
    out[20] = r->door;
    out[21] = r->reason;
    out[22] = r->time_quality;
    out[23] = 0; /* Byte dự phòng bắt buộc bằng 0 trong phiên bản 1. */
}
int Comm_UsersAccept(Comm_Users *users, const Comm_Frame *frame)
{
    uint16_t index, id, i;
    uint8_t size, prefix[5];
    uint32_t crc;
    if (frame->type == COMM_USERS_BEGIN) {
        users->collecting = 0; /* BEGIN mới hủy bảng nhận dở, chưa đụng bảng đang dùng. */
        if (frame->length != 6U || Comm_GetLe(frame->payload + 4, 2) > COMM_USER_MAX) { return 0; }
        users->staging_version = (uint32_t)Comm_GetLe(frame->payload, 4);
        users->count = (uint16_t)Comm_GetLe(frame->payload + 4, 2);
        users->received = 0; /* USER kế tiếp phải bắt đầu tại index 0. */
        users->collecting = 1;
        return 0;
    }
    if (!users->collecting || frame->length < 4U || Comm_GetLe(frame->payload, 4) != users->staging_version) { return 0; }
    if (frame->type == COMM_USER) {
        if (frame->length < 9U) { users->collecting = 0; return 0; }
        index = (uint16_t)Comm_GetLe(frame->payload + 4, 2);
        id = (uint16_t)Comm_GetLe(frame->payload + 6, 2);
        size = frame->payload[8]; /* Số byte UTF-8, không phải số ký tự hiển thị. */
        if (index != users->received || index >= users->count || id == 0xFFFFU ||
            size > COMM_NAME_MAX || frame->length != size + 9U) { users->collecting = 0; return 0; }
        for (i = 0; i < users->received; ++i) {
            if (users->staging[i].id == id) { users->collecting = 0; return 0; }
        }
        users->staging[index].id = id;
        users->staging[index].length = size;
        memcpy(users->staging[index].name, frame->payload + 9, size);
        users->staging[index].name[size] = '\0'; /* Chừa sẵn một byte sau tên dài tối đa. */
        users->received++;
        return 0;
    }
    if (frame->type != COMM_USERS_END || frame->length != 10U || users->received != users->count ||
        Comm_GetLe(frame->payload + 4, 2) != users->count) { users->collecting = 0; return 0; }
    crc = 0xFFFFFFFFUL; /* CRC của snapshot tính trên từng hàng, không gồm version. */
    for (i = 0; i < users->count; ++i) {
        Comm_PutLe(prefix, i, 2);
        Comm_PutLe(prefix + 2, users->staging[i].id, 2);
        prefix[4] = users->staging[i].length;
        crc = Comm_Crc32Update(crc, prefix, sizeof(prefix));
        crc = Comm_Crc32Update(crc, (const uint8_t *)users->staging[i].name, users->staging[i].length);
    }
    users->collecting = 0;
    if ((~crc) != Comm_GetLe(frame->payload + 6, 4)) { return 0; }
    memcpy(users->active, users->staging, users->count * sizeof(Comm_User)); /* Chỉ commit sau đủ hàng và CRC đúng. */
    users->active_count = users->count;
    users->version = users->staging_version;
    return 1;
}
