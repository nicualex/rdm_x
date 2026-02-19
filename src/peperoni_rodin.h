#pragma once
// ────────────────────────────────────────────────────────────────────────
// PeperoniRodin — runtime-loaded driver for peperoni Rodin 1 USB-DMX
// ────────────────────────────────────────────────────────────────────────
#ifndef PEPERONI_RODIN_H
#define PEPERONI_RODIN_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

// Log callback type (same as EnttecPro for consistency)
using PepLogCallback =
    std::function<void(bool direction, const uint8_t *data, int len)>;

class PeperoniRodin {
public:
  PeperoniRodin();
  ~PeperoniRodin();

  // Load/unload the vusbdmx.dll at runtime
  bool LoadDLL();
  void UnloadDLL();
  bool IsDLLLoaded() const { return m_dll != nullptr; }

  // Enumerate available peperoni devices (returns count)
  int ListDevices();

  // Open / close
  bool Open(int deviceIndex);
  void Close();
  bool IsOpen() const { return m_devOpen; }

  // Device info
  std::string GetProductString() const;
  std::string GetSerialNumberString() const;
  uint32_t GetSerialNumber() const;
  std::string GetFirmwareString() const;

  // DMX output
  bool SendDMX(const uint8_t *data, int len);

  // RDM — same signature as EnttecPro for API compatibility
  bool SendRDM(const uint8_t *data, int len);
  bool SendRDMDiscovery(const uint8_t *data, int len);
  int ReceiveRDM(uint8_t *out, int maxLen, uint8_t &statusByte);

  // Purge (no-op for peperoni, RX is handled per-transaction)
  void Purge();

  // Logging
  void SetLogCallback(PepLogCallback cb);

private:
  // ── DLL module + function pointers ──
  HMODULE m_dll = nullptr;

  // vusbdmx.dll function signatures
  using fn_version = USHORT(__stdcall *)();
  using fn_open = BOOL(__stdcall *)(USHORT device, PHANDLE h);
  using fn_close = BOOL(__stdcall *)(HANDLE h);
  using fn_device_id = BOOL(__stdcall *)(HANDLE h, PUSHORT pid);
  using fn_is_rodin1 = BOOL(__stdcall *)(HANDLE h);
  using fn_product_get = BOOL(__stdcall *)(HANDLE h, PWCHAR str, USHORT size);
  using fn_serial_number_get = BOOL(__stdcall *)(HANDLE h, PWCHAR str,
                                                 USHORT size);
  using fn_device_version = BOOL(__stdcall *)(HANDLE h, PUSHORT pversion);
  using fn_tx = BOOL(__stdcall *)(HANDLE h, UCHAR universe, USHORT slots,
                                  PUCHAR buffer, UCHAR config, FLOAT time,
                                  FLOAT time_break, FLOAT time_mab,
                                  PUSHORT ptimestamp, PUCHAR pstatus);
  using fn_rx = BOOL(__stdcall *)(HANDLE h, UCHAR universe, USHORT slots_set,
                                  PUCHAR buffer, FLOAT timeout,
                                  FLOAT timeout_rx, PUSHORT pslots_get,
                                  PUSHORT ptimestamp, PUCHAR pstatus);

  fn_version m_fnVersion = nullptr;
  fn_open m_fnOpen = nullptr;
  fn_close m_fnClose = nullptr;
  fn_device_id m_fnDeviceId = nullptr;
  fn_is_rodin1 m_fnIsRodin1 = nullptr;
  fn_product_get m_fnProductGet = nullptr;
  fn_serial_number_get m_fnSerialNumberGet = nullptr;
  fn_device_version m_fnDeviceVersion = nullptr;
  fn_tx m_fnTx = nullptr;
  fn_rx m_fnRx = nullptr;

  // ── Device state ──
  HANDLE m_handle = INVALID_HANDLE_VALUE;
  bool m_devOpen = false;
  uint32_t m_serialHash = 0;
  std::string m_product;
  std::string m_serial;
  USHORT m_deviceVersion = 0;

  // ── RDM TX/RX state ──
  // After SendRDM/SendRDMDiscovery, the response is already received
  // and stored here for ReceiveRDM to return.
  std::vector<uint8_t> m_rxBuffer;
  int m_rxLen = 0;
  bool m_rxReady = false;
  bool m_lastWasDiscovery = false;

  std::mutex m_mutex;
  PepLogCallback m_logCb;
  void Log(bool tx, const uint8_t *data, int len);

  // Internal RDM helpers
  int TxRdmFrame(UCHAR universe, const uint8_t *rdmPkt, int pktLen);
  int RxRdmFrame(UCHAR universe, float timeout, bool needBreak,
                 std::vector<uint8_t> &out);
};

#endif // PEPERONI_RODIN_H
