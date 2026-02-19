// ────────────────────────────────────────────────────────────────────────
// PeperoniRodin — runtime-loaded driver for peperoni Rodin 1 USB-DMX
// ────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#include "peperoni_rodin.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

// ── vusbdmx.h constants (replicated to avoid header conflicts) ──────────
#define VUSBDMX_BULK_CONFIG_BLOCK 0x02
#define VUSBDMX_BULK_CONFIG_RX 0x04
#define VUSBDMX_BULK_CONFIG_NORETX 0x08

#define VUSBDMX_BULK_STATUS_OK 0x00
#define VUSBDMX_BULK_STATUS_TIMEOUT 0x01
#define VUSBDMX_BULK_STATUS_UNIVERSE_WRONG 0x03
#define VUSBDMX_BULK_STATUS_RX_FRAMEERROR 0x80
#define VUSBDMX_BULK_STATUS_RX_NO_BREAK 0x40

// RDM frame constants for peperoni
static constexpr UCHAR TxConfig = VUSBDMX_BULK_CONFIG_BLOCK |
                                  VUSBDMX_BULK_CONFIG_RX |
                                  VUSBDMX_BULK_CONFIG_NORETX;
static constexpr float TxTimeout = 30e-3f;
static constexpr float TxBreak = 300e-6f;
static constexpr float TxMab = 50e-6f;
static constexpr float RxSlotTimeout = 2.5e-3f;

// ═══════════════════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════════════

PeperoniRodin::PeperoniRodin() {}

PeperoniRodin::~PeperoniRodin() {
  Close();
  UnloadDLL();
}

// ═══════════════════════════════════════════════════════════════════════════
// DLL Management
// ═══════════════════════════════════════════════════════════════════════════

bool PeperoniRodin::LoadDLL() {
  if (m_dll)
    return true; // already loaded

  // Try loading from app directory first, then system search path
  m_dll = LoadLibraryA("vusbdmx.dll");
  if (!m_dll) {
    OutputDebugStringA("[Peperoni] Failed to load vusbdmx.dll\n");
    return false;
  }

  // Resolve all function pointers
  m_fnVersion = (fn_version)GetProcAddress(m_dll, "vusbdmx_version");
  m_fnOpen = (fn_open)GetProcAddress(m_dll, "vusbdmx_open");
  m_fnClose = (fn_close)GetProcAddress(m_dll, "vusbdmx_close");
  m_fnDeviceId = (fn_device_id)GetProcAddress(m_dll, "vusbdmx_device_id");
  m_fnIsRodin1 = (fn_is_rodin1)GetProcAddress(m_dll, "vusbdmx_is_rodin1");
  m_fnProductGet = (fn_product_get)GetProcAddress(m_dll, "vusbdmx_product_get");
  m_fnSerialNumberGet =
      (fn_serial_number_get)GetProcAddress(m_dll, "vusbdmx_serial_number_get");
  m_fnDeviceVersion =
      (fn_device_version)GetProcAddress(m_dll, "vusbdmx_device_version");
  m_fnTx = (fn_tx)GetProcAddress(m_dll, "vusbdmx_tx");
  m_fnRx = (fn_rx)GetProcAddress(m_dll, "vusbdmx_rx");

  // Verify critical functions loaded
  if (!m_fnOpen || !m_fnClose || !m_fnTx || !m_fnRx) {
    OutputDebugStringA(
        "[Peperoni] vusbdmx.dll loaded but missing required functions\n");
    UnloadDLL();
    return false;
  }

  if (m_fnVersion) {
    USHORT ver = m_fnVersion();
    char buf[64];
    snprintf(buf, sizeof(buf), "[Peperoni] DLL version: 0x%04X\n", ver);
    OutputDebugStringA(buf);
  }

  return true;
}

void PeperoniRodin::UnloadDLL() {
  if (m_dll) {
    FreeLibrary(m_dll);
    m_dll = nullptr;
  }
  m_fnVersion = nullptr;
  m_fnOpen = nullptr;
  m_fnClose = nullptr;
  m_fnDeviceId = nullptr;
  m_fnIsRodin1 = nullptr;
  m_fnProductGet = nullptr;
  m_fnSerialNumberGet = nullptr;
  m_fnDeviceVersion = nullptr;
  m_fnTx = nullptr;
  m_fnRx = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Device enumeration
// ═══════════════════════════════════════════════════════════════════════════

int PeperoniRodin::ListDevices() {
  if (!LoadDLL())
    return 0;

  // Try opening devices until one fails
  int count = 0;
  for (USHORT i = 0; i < 16; ++i) {
    HANDLE h = INVALID_HANDLE_VALUE;
    if (m_fnOpen(i, &h) && h != INVALID_HANDLE_VALUE) {
      m_fnClose(h);
      count++;
    } else {
      break;
    }
  }
  return count;
}

// ═══════════════════════════════════════════════════════════════════════════
// Open / Close
// ═══════════════════════════════════════════════════════════════════════════

bool PeperoniRodin::Open(int deviceIndex) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!LoadDLL())
    return false;

  if (m_devOpen)
    Close();

  HANDLE h = INVALID_HANDLE_VALUE;
  if (!m_fnOpen(static_cast<USHORT>(deviceIndex), &h) ||
      h == INVALID_HANDLE_VALUE) {
    OutputDebugStringA("[Peperoni] Failed to open device\n");
    return false;
  }

  m_handle = h;
  m_devOpen = true;

  // Read product string
  if (m_fnProductGet) {
    WCHAR wbuf[128] = {};
    if (m_fnProductGet(m_handle, wbuf, 128)) {
      char mbuf[256] = {};
      WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, mbuf, 256, nullptr, nullptr);
      m_product = mbuf;
    }
  }

  // Read serial number
  if (m_fnSerialNumberGet) {
    WCHAR wbuf[128] = {};
    if (m_fnSerialNumberGet(m_handle, wbuf, 128)) {
      char mbuf[256] = {};
      WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, mbuf, 256, nullptr, nullptr);
      m_serial = mbuf;
      // Hash serial into uint32_t for compatibility
      m_serialHash = 0;
      for (char c : m_serial)
        m_serialHash = m_serialHash * 31 + static_cast<uint8_t>(c);
    }
  }

  // Read device version
  if (m_fnDeviceVersion) {
    m_fnDeviceVersion(m_handle, &m_deviceVersion);
  }

  char buf[256];
  snprintf(buf, sizeof(buf), "[Peperoni] Opened: %s (SN: %s, HW: 0x%04X)\n",
           m_product.c_str(), m_serial.c_str(), m_deviceVersion);
  OutputDebugStringA(buf);

  return true;
}

void PeperoniRodin::Close() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_devOpen && m_fnClose) {
    m_fnClose(m_handle);
  }
  m_handle = INVALID_HANDLE_VALUE;
  m_devOpen = false;
  m_product.clear();
  m_serial.clear();
  m_serialHash = 0;
  m_rxReady = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Device info
// ═══════════════════════════════════════════════════════════════════════════

std::string PeperoniRodin::GetProductString() const { return m_product; }

std::string PeperoniRodin::GetSerialNumberString() const { return m_serial; }

uint32_t PeperoniRodin::GetSerialNumber() const { return m_serialHash; }

std::string PeperoniRodin::GetFirmwareString() const {
  char buf[32];
  snprintf(buf, sizeof(buf), "HW %d.%d", m_deviceVersion >> 8,
           m_deviceVersion & 0xFF);
  return buf;
}

// ═══════════════════════════════════════════════════════════════════════════
// DMX Output
// ═══════════════════════════════════════════════════════════════════════════

bool PeperoniRodin::SendDMX(const uint8_t *data, int len) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_devOpen || !m_fnTx)
    return false;

  USHORT timestamp = 0;
  UCHAR status = 0;

  // data[0] is start code (0x00), len includes start code
  if (!m_fnTx(m_handle, 0 /*universe*/, static_cast<USHORT>(len),
              const_cast<PUCHAR>(data), 0 /*config: no block, no delay*/,
              0 /*time*/, 200e-6f /*break*/, 20e-6f /*mab*/, &timestamp,
              &status)) {
    return false;
  }

  return (status == VUSBDMX_BULK_STATUS_OK);
}

// ═══════════════════════════════════════════════════════════════════════════
// RDM  (compatible with EnttecPro's interface)
// ═══════════════════════════════════════════════════════════════════════════

void PeperoniRodin::Log(bool tx, const uint8_t *data, int len) {
  if (m_logCb)
    m_logCb(tx, data, len);
}

void PeperoniRodin::Purge() {
  // No-op for peperoni — RX is consumed per-transaction via vusbdmx_rx
  m_rxReady = false;
}

void PeperoniRodin::SetLogCallback(PepLogCallback cb) { m_logCb = cb; }

// Internal: send an RDM frame via vusbdmx_tx (with break for normal RDM)
int PeperoniRodin::TxRdmFrame(UCHAR universe, const uint8_t *rdmPkt,
                              int pktLen) {
  USHORT timestamp = 0;
  UCHAR status = 1;

  // Retry up to 3 times on TX failures
  for (int attempt = 3; attempt > 0; --attempt) {
    if (!m_fnTx(m_handle, universe, static_cast<USHORT>(pktLen),
                const_cast<PUCHAR>(rdmPkt), TxConfig, TxTimeout, TxBreak, TxMab,
                &timestamp, &status)) {
      return -1;
    }
    if (status == VUSBDMX_BULK_STATUS_OK)
      return pktLen;

    if (status == VUSBDMX_BULK_STATUS_UNIVERSE_WRONG)
      return -2;

    // Clear any stale RX data before retrying
    USHORT rxSlots = 0;
    UCHAR rxBuf[257] = {};
    m_fnRx(m_handle, universe, 257, rxBuf, 0, 100e-6f, &rxSlots, &timestamp,
           &status);
  }

  return pktLen;
}

// Internal: receive an RDM frame via vusbdmx_rx
int PeperoniRodin::RxRdmFrame(UCHAR universe, float timeout, bool needBreak,
                              std::vector<uint8_t> &out) {
  USHORT slots = 0;
  UCHAR status = 0;
  USHORT timestamp = 0;

  out.resize(257);
  if (!m_fnRx(m_handle, universe, static_cast<USHORT>(out.size()), out.data(),
              timeout, RxSlotTimeout, &slots, &timestamp, &status)) {
    return -1;
  }

  if (status != VUSBDMX_BULK_STATUS_OK) {
    if (status == VUSBDMX_BULK_STATUS_TIMEOUT)
      return -2; // timeout
    if (status & VUSBDMX_BULK_STATUS_RX_FRAMEERROR)
      return -3; // frame error / collision
    if ((status & VUSBDMX_BULK_STATUS_RX_NO_BREAK) && needBreak)
      return -4; // no break
  }

  out.resize(slots);
  return static_cast<int>(slots);
}

// SendRDM — send an already-formed RDM packet (with 0xCC start code)
// The Peperoni sends AND receives in one transaction, so we store the
// response internally for ReceiveRDM to return.
bool PeperoniRodin::SendRDM(const uint8_t *data, int len) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_devOpen || !m_fnTx || !m_fnRx)
    return false;

  m_rxReady = false;
  m_lastWasDiscovery = false;

  Log(true, data, len);

  // TX the RDM frame
  int txResult = TxRdmFrame(0, data, len);
  if (txResult < 0)
    return false;

  // RX the response
  // Timeout: break + mab + tx time + turnaround + max response
  float rxTimeout = TxBreak + TxMab + len * 48e-6f + 2e-3f + 255 * 144e-6f;

  m_rxBuffer.clear();
  int rxResult = RxRdmFrame(0, rxTimeout, true, m_rxBuffer);

  if (rxResult > 0) {
    // Strip the vusbdmx framing (SC + sub-SC + length prefix)
    // The peperoni returns raw RDM data starting with 0xCC
    m_rxLen = rxResult;
    m_rxReady = true;
    Log(false, m_rxBuffer.data(), m_rxLen);
  } else {
    m_rxLen = 0;
    m_rxReady = false;
  }

  return true;
}

// SendRDMDiscovery — send a discovery request (response may lack break)
bool PeperoniRodin::SendRDMDiscovery(const uint8_t *data, int len) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_devOpen || !m_fnTx || !m_fnRx)
    return false;

  m_rxReady = false;
  m_lastWasDiscovery = true;

  Log(true, data, len);

  // TX the discovery frame
  int txResult = TxRdmFrame(0, data, len);
  if (txResult < 0)
    return false;

  // RX with relaxed rules (no break required for discovery responses)
  float rxTimeout = 10e-3f;

  m_rxBuffer.clear();
  int rxResult = RxRdmFrame(0, rxTimeout, false, m_rxBuffer);

  if (rxResult > 0) {
    m_rxLen = rxResult;
    m_rxReady = true;
    Log(false, m_rxBuffer.data(), m_rxLen);
  } else if (rxResult == -2) {
    // Timeout — no device in this branch
    m_rxLen = 0;
    m_rxReady = false;
  } else {
    // Frame error / collision
    m_rxLen = 0;
    m_rxReady = false;
    // Return -3 for collision via status byte
  }

  return true;
}

// ReceiveRDM — return the response that was already captured during Send
int PeperoniRodin::ReceiveRDM(uint8_t *out, int maxLen, uint8_t &statusByte) {
  // No mutex needed — called sequentially after SendRDM
  statusByte = 0;

  if (!m_rxReady || m_rxLen <= 0) {
    statusByte = 0x02; // no data
    return -1;
  }

  int copyLen = (m_rxLen > maxLen) ? maxLen : m_rxLen;
  memcpy(out, m_rxBuffer.data(), copyLen);

  m_rxReady = false;
  return copyLen;
}
