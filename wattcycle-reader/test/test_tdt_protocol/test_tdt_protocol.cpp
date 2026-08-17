// test_tdt_protocol.cpp — host-side tests for the TDT wire protocol.
//
// Runs on the build host with no hardware attached:  pio test -e native
//
// Every fixture below is a real frame captured from this battery via
// aiobmsble 0.27.0 (docs/wattcycle-reader-poc_3.md §5.7, raw log in
// tools/results_from_aiobmsble.txt). The expected values are that capture's
// decode, so these tests are a port of a known-good oracle, not guesses.
//
// The 0x92 response is given in full here; §5.7 elides its payload with "...".
// The bytes come from the raw log line, and its CRC verifies.

#include <string.h>
#include <unity.h>

#include "BmsData.h"
#include "TdtProtocol.h"

using namespace bms;
using namespace bms::tdt;

// --- Fixtures: requests (§5.3) ---------------------------------------------

static const uint8_t kReq8C[] = {0x1e, 0x00, 0x01, 0x03, 0x00, 0x8c,
                                 0x00, 0x00, 0xb1, 0x44, 0x0d};
static const uint8_t kReq8D[] = {0x1e, 0x00, 0x01, 0x03, 0x00, 0x8d,
                                 0x00, 0x00, 0x71, 0x15, 0x0d};
static const uint8_t kReq92[] = {0x1e, 0x00, 0x01, 0x03, 0x00, 0x92,
                                 0x00, 0x00, 0xb7, 0x24, 0x0d};

// --- Fixtures: responses (§5.7) --------------------------------------------

// 0x8C — cells, temps, pack V/I, SOC, capacity, cycles. 43 bytes.
// Note the two framing traps this frame contains, both tested below:
//   - four literal 0x0D bytes (0x0D89, 0x0DA1, 0x0D9C, 0x0D9B) before the
//     real terminator
//   - a literal 0x7E (cell 4 = 0x0B7E) after the head
static const uint8_t kRsp8C[] = {
    0x7e, 0x00, 0x01, 0x03, 0x00, 0x8c, 0x00, 0x20, 0x04, 0x0d, 0x89, 0x0d,
    0xa1, 0x0d, 0x9c, 0x0d, 0x9b, 0x04, 0x0b, 0x82, 0x0b, 0x9d, 0x0b, 0x7f,
    0x0b, 0x7e, 0x40, 0x00, 0x05, 0x70, 0x03, 0xe7, 0x03, 0xe8, 0x00, 0x01,
    0x03, 0xe8, 0x00, 0x64, 0x55, 0xa3, 0x0d,
};

// 0x8D — alarm/protection bitmaps, MOSFET status. 35 bytes.
static const uint8_t kRsp8D[] = {
    0x7e, 0x00, 0x01, 0x03, 0x00, 0x8d, 0x00, 0x18, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbd, 0x3f, 0x0d,
};

// 0x92 — SW version, manufacturer, serial number. 71 bytes.
static const uint8_t kRsp92[] = {
    0x7e, 0x00, 0x01, 0x03, 0x00, 0x92, 0x00, 0x3c, 0x57, 0x54, 0x33, 0x30,
    0x5f, 0x31, 0x30, 0x30, 0x30, 0x34, 0x53, 0x57, 0x31, 0x34, 0x5f, 0x4c,
    0x5f, 0x30, 0x31, 0x00, 0x31, 0x31, 0x31, 0x31, 0x32, 0x32, 0x32, 0x32,
    0x33, 0x33, 0x33, 0x33, 0x34, 0x34, 0x34, 0x34, 0x35, 0x35, 0x35, 0x35,
    0x49, 0x4b, 0x4b, 0x4b, 0x4b, 0x30, 0x30, 0x30, 0x30, 0x41, 0x49, 0x49,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x2e, 0xb7, 0x0d,
};

// --- Helpers ---------------------------------------------------------------

// Push a whole buffer through the reassembler in fixed-size chunks, the way a
// notification stream would arrive. Returns the number of complete frames, and
// leaves the reassembler holding the last one.
static int feedInChunks(FrameReassembler& rx, const uint8_t* data, size_t len,
                        size_t chunk) {
    int complete = 0;
    size_t sent = 0;
    while (sent < len) {
        const size_t n = (len - sent < chunk) ? (len - sent) : chunk;
        size_t off = 0;
        while (off < n) {
            FrameReassembler::Status st;
            off += rx.feed(data + sent + off, n - off, st);
            if (st == FrameReassembler::STATUS_COMPLETE) ++complete;
        }
        sent += n;
    }
    return complete;
}

static FrameReassembler::Status feedAll(FrameReassembler& rx,
                                        const uint8_t* data, size_t len) {
    FrameReassembler::Status last = FrameReassembler::STATUS_INCOMPLETE;
    size_t off = 0;
    while (off < len) {
        FrameReassembler::Status st;
        off += rx.feed(data + off, len - off, st);
        if (st != FrameReassembler::STATUS_INCOMPLETE) last = st;
    }
    return last;
}

void setUp(void) {}
void tearDown(void) {}

// --- CRC (§5.2) ------------------------------------------------------------

// CRC-16/MODBUS over everything before the CRC, transmitted big-endian — the
// opposite of the Modbus convention, and an easy bug to write.
static void assertCrcBigEndian(const uint8_t* frame, size_t len,
                               const char* what) {
    const size_t body = len - 3;
    const uint16_t calc = crc16Modbus(frame, body);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(frame[body], (uint8_t)(calc >> 8), what);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(frame[body + 1], (uint8_t)(calc & 0xFF), what);
}

void test_crc_matches_all_captured_frames(void) {
    assertCrcBigEndian(kReq8C, sizeof(kReq8C), "req 0x8C");
    assertCrcBigEndian(kReq8D, sizeof(kReq8D), "req 0x8D");
    assertCrcBigEndian(kReq92, sizeof(kReq92), "req 0x92");
    assertCrcBigEndian(kRsp8C, sizeof(kRsp8C), "rsp 0x8C");
    assertCrcBigEndian(kRsp8D, sizeof(kRsp8D), "rsp 0x8D");
    assertCrcBigEndian(kRsp92, sizeof(kRsp92), "rsp 0x92");
}

void test_crc_is_not_little_endian(void) {
    // Guards the specific mistake §5.2 warns about: emitting the CRC in normal
    // Modbus byte order. If this ever passes, the CRC is being written wrong.
    const size_t body = sizeof(kRsp8C) - 3;
    const uint16_t calc = crc16Modbus(kRsp8C, body);
    TEST_ASSERT_NOT_EQUAL((uint8_t)(calc & 0xFF), kRsp8C[body]);
}

// --- Request building (§5.3) -----------------------------------------------

void test_build_request_matches_capture(void) {
    uint8_t out[kRequestLen];

    TEST_ASSERT_EQUAL_UINT(kRequestLen, buildRequest(CMD_CELLS_PACK, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kReq8C, out, kRequestLen);

    TEST_ASSERT_EQUAL_UINT(kRequestLen, buildRequest(CMD_ALARMS, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kReq8D, out, kRequestLen);

    TEST_ASSERT_EQUAL_UINT(kRequestLen, buildRequest(CMD_DEVICE_INFO, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kReq92, out, kRequestLen);
}

void test_build_request_uses_0x1E_head(void) {
    // §5.2: request head is 0x1E. This battery never answers 0x7E — probing it
    // costs ~10 s per session for nothing.
    uint8_t out[kRequestLen];
    buildRequest(CMD_CELLS_PACK, out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8(0x1e, out[0]);
}

void test_build_request_rejects_small_buffer(void) {
    uint8_t out[kRequestLen - 1];
    TEST_ASSERT_EQUAL_UINT(0, buildRequest(CMD_CELLS_PACK, out, sizeof(out)));
}

// --- Reassembly (§5.5) -----------------------------------------------------

void test_single_notification_happy_path(void) {
    // MTU 512 on macOS delivered the whole frame in one notification.
    FrameReassembler rx;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_COMPLETE,
                      feedAll(rx, kRsp8C, sizeof(kRsp8C)));
    TEST_ASSERT_EQUAL_HEX8(CMD_CELLS_PACK, rx.frame().cmd);
    TEST_ASSERT_EQUAL_UINT8(0x20, rx.frame().payload_len);
    TEST_ASSERT_EQUAL_UINT8(4, rx.frame().payload[0]);   // cell count
}

void test_fragmented_at_20_7_and_1_byte_chunks(void) {
    // §5.5: verified against captured frames at 20-, 7- and 1-byte chunk sizes.
    // 20 is the payload of a default 23-byte MTU — the case that happens when
    // MTU negotiation is refused.
    const size_t chunks[] = {20, 7, 1, 3, 42};
    for (size_t c = 0; c < sizeof(chunks) / sizeof(chunks[0]); ++c) {
        FrameReassembler rx;
        const int n = feedInChunks(rx, kRsp8C, sizeof(kRsp8C), chunks[c]);
        TEST_ASSERT_EQUAL_INT(1, n);
        TEST_ASSERT_EQUAL_HEX8(CMD_CELLS_PACK, rx.frame().cmd);

        BmsData d;
        TEST_ASSERT_TRUE(decodeCellsAndPack(rx.frame(), d));
        TEST_ASSERT_EQUAL_UINT8(100, d.soc_pct);   // survives any chunking
    }
}

void test_embedded_0x0D_does_not_terminate_frame(void) {
    // The trap in §5.5: cell voltages near 3.4 V encode as 0x0D89, 0x0DA1, ...
    // so accumulating until 0x0D silently truncates the frame. Assert the
    // fixture really does contain four of them before the terminator, then
    // assert the reassembler is unbothered.
    int embedded = 0;
    for (size_t i = 0; i < sizeof(kRsp8C) - 1; ++i)
        if (kRsp8C[i] == 0x0d) ++embedded;
    TEST_ASSERT_EQUAL_INT(4, embedded);

    FrameReassembler rx;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_COMPLETE,
                      feedAll(rx, kRsp8C, sizeof(kRsp8C)));
    TEST_ASSERT_EQUAL_UINT8(0x20, rx.frame().payload_len);
}

void test_embedded_0x7E_does_not_start_new_frame(void) {
    // Cell 4 reads 0x0B7E, so the response head appears inside the payload.
    // Once framing is length-driven, head bytes must be ignored.
    int embedded = 0;
    for (size_t i = 1; i < sizeof(kRsp8C); ++i)
        if (kRsp8C[i] == 0x7e) ++embedded;
    TEST_ASSERT_EQUAL_INT(1, embedded);

    FrameReassembler rx;
    TEST_ASSERT_EQUAL_INT(1, feedInChunks(rx, kRsp8C, sizeof(kRsp8C), 1));
}

void test_resync_discards_leading_garbage(void) {
    uint8_t buf[8 + sizeof(kRsp8C)];
    const uint8_t junk[] = {0x00, 0xff, 0x12, 0x34, 0x0d, 0xaa, 0x55, 0x99};
    memcpy(buf, junk, sizeof(junk));
    memcpy(buf + sizeof(junk), kRsp8C, sizeof(kRsp8C));

    FrameReassembler rx;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_COMPLETE, feedAll(rx, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(CMD_CELLS_PACK, rx.frame().cmd);
    TEST_ASSERT_EQUAL_UINT(sizeof(junk), rx.discardedBytes());
}

void test_two_frames_in_one_chunk(void) {
    uint8_t buf[sizeof(kRsp8C) + sizeof(kRsp8D)];
    memcpy(buf, kRsp8C, sizeof(kRsp8C));
    memcpy(buf + sizeof(kRsp8C), kRsp8D, sizeof(kRsp8D));

    FrameReassembler rx;
    uint8_t seen[2] = {0, 0};
    int n = 0;
    size_t off = 0;
    while (off < sizeof(buf)) {
        FrameReassembler::Status st;
        off += rx.feed(buf + off, sizeof(buf) - off, st);
        if (st == FrameReassembler::STATUS_COMPLETE && n < 2) seen[n++] = rx.frame().cmd;
    }
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_HEX8(CMD_CELLS_PACK, seen[0]);
    TEST_ASSERT_EQUAL_HEX8(CMD_ALARMS, seen[1]);
}

void test_crc_error_is_detected(void) {
    uint8_t bad[sizeof(kRsp8C)];
    memcpy(bad, kRsp8C, sizeof(bad));
    bad[10] ^= 0x01;   // flip one bit in cell 1

    FrameReassembler rx;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_CRC_ERROR, feedAll(rx, bad, sizeof(bad)));
}

void test_bad_terminator_is_detected(void) {
    uint8_t bad[sizeof(kRsp8C)];
    memcpy(bad, kRsp8C, sizeof(bad));
    bad[sizeof(bad) - 1] = 0x00;

    FrameReassembler rx;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_BAD_TERMINATOR, feedAll(rx, bad, sizeof(bad)));
}

void test_reassembler_recovers_after_bad_frame(void) {
    uint8_t bad[sizeof(kRsp8C)];
    memcpy(bad, kRsp8C, sizeof(bad));
    bad[12] ^= 0xff;

    FrameReassembler rx;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_CRC_ERROR, feedAll(rx, bad, sizeof(bad)));
    // A corrupt frame must not wedge the stream — the next good one decodes.
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_COMPLETE, feedAll(rx, kRsp8C, sizeof(kRsp8C)));
    TEST_ASSERT_EQUAL_HEX8(CMD_CELLS_PACK, rx.frame().cmd);
}

void test_partial_frame_times_out(void) {
    // §5.5 rule 5: a frame that stops mid-flight is dropped after ~1 s so the
    // next notification isn't glued onto its tail.
    FrameReassembler rx;
    FrameReassembler::Status st;
    rx.feed(kRsp8C, 12, st, 1000);
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_INCOMPLETE, st);
    TEST_ASSERT_TRUE(rx.hasPartial());

    rx.tick(1500);
    TEST_ASSERT_TRUE(rx.hasPartial());     // not yet
    rx.tick(2000);
    TEST_ASSERT_FALSE(rx.hasPartial());    // dropped

    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_COMPLETE, feedAll(rx, kRsp8C, sizeof(kRsp8C)));
}

// --- Decode 0x8C (§5.4, ground truth in §5.7) ------------------------------

void test_decode_0x8C_matches_reference_capture(void) {
    FrameReassembler rx;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_COMPLETE, feedAll(rx, kRsp8C, sizeof(kRsp8C)));

    BmsData d;
    TEST_ASSERT_TRUE(decodeCellsAndPack(rx.frame(), d));
    TEST_ASSERT_TRUE(d.valid);

    // 4 cells / 4 temp sensors
    TEST_ASSERT_EQUAL_UINT8(4, d.cell_count);
    TEST_ASSERT_EQUAL_UINT8(4, d.temp_count);

    // 3.465, 3.489, 3.484, 3.483 V
    TEST_ASSERT_EQUAL_UINT16(3465, d.cell_mV[0]);
    TEST_ASSERT_EQUAL_UINT16(3489, d.cell_mV[1]);
    TEST_ASSERT_EQUAL_UINT16(3484, d.cell_mV[2]);
    TEST_ASSERT_EQUAL_UINT16(3483, d.cell_mV[3]);
    TEST_ASSERT_EQUAL_UINT16(24, d.deltaCell_mV());   // delta 24 mV

    // 21.5 (ambient), 24.2 (MOSFET), 21.2, 21.1 °C — raw is 0.1 K
    TEST_ASSERT_EQUAL_INT16(215, d.temp_dC[0]);
    TEST_ASSERT_EQUAL_INT16(242, d.temp_dC[1]);
    TEST_ASSERT_EQUAL_INT16(212, d.temp_dC[2]);
    TEST_ASSERT_EQUAL_INT16(211, d.temp_dC[3]);

    TEST_ASSERT_EQUAL_UINT32(13920, d.pack_mV);       // 13.92 V
    TEST_ASSERT_EQUAL_UINT8(100, d.soc_pct);          // 100 %
    TEST_ASSERT_EQUAL_UINT16(999, d.remaining_dAh);   // 99.9 Ah
    TEST_ASSERT_EQUAL_UINT16(1000, d.nominal_dAh);    // 100.0 Ah
    TEST_ASSERT_EQUAL_UINT16(1, d.cycles);
    TEST_ASSERT_EQUAL_UINT16(1000, d.soh_dpct);       // 100.0 %
}

void test_decode_0x8C_current_encoding(void) {
    // The field worth extra care (§5.4): raw 0x4000 is the discharge flag with
    // zero magnitude. Read as a plain signed int16 it gives 16384, not 0.
    FrameReassembler rx;
    feedAll(rx, kRsp8C, sizeof(kRsp8C));

    BmsData d;
    TEST_ASSERT_TRUE(decodeCellsAndPack(rx.frame(), d));
    TEST_ASSERT_EQUAL_INT32(0, d.current_mA);         // 0.0 A at rest
    TEST_ASSERT_TRUE(d.discharging);                  // raw flag is set

    // And synthesised magnitudes, since the real pack has only been seen at
    // rest. 0x4064 -> discharge 100 * 10 mA = 1.00 A; 0x0064 -> charge 1.00 A.
    // NOTE: the sign convention itself is unverified (§5.8) — these lock in the
    // magnitude and flag extraction, not the polarity.
    uint8_t f[sizeof(kRsp8C)];
    memcpy(f, kRsp8C, sizeof(f));
    const size_t cur = 8 + 1 + 8 + 1 + 8;   // header + N + cells + M + temps

    f[cur] = 0x40; f[cur + 1] = 0x64;
    uint16_t crc = crc16Modbus(f, sizeof(f) - 3);
    f[sizeof(f) - 3] = (uint8_t)(crc >> 8);
    f[sizeof(f) - 2] = (uint8_t)(crc & 0xFF);
    FrameReassembler rx2;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_COMPLETE, feedAll(rx2, f, sizeof(f)));
    TEST_ASSERT_TRUE(decodeCellsAndPack(rx2.frame(), d));
    TEST_ASSERT_EQUAL_INT32(-1000, d.current_mA);
    TEST_ASSERT_TRUE(d.discharging);

    f[cur] = 0x00; f[cur + 1] = 0x64;
    crc = crc16Modbus(f, sizeof(f) - 3);
    f[sizeof(f) - 3] = (uint8_t)(crc >> 8);
    f[sizeof(f) - 2] = (uint8_t)(crc & 0xFF);
    FrameReassembler rx3;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_COMPLETE, feedAll(rx3, f, sizeof(f)));
    TEST_ASSERT_TRUE(decodeCellsAndPack(rx3.frame(), d));
    TEST_ASSERT_EQUAL_INT32(1000, d.current_mA);
    TEST_ASSERT_FALSE(d.discharging);
}

void test_decode_rejects_wrong_command(void) {
    FrameReassembler rx;
    feedAll(rx, kRsp92, sizeof(kRsp92));
    BmsData d;
    TEST_ASSERT_FALSE(decodeCellsAndPack(rx.frame(), d));
    TEST_ASSERT_FALSE(d.valid);
}

void test_decode_rejects_truncated_payload(void) {
    // A frame whose length byte is honest but whose payload can't hold the
    // 14-byte tail. Must be rejected, not read past the end.
    uint8_t f[kHeaderLen + 10 + kTrailerLen];
    memcpy(f, kRsp8C, kHeaderLen);
    f[kOffsetPayloadLen] = 10;
    f[kHeaderLen] = 4;                       // claims 4 cells
    for (size_t i = 1; i < 10; ++i) f[kHeaderLen + i] = 0;
    const uint16_t crc = crc16Modbus(f, sizeof(f) - 3);
    f[sizeof(f) - 3] = (uint8_t)(crc >> 8);
    f[sizeof(f) - 2] = (uint8_t)(crc & 0xFF);
    f[sizeof(f) - 1] = kTerminator;

    FrameReassembler rx;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_COMPLETE, feedAll(rx, f, sizeof(f)));
    BmsData d;
    TEST_ASSERT_FALSE(decodeCellsAndPack(rx.frame(), d));
}

// --- Decode 0x92 (§5.7) ----------------------------------------------------

void test_decode_0x92_device_info(void) {
    FrameReassembler rx;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_COMPLETE, feedAll(rx, kRsp92, sizeof(kRsp92)));
    TEST_ASSERT_EQUAL_HEX8(CMD_DEVICE_INFO, rx.frame().cmd);
    TEST_ASSERT_EQUAL_UINT8(0x3c, rx.frame().payload_len);

    DeviceInfo info;
    TEST_ASSERT_TRUE(decodeDeviceInfo(rx.frame(), info));
    TEST_ASSERT_EQUAL_STRING("WT30_10004SW14_L_01", info.sw_version);
    TEST_ASSERT_EQUAL_STRING("11112222333344445555", info.manufacturer);
    TEST_ASSERT_EQUAL_STRING("IKKKK0000AII00000000", info.serial_number);
}

// --- 0x8D framing only (decode deferred to M6, §5.7) -----------------------

void test_0x8D_frames_and_validates(void) {
    // 0x8D is only partially understood, so there is no decoder yet — but the
    // frame must still reassemble and pass CRC, because M3 dumps it raw.
    FrameReassembler rx;
    TEST_ASSERT_EQUAL(FrameReassembler::STATUS_COMPLETE, feedAll(rx, kRsp8D, sizeof(kRsp8D)));
    TEST_ASSERT_EQUAL_HEX8(CMD_ALARMS, rx.frame().cmd);
    TEST_ASSERT_EQUAL_UINT8(0x18, rx.frame().payload_len);
    TEST_ASSERT_EQUAL_UINT8(4, rx.frame().payload[0]);    // mirrors cell count
    TEST_ASSERT_EQUAL_UINT8(4, rx.frame().payload[5]);    // mirrors temp count
}

// --- Runner ----------------------------------------------------------------

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_crc_matches_all_captured_frames);
    RUN_TEST(test_crc_is_not_little_endian);

    RUN_TEST(test_build_request_matches_capture);
    RUN_TEST(test_build_request_uses_0x1E_head);
    RUN_TEST(test_build_request_rejects_small_buffer);

    RUN_TEST(test_single_notification_happy_path);
    RUN_TEST(test_fragmented_at_20_7_and_1_byte_chunks);
    RUN_TEST(test_embedded_0x0D_does_not_terminate_frame);
    RUN_TEST(test_embedded_0x7E_does_not_start_new_frame);
    RUN_TEST(test_resync_discards_leading_garbage);
    RUN_TEST(test_two_frames_in_one_chunk);
    RUN_TEST(test_crc_error_is_detected);
    RUN_TEST(test_bad_terminator_is_detected);
    RUN_TEST(test_reassembler_recovers_after_bad_frame);
    RUN_TEST(test_partial_frame_times_out);

    RUN_TEST(test_decode_0x8C_matches_reference_capture);
    RUN_TEST(test_decode_0x8C_current_encoding);
    RUN_TEST(test_decode_rejects_wrong_command);
    RUN_TEST(test_decode_rejects_truncated_payload);

    RUN_TEST(test_decode_0x92_device_info);

    RUN_TEST(test_0x8D_frames_and_validates);

    return UNITY_END();
}
