#ifndef COMM_CONFIG_H
#define COMM_CONFIG_H


#define COMM_DEVICE_ID          1UL /* ID c?a, kh�c 0 v� gi? ?n d?nh qua reset. */
#define COMM_FIRMWARE_VERSION   3UL /* Tru?ng version firmware trong HELLO. */
#define COMM_TASK_STACK         384U /* Word tr�n port Cortex-M3, kh�ng ph?i byte nhu ESP32. */
#define COMM_LOG_QUEUE_LEN      12U /* G?m c? b?n dang ch? ACK, luu trong RAM. */
#define COMM_EVENT_QUEUE_LEN    6U  /* Queue Comm -> Main, t�ch kh?i queue log. */
#define COMM_ACK_MS             1000UL /* Timeout ba l?n g?i d?u. */
#define COMM_RETRY_SLOW_MS      5000UL /* Kho?ng ch? sau l?n g?i th? ba, kh�ng b? b?n ghi. */
#define COMM_HEARTBEAT_MS       2000UL /* Chu k? g?i HELLO cho ESP. */
#define COMM_LINK_TIMEOUT_MS    6000UL /* Qu� l�u kh�ng c� khung h?p l? th� b�o m?t link. */
#define COMM_OTA_HOLD_MS        30000UL /* Gi?i h?n im l?ng sau READY n?u ESP chua reset MCU. */
#define COMM_OTA_MAIN_MS        12000UL /* Th?i gian ch? Main chu?n b? v� duy?t OTA. */
#define COMM_APP_BYTES          (60UL * 1024UL) /* Kh�ng ch?ng ?ng d?ng l�n b?n trang d? li?u. */
#define COMM_DEBUG_TERMINAL     0 /* B?n v?n h�nh kh�ng cho terminal g?i th?ng l?nh qu?n l� v�n tay. */
#endif
