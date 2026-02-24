// tests/cpp/test_rdm_core.cpp
// Unit tests for: UIDToString, StringToUID, RDMChecksum, BuildRDMPacket, BytesToHex
// No hardware is opened — all functions under test are pure logic.
#include <gtest/gtest.h>
#include "rdm.h"
#include "validator.h"
#include <cstdint>
#include <vector>
#include <string>

// ═══════════════════════════════════════════════════════════════════════════
// UIDToString
// ═══════════════════════════════════════════════════════════════════════════

TEST(UIDToString, TypicalDevice) {
    // mfg = 0x454E, dev = 0x54000001
    uint64_t uid = 0x454E54000001ULL;
    EXPECT_EQ(UIDToString(uid), "454E:54000001");
}

TEST(UIDToString, AllZeros) {
    EXPECT_EQ(UIDToString(0ULL), "0000:00000000");
}

TEST(UIDToString, BroadcastUID) {
    EXPECT_EQ(UIDToString(RDM_BROADCAST_UID), "FFFF:FFFFFFFF");
}

TEST(UIDToString, MaxMfgZeroDev) {
    uint64_t uid = (uint64_t)0xFFFF << 32;
    EXPECT_EQ(UIDToString(uid), "FFFF:00000000");
}

TEST(UIDToString, ZeroMfgMaxDev) {
    uint64_t uid = 0xFFFFFFFFULL;
    EXPECT_EQ(UIDToString(uid), "0000:FFFFFFFF");
}

TEST(UIDToString, OutputIsUppercase) {
    uint64_t uid = ((uint64_t)0xABCD << 32) | 0xEF012345ULL;
    EXPECT_EQ(UIDToString(uid), "ABCD:EF012345");
}

TEST(UIDToString, FormatColonAtPosition4) {
    std::string s = UIDToString(0x123456789ABCULL);
    EXPECT_EQ(s.size(), 13u);
    EXPECT_EQ(s[4], ':');
}

// ═══════════════════════════════════════════════════════════════════════════
// StringToUID
// ═══════════════════════════════════════════════════════════════════════════

TEST(StringToUID, AllZeros) {
    EXPECT_EQ(StringToUID("0000:00000000"), 0ULL);
}

TEST(StringToUID, BroadcastUID) {
    EXPECT_EQ(StringToUID("FFFF:FFFFFFFF"), RDM_BROADCAST_UID);
}

TEST(StringToUID, LowercaseHex) {
    uint64_t expected = ((uint64_t)0xABCD << 32) | 0xEF012345ULL;
    EXPECT_EQ(StringToUID("abcd:ef012345"), expected);
}

TEST(StringToUID, RoundTripMultipleUIDs) {
    const std::vector<uint64_t> uids = {
        0x0000000000000001ULL,
        0x0001000000000000ULL,
        0x7FFF7FFFFFFFULL,
        0x000100000001ULL,
        ((uint64_t)0x454E << 32) | 0x00000001ULL,
    };
    for (auto uid : uids) {
        EXPECT_EQ(StringToUID(UIDToString(uid)), uid)
            << "Round-trip failed for UID 0x" << std::hex << uid;
    }
}

TEST(StringToUID, MalformedInputDoesNotCrash) {
    // Must not crash; result value is unspecified
    EXPECT_NO_THROW(StringToUID("ZZZZ:FFFFFFFF"));
    EXPECT_NO_THROW(StringToUID(""));
    EXPECT_NO_THROW(StringToUID("not-a-uid"));
}

// ═══════════════════════════════════════════════════════════════════════════
// RDMChecksum
// ═══════════════════════════════════════════════════════════════════════════

TEST(RDMChecksum, EmptyData) {
    EXPECT_EQ(RDMChecksum(nullptr, 0), 0u);
}

TEST(RDMChecksum, SingleByte) {
    const uint8_t data[] = {0xCC};
    EXPECT_EQ(RDMChecksum(data, 1), 0x00CCu);
}

TEST(RDMChecksum, TwoBytes) {
    const uint8_t data[] = {0xCC, 0x01};
    EXPECT_EQ(RDMChecksum(data, 2), 0x00CDu);
}

TEST(RDMChecksum, KnownSum) {
    const uint8_t data[] = {0x01, 0x02, 0x03};
    EXPECT_EQ(RDMChecksum(data, 3), 0x0006u);
}

TEST(RDMChecksum, AllZeroBytes) {
    const std::vector<uint8_t> data(24, 0x00);
    EXPECT_EQ(RDMChecksum(data.data(), 24), 0x0000u);
}

TEST(RDMChecksum, FourFFBytes) {
    // 4 * 0xFF = 1020 = 0x03FC
    const uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(RDMChecksum(data, 4), 0x03FCu);
}

TEST(RDMChecksum, Wraps16Bit) {
    // 258 * 0xFF: sum wraps mod 2^16
    std::vector<uint8_t> data(258, 0xFF);
    uint16_t expected = 0;
    for (int i = 0; i < 258; ++i) expected += 0xFF;
    EXPECT_EQ(RDMChecksum(data.data(), static_cast<int>(data.size())), expected);
}

// ═══════════════════════════════════════════════════════════════════════════
// BuildRDMPacket
// ═══════════════════════════════════════════════════════════════════════════

// Convenience wrapper that also validates common invariants
static std::vector<uint8_t> BuildAndCheck(
    uint64_t dest, uint64_t src,
    uint8_t trans, uint8_t port, uint8_t msgCount,
    uint16_t subDev, uint8_t cmdClass, uint16_t pid,
    const uint8_t* param = nullptr, uint8_t paramLen = 0)
{
    auto pkt = BuildRDMPacket(dest, src, trans, port, msgCount,
                              subDev, cmdClass, pid, param, paramLen);
    EXPECT_EQ(pkt.size(), static_cast<size_t>(26 + paramLen));
    EXPECT_EQ(pkt[0], RDM_START_CODE);   // 0xCC
    EXPECT_EQ(pkt[1], RDM_SUB_START);    // 0x01
    EXPECT_EQ(pkt[2], static_cast<uint8_t>(24 + paramLen)); // message length
    return pkt;
}

TEST(BuildRDMPacket, SizeNoParamData) {
    auto pkt = BuildAndCheck(RDM_BROADCAST_UID, 0x454E00000001ULL,
                             0x01, 0x01, 0x00, 0x0000, RDM_CC_DISCOVERY, PID_DISC_UNIQUE_BRANCH);
    EXPECT_EQ(pkt.size(), 26u);
}

TEST(BuildRDMPacket, SizeWithParamData) {
    uint8_t param[12] = {};
    auto pkt = BuildAndCheck(RDM_BROADCAST_UID, 0x454E00000001ULL,
                             0x00, 0x01, 0x00, 0x0000, RDM_CC_DISCOVERY, PID_DISC_UNIQUE_BRANCH,
                             param, 12);
    EXPECT_EQ(pkt.size(), 38u);
}

TEST(BuildRDMPacket, StartCodes) {
    auto pkt = BuildRDMPacket(0ULL, 0ULL, 0, 0, 0, 0, RDM_CC_GET, PID_DEVICE_INFO);
    EXPECT_EQ(pkt[0], 0xCC);
    EXPECT_EQ(pkt[1], 0x01);
}

TEST(BuildRDMPacket, DestUIDBigEndian) {
    uint64_t dest = 0xAABBCCDDEEFFULL;
    auto pkt = BuildRDMPacket(dest, 0ULL, 0, 0, 0, 0, RDM_CC_GET, PID_DEVICE_INFO);
    EXPECT_EQ(pkt[3], 0xAA);
    EXPECT_EQ(pkt[4], 0xBB);
    EXPECT_EQ(pkt[5], 0xCC);
    EXPECT_EQ(pkt[6], 0xDD);
    EXPECT_EQ(pkt[7], 0xEE);
    EXPECT_EQ(pkt[8], 0xFF);
}

TEST(BuildRDMPacket, SrcUIDBigEndian) {
    uint64_t src = 0x112233445566ULL;
    auto pkt = BuildRDMPacket(0ULL, src, 0, 0, 0, 0, RDM_CC_GET, PID_DEVICE_INFO);
    EXPECT_EQ(pkt[9],  0x11);
    EXPECT_EQ(pkt[10], 0x22);
    EXPECT_EQ(pkt[11], 0x33);
    EXPECT_EQ(pkt[12], 0x44);
    EXPECT_EQ(pkt[13], 0x55);
    EXPECT_EQ(pkt[14], 0x66);
}

TEST(BuildRDMPacket, TransNumPortMsgCount) {
    auto pkt = BuildRDMPacket(0ULL, 0ULL, 0xAB, 0x05, 0x03, 0, RDM_CC_GET, PID_DEVICE_INFO);
    EXPECT_EQ(pkt[15], 0xAB); // transaction number
    EXPECT_EQ(pkt[16], 0x05); // port / response type
    EXPECT_EQ(pkt[17], 0x03); // message count
}

TEST(BuildRDMPacket, SubDeviceBigEndian) {
    auto pkt = BuildRDMPacket(0ULL, 0ULL, 0, 0, 0, 0x1234, RDM_CC_GET, PID_DEVICE_INFO);
    EXPECT_EQ(pkt[18], 0x12);
    EXPECT_EQ(pkt[19], 0x34);
}

TEST(BuildRDMPacket, CommandClassAndPIDBigEndian) {
    auto pkt = BuildRDMPacket(0ULL, 0ULL, 0, 0, 0, 0, RDM_CC_GET, 0x00F0);
    EXPECT_EQ(pkt[20], 0x20); // command class
    EXPECT_EQ(pkt[21], 0x00); // PID high byte
    EXPECT_EQ(pkt[22], 0xF0); // PID low byte
}

TEST(BuildRDMPacket, ParamLenField) {
    uint8_t param[5] = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto pkt = BuildRDMPacket(0ULL, 0ULL, 0, 0, 0, 0, RDM_CC_GET, PID_DEVICE_INFO, param, 5);
    EXPECT_EQ(pkt[23], 5u);
}

TEST(BuildRDMPacket, ParamDataCopied) {
    uint8_t param[3] = {0xDE, 0xAD, 0xBE};
    auto pkt = BuildRDMPacket(0ULL, 0ULL, 0, 0, 0, 0, RDM_CC_GET, PID_DEVICE_INFO, param, 3);
    EXPECT_EQ(pkt[24], 0xDE);
    EXPECT_EQ(pkt[25], 0xAD);
    EXPECT_EQ(pkt[26], 0xBE);
}

TEST(BuildRDMPacket, NoParamData_ParamLenIsZero) {
    auto pkt = BuildRDMPacket(0ULL, 0ULL, 0, 0, 0, 0, RDM_CC_GET, PID_DEVICE_INFO, nullptr, 0);
    EXPECT_EQ(pkt[23], 0u);
}

TEST(BuildRDMPacket, MessageLengthField) {
    // msgLen field (byte 2) = 24 + paramLen, not including the 2-byte checksum
    uint8_t param[7] = {};
    auto pkt = BuildRDMPacket(0ULL, 0ULL, 0, 0, 0, 0, RDM_CC_GET, PID_DEVICE_INFO, param, 7);
    EXPECT_EQ(pkt[2], 31u); // 24 + 7
}

TEST(BuildRDMPacket, ChecksumCorrectNoParam) {
    auto pkt = BuildRDMPacket(0ULL, 0ULL, 0, 0, 0, 0, RDM_CC_GET, PID_DEVICE_INFO);
    const int msgLen = 24;
    uint16_t expected = RDMChecksum(pkt.data(), msgLen);
    uint16_t actual = (static_cast<uint16_t>(pkt[msgLen]) << 8)
                    |  static_cast<uint16_t>(pkt[msgLen + 1]);
    EXPECT_EQ(actual, expected);
}

TEST(BuildRDMPacket, ChecksumCorrectWithParam) {
    uint8_t param[] = {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                       0x00, 0x00, 0xFF, 0xFF};
    auto pkt = BuildRDMPacket(RDM_BROADCAST_UID, 0x454E00000001ULL,
                              0x00, 0x01, 0x00, 0x0000,
                              RDM_CC_DISCOVERY, PID_DISC_UNIQUE_BRANCH, param, 12);
    const int msgLen = 36;
    uint16_t expected = RDMChecksum(pkt.data(), msgLen);
    uint16_t actual = (static_cast<uint16_t>(pkt[msgLen]) << 8)
                    |  static_cast<uint16_t>(pkt[msgLen + 1]);
    EXPECT_EQ(actual, expected);
}

TEST(BuildRDMPacket, BroadcastDestUID) {
    auto pkt = BuildRDMPacket(RDM_BROADCAST_UID, 0ULL, 0, 0, 0, 0, RDM_CC_DISCOVERY, PID_DISC_UNIQUE_BRANCH);
    for (int i = 3; i <= 8; ++i)
        EXPECT_EQ(pkt[i], 0xFF) << "Broadcast byte at index " << i;
}

// ═══════════════════════════════════════════════════════════════════════════
// BytesToHex  — signature: BytesToHex(const uint8_t* data, int len)
// ═══════════════════════════════════════════════════════════════════════════

TEST(BytesToHex, EmptyInput) {
    uint8_t dummy[1] = {};
    EXPECT_EQ(BytesToHex(dummy, 0), "");
}

TEST(BytesToHex, SingleByteZero) {
    const uint8_t data[] = {0x00};
    EXPECT_EQ(BytesToHex(data, 1), "00");
}

TEST(BytesToHex, SingleByteFF) {
    const uint8_t data[] = {0xFF};
    EXPECT_EQ(BytesToHex(data, 1), "FF");
}

TEST(BytesToHex, ThreeBytes) {
    const uint8_t data[] = {0x0A, 0x1B, 0xFF};
    EXPECT_EQ(BytesToHex(data, 3), "0A 1B FF");
}

TEST(BytesToHex, SpaceSeparated_NoTrailingSpace) {
    const uint8_t data[] = {0x01, 0x02};
    std::string result = BytesToHex(data, 2);
    EXPECT_EQ(result, "01 02");
    EXPECT_NE(result.back(), ' ');
}

TEST(BytesToHex, OutputIsUppercase) {
    const uint8_t data[] = {0xAB, 0xCD, 0xEF};
    EXPECT_EQ(BytesToHex(data, 3), "AB CD EF");
}

TEST(BytesToHex, AllZeroBytes) {
    const uint8_t data[] = {0x00, 0x00, 0x00};
    EXPECT_EQ(BytesToHex(data, 3), "00 00 00");
}

TEST(BytesToHex, LeadingZeroPadded) {
    // 0x0A must print as "0A", not "A"
    const uint8_t data[] = {0x0A};
    std::string result = BytesToHex(data, 1);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], '0');
}
