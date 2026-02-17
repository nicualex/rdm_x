// ────────────────────────────────────────────────────────────────────────
// RDM_X — DMX / RDM Fixture Validation Tool
// Main entry: Win32 + DirectX 11 + Dear ImGui
// ────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#include <atomic>
#include <cstdio>
#include <d3d11.h>
#include <deque>
#include <mutex>
#include <string>
#include <tchar.h>
#include <thread>
#include <vector>
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "enttec_pro.h"
#include "parameter_loader.h"
#include "rdm.h"
#include "validator.h"

// ── DirectX 11 globals ──────────────────────────────────────────────────
static ID3D11Device *g_pd3dDevice = nullptr;
static ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
static IDXGISwapChain *g_pSwapChain = nullptr;
static ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
// Forward declare from imgui_impl_win32.cpp
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam,
                              LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    return true;
  switch (msg) {
  case WM_SIZE:
    if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
      CleanupRenderTarget();
      g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam),
                                  DXGI_FORMAT_UNKNOWN, 0);
      CreateRenderTarget();
    }
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ── Application state ───────────────────────────────────────────────────
static EnttecPro g_pro;
static std::vector<RDMParameter> g_params;
static std::vector<uint64_t> g_discoveredUIDs;
static int g_selectedUID = -1;
static std::vector<ValidationResult> g_validationResults;
static bool g_isConnected = false;
static bool g_discovering = false;
static bool g_validating = false;

// DMX control
static int g_dmxLevel = 0;
static bool g_dmxBroadcast = true;

// Controller UID (arbitrary — used for RDM source address)
static const uint64_t kControllerUID = 0x454E540001ULL; // "ENT" + 0001

// ── Log system ──────────────────────────────────────────────────────────
struct LogEntry {
  bool isTX; // true=TX (cyan), false=RX (green)
  std::string text;
};
static std::mutex g_logMutex;
static std::deque<LogEntry> g_logEntries;
static const int kMaxLogEntries = 500;

static void AddLog(bool tx, const std::string &text) {
  std::lock_guard<std::mutex> lk(g_logMutex);
  if (g_logEntries.size() >= kMaxLogEntries)
    g_logEntries.pop_front();
  g_logEntries.push_back({tx, text});
}

// ── Worker thread helpers ───────────────────────────────────────────────
static std::thread g_workerThread;
static std::atomic<bool> g_workerBusy{false};

static void WorkerDiscovery() {
  g_workerBusy = true;
  g_discovering = true;
  AddLog(true, "--- Starting RDM Discovery ---");
  AddLog(true, "    (DMX output paused during discovery)");
  auto uids = RDMDiscovery(g_pro, kControllerUID);
  g_discoveredUIDs = uids;
  char buf[64];
  snprintf(buf, sizeof(buf), "--- Discovery complete: %d device(s) found ---",
           static_cast<int>(uids.size()));
  AddLog(false, buf);
  g_discovering = false;
  g_workerBusy = false;
}

static void WorkerValidate(uint64_t uid) {
  g_workerBusy = true;
  g_validating = true;
  AddLog(true, "--- Validating " + UIDToString(uid) + " ---");
  auto results = ValidateFixture(g_pro, kControllerUID, uid, g_params);
  g_validationResults = results;
  AddLog(false, "--- Validation complete ---");
  g_validating = false;
  g_workerBusy = false;
}

// ── DMX sender (called every frame when connected) ──────────────────────
static void SendDMXFrame() {
  if (!g_isConnected || !g_pro.IsOpen())
    return;

  // CRITICAL: Do NOT send DMX while the worker thread is doing
  // RDM discovery or validation. DMX Label 6 packets interleave
  // with RDM Label 7/5 transactions and reset the widget's bus
  // state, causing discovery to never see responses.
  if (g_workerBusy.load())
    return;

  uint8_t dmxData[513];
  memset(dmxData, 0, sizeof(dmxData));
  dmxData[0] = 0x00; // DMX start code

  if (g_dmxBroadcast) {
    memset(dmxData + 1, static_cast<uint8_t>(g_dmxLevel), 512);
  }

  g_pro.SendDMX(dmxData, 513);
}

// ── ImGui color helpers ─────────────────────────────────────────────────
static ImVec4 StatusColor(ValidationStatus s) {
  switch (s) {
  case ValidationStatus::GREEN:
    return ImVec4(0.1f, 0.8f, 0.1f, 1.0f);
  case ValidationStatus::YELLOW:
    return ImVec4(0.9f, 0.8f, 0.1f, 1.0f);
  case ValidationStatus::RED:
    return ImVec4(0.9f, 0.15f, 0.1f, 1.0f);
  }
  return ImVec4(1, 1, 1, 1);
}

static const char *StatusText(ValidationStatus s) {
  switch (s) {
  case ValidationStatus::GREEN:
    return "OK";
  case ValidationStatus::YELLOW:
    return "WARN";
  case ValidationStatus::RED:
    return "FAIL";
  }
  return "??";
}

// ═══════════════════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════════════════
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  // ── Create window ───────────────────────────────────────────────────
  WNDCLASSEX wc = {sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0,       0,
                   hInstance,          nullptr,    nullptr, nullptr, nullptr,
                   _T("RDM_X"),        nullptr};
  RegisterClassEx(&wc);
  HWND hwnd =
      CreateWindow(wc.lpszClassName, _T("RDM_X  -  DMX/RDM Fixture Validator"),
                   WS_OVERLAPPEDWINDOW, 100, 100, 1400, 900, nullptr, nullptr,
                   wc.hInstance, nullptr);

  if (!CreateDeviceD3D(hwnd)) {
    CleanupDeviceD3D();
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 1;
  }
  ShowWindow(hwnd, SW_SHOWDEFAULT);
  UpdateWindow(hwnd);

  // ── ImGui setup ─────────────────────────────────────────────────────
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // Dark, high-contrast style
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style.FrameRounding = 2.0f;
  style.GrabRounding = 2.0f;
  style.WindowPadding = ImVec2(6, 6);
  style.ItemSpacing = ImVec2(6, 4);
  style.FramePadding = ImVec2(6, 3);
  style.ScrollbarSize = 14.0f;
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
  style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
  style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.35f, 1.0f);
  style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.27f, 1.0f);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.35f, 0.45f, 1.0f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.17f, 1.0f);
  style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
  style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.16f, 0.20f, 1.0f);
  style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.30f, 0.30f, 0.35f, 1.0f);
  style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.22f, 0.22f, 0.26f, 1.0f);

  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

  // ── Load parameters ─────────────────────────────────────────────────
  g_params = LoadParameters("Vaya_RDM_map.csv");
  if (g_params.empty()) {
    // Try executable-relative path
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir(exePath);
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos)
      dir = dir.substr(0, pos + 1);
    g_params = LoadParameters(dir + "Vaya_RDM_map.csv");
  }

  // Wire up the log callback
  g_pro.SetLogCallback([](bool tx, const uint8_t *data, int len) {
    std::string hex;
    hex.reserve(len * 3 + 8);
    hex = tx ? "TX: " : "RX: ";
    for (int i = 0; i < len && i < 64; ++i) {
      char buf[4];
      snprintf(buf, sizeof(buf), "%02X ", data[i]);
      hex += buf;
    }
    if (len > 64)
      hex += "...";
    AddLog(tx, hex);
  });

  AddLog(false, "RDM_X started. Loaded " + std::to_string(g_params.size()) +
                    " GET parameters from CSV.");

  // ── Main loop ───────────────────────────────────────────────────────
  bool running = true;
  MSG msg;
  ZeroMemory(&msg, sizeof(msg));

  while (running) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      if (msg.message == WM_QUIT)
        running = false;
    }
    if (!running)
      break;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // ── Dockspace ───────────────────────────────────────────────────
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    // ══════════════════════════════════════════════════════════════════
    // TOP BAR — DMX CONTROL
    // ══════════════════════════════════════════════════════════════════
    ImGui::Begin("DMX Control", nullptr, ImGuiWindowFlags_NoCollapse);
    {
      ImGui::Text("DMX Output:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(300);
      ImGui::SliderInt("##dmxlevel", &g_dmxLevel, 0, 255, "Level: %d");
      ImGui::SameLine();
      if (ImGui::Button("Blackout"))
        g_dmxLevel = 0;
      ImGui::SameLine();
      ImGui::Checkbox("Broadcast All Channels", &g_dmxBroadcast);

      // Send DMX every frame if connected
      if (g_isConnected)
        SendDMXFrame();
    }
    ImGui::End();

    // ══════════════════════════════════════════════════════════════════
    // LEFT PANE — CONNECTION & DEVICES
    // ══════════════════════════════════════════════════════════════════
    ImGui::Begin("Connection", nullptr, ImGuiWindowFlags_NoCollapse);
    {
      ImGui::SeparatorText("FTDI Device");

      static int selectedDevice = 0;
      static int cachedNumDevices = -1;
      // Only enumerate once at startup or on explicit refresh
      if (cachedNumDevices < 0)
        cachedNumDevices = EnttecPro::ListDevices();
      if (ImGui::Button("Refresh")) {
        cachedNumDevices = EnttecPro::ListDevices();
      }
      ImGui::SameLine();
      char label[32];
      snprintf(label, sizeof(label), "Device %d / %d", selectedDevice,
               cachedNumDevices);
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##device", label)) {
        for (int i = 0; i < cachedNumDevices; ++i) {
          char item[32];
          snprintf(item, sizeof(item), "Device %d", i);
          if (ImGui::Selectable(item, i == selectedDevice))
            selectedDevice = i;
        }
        ImGui::EndCombo();
      }

      if (!g_isConnected) {
        if (ImGui::Button("Connect", ImVec2(-1, 0))) {
          if (g_pro.Open(selectedDevice)) {
            g_isConnected = true;
            AddLog(false, "Connected. FW: " + g_pro.GetFirmwareString());
          } else {
            AddLog(true, "ERROR: Failed to open device " +
                             std::to_string(selectedDevice));
          }
        }
      } else {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1), "CONNECTED");
        ImGui::Text("Firmware: %s", g_pro.GetFirmwareString().c_str());
        ImGui::Text("SN: %08X", g_pro.GetSerialNumber());

        if (ImGui::Button("Disconnect", ImVec2(-1, 0))) {
          g_pro.Close();
          g_isConnected = false;
          g_discoveredUIDs.clear();
          g_validationResults.clear();
          g_selectedUID = -1;
          AddLog(false, "Disconnected.");
        }
      }

      ImGui::Spacing();
      ImGui::SeparatorText("RDM Discovery");

      bool busy = g_workerBusy.load();
      if (g_isConnected && !busy) {
        if (ImGui::Button("Discover Devices", ImVec2(-1, 0))) {
          g_discoveredUIDs.clear();
          g_validationResults.clear();
          g_selectedUID = -1;
          if (g_workerThread.joinable())
            g_workerThread.join();
          g_workerThread = std::thread(WorkerDiscovery);
        }
      }
      if (g_discovering) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Discovering...");
      }

      ImGui::Spacing();
      ImGui::SeparatorText("Discovered Devices");

      if (g_discoveredUIDs.empty()) {
        ImGui::TextDisabled("No devices found");
      } else {
        for (int i = 0; i < static_cast<int>(g_discoveredUIDs.size()); ++i) {
          std::string uidStr = UIDToString(g_discoveredUIDs[i]);
          bool selected = (i == g_selectedUID);
          if (ImGui::Selectable(uidStr.c_str(), selected)) {
            g_selectedUID = i;
            // Auto-validate on selection
            if (!g_workerBusy.load()) {
              g_validationResults.clear();
              if (g_workerThread.joinable())
                g_workerThread.join();
              uint64_t uid = g_discoveredUIDs[i];
              g_workerThread = std::thread(WorkerValidate, uid);
            }
          }
        }
      }
    }
    ImGui::End();

    // ══════════════════════════════════════════════════════════════════
    // MAIN PANE — VALIDATION GRID
    // ══════════════════════════════════════════════════════════════════
    ImGui::Begin("Validation Results", nullptr, ImGuiWindowFlags_NoCollapse);
    {
      if (g_validating) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Validating...");
      }

      if (g_selectedUID >= 0 &&
          g_selectedUID < static_cast<int>(g_discoveredUIDs.size())) {
        ImGui::Text("Device: %s",
                    UIDToString(g_discoveredUIDs[g_selectedUID]).c_str());
        ImGui::Separator();
      }

      // Summary counters
      int greenCount = 0, yellowCount = 0, redCount = 0;
      for (auto &vr : g_validationResults) {
        switch (vr.status) {
        case ValidationStatus::GREEN:
          greenCount++;
          break;
        case ValidationStatus::YELLOW:
          yellowCount++;
          break;
        case ValidationStatus::RED:
          redCount++;
          break;
        }
      }
      if (!g_validationResults.empty()) {
        ImGui::TextColored(ImVec4(0.1f, 0.8f, 0.1f, 1), "PASS: %d", greenCount);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.1f, 1), "  WARN: %d",
                           yellowCount);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.15f, 0.1f, 1), "  FAIL: %d",
                           redCount);
        ImGui::Separator();
      }

      // Table
      ImGuiTableFlags tableFlags =
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
          ImGuiTableFlags_SizingStretchProp;
      if (ImGui::BeginTable("##validation", 5, tableFlags)) {
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Mandatory", ImGuiTableColumnFlags_WidthFixed,
                                70);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (const auto &vr : g_validationResults) {
          ImGui::TableNextRow();

          // PID
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("0x%04X", vr.pid);

          // Name
          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(vr.name.c_str());

          // Value
          ImGui::TableSetColumnIndex(2);
          ImGui::TextUnformatted(vr.value.c_str());

          // Status — colored background
          ImGui::TableSetColumnIndex(3);
          ImVec4 col = StatusColor(vr.status);
          ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                 ImGui::ColorConvertFloat4ToU32(
                                     ImVec4(col.x, col.y, col.z, 0.35f)));
          ImGui::TextColored(col, "%s", StatusText(vr.status));

          // Mandatory
          ImGui::TableSetColumnIndex(4);
          if (vr.isMandatory)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1), "YES");
          else
            ImGui::TextDisabled("no");
        }
        ImGui::EndTable();
      }
    }
    ImGui::End();

    // ══════════════════════════════════════════════════════════════════
    // BOTTOM PANE — HEX LOG
    // ══════════════════════════════════════════════════════════════════
    ImGui::Begin("Protocol Log", nullptr, ImGuiWindowFlags_NoCollapse);
    {
      if (ImGui::Button("Clear Log")) {
        std::lock_guard<std::mutex> lk(g_logMutex);
        g_logEntries.clear();
      }
      ImGui::Separator();

      ImGui::BeginChild("##logscroll", ImVec2(0, 0), ImGuiChildFlags_None,
                        ImGuiWindowFlags_HorizontalScrollbar);
      {
        std::lock_guard<std::mutex> lk(g_logMutex);
        for (const auto &entry : g_logEntries) {
          ImVec4 color = entry.isTX
                             ? ImVec4(0.3f, 0.85f, 1.0f, 1.0f) // cyan for TX
                             : ImVec4(0.3f, 1.0f, 0.4f, 1.0f); // green for RX
          ImGui::TextColored(color, "%s", entry.text.c_str());
        }
        // Auto-scroll to bottom
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20)
          ImGui::SetScrollHereY(1.0f);
      }
      ImGui::EndChild();
    }
    ImGui::End();

    // ── Render ──────────────────────────────────────────────────────
    ImGui::Render();
    const float clear_color[4] = {0.06f, 0.06f, 0.08f, 1.0f};
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView,
                                            nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView,
                                               clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(1, 0);
  }

  // ── Cleanup ─────────────────────────────────────────────────────────
  if (g_workerThread.joinable())
    g_workerThread.join();
  g_pro.Close();

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  CleanupDeviceD3D();
  DestroyWindow(hwnd);
  UnregisterClass(wc.lpszClassName, wc.hInstance);
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════
// DirectX 11 boilerplate
// ═══════════════════════════════════════════════════════════════════════
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static bool CreateDeviceD3D(HWND hWnd) {
  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferCount = 2;
  sd.BufferDesc.Width = 0;
  sd.BufferDesc.Height = 0;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hWnd;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[] = {D3D_FEATURE_LEVEL_11_0};
  HRESULT hr = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevelArray, 1,
      D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel,
      &g_pd3dDeviceContext);
  if (FAILED(hr))
    return false;

  CreateRenderTarget();
  return true;
}

static void CleanupDeviceD3D() {
  CleanupRenderTarget();
  if (g_pSwapChain) {
    g_pSwapChain->Release();
    g_pSwapChain = nullptr;
  }
  if (g_pd3dDeviceContext) {
    g_pd3dDeviceContext->Release();
    g_pd3dDeviceContext = nullptr;
  }
  if (g_pd3dDevice) {
    g_pd3dDevice->Release();
    g_pd3dDevice = nullptr;
  }
}

static void CreateRenderTarget() {
  ID3D11Texture2D *pBackBuffer = nullptr;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
  if (pBackBuffer) {
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr,
                                         &g_mainRenderTargetView);
    pBackBuffer->Release();
  }
}

static void CleanupRenderTarget() {
  if (g_mainRenderTargetView) {
    g_mainRenderTargetView->Release();
    g_mainRenderTargetView = nullptr;
  }
}
