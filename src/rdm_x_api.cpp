// ────────────────────────────────────────────────────────────────────────
// rdm_x_core.dll — C API Implementation
// ────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#include "rdm_x_api.h"
#include "enttec_pro.h"
#include "parameter_loader.h"
#include "peperoni_rodin.h"
#include "rdm.h"
#include "validator.h"
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ── Globals ─────────────────────────────────────────────────────────────
static EnttecPro g_enttec;
static PeperoniRodin g_peperoni;
static int g_driverType = RDX_DRIVER_ENTTEC;
static std::vector<RDMParameter> g_params;
static std::vector<uint64_t> g_discoveredUIDs;
static std::string g_fwString;
static LARGE_INTEGER g_perfFreq;
static LARGE_INTEGER g_dllLoadTime;

// Source UID for RDM commands
static uint64_t GetControllerUID() {
  if (g_driverType == RDX_DRIVER_PEPERONI) {
    uint32_t sn = g_peperoni.GetSerialNumber();
    return (0x7065ULL << 32) | sn;
  }
  uint32_t sn = g_enttec.GetSerialNumber();
  return (0x454EULL << 32) | sn;
}

// ── Timing helpers ──────────────────────────────────────────────────
#include <cstdarg>
static int64_t NowUs() {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return (now.QuadPart - g_dllLoadTime.QuadPart) * 1000000LL /
         g_perfFreq.QuadPart;
}

// Forward declaration of log callback (defined at bottom of file)
static RDX_LogCallback g_logCb;

// Debug helper — routes through OutputDebugString + log callback
static void DiscLog(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  OutputDebugStringA(buf);
  if (g_logCb)
    g_logCb(false, buf, NowUs());
}

// ── DLL Entry Point ─────────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    QueryPerformanceFrequency(&g_perfFreq);
    QueryPerformanceCounter(&g_dllLoadTime);
  }
  return TRUE;
}

// ═══════════════════════════════════════════════════════════════════════
// Driver selection
// ═══════════════════════════════════════════════════════════════════════

RDX_API void RDX_SetDriver(int driverType) { g_driverType = driverType; }

RDX_API int RDX_GetDriver() { return g_driverType; }

RDX_API const char *RDX_GetDriverName(int driverType) {
  switch (driverType) {
  case RDX_DRIVER_ENTTEC:
    return "Enttec USB DMX PRO";
  case RDX_DRIVER_PEPERONI:
    return "Peperoni Rodin 1";
  default:
    return "Unknown";
  }
}

// ═══════════════════════════════════════════════════════════════════════
// Device management
// ═══════════════════════════════════════════════════════════════════════

RDX_API int RDX_ListDevices() {
  if (g_driverType == RDX_DRIVER_PEPERONI)
    return g_peperoni.ListDevices();
  return EnttecPro::ListDevices();
}

RDX_API bool RDX_Open(int deviceIndex) {
  if (g_driverType == RDX_DRIVER_PEPERONI)
    return g_peperoni.Open(deviceIndex);
  return g_enttec.Open(deviceIndex);
}

RDX_API void RDX_Close() {
  if (g_driverType == RDX_DRIVER_PEPERONI)
    g_peperoni.Close();
  else
    g_enttec.Close();
}

RDX_API bool RDX_IsOpen() {
  if (g_driverType == RDX_DRIVER_PEPERONI)
    return g_peperoni.IsOpen();
  return g_enttec.IsOpen();
}

RDX_API const char *RDX_FirmwareString() {
  if (g_driverType == RDX_DRIVER_PEPERONI)
    g_fwString = g_peperoni.GetFirmwareString();
  else
    g_fwString = g_enttec.GetFirmwareString();
  return g_fwString.c_str();
}

RDX_API uint32_t RDX_SerialNumber() {
  if (g_driverType == RDX_DRIVER_PEPERONI)
    return g_peperoni.GetSerialNumber();
  return g_enttec.GetSerialNumber();
}

// ═══════════════════════════════════════════════════════════════════════
// DMX
// ═══════════════════════════════════════════════════════════════════════

RDX_API bool RDX_SendDMX(const uint8_t *data, int len) {
  if (g_driverType == RDX_DRIVER_PEPERONI)
    return g_peperoni.SendDMX(data, len);
  return g_enttec.SendDMX(data, len);
}

// ═══════════════════════════════════════════════════════════════════════
// Discovery
// ═══════════════════════════════════════════════════════════════════════

RDX_API int RDX_Discover() {
  if (g_driverType == RDX_DRIVER_PEPERONI)
    g_discoveredUIDs = RDMDiscovery(g_peperoni, GetControllerUID());
  else
    g_discoveredUIDs = RDMDiscovery(g_enttec, GetControllerUID());
  return static_cast<int>(g_discoveredUIDs.size());
}

RDX_API bool RDX_GetDiscoveredUID(int index, uint64_t *uid) {
  if (index < 0 || index >= static_cast<int>(g_discoveredUIDs.size()))
    return false;
  if (uid)
    *uid = g_discoveredUIDs[index];
  return true;
}

// ═══════════════════════════════════════════════════════════════════════
// RDM Commands with timing
// ═══════════════════════════════════════════════════════════════════════

static uint8_t s_transNum = 0;

static bool SendRDMCommand(uint64_t destUID, uint16_t pid, uint8_t commandClass,
                           const uint8_t *paramData, int paramLen,
                           RDX_Response *out) {
  if (!out)
    return false;
  memset(out, 0, sizeof(RDX_Response));

  bool devOpen = (g_driverType == RDX_DRIVER_PEPERONI) ? g_peperoni.IsOpen()
                                                       : g_enttec.IsOpen();
  if (!devOpen) {
    out->status = RDX_STATUS_TIMEOUT;
    DiscLog("[RDM CMD] ERROR: device not open\n");
    return false;
  }

  // Build the RDM packet
  auto pkt = BuildRDMPacket(destUID, GetControllerUID(), s_transNum++, 1, 0, 0,
                            commandClass, pid, paramData,
                            static_cast<uint8_t>(paramLen));

  DiscLog("[RDM CMD] Sending %s PID 0x%04X to %04X:%08X (%d bytes)\n",
          commandClass == 0x20 ? "GET" : "SET", pid,
          (unsigned)((destUID >> 32) & 0xFFFF),
          (unsigned)(destUID & 0xFFFFFFFF), (int)pkt.size());

  // ── Quiet period: purge RX buffer (via mutex-guarded Purge) ──
  if (g_driverType == RDX_DRIVER_PEPERONI)
    g_peperoni.Purge();
  else
    g_enttec.Purge();
  Sleep(20);

  // Measure TX→RX latency with high-precision timer
  LARGE_INTEGER txTime, rxTime;
  QueryPerformanceCounter(&txTime);

  // Send via the active driver
  bool sendOk;
  if (g_driverType == RDX_DRIVER_PEPERONI)
    sendOk = g_peperoni.SendRDM(pkt.data(), static_cast<int>(pkt.size()));
  else
    sendOk = g_enttec.SendRDM(pkt.data(), static_cast<int>(pkt.size()));

  if (!sendOk) {
    out->status = RDX_STATUS_TIMEOUT;
    DiscLog("[RDM CMD] SendRDM FAILED\n");
    return false;
  }

  DiscLog("[RDM CMD] Sent, waiting for Label 5 response...\n");

  // Wait for widget to process (shorter for Peperoni since it blocks)
  if (g_driverType != RDX_DRIVER_PEPERONI)
    Sleep(50);

  // Read response
  uint8_t rxBuf[512];
  uint8_t statusByte = 0;
  int rxLen;
  if (g_driverType == RDX_DRIVER_PEPERONI)
    rxLen = g_peperoni.ReceiveRDM(rxBuf, sizeof(rxBuf), statusByte);
  else
    rxLen = g_enttec.ReceiveRDM(rxBuf, sizeof(rxBuf), statusByte);

  QueryPerformanceCounter(&rxTime);

  // Calculate latency in microseconds
  out->latencyUs =
      (rxTime.QuadPart - txTime.QuadPart) * 1000000LL / g_perfFreq.QuadPart;

  DiscLog("[RDM CMD] ReceiveRDM returned %d bytes, statusByte=0x%02X, "
          "latency=%lldus\n",
          rxLen, statusByte, out->latencyUs);

  if (rxLen <= 0) {
    out->status = RDX_STATUS_TIMEOUT;
    DiscLog("[RDM CMD] TIMEOUT - no response\n");
    return true; // function succeeded, but fixture didn't respond
  }

  // Log first few bytes of response
  {
    char hexDump[128] = {};
    int dumpLen = (rxLen < 30) ? rxLen : 30;
    for (int i = 0; i < dumpLen; ++i)
      snprintf(hexDump + i * 3, 4, "%02X ", rxBuf[i]);
    DiscLog("[RDM CMD] RX data: %s\n", hexDump);
  }

  // Validate RDM checksum (last 2 bytes of response)
  if (rxLen >= 26) { // minimum: 24-byte header + 2-byte checksum
    int msgLen = rxLen - 2;
    uint16_t expected = (rxBuf[msgLen] << 8) | rxBuf[msgLen + 1];
    uint16_t computed = RDMChecksum(rxBuf, msgLen);
    out->checksumValid = (expected == computed);
    if (!out->checksumValid) {
      out->status = RDX_STATUS_CHECKSUM_ERR;
      // Still copy the data for inspection
      int copyLen = (rxLen > 231) ? 231 : rxLen;
      memcpy(out->data, rxBuf, copyLen);
      out->dataLen = copyLen;
      return true;
    }
  } else {
    out->checksumValid = false;
  }

  // Parse the RDM response
  if (rxLen < 24) {
    out->status = RDX_STATUS_INVALID;
    return true;
  }

  // Check start code
  if (rxBuf[0] != 0xCC) { // RDM_START_CODE
    DiscLog("[RDM CMD] INVALID: start code is 0x%02X (expected 0xCC)\n",
            rxBuf[0]);
    out->status = RDX_STATUS_INVALID;
    return true;
  }

  uint8_t respType = rxBuf[16];
  uint8_t pdl = rxBuf[23];
  DiscLog("[RDM CMD] respType=0x%02X pdl=%d\n", respType, pdl);

  switch (respType) {
  case 0x00: // ACK
    out->status = RDX_STATUS_ACK;
    DiscLog("[RDM CMD] ACK with %d bytes param data\n", pdl);
    if (pdl > 0 && 24 + pdl <= rxLen) {
      int copyLen = (pdl > 231) ? 231 : pdl;
      memcpy(out->data, rxBuf + 24, copyLen);
      out->dataLen = copyLen;
    }
    break;
  case 0x01: // ACK_TIMER
    out->status = RDX_STATUS_ACK_TIMER;
    DiscLog("[RDM CMD] ACK_TIMER\n");
    break;
  case 0x02: // NACK
    out->status = RDX_STATUS_NACK;
    if (pdl >= 2)
      out->nackReason = (rxBuf[24] << 8) | rxBuf[25];
    DiscLog("[RDM CMD] NACK reason=0x%04X\n", out->nackReason);
    break;
  default:
    out->status = RDX_STATUS_INVALID;
    DiscLog("[RDM CMD] Unknown response type 0x%02X\n", respType);
    break;
  }

  return true;
}

RDX_API bool RDX_SendGET(uint64_t destUID, uint16_t pid,
                         const uint8_t *paramData, int paramLen,
                         RDX_Response *response) {
  return SendRDMCommand(destUID, pid, 0x20, paramData, paramLen, response);
}

RDX_API bool RDX_SendSET(uint64_t destUID, uint16_t pid,
                         const uint8_t *paramData, int paramLen,
                         RDX_Response *response) {
  return SendRDMCommand(destUID, pid, 0x30, paramData, paramLen, response);
}

// ═══════════════════════════════════════════════════════════════════════
// Parameter database
// ═══════════════════════════════════════════════════════════════════════

RDX_API int RDX_LoadParameters(const char *csvPath) {
  g_params = LoadParameters(csvPath ? csvPath : "");
  return static_cast<int>(g_params.size());
}

RDX_API bool RDX_GetParameterInfo(int index, uint16_t *pid, char *name,
                                  int nameMaxLen, char *cmdClass,
                                  int cmdClassMaxLen, bool *isMandatory) {
  if (index < 0 || index >= static_cast<int>(g_params.size()))
    return false;

  const auto &p = g_params[index];
  if (pid)
    *pid = p.pid;
  if (isMandatory)
    *isMandatory = p.isMandatory;
  if (name && nameMaxLen > 0) {
    strncpy(name, p.name.c_str(), nameMaxLen - 1);
    name[nameMaxLen - 1] = '\0';
  }
  if (cmdClass && cmdClassMaxLen > 0) {
    strncpy(cmdClass, p.commandClass.c_str(), cmdClassMaxLen - 1);
    cmdClass[cmdClassMaxLen - 1] = '\0';
  }
  return true;
}

// ═══════════════════════════════════════════════════════════════════════
// Logging
// ═══════════════════════════════════════════════════════════════════════

RDX_API void RDX_SetLogCallback(RDX_LogCallback cb) {
  g_logCb = cb;

  // Set log callback on Enttec
  g_enttec.SetLogCallback([](bool tx, const uint8_t *data, int len) {
    if (!g_logCb)
      return;
    if (tx && len >= 2 && data[1] == 0x06)
      return;
    std::string hex;
    hex.reserve(len * 3 + 8);
    for (int i = 0; i < len && i < 128; ++i) {
      char buf[4];
      snprintf(buf, sizeof(buf), "%02X ", data[i]);
      hex += buf;
    }
    if (len > 128)
      hex += "...";
    g_logCb(tx, hex.c_str(), NowUs());
  });

  // Set log callback on Peperoni
  g_peperoni.SetLogCallback([](bool tx, const uint8_t *data, int len) {
    if (!g_logCb)
      return;
    std::string hex;
    hex.reserve(len * 3 + 8);
    for (int i = 0; i < len && i < 128; ++i) {
      char buf[4];
      snprintf(buf, sizeof(buf), "%02X ", data[i]);
      hex += buf;
    }
    if (len > 128)
      hex += "...";
    g_logCb(tx, hex.c_str(), NowUs());
  });
}
