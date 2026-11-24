#ifndef TASK_AS608_H
#define TASK_AS608_H

#include "FreeRTOS.h"
#include <stdint.h>


#define AS608_ID_ALL    0xFFFFU
#define AS608_MAX_ID        300U
#define AS608_INDEX_BYTES   ((AS608_MAX_ID + 7U) / 8U)

/* ================== Lenh task khac gui vao ================== */
typedef enum {
    AS608_CMD_PAUSE = 0,    /* dung quet (sang numpad, dang khoa, dang o menu) */
    AS608_CMD_RESUME,       /* quet lai */
    AS608_CMD_ENROLL,       /* dang ky van tay moi vao ID trong dau tien */
    AS608_CMD_CANCEL,       /* huy lenh dang chay (duoc dua len dau queue) */
    AS608_CMD_REENROLL,     /* lam lai van tay, giu nguyen ID: usId = ID can lam lai */
    AS608_CMD_DELETE_ID,    /* xoa nguoi: usId = ID can xoa */
    AS608_CMD_LOOKUP,       /* tra: dat tay, bao ngon nay la ID may (khong mo cua) */
    AS608_CMD_EMPTY,        /* xoa TOAN BO thu vien (menu phai hoi xac nhan truoc) */
    AS608_CMD_READ_INDEX    /* doc danh sach ID dang co van tay (them 20/11, menu danh sach) */
} AS608_CmdType;

typedef struct {
    AS608_CmdType eCmd;
    uint16_t      usId;     
} AS608_Command;

/* ================== Su kien task AS608 gui ra ================== */
typedef enum {
    AS608_EVT_READY = 0,    /* module san sang, usId = so mau dang luu */
    AS608_EVT_ERROR,        /* loi, usValue = ma loi cua lenh hong (0xFF: mat giao tiep) */
    AS608_EVT_MATCH,        /* nhan ra van tay, usId = ID, usValue = diem */
    AS608_EVT_NO_MATCH,     /* co ngon tay nhung khong co trong thu vien */
    AS608_EVT_BAD_IMAGE,    /* anh xau, nhac dat lai tay (khong tinh la 1 lan thu) */
    AS608_EVT_PAUSED,       /* da dung quet theo lenh PAUSE */
    AS608_EVT_RESUMED,      /* da quet lai theo lenh RESUME */
    AS608_EVT_ENROLL_STEP,  /* dang ky: usValue = AS608_EnrollStep (de OLED nhac) */
    AS608_EVT_ENROLL_OK,    /* dang ky xong, usId = ID moi */
    AS608_EVT_ENROLL_FAIL,  /* dang ky hong, usValue = AS608_FailReason */
    AS608_EVT_DUPLICATE,    /* ngon nay da co, usId = ID dang luu ngon do */
    AS608_EVT_CANCELLED,    /* lenh dang chay da bi huy */
    AS608_EVT_DELETED,      /* da xoa, usId = ID da xoa (AS608_ID_ALL: xoa toan bo) */
    AS608_EVT_LOOKUP_RESULT,/* ket qua tra, usId = ID, usValue = diem (KHONG phai mo cua) */
    AS608_EVT_NOT_FOUND,    /* tra: ngon nay khong co trong thu vien */
    AS608_EVT_TIMEOUT,      /* tra: het gio ma khong dat tay */
    AS608_EVT_INDEX_READY   /* READ_INDEX xong, usId = so ID co van tay; lay mang bit bang vTaskAS608GetIndex() */
} AS608_EventType;

/* Buoc dang ky (usValue cua AS608_EVT_ENROLL_STEP) */
typedef enum {
    AS608_STEP_PLACE_1 = 0, /* moi dat tay lan 1 */
    AS608_STEP_REMOVE,      /* moi nhac tay ra */
    AS608_STEP_PLACE_2      /* moi dat lai ngon do */
} AS608_EnrollStep;

/* Ly do dang ky hong (usValue cua AS608_EVT_ENROLL_FAIL) */
typedef enum {
    AS608_FAIL_TIMEOUT = 0, /* het gio cho dat / nhac tay */
    AS608_FAIL_MISMATCH,    /* 2 lan dat khong cung 1 ngon */
    AS608_FAIL_FULL,        /* thu vien day */
    AS608_FAIL_FLASH,       /* module ghi flash loi */
    AS608_FAIL_COMM,        /* loi giao tiep */
    AS608_FAIL_INVALID_ID   /* ID vuot qua dung luong thu vien */
} AS608_FailReason;


typedef struct {
    AS608_EventType eType;
    uint16_t        usId;       /* ID van tay hoac so mau, tuy su kien */
    uint16_t        usValue;    /* diem so, ma loi, buoc, ly do..., tuy su kien */
} AS608_Event;


BaseType_t xTaskAS608Create(UBaseType_t uxPriority);


BaseType_t xTaskAS608SendCommand(AS608_CmdType eCmd, uint16_t usId, TickType_t xTicksToWait);
BaseType_t xTaskAS608GetEvent(AS608_Event *pxEvent, TickType_t xTicksToWait);

void vTaskAS608GetIndex(uint8_t *pucBitmap);

#endif 