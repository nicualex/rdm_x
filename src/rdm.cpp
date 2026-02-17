// RDM protocol layer - Implementation
#include "rdm.h"
#include "enttec_pro.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <windows.h>

// UID helpers
std::string UIDToString(uint64_t uid) {
  char buf[16];
  uint16_t mfg = static_cast<uint16_t>((uid >> 32) & 0xFFFF);
  uint32_t dev = static_cast<uint32_t>(uid & 0xFFFFFFFF);
  snprintf(buf, sizeof(buf), "%04X:%08X", mfg, dev);
  return buf;
}

uint64_t StringToUID(const std::string &s) {
  uint32_t hi = 0, lo = 0;
  sscanf(s.c_str(), "%X:%X", &hi, &lo);
  return (static_cast<uint64_t>(hi) << 32) | lo;
}

static void PackUID(uint8_t *dst, uint64_t uid) {
  dst[0] = static_cast<uint8_t>((uid >> 40) & 0xFF);
  dst[1] = static_cast<uint8_t>((uid >> 32) & 0xFF);
  dst[2] = static_cast<uint8_t>((uid >> 24) & 0xFF);
  dst[3] = static_cast<uint8_t>((uid >> 16) & 0xFF);
  dst[4] = static_cast<uint8_t>((uid >> 8) & 0xFF);
  dst[5] = static_cast<uint8_t>((uid) & 0xFF);
}

static uint64_t UnpackUID(const uint8_t *src) {
  return (static_cast<uint64_t>(src[0]) << 40) |
         (static_cast<uint64_t>(src[1]) << 32) |
         (static_cast<uint64_t>(src[2]) << 24) |
         (static_cast<uint64_t>(src[3]) << 16) |
         (static_cast<uint64_t>(src[4]) << 8) | (static_cast<uint64_t>(src[5]));
}

// Checksum
uint16_t RDMChecksum(const uint8_t *data, int len) {
  uint16_t sum = 0;
  for (int i = 0; i < len; ++i)
    sum += data[i];
  return sum;
}

// Build RDM Packet
std::vector<uint8_t> BuildRDMPacket(uint64_t destUID, uint64_t srcUID,
                                    uint8_t transNum, uint8_t portOrRespType,
                                    uint8_t msgCount, uint16_t subDevice,
                                    uint8_t commandClass, uint16_t pid,
                                    const uint8_t *paramData,
                                    uint8_t paramLen) {
  // RDM message length = from start code through paramLen (24 bytes header +
  // paramLen)
  int msgLen = 24 + paramLen;           // total without checksum
  std::vector<uint8_t> pkt(msgLen + 2); // +2 for 16-bit checksum

  pkt[0] = RDM_START_CODE;               // 0xCC
  pkt[1] = RDM_SUB_START;                // 0x01
  pkt[2] = static_cast<uint8_t>(msgLen); // message length (excl checksum)
  PackUID(&pkt[3], destUID);
  PackUID(&pkt[9], srcUID);
  pkt[15] = transNum;
  pkt[16] = portOrRespType; // port ID or response type
  pkt[17] = msgCount;
  pkt[18] = static_cast<uint8_t>((subDevice >> 8) & 0xFF);
  pkt[19] = static_cast<uint8_t>(subDevice & 0xFF);
  pkt[20] = commandClass;
  pkt[21] = static_cast<uint8_t>((pid >> 8) & 0xFF);
  pkt[22] = static_cast<uint8_t>(pid & 0xFF);
  pkt[23] = paramLen;

  if (paramLen > 0 && paramData)
    memcpy(&pkt[24], paramData, paramLen);

  // Append checksum (sum of all bytes from start code through param data)
  uint16_t cksum = RDMChecksum(pkt.data(), msgLen);
  pkt[msgLen] = static_cast<uint8_t>((cksum >> 8) & 0xFF);
  pkt[msgLen + 1] = static_cast<uint8_t>(cksum & 0xFF);

  return pkt;
}

// Send and receive a single RDM transaction
static uint8_t s_transNum = 0;

RDMResponse RDMGetCommand(EnttecPro &pro, uint64_t srcUID, uint64_t destUID,
                          uint16_t pid, const uint8_t *paramData,
                          uint8_t paramLen) {
  RDMResponse resp;
  auto pkt = BuildRDMPacket(destUID, srcUID, s_transNum++, 1, // port 1
                            0, 0, // msg count, sub-device
                            RDM_CC_GET, pid, paramData, paramLen);

  if (!pro.SendRDM(pkt.data(), static_cast<int>(pkt.size()))) {
    resp.type = RDMResponseType::TIMEOUT;
    return resp;
  }

  // Allow the fixture time to respond
  Sleep(30);

  uint8_t rxBuf[512];
  uint8_t statusByte = 0;
  int rxLen = pro.ReceiveRDM(rxBuf, sizeof(rxBuf), statusByte);
  if (rxLen <= 0) {
    resp.type = RDMResponseType::TIMEOUT;
    return resp;
  }

  // Parse the RDM response
  if (rxLen < 24) {
    resp.type = RDMResponseType::INVALID;
    return resp;
  }

  // Check start code
  if (rxBuf[0] != RDM_START_CODE) {
    resp.type = RDMResponseType::INVALID;
    return resp;
  }

  uint8_t respType = rxBuf[16];
  uint8_t pdl = rxBuf[23];

  switch (respType) {
  case 0x00: // ACK
    resp.type = RDMResponseType::ACK;
    if (pdl > 0 && 24 + pdl <= rxLen)
      resp.data.assign(rxBuf + 24, rxBuf + 24 + pdl);
    break;
  case 0x01: // ACK_TIMER
    resp.type = RDMResponseType::ACK_TIMER;
    break;
  case 0x02: // NACK
    resp.type = RDMResponseType::NACK;
    if (pdl >= 2)
      resp.nackReason = (rxBuf[24] << 8) | rxBuf[25];
    break;
  default:
    resp.type = RDMResponseType::INVALID;
    break;
  }

  return resp;
}

// ============================================================================
// Discovery helpers
// ============================================================================

// Debug helper — forwards to OutputDebugStringA so DebugView can catch it.
static void DiscLog(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  OutputDebugStringA(buf);
}

// Send DISC_MUTE to a specific UID.  Returns true if we got a response (ACK).
static bool SendDiscMute(EnttecPro &pro, uint64_t srcUID, uint64_t uid) {
  DiscLog("[RDM] DISC_MUTE -> %s\n", UIDToString(uid).c_str());
  auto pkt = BuildRDMPacket(uid, srcUID, s_transNum++, 1, 0, 0,
                            RDM_CC_DISCOVERY, PID_DISC_MUTE);
  if (!pro.SendRDM(pkt.data(), static_cast<int>(pkt.size()))) {
    DiscLog("[RDM]   MUTE send failed\n");
    return false;
  }
  Sleep(50);
  uint8_t buf[256];
  uint8_t st;
  int len = pro.ReceiveRDM(buf, sizeof(buf), st);
  DiscLog("[RDM]   MUTE rx len=%d  status=0x%02X\n", len, st);
  return (len > 0);
}

// Send DISC_UN_MUTE broadcast.  No response expected.
static void SendDiscUnMute(EnttecPro &pro, uint64_t srcUID) {
  DiscLog("[RDM] DISC_UN_MUTE (broadcast)\n");
  auto pkt = BuildRDMPacket(RDM_BROADCAST_UID, srcUID, s_transNum++, 1, 0, 0,
                            RDM_CC_DISCOVERY, PID_DISC_UN_MUTE);
  pro.SendRDM(pkt.data(), static_cast<int>(pkt.size()));
  Sleep(100);
  // Broadcast: no response expected; purge any stale data
  pro.Purge();
}

// Attempt DISC_UNIQUE_BRANCH.
// Returns:
//  -1 = no response (no devices in range)
//   0 = collision (multiple devices or garbled response)
//   1 = single device, UID written to *foundUID
static int TryDiscBranch(EnttecPro &pro, uint64_t srcUID, uint64_t lower,
                         uint64_t upper, uint64_t *foundUID) {
  uint8_t pd[12];
  PackUID(pd, lower);
  PackUID(pd + 6, upper);

  auto pkt = BuildRDMPacket(RDM_BROADCAST_UID, srcUID, s_transNum++, 1, 0, 0,
                            RDM_CC_DISCOVERY, PID_DISC_UNIQUE_BRANCH, pd, 12);

  DiscLog("[RDM] BRANCH [%s - %s]  pktSz=%d\n", UIDToString(lower).c_str(),
          UIDToString(upper).c_str(), (int)pkt.size());

  if (!pro.SendRDMDiscovery(pkt.data(), static_cast<int>(pkt.size()))) {
    DiscLog("[RDM]   BRANCH send failed!\n");
    return -1;
  }

  // Discovery responses need more time than normal RDM GETs.
  Sleep(50);

  uint8_t rxBuf[512];
  uint8_t statusByte = 0;
  int rxLen = pro.ReceiveRDM(rxBuf, sizeof(rxBuf), statusByte);

  DiscLog("[RDM]   BRANCH rx: len=%d  statusByte=0x%02X\n", rxLen, statusByte);

  if (rxLen <= 0) {
    DiscLog("[RDM]   -> no response\n");
    return -1; // no response
  }

  // Dump first 32 bytes for debugging
  {
    char hexDump[200];
    int pos = 0;
    for (int i = 0; i < rxLen && i < 32; ++i) {
      pos += snprintf(hexDump + pos, sizeof(hexDump) - pos, "%02X ", rxBuf[i]);
    }
    DiscLog("[RDM]   BRANCH rxdata: %s\n", hexDump);
  }

  // The Enttec Pro widget returns the raw discovery response bytes
  // from the bus (everything after the DMX start code).
  //
  // Per E1.20 Section 7.5.3, the discovery response is encoded:
  //   - 0 to 7 preamble bytes (0xFE)
  //   - 1 preamble separator byte (0xAA)
  //   - 12 encoded UID bytes (6 pairs: each UID byte sent as
  //     byte|0xAA then byte|0x55)
  //   - 4 encoded checksum bytes (same encoding, 2-byte checksum)
  //
  // The widget's status byte might indicate the response type.
  // statusByte == 0 typically means a valid response was received.

  // Strip preamble (0xFE bytes)
  int offset = 0;
  while (offset < rxLen && rxBuf[offset] == 0xFE)
    offset++;

  // Next should be separator 0xAA
  if (offset < rxLen && rxBuf[offset] == 0xAA)
    offset++;

  int remaining = rxLen - offset;

  DiscLog("[RDM]   after preamble strip: offset=%d  remaining=%d\n", offset,
          remaining);

  // We need at least 12 bytes for the encoded UID (6 pairs)
  // Ideally 16 bytes (12 UID + 4 checksum) but some devices
  // may not send the full checksum.
  if (remaining < 12) {
    // Insufficient data: could be a collision (garbled) or short response
    if (remaining > 0) {
      DiscLog("[RDM]   -> COLLISION (short data: %d bytes)\n", remaining);
      return 0; // some data = collision
    }
    DiscLog("[RDM]   -> no data after preamble\n");
    return -1; // no data after preamble
  }

  // Decode 6 UID bytes from the encoded pairs
  // Per E1.20: byte1 = original | 0xAA, byte2 = original | 0x55
  // Reconstruct: original = (byte1 & 0x55) | (byte2 & 0xAA)
  uint8_t decoded[6];
  for (int i = 0; i < 6; ++i) {
    uint8_t b1 = rxBuf[offset + i * 2];
    uint8_t b2 = rxBuf[offset + i * 2 + 1];
    decoded[i] = (b1 & 0x55) | (b2 & 0xAA);
  }

  uint64_t uid = UnpackUID(decoded);
  DiscLog("[RDM]   -> FOUND UID: %s\n", UIDToString(uid).c_str());

  if (foundUID)
    *foundUID = uid;
  return 1;
}

// Recursive binary tree discovery
static void DiscoverBranch(EnttecPro &pro, uint64_t srcUID, uint64_t lower,
                           uint64_t upper, std::vector<uint64_t> &found,
                           int depth = 0) {
  // Limit recursion depth to prevent stack overflow
  if (depth >= 48)
    return;

  uint64_t uid = 0;
  int result = TryDiscBranch(pro, srcUID, lower, upper, &uid);

  if (result == -1)
    return; // no devices in this range

  if (result == 1) {
    // Got a single UID - mute it and continue searching the same range
    found.push_back(uid);
    SendDiscMute(pro, srcUID, uid);

    // Check if there are more devices in this range
    DiscoverBranch(pro, srcUID, lower, upper, found, depth + 1);
  } else if (result == 0) {
    // Collision - binary split the search range
    if (lower >= upper)
      return; // can't split further

    uint64_t mid = lower + (upper - lower) / 2;
    DiscoverBranch(pro, srcUID, lower, mid, found, depth + 1);
    DiscoverBranch(pro, srcUID, mid + 1, upper, found, depth + 1);
  }
}

// Public discovery entry point
std::vector<uint64_t> RDMDiscovery(EnttecPro &pro, uint64_t srcUID) {
  std::vector<uint64_t> found;
  DiscLog("[RDM] ===== Starting RDM Discovery (src=%s) =====\n",
          UIDToString(srcUID).c_str());

  // Un-mute all devices (send twice for reliability)
  SendDiscUnMute(pro, srcUID);
  Sleep(100);
  SendDiscUnMute(pro, srcUID);
  Sleep(100);

  // Search the entire UID space (0x000000000000 to 0xFFFEFFFFFFFF)
  DiscoverBranch(pro, srcUID, 0x000000000000ULL, 0xFFFEFFFFFFFFULL, found);

  DiscLog("[RDM] ===== Discovery complete: found %d device(s) =====\n",
          (int)found.size());
  return found;
}
