#pragma once
// EnttecPro - FTDI D2XX serial wrapper for the Enttec DMX USB PRO
#ifndef ENTTEC_PRO_H
#define ENTTEC_PRO_H

#include "FTD2XX.H"
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

// Enttec PRO protocol constants (from pro_driver.h)
constexpr uint8_t PRO_START_CODE = 0x7E;
constexpr uint8_t PRO_END_CODE = 0xE7;
constexpr int PRO_HEADER_LENGTH = 4;
constexpr int PRO_MAX_PACKET = 600;

// Widget message labels
constexpr uint8_t LABEL_GET_WIDGET_PARAMS = 3;
constexpr uint8_t LABEL_SET_WIDGET_PARAMS = 4;
constexpr uint8_t LABEL_RX_DMX_ON_CHANGE = 8;
constexpr uint8_t LABEL_RX_DMX_PACKET = 5; // also used for RDM RX
constexpr uint8_t LABEL_TX_DMX = 6;
constexpr uint8_t LABEL_TX_RDM = 7;
constexpr uint8_t LABEL_GET_WIDGET_SN = 10;
constexpr uint8_t LABEL_TX_RDM_DISCOVERY = 11; // discovery request (no break)

// Widget params structure
#pragma pack(push, 1)
struct WidgetParams {
  uint8_t firmwareLSB;
  uint8_t firmwareMSB;
  uint8_t breakTime;
  uint8_t mabTime;
  uint8_t refreshRate;
};
#pragma pack(pop)

// Log callback type (direction: true = TX, false = RX)
using LogCallback =
    std::function<void(bool direction, const uint8_t *data, int len)>;

// EnttecPro class
class EnttecPro {
public:
  EnttecPro();
  ~EnttecPro();

  // Enumerate available FTDI devices
  static int ListDevices();

  // Open / close
  bool Open(int deviceIndex);
  void Close();
  bool IsOpen() const { return m_handle != nullptr; }
  FT_HANDLE GetHandle() const { return m_handle; }

  // Widget info (valid after Open)
  const WidgetParams &GetParams() const { return m_params; }
  std::string GetFirmwareString() const;
  uint32_t GetSerialNumber() const { return m_serialNumber; }

  // DMX output
  // data[0] must be the start code (usually 0x00).
  // len includes the start code byte, so max is 513 (1 + 512).
  bool SendDMX(const uint8_t *data, int len);

  // RDM
  // Sends an already-formed RDM packet (including RDM start code 0xCC)
  // via Label 7 (with break). Returns true if the widget accepted the write.
  bool SendRDM(const uint8_t *data, int len);

  // Sends an RDM Discovery request via Label 11 (NO break).
  // Required for DISC_UNIQUE_BRANCH per E1.20.
  bool SendRDMDiscovery(const uint8_t *data, int len);

  // Receives the next RDM response from the widget (Label 5).
  // Returns the number of payload bytes placed in `out`, or -1 on
  // timeout / error.  `statusByte` receives the widget status.
  int ReceiveRDM(uint8_t *out, int maxLen, uint8_t &statusByte);

  // Low-level (exposed for advanced use)
  bool SendPacket(uint8_t label, const uint8_t *data, int length);
  int ReceivePacket(uint8_t label, uint8_t *data, int maxLen);

  // Purge buffers
  void Purge();

  // Logging
  void SetLogCallback(LogCallback cb);

private:
  void CloseInternal(); // no-mutex version, caller must hold m_mutex
  void PurgeInternal(); // no-mutex version, caller must hold m_mutex

  FT_HANDLE m_handle = nullptr;
  WidgetParams m_params = {};
  uint32_t m_serialNumber = 0;
  LogCallback m_logCb;
  std::mutex m_mutex;

  void Log(bool tx, const uint8_t *data, int len);
};

#endif // ENTTEC_PRO_H
