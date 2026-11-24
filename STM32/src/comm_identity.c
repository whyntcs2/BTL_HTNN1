/**
 * @file comm_identity.c
 * @brief Cấp boot ID qua journal flash; không lưu các bản ghi ra vào trong file này.
 */
#include "comm_identity.h"     /* API dự trữ boot ID trước scheduler. */
#include "stm32f10x_flash.h"   /* Xóa trang và ghi flash bằng SPL. */

#define ID_PAGE0 0x0800F000UL  /* Trang 60 của STM32F103C8, ngoài ứng dụng 60 KB. */
#define ID_PAGE1 0x0800F400UL  /* Trang 61; giữ trang cũ khi cần đổi trang ghi. */
#define ID_STRIDE 12UL        /* Mỗi ô: value 4, inverse 4, marker 2, dự phòng 2 byte. */
#define ID_SLOTS (1024UL / ID_STRIDE) /* 85 ô trên một trang 1 KB. */
#define ID_MARKER 0xA75AU     /* Ghi sau cùng để phân biệt bản hoàn tất và bản ghi dở. */
#define READ32(addr) (*(volatile const uint32_t *)(addr)) /* Đọc trực tiếp flash 32 bit. */
#define READ16(addr) (*(volatile const uint16_t *)(addr)) /* Đọc dấu commit 16 bit. */

int CommIdentity_ReserveBoot(uint32_t *boot)
{
    const uint32_t pages[2] = {ID_PAGE0, ID_PAGE1};
    uint32_t page, slot, address, value;
    uint32_t highest = 0, active = 0, free_slot = ID_SLOTS;
    int virgin = 1;           /* Chỉ bằng 1 nếu toàn bộ các ô còn ở trạng thái xóa. */
    int ok;
    if (boot == 0) { return 0; }
    for (page = 0; page < 2; ++page) {
        for (slot = 0; slot < ID_SLOTS; ++slot) {
            address = pages[page] + slot * ID_STRIDE;
            if (READ32(address) != 0xFFFFFFFFUL || READ32(address + 4) != 0xFFFFFFFFUL ||
                READ32(address + 8) != 0xFFFFFFFFUL) {
                virgin = 0; /* Có dữ liệu thì không được giả định là lần khởi động đầu. */
            }
            value = READ32(address);
            if (READ16(address + 8) == ID_MARKER && value != 0 && value != 0xFFFFFFFFUL &&
                READ32(address + 4) == ~value && value > highest) {
                highest = value; /* Chỉ dùng bản có marker và nghịch đảo đúng. */
                active = page;
            }
        }
    }
    if ((highest == 0 && !virgin) || highest >= 0xFFFFFFFEUL) {
        return 0;            /* Không tự xóa dữ liệu lạ hoặc quay vòng ID về giá trị cũ. */
    }
    for (slot = 0; slot < ID_SLOTS; ++slot) {
        address = pages[active] + slot * ID_STRIDE;
        if (READ32(address) == 0xFFFFFFFFUL && READ32(address + 4) == 0xFFFFFFFFUL &&
            READ32(address + 8) == 0xFFFFFFFFUL) {
            free_slot = slot; /* Chỉ ghi vào ô hoàn toàn trống, bỏ qua ô ghi dở. */
            break;
        }
    }
    FLASH_Unlock();           /* Mở khóa bộ điều khiển ghi flash. */
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    if (free_slot == ID_SLOTS) {
        active ^= 1UL;       /* Giữ nguyên trang chứa highest, chỉ xóa trang còn lại. */
        if (FLASH_ErasePage(pages[active]) != FLASH_COMPLETE) {
            FLASH_Lock();
            return 0;
        }
        free_slot = 0;
    }
    address = pages[active] + free_slot * ID_STRIDE;
    value = highest + 1UL;   /* Một boot ID cho một lần khởi động, không phải mỗi log. */
    ok = FLASH_ProgramWord(address, value) == FLASH_COMPLETE &&
         FLASH_ProgramWord(address + 4, ~value) == FLASH_COMPLETE &&
         FLASH_ProgramHalfWord(address + 8, ID_MARKER) == FLASH_COMPLETE;
    FLASH_Lock();            /* Luôn khóa lại sau cả thành công và thất bại. */
    if (!ok || READ32(address) != value || READ32(address + 4) != ~value || READ16(address + 8) != ID_MARKER) {
        return 0;            /* Không cho phát log với một ID chưa được đọc kiểm tra. */
    }
    *boot = value;           /* Chỉ xuất ID sau khi dấu commit đã được xác nhận. */
    return 1;
}
