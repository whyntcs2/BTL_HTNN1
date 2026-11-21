# Giao thức STM32 — ESP32, phiên bản 1

Ứng dụng dùng UART 115200 8N1. Giao thức ROM AN3155 chỉ dùng trong phiên nạp,
chuyển UART sang 8E1 và không dùng khung ở tài liệu này.

## Khung ứng dụng

| Vị trí | Trường | Kích thước |
|---|---|---|
| 0 | SOF A5 5A | 2 byte |
| 2 | VERSION = 1 | 1 byte |
| 3 | TYPE | 1 byte |
| 4 | LENGTH payload | 2 byte little-endian |
| 6 | SEQUENCE | 2 byte little-endian |
| 8 | PAYLOAD | 0..192 byte |
| 8 + LENGTH | CRC16 | 2 byte little-endian |

CRC-16/CCITT-FALSE: poly 1021, init FFFF, refin/refout false, xorout 0000.
Tính từ VERSION đến hết PAYLOAD; không gồm SOF hoặc chính trường CRC.
Check vector ASCII `123456789` cho 29B1. SEQUENCE có thể tràn về 0.

Các số nhiều byte dưới đây đều little-endian. Không gửi nguyên struct vì C/C++ có padding.
Parser kiểm tra độ dài trước CRC, bỏ byte sai để tìm lại SOF; gói dở được bỏ sau khoảng nghỉ 250 ms.

## Loại thông điệp

| TYPE | Tên | Payload |
|---|---|---|
| 1 | HELLO | version firmware u32; STM32 gửi khoảng mỗi 2 giây |
| 2 | LOG | Bản ghi 24 byte theo bảng bên dưới |
| 3 | ACK_STORED | device u32 + event u64; sequence giữ nguyên của LOG |
| 4 | REJECT | reason u8: 1 invalid, 2 full, 3 storage, 4 ID conflict |
| 5 | STATUS | wifi u8 + cloud u8 + pending_logs u16 |
| 6 | TIME | Unix epoch UTC u32 |
| 7 | USERS_BEGIN | version u32 + count u16 |
| 8 | USER | version u32 + index u16 + id u16 + name_length u8 + UTF-8 name |
| 9 | USERS_END | version u32 + count u16 + CRC32 u32 |
| 10 | OTA_PREPARE | image_size u32 + SHA-256 32 byte |
| 11 | OTA_READY | Rỗng; sequence giữ nguyên của OTA_PREPARE |
| 12 | OTA_ABORT | Rỗng; sequence giữ nguyên của OTA_PREPARE |

## Payload LOG

| Offset | Trường | Độ dài | Ý nghĩa |
|---|---|---|---|
| 0 | device | 4 | ID cấu hình của cửa, khác 0 |
| 4 | event | 8 | boot_id cao 32 bit + counter thấp 32 bit |
| 12 | epoch | 4 | Thời điểm xảy ra, UTC |
| 16 | user | 2 | ID vân tay/người dùng; FFFF nếu không xác định |
| 18 | method | 1 | 1 FINGER, 2 PIN, 3 SYSTEM |
| 19 | result | 1 | 0 FAIL, 1 SUCCESS, 2 ALERT |
| 20 | door | 1 | 0 lệnh đóng, 1 lệnh mở, 2 chưa rõ |
| 21 | reason | 1 | 0 bình thường, 1 sai vân tay, 2 sai PIN, 3 vào khóa |
| 22 | time_quality | 1 | 0 chưa đồng bộ, 1 giờ hợp lệ |
| 23 | reserved | 1 | Bắt buộc 0 |

STM32 giữ nguyên bản LOG và sequence qua các lần thử; không đóng dấu thời gian lại khi gửi bù.
Task_Comm dùng xQueuePeek để lấy đầu queue. Chỉ xQueueReceive để bỏ bản đó khi nhận
ACK_STORED có đủ sequence, device và event trùng. Sai ACK, REJECT hoặc timeout không xóa log.
Thử lại sau 1 giây, từ lần thứ 3 chuyển khoảng nghỉ 5 giây, vẫn giữ bản đang chờ.

ESP nhận LOG hợp lệ, lưu NVS và đọc đối chiếu, sau đó ACK. Nếu đã có cùng ID và cùng nội dung,
ACK lại mà không thêm ô NVS. Nếu cùng ID nhưng nội dung khác, REJECT/CONFLICT.
Khi log đã lên Sheets và ESP đã xóa ô, một LOG gửi lại vẫn có thể được lưu lại;
Apps Script tiếp tục chống trùng theo ID để không tạo thêm dòng.

boot_id nằm trong journal hai trang flash STM32 60/61; không tăng trong mỗi lần gửi lại.
Mỗi lần khởi động dự trữ một boot_id trước scheduler. Hai trang luân phiên; chỉ xóa trang cũ hơn,
ghi counter và nghịch đảo trước dấu commit cuối cùng. Nếu dữ liệu không đủ tin cậy thì không
tạo Comm task. Không đổi COMM_DEVICE_ID giữa các lần khởi động của cùng một cửa.

## Bảng tên

Tối đa 32 dòng, tên tối đa 48 byte UTF-8. USER phải có index liên tiếp từ 0.
Comm chỉ thay bảng active khi đã nhận đủ count, không trùng ID và CRC cuối đúng.
Thiếu dòng, quá dung lượng hoặc sai CRC giữ nguyên bảng active trước đó.

CRC-32/ISO-HDLC: poly phản chiếu EDB88320, init FFFFFFFF, xorout FFFFFFFF.
Tính trên các dòng nối tiếp theo thứ tự index, mỗi dòng gồm:

```text
index:u16 | id:u16 | name_length:u8 | name:name_length bytes
```

Không gồm version trong CRC snapshot. Bảng rỗng có CRC32 = 0 và được áp dụng sau USERS_END đúng.
ESP gửi lại snapshot định kỳ; bản thử chưa có ACK riêng cho từng dòng bảng tên.
Nếu mất một dòng do nhiễu thì Comm giữ bảng cũ đến lần đồng bộ sau.

## Bắt tay OTA

ESP gửi OTA_PREPARE một lần và chờ tối đa 12 giây. Main từ chối khi khóa/menu hoặc kích thước sai.
Main nhận yêu cầu hợp lệ mới phát PAUSE/CLOSE, chờ hai task xác nhận đã xử lý,
chờ servo thêm 1 giây và chờ queue log rỗng. Sau đó Main gửi phản hồi cho Comm qua queue;
Comm kiểm tra lại queue log rỗng, mới phát OTA_READY và ngừng các khung ứng dụng.

Task_Comm vẫn gửi LOG trong giai đoạn chuẩn bị để có thể xả queue; không dừng LOG ngay khi PREPARE đến.
Nếu không đủ điều kiện trong 10 giây, Main yêu cầu OTA_ABORT và đưa vân tay về trạng thái chạy.
Comm cũng có timeout yêu cầu Main 12 giây; quá hạn gửi ABORT về ESP và giữ thông báo hủy cho Main.
Nếu queue Main đầy, một PREPARE mới không được ghi đè thông báo hủy của phiên cũ. Nếu ESP không reset STM32 sau READY, Comm hủy chế độ im lặng
sau 30 giây và báo Main để hệ thống có thể trở lại.

Trong phiên AN3155, ESP chỉ gửi lệnh ROM. Nạp thành công mới reset về ứng dụng và chờ HELLO 4 byte.
ACK ROM = 79 là byte giao thức nạp, không dùng thay cho ACK_STORED hoặc OTA_READY.

## Giới hạn bảo đảm

CRC phát hiện lỗi truyền, không phải chữ ký hay xác thực người gửi. UART là đường nối nội bộ tin cậy.
ACK_STORED chỉ nói ESP đã nhận quyền giữ log, không nói log đã lên Sheets.
Các queue có dung lượng hữu hạn. Queue STM32 là RAM; boot_id bền vững không làm bản ghi RAM bền vững.
Không tuyên bố không mất bản ghi trong mọi tình huống nguồn điện, hỏng flash hoặc queue đầy.


Module STM32 chỉ thực hiện phía Comm của giao thức. Phía ESP/Sheets là hợp đồng đầu bên kia,
không được kèm trong phần source bàn giao này. Khung OTA_PREPARE có SHA-256 để phía ESP đối chiếu ảnh;
Task_Comm chỉ chuyển kích thước/sequence cho Main, không xác thực chữ ký hoặc nội dung firmware.
