#ifndef COMM_IDENTITY_H
#define COMM_IDENTITY_H
#include <stdint.h>
/* Gọi một lần trước scheduler; trả 0 nếu flash lỗi, không tự xóa dữ liệu chưa xác định. */
int CommIdentity_ReserveBoot(uint32_t *boot);
#endif
