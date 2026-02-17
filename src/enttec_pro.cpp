// ────────────────────────────────────────────────────────────────────────
// EnttecPro — Implementation
// ────────────────────────────────────────────────────────────────────────
#include "enttec_pro.h"
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────
EnttecPro::EnttecPro() = default;
EnttecPro::~EnttecPro() { Close(); }

// ── Device enumeration ──────────────────────────────────────────────────
int EnttecPro::ListDevices() {
  DWORD numDevs = 0;
  FT_STATUS st = FT_ListDevices(&numDevs, nullptr, FT_LIST_NUMBER_ONLY);
  return (st == FT_OK) ? static_cast<int>(numDevs) : 0;
}

// ── Open ────────────────────────────────────────────────────────────────
bool EnttecPro::Open(int deviceIndex) {
  // Close any existing connection first (uses the mutex internally)
  Close();

  std::lock_guard<std::mutex> lk(m_mutex);

  // Retry up to 3 times (same pattern as the Enttec example)
  FT_STATUS st = FT_OTHER_ERROR;
  for (int tries = 0; tries < 3; ++tries) {
    st = FT_Open(deviceIndex, &m_handle);
    if (st == FT_OK)
      break;
    m_handle = nullptr; // ensure clean state on retry
    Sleep(750);
  }
  if (st != FT_OK || m_handle == nullptr) {
    m_handle = nullptr;
    return false;
  }

  // ── Complete FTDI initialization (matches Enttec reference code) ──
  FT_SetBaudRate(m_handle, 57600);
  FT_SetDataCharacteristics(m_handle, FT_BITS_8, FT_STOP_BITS_1,
                            FT_PARITY_NONE);
  FT_SetFlowControl(m_handle, FT_FLOW_NONE, 0, 0);
  FT_ClrRts(m_handle);
  FT_SetLatencyTimer(m_handle, 2); // 2ms latency (default 16ms is too slow)
  FT_SetUSBParameters(m_handle, 64, 0); // USB transfer size 64 bytes
  FT_SetTimeouts(m_handle, 500, 100);   // R=500ms, W=100ms
  FT_Purge(m_handle, FT_PURGE_RX | FT_PURGE_TX);

  // Query widget parameters (Label 3)
  int zero = 0;
  if (!SendPacket(LABEL_GET_WIDGET_PARAMS, reinterpret_cast<uint8_t *>(&zero),
                  2)) {
    CloseInternal();
    return false;
  }

  int recv = ReceivePacket(LABEL_GET_WIDGET_PARAMS,
                           reinterpret_cast<uint8_t *>(&m_params),
                           sizeof(WidgetParams));
  if (recv <= 0) {
    // Retry once
    PurgeInternal();
    SendPacket(LABEL_GET_WIDGET_PARAMS, reinterpret_cast<uint8_t *>(&zero), 2);
    recv = ReceivePacket(LABEL_GET_WIDGET_PARAMS,
                         reinterpret_cast<uint8_t *>(&m_params),
                         sizeof(WidgetParams));
    if (recv <= 0) {
      CloseInternal();
      return false;
    }
  }

  // Query serial number (Label 10)
  uint8_t snBuf[4] = {};
  SendPacket(LABEL_GET_WIDGET_SN, reinterpret_cast<uint8_t *>(&zero), 2);
  ReceivePacket(LABEL_GET_WIDGET_SN, snBuf, 4);
  m_serialNumber =
      snBuf[0] | (snBuf[1] << 8) | (snBuf[2] << 16) | (snBuf[3] << 24);

  return true;
}

// ── Close (public, acquires mutex) ──────────────────────────────────────
void EnttecPro::Close() {
  std::lock_guard<std::mutex> lk(m_mutex);
  CloseInternal();
}

// ── Close (internal, caller must already hold mutex) ────────────────────
void EnttecPro::CloseInternal() {
  if (m_handle) {
    FT_Close(m_handle);
    m_handle = nullptr;
  }
  m_params = {};
  m_serialNumber = 0;
}

// ── Firmware string ─────────────────────────────────────────────────────
std::string EnttecPro::GetFirmwareString() const {
  char buf[32];
  snprintf(buf, sizeof(buf), "%d.%d", m_params.firmwareMSB,
           m_params.firmwareLSB);
  return buf;
}

// ── Send packet (framing: 0x7E | label | len_lo | len_hi | data | 0xE7)
bool EnttecPro::SendPacket(uint8_t label, const uint8_t *data, int length) {
  if (!m_handle)
    return false;

  DWORD written = 0;

  // Header
  uint8_t header[PRO_HEADER_LENGTH];
  header[0] = PRO_START_CODE;
  header[1] = label;
  header[2] = static_cast<uint8_t>(length & 0xFF);
  header[3] = static_cast<uint8_t>(length >> 8);

  FT_STATUS res;
  res = FT_Write(m_handle, header, PRO_HEADER_LENGTH, &written);
  if (written != PRO_HEADER_LENGTH)
    return false;

  // Payload
  if (length > 0 && data) {
    res = FT_Write(m_handle, const_cast<uint8_t *>(data), length, &written);
    if (static_cast<int>(written) != length)
      return false;
  }

  // End code
  uint8_t endCode = PRO_END_CODE;
  res = FT_Write(m_handle, &endCode, 1, &written);
  if (written != 1)
    return false;

  // Log TX
  if (m_logCb) {
    std::vector<uint8_t> frame(PRO_HEADER_LENGTH + length + 1);
    memcpy(frame.data(), header, PRO_HEADER_LENGTH);
    if (length > 0 && data)
      memcpy(frame.data() + PRO_HEADER_LENGTH, data, length);
    frame.back() = PRO_END_CODE;
    Log(true, frame.data(), static_cast<int>(frame.size()));
  }

  return (res == FT_OK);
}

// ── Receive packet ──────────────────────────────────────────────────────
//    Scans for start code, matches label, reads length, payload, end code.
//    Returns bytes copied into `data`, or -1 on failure / timeout.
int EnttecPro::ReceivePacket(uint8_t label, uint8_t *data, int maxLen) {
  if (!m_handle)
    return -1;

  FT_STATUS res;
  DWORD bytesRead = 0;
  uint8_t byte = 0;
  bool found = false;

  // Scan for start code + matching label (bounded attempts)
  for (int attempts = 0; attempts < 100; ++attempts) {
    // Find 0x7E
    bool gotStart = false;
    for (int scan = 0; scan < 512; ++scan) {
      res = FT_Read(m_handle, &byte, 1, &bytesRead);
      if (bytesRead == 0)
        return -1;
      if (byte == PRO_START_CODE) {
        gotStart = true;
        break;
      }
    }
    if (!gotStart)
      return -1;

    // Read label
    res = FT_Read(m_handle, &byte, 1, &bytesRead);
    if (bytesRead == 0)
      return -1;
    if (byte == label) {
      found = true;
      break;
    }
  }

  // CRITICAL: if we never matched the label, bail out
  if (!found)
    return -1;

  // Read length (2 bytes, little-endian)
  uint8_t lenBytes[2];
  res = FT_Read(m_handle, lenBytes, 2, &bytesRead);
  if (bytesRead != 2)
    return -1;
  int length = lenBytes[0] | (static_cast<int>(lenBytes[1]) << 8);

  if (length > PRO_MAX_PACKET || length < 0)
    return -1;

  // Read payload
  std::vector<uint8_t> buffer(length);
  if (length > 0) {
    res = FT_Read(m_handle, buffer.data(), length, &bytesRead);
    if (static_cast<int>(bytesRead) != length)
      return -1;
  }

  // Check end code
  res = FT_Read(m_handle, &byte, 1, &bytesRead);
  if (bytesRead == 0 || byte != PRO_END_CODE)
    return -1;

  // Copy to caller
  int toCopy = (length < maxLen) ? length : maxLen;
  if (toCopy > 0 && data)
    memcpy(data, buffer.data(), toCopy);

  // Log RX
  if (m_logCb) {
    std::vector<uint8_t> frame(4 + length + 1);
    frame[0] = PRO_START_CODE;
    frame[1] = label;
    frame[2] = lenBytes[0];
    frame[3] = lenBytes[1];
    if (length > 0)
      memcpy(frame.data() + 4, buffer.data(), length);
    frame.back() = PRO_END_CODE;
    Log(false, frame.data(), static_cast<int>(frame.size()));
  }

  return toCopy;
}

// ── DMX output ──────────────────────────────────────────────────────────
bool EnttecPro::SendDMX(const uint8_t *data, int len) {
  std::lock_guard<std::mutex> lk(m_mutex);
  return SendPacket(LABEL_TX_DMX, data, len);
}

// ── RDM TX ──────────────────────────────────────────────────────────────
//    Do NOT purge here — reference implementations (ofxDmxUsbPro, OLA)
//    don't purge before RDM send. TX purge can interfere with the widget's
//    internal RDM state machine. Caller handles purging if needed.
bool EnttecPro::SendRDM(const uint8_t *data, int len) {
  std::lock_guard<std::mutex> lk(m_mutex);
  return SendPacket(LABEL_TX_RDM, data, len);
}

// ── RDM Discovery TX (Label 11 — no break) ─────────────────────────────
bool EnttecPro::SendRDMDiscovery(const uint8_t *data, int len) {
  std::lock_guard<std::mutex> lk(m_mutex);
  PurgeInternal();
  return SendPacket(LABEL_TX_RDM_DISCOVERY, data, len);
}

// ── RDM RX ──────────────────────────────────────────────────────────────
int EnttecPro::ReceiveRDM(uint8_t *out, int maxLen, uint8_t &statusByte) {
  std::lock_guard<std::mutex> lk(m_mutex);
  uint8_t buf[PRO_MAX_PACKET];
  int got = ReceivePacket(LABEL_RX_DMX_PACKET, buf, sizeof(buf));
  if (got <= 0) {
    statusByte = 0xFF;
    return -1;
  }

  statusByte = buf[0];
  int rdmLen = got - 1;
  if (rdmLen > maxLen)
    rdmLen = maxLen;
  if (rdmLen > 0)
    memcpy(out, buf + 1, rdmLen);
  return rdmLen;
}

// ── Purge (public) ──────────────────────────────────────────────────────
void EnttecPro::Purge() {
  std::lock_guard<std::mutex> lk(m_mutex);
  PurgeInternal();
}

// ── Purge (internal, caller holds mutex) ────────────────────────────────
void EnttecPro::PurgeInternal() {
  if (m_handle) {
    FT_Purge(m_handle, FT_PURGE_TX);
    FT_Purge(m_handle, FT_PURGE_RX);
  }
}

// ── Logging ─────────────────────────────────────────────────────────────
void EnttecPro::SetLogCallback(LogCallback cb) { m_logCb = std::move(cb); }

void EnttecPro::Log(bool tx, const uint8_t *data, int len) {
  if (m_logCb)
    m_logCb(tx, data, len);
}
