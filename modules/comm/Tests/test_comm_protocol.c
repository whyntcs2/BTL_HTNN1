#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "comm_protocol.h"
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); exit(1); } } while (0)
static void feed(Comm_Parser *parser, const uint8_t *bytes, size_t size)
{
    for (size_t i = 0; i < size; ++i) { Comm_ParserFeed(parser, bytes[i]); }
}
static void begin(Comm_Users *users, uint32_t version, uint16_t count)
{
    Comm_Frame frame = {0}; frame.type = COMM_USERS_BEGIN; frame.length = 6;
    Comm_PutLe(frame.payload, version, 4); Comm_PutLe(frame.payload + 4, count, 2);
    CHECK(!Comm_UsersAccept(users, &frame));
}
static void row(Comm_Users *users, uint16_t index, uint16_t id, const char *name)
{
    Comm_Frame frame = {0}; size_t size = strlen(name); CHECK(size <= COMM_NAME_MAX);
    frame.type = COMM_USER; frame.length = (uint16_t)(9 + size);
    Comm_PutLe(frame.payload, users->staging_version, 4);
    Comm_PutLe(frame.payload + 4, index, 2); Comm_PutLe(frame.payload + 6, id, 2);
    frame.payload[8] = (uint8_t)size; memcpy(frame.payload + 9, name, size);
    CHECK(!Comm_UsersAccept(users, &frame));
}
static int end(Comm_Users *users, uint32_t crc)
{
    Comm_Frame frame = {0}; frame.type = COMM_USERS_END; frame.length = 10;
    Comm_PutLe(frame.payload, users->staging_version, 4);
    Comm_PutLe(frame.payload + 4, users->count, 2); Comm_PutLe(frame.payload + 6, crc, 4);
    return Comm_UsersAccept(users, &frame);
}
int main(void)
{
    const uint8_t digits[] = "123456789";
    const uint8_t name_row[] = {0, 0, 7, 0, 3, 'T', 'a', 'i'};
    uint8_t bytes[COMM_FRAME_MAX], bad[COMM_FRAME_MAX], log[COMM_LOG_BYTES];
    Comm_Frame frame = {0}, received;
    Comm_Parser parser = {{0}, 0}; Comm_Users users = {0}; Comm_Log record = {0};
    size_t size; uint32_t crc = ~Comm_Crc32Update(0xFFFFFFFFUL, name_row, sizeof(name_row));
    CHECK(Comm_Crc16(digits, 9) == 0x29B1);
    CHECK((~Comm_Crc32Update(0xFFFFFFFFUL, digits, 9)) == 0xCBF43926UL);
    puts("PASS: CRC16 and CRC32 match published check vectors");
    frame.type = COMM_LOG; frame.sequence = 0xABCD; frame.length = 24;
    record.device = 0x12345678; record.event = 0x0102030405060708ULL;
    record.epoch = 1700000000; record.user = 7; record.method = 2;
    record.result = record.door = record.time_quality = 1;
    Comm_EncodeLog(&record, log); memcpy(frame.payload, log, sizeof(log));
    CHECK(log[0] == 0x78 && log[4] == 8 && log[11] == 1 && log[23] == 0);
    size = Comm_Encode(&frame, bytes, sizeof(bytes)); CHECK(size == 34);
    CHECK(bytes[0] == 0xA5 && bytes[1] == 0x5A && bytes[2] == 1 && bytes[3] == 2);
    CHECK(bytes[4] == 24 && bytes[5] == 0 && bytes[6] == 0xCD && bytes[7] == 0xAB);
    feed(&parser, bytes, size); CHECK(Comm_ParserNext(&parser, &received));
    CHECK(received.sequence == 0xABCD && received.length == 24 && memcmp(received.payload, log, 24) == 0);
    puts("PASS: LOG layout and little-endian frame fields are stable");
    memcpy(bad, bytes, size); bad[10] ^= 0x10;
    Comm_ParserFeed(&parser, 0xAA); feed(&parser, bad, size);
    CHECK(!Comm_ParserNext(&parser, &received));
    feed(&parser, bytes, 9); CHECK(!Comm_ParserNext(&parser, &received));
    feed(&parser, bytes + 9, size - 9); CHECK(Comm_ParserNext(&parser, &received));
    puts("PASS: noise, bad CRC and fragmented input recover at the next valid frame");
    frame.length = COMM_PAYLOAD_MAX; memset(frame.payload, 0xCC, sizeof(frame.payload));
    size = Comm_Encode(&frame, bytes, sizeof(bytes)); CHECK(size == COMM_FRAME_MAX);
    feed(&parser, bytes, size); CHECK(Comm_ParserNext(&parser, &received) && received.length == COMM_PAYLOAD_MAX);
    CHECK(Comm_Encode(&frame, bytes, size - 1) == 0);
    frame.length++; CHECK(Comm_Encode(&frame, bytes, sizeof(bytes)) == 0);
    puts("PASS: payload and output capacity limits prevent buffer overwrite");
    for (unsigned i = 0; i < COMM_FRAME_MAX * 3; ++i) { Comm_ParserFeed(&parser, 0xEE); }
    CHECK(parser.used == COMM_FRAME_MAX && !Comm_ParserNext(&parser, &received));
    frame.length = 0; size = Comm_Encode(&frame, bytes, sizeof(bytes));
    feed(&parser, bytes, size); CHECK(Comm_ParserNext(&parser, &received));
    puts("PASS: a saturated parser keeps bounded storage and can resynchronize");
    begin(&users, 42, 1); row(&users, 0, 7, "Tai");
    CHECK(users.active_count == 0 && users.version == 0);
    CHECK(end(&users, crc) && users.active_count == 1 && users.version == 42);
    CHECK(strcmp(users.active[0].name, "Tai") == 0);
    begin(&users, 43, 1); row(&users, 0, 8, "Tai"); CHECK(!end(&users, crc));
    CHECK(users.active[0].id == 7 && users.version == 42);
    begin(&users, 44, 2); row(&users, 0, 8, "Tai"); CHECK(!end(&users, crc));
    CHECK(users.active_count == 1 && users.version == 42);
    puts("PASS: complete table commits atomically; bad CRC or missing rows preserve names and version");
    begin(&users, 45, 2); row(&users, 0, 7, "Tai"); row(&users, 1, 7, "Tai");
    CHECK(!users.collecting && users.active_count == 1);
    begin(&users, 46, 1); row(&users, 1, 8, "Tai"); CHECK(!users.collecting);
    begin(&users, 47, COMM_USER_MAX + 1); CHECK(!users.collecting && users.version == 42);
    puts("PASS: duplicate IDs, skipped indices and oversized tables cannot replace active names");
    begin(&users, 48, 0); CHECK(end(&users, 0) && users.active_count == 0 && users.version == 48);
    puts("PASS: empty snapshot clears names only after a valid final CRC");
    puts("ALL 8 COMM PROTOCOL TEST GROUPS PASSED"); return 0;
}
