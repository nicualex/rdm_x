#pragma once
// ────────────────────────────────────────────────────────────────────────
// RDM protocol layer — packet construction, checksums, discovery
// ────────────────────────────────────────────────────────────────────────
#ifndef RDM_H
#define RDM_H

#include <cstdint>
#include <vector>
#include <string>

class EnttecPro;   // forward

// ── RDM constants ───────────────────────────────────────────────────────
constexpr uint8_t  RDM_START_CODE       = 0xCC;
constexpr uint8_t  RDM_SUB_START        = 0x01;

// Command classes
constexpr uint8_t  RDM_CC_DISCOVERY     = 0x10;
constexpr uint8_t  RDM_CC_DISCOVERY_RSP = 0x11;
constexpr uint8_t  RDM_CC_GET           = 0x20;
constexpr uint8_t  RDM_CC_GET_RSP       = 0x21;
constexpr uint8_t  RDM_CC_SET           = 0x30;
constexpr uint8_t  RDM_CC_SET_RSP       = 0x31;

// Standard PIDs
constexpr uint16_t PID_DISC_UNIQUE_BRANCH  = 0x0001;
constexpr uint16_t PID_DISC_MUTE           = 0x0002;
constexpr uint16_t PID_DISC_UN_MUTE        = 0x0003;
constexpr uint16_t PID_SUPPORTED_PARAMS    = 0x0050;
constexpr uint16_t PID_DEVICE_INFO         = 0x0060;
constexpr uint16_t PID_IDENTIFY_DEVICE     = 0x1000;

// Broadcast UID
constexpr uint64_t RDM_BROADCAST_UID = 0xFFFFFFFFFFFFULL;

// ── Response types ──────────────────────────────────────────────────────
enum class RDMResponseType {
    ACK,
    ACK_TIMER,
    NACK,
    TIMEOUT,
    COLLISION,       // discovery-specific: multiple responders
    INVALID,
};

struct RDMResponse {
    RDMResponseType type  = RDMResponseType::TIMEOUT;
    uint16_t        nackReason = 0;
    std::vector<uint8_t> data;
};

// ── Helper to format a 48-bit UID as a string ───────────────────────────
std::string UIDToString(uint64_t uid);
uint64_t    StringToUID(const std::string& s);

// ── Checksum ────────────────────────────────────────────────────────────
uint16_t RDMChecksum(const uint8_t* data, int len);

// ── Packet builder ──────────────────────────────────────────────────────
//    Builds a complete RDM packet (start code through checksum).
//    `srcUID` is this controller's UID (typically 0x454E540001).
std::vector<uint8_t> BuildRDMPacket(
    uint64_t destUID, uint64_t srcUID,
    uint8_t  transNum, uint8_t portOrResponseType,
    uint8_t  msgCount, uint16_t subDevice,
    uint8_t  commandClass, uint16_t pid,
    const uint8_t* paramData = nullptr, uint8_t paramLen = 0);

// ── Discovery ───────────────────────────────────────────────────────────
//    Performs full binary-tree RDM discovery. Returns list of found UIDs.
std::vector<uint64_t> RDMDiscovery(EnttecPro& pro, uint64_t srcUID);

// ── GET command ─────────────────────────────────────────────────────────
RDMResponse RDMGetCommand(EnttecPro& pro, uint64_t srcUID,
                          uint64_t destUID, uint16_t pid,
                          const uint8_t* paramData = nullptr, uint8_t paramLen = 0);

#endif // RDM_H
