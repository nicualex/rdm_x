#pragma once
// ────────────────────────────────────────────────────────────────────────
// rdm_x_core.dll — Public C API
// ────────────────────────────────────────────────────────────────────────
#ifndef RDM_X_API_H
#define RDM_X_API_H

#include <cstdint>

#ifdef RDX_EXPORTS
#define RDX_API __declspec(dllexport)
#else
#define RDX_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ── Device management ───────────────────────────────────────────────────
RDX_API int RDX_ListDevices();
RDX_API bool RDX_Open(int deviceIndex);
RDX_API void RDX_Close();
RDX_API bool RDX_IsOpen();
RDX_API const char *RDX_FirmwareString();
RDX_API uint32_t RDX_SerialNumber();

// ── DMX output ──────────────────────────────────────────────────────────
RDX_API bool RDX_SendDMX(const uint8_t *data, int len);

// ── RDM Discovery ───────────────────────────────────────────────────────
RDX_API int RDX_Discover(); // returns UID count
RDX_API bool RDX_GetDiscoveredUID(int index, uint64_t *uid);

// ── RDM Command Response ────────────────────────────────────────────────
#define RDX_STATUS_ACK 0
#define RDX_STATUS_ACK_TIMER 1
#define RDX_STATUS_NACK 2
#define RDX_STATUS_TIMEOUT 3
#define RDX_STATUS_CHECKSUM_ERR 4
#define RDX_STATUS_INVALID 5

#pragma pack(push, 1)
typedef struct {
  int status;         // RDX_STATUS_*
  int nackReason;     // valid when status == NACK
  int dataLen;        // bytes in data[]
  uint8_t data[231];  // max RDM PDL
  int64_t latencyUs;  // TX-to-RX microseconds
  bool checksumValid; // was the RDM checksum correct?
} RDX_Response;
#pragma pack(pop)

// Send a single RDM GET command and capture the response + timing.
RDX_API bool RDX_SendGET(uint64_t destUID, uint16_t pid,
                         const uint8_t *paramData, int paramLen,
                         RDX_Response *response);

// Send a single RDM SET command and capture the response + timing.
RDX_API bool RDX_SendSET(uint64_t destUID, uint16_t pid,
                         const uint8_t *paramData, int paramLen,
                         RDX_Response *response);

// ── Parameter database ──────────────────────────────────────────────────
RDX_API int RDX_LoadParameters(const char *csvPath); // returns count
RDX_API bool RDX_GetParameterInfo(int index, uint16_t *pid, char *name,
                                  int nameMaxLen, char *cmdClass,
                                  int cmdClassMaxLen, bool *isMandatory);

// ── Logging ─────────────────────────────────────────────────────────────
// Callback: isTX, hex string, timestamp in microseconds since DLL load.
typedef void(__stdcall *RDX_LogCallback)(bool isTX, const char *hex,
                                         int64_t timestampUs);
RDX_API void RDX_SetLogCallback(RDX_LogCallback cb);

#ifdef __cplusplus
}
#endif

#endif // RDM_X_API_H
