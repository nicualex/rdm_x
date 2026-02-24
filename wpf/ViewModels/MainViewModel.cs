using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace RDM_X.ViewModels;

// ── Data models ─────────────────────────────────────────────────────────
public class DeviceItem
{
    public int Index { get; set; }
    public string Label => $"Device {Index}";
    public override string ToString() => Label;
}

public class DriverType
{
    public int Id { get; set; }
    public string Name { get; set; } = "";
    public override string ToString() => Name;
}
public class DiscoveredUID
{
    public ulong UID { get; set; }
    public string Display => $"{(UID >> 32) & 0xFFFF:X4}:{UID & 0xFFFFFFFF:X8}";
    public override string ToString() => Display;
}

public partial class PidResult : ObservableObject
{
    [ObservableProperty] private ushort _pid;
    [ObservableProperty] private string _name = "";
    [ObservableProperty] private string _cmdClass = "";
    [ObservableProperty] private bool _isMandatory;
    [ObservableProperty] private int _status = -1;   // -1 = not queried
    [ObservableProperty] private string _value = "";
    [ObservableProperty] private long _latencyUs;
    public double LatencyMs => _latencyUs / 1000.0;
    partial void OnLatencyUsChanged(long value) => OnPropertyChanged(nameof(LatencyMs));
    [ObservableProperty] private bool _checksumValid;
    [ObservableProperty] private string _rawHex = "";
    [ObservableProperty] private int _nackReason;
    [ObservableProperty] private string _supportedStatus = "—"; // ✅ ❌ ⚠ —
}

public class LogEntry
{
    public bool IsTX { get; set; }
    public string Hex { get; set; } = "";
    public long TimestampUs { get; set; }
    public string TimeStr => $"{TimestampUs / 1000.0:F1}ms";
    public string Direction => IsTX ? "TX" : "RX";
}

// ── DMX Channel model ───────────────────────────────────────────────────
public partial class DmxChannel : ObservableObject
{
    public int Index { get; init; }
    public string Label => $"Ch {Index}";
    [ObservableProperty] private byte _level;
}

// ── Main ViewModel ──────────────────────────────────────────────────────
public partial class MainViewModel : ObservableObject
{
    // ── Driver selection ────────────────────────────────────────────────
    [ObservableProperty] private int _selectedDriverIndex;
    public List<DriverType> DriverTypes { get; } = new()
    {
        new DriverType { Id = NativeInterop.DRIVER_ENTTEC, Name = "Enttec USB DMX PRO" },
        new DriverType { Id = NativeInterop.DRIVER_PEPERONI, Name = "Peperoni Rodin 1" },
    };

    partial void OnSelectedDriverIndexChanged(int value)
    {
        if (value >= 0 && value < DriverTypes.Count)
        {
            NativeInterop.RDX_SetDriver(DriverTypes[value].Id);
            RefreshDevices();
        }
    }

    // ── Connection state ────────────────────────────────────────────────
    [ObservableProperty] private bool _isConnected;
    [ObservableProperty] private string _firmwareVersion = "";
    [ObservableProperty] private string _serialNumber = "";
    [ObservableProperty] private int _selectedDeviceIndex;
    [ObservableProperty] private bool _isBusy;
    [ObservableProperty] private string _busyText = "";

    // ── DMX ─────────────────────────────────────────────────────────────
    [ObservableProperty] private int _dmxLevel;
    [ObservableProperty] private bool _dmxBroadcast = true;
    [ObservableProperty] private int _dmxFrameCount;
    [ObservableProperty] private int _dmxRefreshRate = 25;  // Hz
    private DispatcherTimer? _dmxTimer;

    // ── DMX Effects (Auto-Fade / Chase) ───────────────────────────────
    [ObservableProperty] private int _fadeFrom;
    [ObservableProperty] private int _fadeTo = 255;
    [ObservableProperty] private double _fadeDuration = 2.0; // seconds
    [ObservableProperty] private double _chaseDwell = 0.2;   // seconds per channel
    [ObservableProperty] private bool _effectRunning;
    [ObservableProperty] private string _effectStatus = "";
    private CancellationTokenSource? _effectCts;

    // ── Discovery ───────────────────────────────────────────────────────────
    [ObservableProperty] private DiscoveredUID? _selectedUID;
    [ObservableProperty] private bool _hasSelectedFixture;

    partial void OnSelectedUIDChanged(DiscoveredUID? value)
        => HasSelectedFixture = IsConnected && value != null;

    partial void OnIsConnectedChanged(bool value)
        => HasSelectedFixture = value && SelectedUID != null;

    // ── PID Results ─────────────────────────────────────────────────────
    [ObservableProperty] private PidResult? _selectedPid;
    [ObservableProperty] private string _customPayloadHex = "";

    // ── Compliance scorecard ────────────────────────────────────────────
    [ObservableProperty] private int _passCount;
    [ObservableProperty] private int _warnCount;
    [ObservableProperty] private int _failCount;
    [ObservableProperty] private int _timeoutCount;

    // ── Device control (DMXster quick wins) ─────────────────────────────
    [ObservableProperty] private bool _identifyActive;
    [ObservableProperty] private string _dmxStartAddress = "—";
    [ObservableProperty] private string _deviceInfoText = "";
    [ObservableProperty] private string _dmxAddressInput = "—";
    [ObservableProperty] private string _dmxFootprint = "—";
    private HashSet<ushort> _supportedPids = new();

    // ── Collections ─────────────────────────────────────────────────────
    public ObservableCollection<DeviceItem> Devices { get; } = new();
    public ObservableCollection<DiscoveredUID> DiscoveredUIDs { get; } = new();
    public ObservableCollection<PidResult> PidResults { get; } = new();
    public ObservableCollection<LogEntry> LogEntries { get; } = new();
    public ObservableCollection<DmxChannel> DmxChannels { get; } = new();

    private readonly Dispatcher _dispatcher;

    public MainViewModel()
    {
        _dispatcher = Application.Current?.Dispatcher ?? Dispatcher.CurrentDispatcher;

        // Wire up native log callback
        NativeInterop.SetLogCallback((tx, hex, ts) =>
        {
            _dispatcher.BeginInvoke(() =>
            {
                LogEntries.Add(new LogEntry { IsTX = tx, Hex = hex, TimestampUs = ts });
                while (LogEntries.Count > 500) LogEntries.RemoveAt(0);
            });
        });

        // DMX timer (adjustable refresh rate)
        _dmxTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1000.0 / _dmxRefreshRate) };
        _dmxTimer.Tick += (_, _) => SendDMXFrame();
        _dmxTimer.Start();

        RefreshDevices();
        LoadParameters();
        RebuildDmxChannels(16);
    }

    private void RebuildDmxChannels(int count)
    {
        DmxChannels.Clear();
        for (int i = 1; i <= count; i++)
            DmxChannels.Add(new DmxChannel { Index = i });
    }

    partial void OnDmxFootprintChanged(string value)
    {
        if (int.TryParse(value, out int fp) && fp > 0 && fp <= 512)
            RebuildDmxChannels(fp);
    }

    // ── Device management ───────────────────────────────────────────────
    [RelayCommand]
    private void RefreshDevices()
    {
        Devices.Clear();
        int count = NativeInterop.RDX_ListDevices();
        for (int i = 0; i < count; i++)
            Devices.Add(new DeviceItem { Index = i });
    }

    [RelayCommand]
    private void Connect()
    {
        if (Devices.Count == 0)
        {
            System.Windows.MessageBox.Show("No devices found.", "Connect");
            return;
        }
        try
        {
            bool ok = NativeInterop.RDX_Open(SelectedDeviceIndex);
            if (ok)
            {
                IsConnected = true;
                FirmwareVersion = NativeInterop.GetFirmwareString();
                SerialNumber = $"{NativeInterop.RDX_SerialNumber():X8}";
            }
            else
            {
                System.Windows.MessageBox.Show(
                    $"RDX_Open({SelectedDeviceIndex}) failed after auto-reset attempt.\nTry unplugging and re-plugging the USB adapter.",
                    "Connect Failed");
            }
        }
        catch (Exception ex)
        {
            System.Windows.MessageBox.Show($"Connect error: {ex.Message}\n{ex.GetType().Name}", "Connect Error");
        }
    }

    [RelayCommand]
    private void Disconnect()
    {
        NativeInterop.RDX_Close();
        IsConnected = false;
        DiscoveredUIDs.Clear();
        SelectedUID = null;
        FirmwareVersion = "";
        SerialNumber = "";
    }

    // ── DMX ─────────────────────────────────────────────────────────────────────
    // Narrow flag: only true while a native RDM call is on the serial port.
    // Unlike IsBusy (which spans the entire async query), this allows DMX
    // frames to send between successive RDM queries in a batch loop.
    private volatile bool _rdmInFlight;

    private void SendDMXFrame()
    {
        if (!IsConnected || _rdmInFlight) return;

        byte[] dmx = new byte[513];
        dmx[0] = 0x00;

        // Broadcast: all 512 channels = master level (original behavior)
        if (DmxBroadcast)
        {
            for (int i = 1; i < 513; i++)
                dmx[i] = (byte)DmxLevel;
        }

        // Per-channel faders overlay at fixture's start address (direct values)
        int startAddr = 1;
        if (int.TryParse(DmxStartAddress, out int parsed) && parsed >= 1)
            startAddr = parsed;

        foreach (var ch in DmxChannels)
        {
            int slot = startAddr + ch.Index - 1;
            if (slot >= 1 && slot <= 512)
                dmx[slot] = (byte)Math.Max(dmx[slot], ch.Level);
        }

        if (NativeInterop.RDX_SendDMX(dmx, 513))
            DmxFrameCount++;
    }

    /// <summary>
    /// Runs a native RDM call on a background thread, setting _rdmInFlight
    /// during execution so the DMX timer skips frames only while the serial
    /// port is actually busy with RDM I/O.
    /// </summary>
    private async Task<T> RunRdmAsync<T>(Func<T> nativeCall)
    {
        return await Task.Run(() =>
        {
            _rdmInFlight = true;
            try { return nativeCall(); }
            finally { _rdmInFlight = false; }
        });
    }

    [RelayCommand]
    private void Blackout() => DmxLevel = 0;

    partial void OnDmxRefreshRateChanged(int value)
    {
        if (_dmxTimer != null && value >= 1 && value <= 44)
            _dmxTimer.Interval = TimeSpan.FromMilliseconds(1000.0 / value);
    }

    [RelayCommand]
    private void StopEffect()
    {
        _effectCts?.Cancel();
        _effectCts = null;
        EffectRunning = false;
        EffectStatus = "Stopped";
        DmxLevel = 0;
        foreach (var ch in DmxChannels) ch.Level = 0;
    }

    [RelayCommand]
    private async Task AutoFadeAsync()
    {
        if (!IsConnected) return;
        StopEffect();

        _effectCts = new CancellationTokenSource();
        var token = _effectCts.Token;
        EffectRunning = true;
        EffectStatus = "Auto-Fade running...";

        try
        {
            int from = Math.Clamp(FadeFrom, 0, 255);
            int to   = Math.Clamp(FadeTo, 0, 255);
            int steps = (int)(FadeDuration * DmxRefreshRate);
            if (steps < 1) steps = 1;

            // Continuous loop: fade up then down
            while (!token.IsCancellationRequested)
            {
                // Fade from → to
                for (int s = 0; s <= steps && !token.IsCancellationRequested; s++)
                {
                    float t = (float)s / steps;
                    int level = (int)(from + (to - from) * t);
                    foreach (var ch in DmxChannels) ch.Level = (byte)level;
                    EffectStatus = $"Fade ▲ {level}";
                    await Task.Delay(1000 / DmxRefreshRate, token);
                }
                // Fade to → from
                for (int s = 0; s <= steps && !token.IsCancellationRequested; s++)
                {
                    float t = (float)s / steps;
                    int level = (int)(to + (from - to) * t);
                    foreach (var ch in DmxChannels) ch.Level = (byte)level;
                    EffectStatus = $"Fade ▼ {level}";
                    await Task.Delay(1000 / DmxRefreshRate, token);
                }
            }
        }
        catch (OperationCanceledException) { }
        finally
        {
            foreach (var ch in DmxChannels) ch.Level = 0;
            EffectRunning = false;
            EffectStatus = "Fade stopped";
        }
    }

    [RelayCommand]
    private async Task ChaseAsync()
    {
        if (!IsConnected || DmxChannels.Count == 0) return;
        StopEffect();

        _effectCts = new CancellationTokenSource();
        var token = _effectCts.Token;
        EffectRunning = true;
        EffectStatus = "Chase running...";

        try
        {
            int dwellMs = Math.Max(50, (int)(ChaseDwell * 1000));

            while (!token.IsCancellationRequested)
            {
                for (int i = 0; i < DmxChannels.Count && !token.IsCancellationRequested; i++)
                {
                    // Turn off all, then light current channel
                    foreach (var ch in DmxChannels) ch.Level = 0;
                    DmxChannels[i].Level = 255;
                    EffectStatus = $"Chase: Ch {DmxChannels[i].Index}";
                    await Task.Delay(dwellMs, token);
                }
            }
        }
        catch (OperationCanceledException) { }
        finally
        {
            foreach (var ch in DmxChannels) ch.Level = 0;
            EffectRunning = false;
            EffectStatus = "Chase stopped";
        }
    }

    // ── Identify toggle (PID 0x1000) ────────────────────────────────────
    [RelayCommand]
    private async Task ToggleIdentifyAsync()
    {
        if (SelectedUID == null || !IsConnected || IsBusy) return;
        IsBusy = true;
        var destUID = SelectedUID.UID;
        bool newState = !IdentifyActive;
        byte[] payload = new byte[] { (byte)(newState ? 0x01 : 0x00) };

        BusyText = newState ? "Identify ON..." : "Identify OFF...";
        _dmxTimer?.Stop();
        try
        {
            var result = await RunRdmAsync(() =>
            {
                NativeInterop.RDX_SendSET(destUID, 0x1000, payload, 1, out var resp);
                return resp;
            });

            if (result.Status == NativeInterop.STATUS_ACK)
                IdentifyActive = newState;
        }
        finally
        {
            _dmxTimer?.Start();
            IsBusy = false;
            BusyText = "";
        }
    }

    // ── DMX Start Address (PID 0x00F0) ──────────────────────────────────
    [RelayCommand]
    private async Task GetDmxAddressAsync()
    {
        if (SelectedUID == null || !IsConnected || IsBusy) return;
        IsBusy = true;
        BusyText = "Getting DMX address...";
        _dmxTimer?.Stop();
        try
        {
            var destUID = SelectedUID.UID;

            var result = await RunRdmAsync(() =>
            {
                NativeInterop.RDX_SendGET(destUID, 0x00F0, null, 0, out var resp);
                return resp;
            });

            if (result.Status == NativeInterop.STATUS_ACK && result.DataLen >= 2 && result.Data != null)
            {
                int addr = (result.Data[0] << 8) | result.Data[1];
                DmxStartAddress = addr.ToString();
                DmxAddressInput = addr.ToString();
            }
        }
        finally
        {
            _dmxTimer?.Start();
            IsBusy = false;
            BusyText = "";
        }
    }

    [RelayCommand]
    private async Task SetDmxAddressAsync()
    {
        if (SelectedUID == null || !IsConnected || IsBusy) return;
        if (!int.TryParse(DmxAddressInput, out int addr) || addr < 1 || addr > 512) return;

        IsBusy = true;
        BusyText = $"Setting DMX address to {addr}...";
        _dmxTimer?.Stop();
        try
        {
            var destUID = SelectedUID.UID;
            byte[] payload = new byte[] { (byte)(addr >> 8), (byte)(addr & 0xFF) };

            var result = await RunRdmAsync(() =>
            {
                NativeInterop.RDX_SendSET(destUID, 0x00F0, payload, 2, out var resp);
                return resp;
            });

            if (result.Status == NativeInterop.STATUS_ACK)
                DmxStartAddress = addr.ToString();
        }
        finally
        {
            _dmxTimer?.Start();
            IsBusy = false;
            BusyText = "";
        }
    }

    // ── Device Info query (PID 0x0060) ──────────────────────────────────
    [RelayCommand]
    private async Task GetDeviceInfoAsync()
    {
        if (SelectedUID == null || !IsConnected || IsBusy) return;
        IsBusy = true;
        BusyText = "Getting Device Info...";
        _dmxTimer?.Stop();
        try
        {
            var destUID = SelectedUID.UID;

            var result = await RunRdmAsync(() =>
            {
                NativeInterop.RDX_SendGET(destUID, 0x0060, null, 0, out var resp);
                return resp;
            });

            if (result.Status == NativeInterop.STATUS_ACK && result.DataLen >= 19 && result.Data != null)
                DeviceInfoText = DecodeDeviceInfo(result.Data, result.DataLen);
            else
                DeviceInfoText = "Failed to read Device Info";
        }
        finally
        {
            _dmxTimer?.Start();
            IsBusy = false;
            BusyText = "";
        }
    }

    // ── Discovery ───────────────────────────────────────────────────────
    [RelayCommand]
    private async Task DiscoverAsync()
    {
        if (!IsConnected || IsBusy) return;
        IsBusy = true;
        BusyText = "Discovering RDM devices...";
        _dmxTimer?.Stop();
        try
        {
            DiscoveredUIDs.Clear();
            SelectedUID = null;

            int count = await RunRdmAsync(() => NativeInterop.RDX_Discover());

            for (int i = 0; i < count; i++)
            {
                if (NativeInterop.RDX_GetDiscoveredUID(i, out ulong uid))
                {
                    if (!DiscoveredUIDs.Any(d => d.UID == uid))
                        DiscoveredUIDs.Add(new DiscoveredUID { UID = uid });
                }
            }
        }
        finally
        {
            _dmxTimer?.Start();
            IsBusy = false;
            BusyText = "";
        }
    }

    // ── PID loading ─────────────────────────────────────────────────────
    private void LoadParameters()
    {
        string csvPath = System.IO.Path.Combine(AppContext.BaseDirectory, "Vaya_RDM_map.csv");
        int count = NativeInterop.RDX_LoadParameters(csvPath);
        for (int i = 0; i < count; i++)
        {
            var name = new StringBuilder(256);
            var cls  = new StringBuilder(64);
            if (NativeInterop.RDX_GetParameterInfo(i, out ushort pid,
                name, 256, cls, 64, out bool mandatory))
            {
                PidResults.Add(new PidResult
                {
                    Pid = pid,
                    Name = name.ToString(),
                    CmdClass = cls.ToString(),
                    IsMandatory = mandatory
                });
            }
        }
    }

    // ── Single PID query ────────────────────────────────────────────────
    [RelayCommand]
    private async Task QuerySelectedPidAsync()
    {
        if (SelectedUID == null || SelectedPid == null || !IsConnected || IsBusy) return;

        IsBusy = true;
        BusyText = $"Querying PID 0x{SelectedPid.Pid:X4}...";
        _dmxTimer?.Stop();
        try
        {
            byte[]? payload = ParseHexPayload(CustomPayloadHex);
            var pid = SelectedPid;
            var destUID = SelectedUID.UID;

            var result = await RunRdmAsync(() =>
            {
                NativeInterop.RDX_SendGET(destUID, pid.Pid,
                    payload, payload?.Length ?? 0, out var resp);
                return resp;
            });

            ApplyResult(pid, result);
        }
        finally
        {
            _dmxTimer?.Start();
            IsBusy = false;
            BusyText = "";
        }
    }

    // ── Batch query: all GETs ───────────────────────────────────────────
    [RelayCommand]
    private async Task QueryAllPidsAsync()
    {
        if (SelectedUID == null || !IsConnected || IsBusy) return;

        IsBusy = true;
        // Stop DMX output entirely during RDM batch to prevent widget collisions
        _dmxTimer?.Stop();
        try
        {
            var destUID = SelectedUID.UID;
            var getItems = PidResults.Where(p =>
                p.CmdClass.Contains("GET", StringComparison.OrdinalIgnoreCase)).ToList();

            for (int i = 0; i < getItems.Count; i++)
            {
                var pid = getItems[i];
                BusyText = $"Querying {i + 1}/{getItems.Count}: 0x{pid.Pid:X4} {pid.Name}";

                var result = await RunRdmAsync(() =>
                {
                    NativeInterop.RDX_SendGET(destUID, pid.Pid,
                        null, 0, out var resp);
                    return resp;
                });

                ApplyResult(pid, result);
            }

            UpdateScorecard();
        }
        finally
        {
            _dmxTimer?.Start();
            IsBusy = false;
            BusyText = "";
        }
    }

    // ── Send SET ────────────────────────────────────────────────────────
    [RelayCommand]
    private async Task SendSetAsync()
    {
        if (SelectedUID == null || SelectedPid == null || !IsConnected || IsBusy) return;

        IsBusy = true;
        BusyText = $"SET PID 0x{SelectedPid.Pid:X4}...";
        _dmxTimer?.Stop();
        try
        {
            byte[]? payload = ParseHexPayload(CustomPayloadHex);
            var pid = SelectedPid;
            var destUID = SelectedUID.UID;

            var result = await RunRdmAsync(() =>
            {
                NativeInterop.RDX_SendSET(destUID, pid.Pid,
                    payload, payload?.Length ?? 0, out var resp);
                return resp;
            });

            ApplyResult(pid, result);
        }
        finally
        {
            _dmxTimer?.Start();
            IsBusy = false;
            BusyText = "";
        }
    }

    // ── SUPPORTED_PARAMETERS cross-check (PID 0x0050) ───────────────────
    [RelayCommand]
    private async Task QuerySupportedPidsAsync()
    {
        if (SelectedUID == null || !IsConnected || IsBusy) return;

        IsBusy = true;
        BusyText = "Querying SUPPORTED_PARAMETERS...";
        _dmxTimer?.Stop();
        try
        {
            var destUID = SelectedUID.UID;

            var result = await RunRdmAsync(() =>
            {
                NativeInterop.RDX_SendGET(destUID, 0x0050,
                    null, 0, out var resp);
                return resp;
            });

            _supportedPids.Clear();
            if (result.Status == NativeInterop.STATUS_ACK && result.DataLen > 0 && result.Data != null)
            {
                int count = result.DataLen / 2;
                for (int i = 0; i < count; i++)
                {
                    ushort pid = (ushort)((result.Data[i * 2] << 8) | result.Data[i * 2 + 1]);
                    _supportedPids.Add(pid);
                }
            }

            // Also add mandatory PIDs that are always supported (per E1.20)
            _supportedPids.Add(0x0050); // SUPPORTED_PARAMETERS itself
            _supportedPids.Add(0x0060); // DEVICE_INFO
            _supportedPids.Add(0x1000); // IDENTIFY_DEVICE

            // Update SupportedStatus on each PidResult row
            foreach (var p in PidResults)
            {
                if (_supportedPids.Contains(p.Pid))
                    p.SupportedStatus = "✅";
                else if (p.IsMandatory)
                    p.SupportedStatus = "⚠ Missing";
                else
                    p.SupportedStatus = "❌";
            }

            // Also apply to the PID 0x0050 row itself
            var pidRow = PidResults.FirstOrDefault(p => p.Pid == 0x0050);
            if (pidRow != null)
                ApplyResult(pidRow, result);

            UpdateScorecard();
        }
        finally
        {
            _dmxTimer?.Start();
            IsBusy = false;
            BusyText = $"{_supportedPids.Count} supported PIDs";
        }
    }

    // ── CSV Export ───────────────────────────────────────────────────────
    [RelayCommand]
    private void ExportCsv()
    {
        var dlg = new Microsoft.Win32.SaveFileDialog
        {
            Filter = "CSV files (*.csv)|*.csv",
            FileName = $"RDM_Results_{DateTime.Now:yyyyMMdd_HHmmss}.csv",
            DefaultExt = ".csv"
        };
        if (dlg.ShowDialog() != true) return;

        var sb = new StringBuilder();
        sb.AppendLine("PID,Name,Mandatory,Supported,Status,Latency_us,Value,RawHex");
        foreach (var p in PidResults)
        {
            string statusStr = p.Status switch
            {
                NativeInterop.STATUS_ACK => "ACK",
                NativeInterop.STATUS_ACK_TIMER => "ACK_TIMER",
                NativeInterop.STATUS_NACK => "NACK",
                NativeInterop.STATUS_TIMEOUT => "TIMEOUT",
                NativeInterop.STATUS_CHECKSUM_ERR => "CHECKSUM_ERR",
                NativeInterop.STATUS_INVALID => "INVALID",
                _ => "NOT_QUERIED"
            };
            string valEsc = p.Value.Replace("\"", "\"\"");
            sb.AppendLine($"0x{p.Pid:X4},\"{p.Name}\",{p.IsMandatory},{p.SupportedStatus},{statusStr},{p.LatencyUs},\"{valEsc}\",\"{p.RawHex}\"");
        }
        System.IO.File.WriteAllText(dlg.FileName, sb.ToString());
        BusyText = $"Exported to {System.IO.Path.GetFileName(dlg.FileName)}";
    }

    // ── DMX Fader utilities ─────────────────────────────────────────────
    [RelayCommand]
    private void DmxAllZero()
    {
        DmxLevel = 0;
        foreach (var ch in DmxChannels) ch.Level = 0;
    }

    [RelayCommand]
    private void DmxAllFull()
    {
        DmxLevel = 0;
        foreach (var ch in DmxChannels) ch.Level = 255;
    }

    // ── Log ─────────────────────────────────────────────────────────────
    [RelayCommand]
    private void ClearLog() => LogEntries.Clear();

    [RelayCommand]
    private void CopyLog()
    {
        try
        {
            var snapshot = LogEntries.ToArray();
            var sb = new System.Text.StringBuilder();
            foreach (var entry in snapshot)
                sb.AppendLine($"{entry.TimeStr} {entry.Direction} {entry.Hex}");
            if (sb.Length > 0)
                System.Windows.Clipboard.SetDataObject(sb.ToString(), true);
        }
        catch { /* clipboard busy or locked */ }
    }

    // ── Helpers ─────────────────────────────────────────────────────────
    private void ApplyResult(PidResult pid, NativeInterop.RDX_Response resp)
    {
        _dispatcher.Invoke(() =>
        {
            pid.Status = resp.Status;
            pid.LatencyUs = resp.LatencyUs;
            pid.ChecksumValid = resp.ChecksumValid;
            pid.NackReason = resp.NackReason;

            if (resp.DataLen > 0 && resp.Data != null)
            {
                var hex = new StringBuilder(resp.DataLen * 3);
                for (int i = 0; i < resp.DataLen; i++)
                    hex.Append($"{resp.Data[i]:X2} ");
                pid.RawHex = hex.ToString().TrimEnd();

                // Auto-decode known PIDs
                pid.Value = pid.Pid switch
                {
                    // ── Standard E1.20 PIDs ──
                    0x0050 => DecodeSupported(resp.Data, resp.DataLen),
                    0x0051 => DecodeParamDescription(resp.Data, resp.DataLen),
                    0x0060 => DecodeDeviceInfo(resp.Data, resp.DataLen),
                    0x0080 => DecodeAscii(resp.Data, resp.DataLen), // Model description
                    0x0081 => DecodeAscii(resp.Data, resp.DataLen), // Manufacturer label
                    0x0082 => DecodeAscii(resp.Data, resp.DataLen), // Device label
                    0x00C0 => DecodeAscii(resp.Data, resp.DataLen), // Software version label
                    0x00E0 when resp.DataLen >= 2 => $"Personality {resp.Data[0]}/{resp.Data[1]}",
                    0x00E1 => DecodePersonalityDesc(resp.Data, resp.DataLen),
                    0x00F0 when resp.DataLen >= 2 => $"Address: {(resp.Data[0] << 8) | resp.Data[1]}",
                    0x0200 => DecodeSensorDef(resp.Data, resp.DataLen),
                    0x0201 => DecodeSensorValue(resp.Data, resp.DataLen),
                    0x0400 when resp.DataLen >= 4 => $"{DecodeUInt32(resp.Data)} hours",
                    0x1000 when resp.DataLen >= 1 => resp.Data[0] != 0 ? "Identify ON" : "Identify OFF",

                    // ── CK/Vaya Manufacturer PIDs ──
                    0x8060 when resp.DataLen >= 6 => $"SN: {resp.Data[0]:X2}{resp.Data[1]:X2}:{resp.Data[2]:X2}{resp.Data[3]:X2}{resp.Data[4]:X2}{resp.Data[5]:X2}",
                    0x8070 when resp.DataLen >= 4 => $"Model: 0x{(resp.Data[0] << 8) | resp.Data[1]:X4} | Cat: 0x{(resp.Data[2] << 8) | resp.Data[3]:X4}",
                    0x8072 => DecodeAscii(resp.Data, resp.DataLen), // SKU
                    0x8090 when resp.DataLen >= 7 => $"{(resp.Data[0] << 8) | resp.Data[1]:D4}-{resp.Data[2]:D2}-{resp.Data[3]:D2} {resp.Data[4]:D2}:{resp.Data[5]:D2}:{resp.Data[6]:D2}",
                    0x80C0 when resp.DataLen >= 4 => $"SFT-{(resp.Data[0] << 8) | resp.Data[1]:D6}-{resp.Data[2]:D2} v{resp.Data[3]}",
                    0x8208 when resp.DataLen >= 5 => $"Max temp: {(sbyte)resp.Data[0]}°C @ {DecodeUInt32(resp.Data, 1)}s",
                    0x8400 when resp.DataLen >= 4 => FormatSeconds(DecodeUInt32(resp.Data)),
                    0x8600 when resp.DataLen >= 1 => resp.Data[0] == 0 ? "16-bit (high-res)" : "8-bit (standard)",
                    0x8610 when resp.DataLen >= 2 => $"Ch0 startup: {(resp.Data[0] << 8) | resp.Data[1]}",
                    0x8611 when resp.DataLen >= 2 => $"Ch1 startup: {(resp.Data[0] << 8) | resp.Data[1]}",
                    0x8612 when resp.DataLen >= 2 => $"Ch2 startup: {(resp.Data[0] << 8) | resp.Data[1]}",
                    0x8613 when resp.DataLen >= 2 => $"Ch3 startup: {(resp.Data[0] << 8) | resp.Data[1]}",
                    0x9001 when resp.DataLen >= 4 => DecodeAscii(resp.Data, resp.DataLen) switch
                    {
                        "OPEN" => "🔓 Unlocked (OPEN)",
                        "LOCK" => "🔒 Locked (LOCK)",
                        var s => s
                    },
                    0x9F00 when resp.DataLen >= 5 => DecodeChannelMap(resp.Data),
                    0x9F02 when resp.DataLen >= 1 => $"Chemistry: 0x{resp.Data[0]:X2}",
                    0x9F0A when resp.DataLen >= 1 => $"Shutdown at: {(sbyte)resp.Data[0]}°C",
                    0x9F0C when resp.DataLen >= 1 => $"Resume at: {(sbyte)resp.Data[0]}°C",

                    // Fallback: hex + ASCII like classic hex editors
                    _ => HexAsciiDump(resp.Data, resp.DataLen)
                };
            }
            else if (resp.Status == NativeInterop.STATUS_ACK)
            {
                pid.Value = "ACK ✓ (empty response)";
            }
            else if (resp.Status == NativeInterop.STATUS_NACK)
            {
                pid.Value = $"NACK — {DecodeNackReason(resp.NackReason)}";
            }
            else if (resp.Status == NativeInterop.STATUS_TIMEOUT)
            {
                pid.Value = "No response";
            }
            else
            {
                pid.Value = "";
            }
        });
    }

    // ── PID helper decoders ─────────────────────────────────────────────
    private static string HexAsciiDump(byte[] d, int len)
    {
        var hex = new StringBuilder(len * 3);
        var ascii = new StringBuilder(len);
        for (int i = 0; i < len; i++)
        {
            hex.Append($"{d[i]:X2} ");
            ascii.Append(d[i] >= 0x20 && d[i] <= 0x7E ? (char)d[i] : '.');
        }
        return $"{hex.ToString().TrimEnd()} | {ascii}";
    }

    private static uint DecodeUInt32(byte[] d, int offset = 0) =>
        (uint)((d[offset] << 24) | (d[offset + 1] << 16) | (d[offset + 2] << 8) | d[offset + 3]);

    private static string FormatSeconds(uint secs)
    {
        var ts = TimeSpan.FromSeconds(secs);
        return ts.TotalDays >= 1 ? $"{ts.Days}d {ts.Hours}h {ts.Minutes}m ({secs}s)"
                                 : $"{ts.Hours}h {ts.Minutes}m {ts.Seconds}s ({secs}s)";
    }

    private static string DecodeChannelMap(byte[] d)
    {
        var sb = new StringBuilder("Map: ");
        for (int i = 0; i < 5; i++)
        {
            if (d[i] == 0xFF) sb.Append($"Ch{i}=OFF ");
            else sb.Append($"Ch{i}→{d[i]} ");
        }
        return sb.ToString().TrimEnd();
    }

    private static string DecodeParamDescription(byte[] d, int len)
    {
        if (len < 20) return "Incomplete";
        ushort pid = (ushort)((d[0] << 8) | d[1]);
        int pdlSize = d[2];
        int dataType = d[3];
        int cmdClass = d[4];
        string desc = len > 20 ? Encoding.ASCII.GetString(d, 20, Math.Min(len - 20, 32)).TrimEnd('\0') : "";
        return $"PID 0x{pid:X4}: pdl={pdlSize} type={dataType} cc={cmdClass} \"{desc}\"";
    }

    private static string DecodePersonalityDesc(byte[] d, int len)
    {
        if (len < 3) return "Incomplete";
        int persNum = d[0];
        int slots = (d[1] << 8) | d[2];
        string desc = len > 3 ? Encoding.ASCII.GetString(d, 3, Math.Min(len - 3, 32)).TrimEnd('\0') : "";
        return $"Pers {persNum}: {slots} slots \"{desc}\"";
    }

    private static string DecodeSensorDef(byte[] d, int len)
    {
        if (len < 13) return "Incomplete";
        int sensorNum = d[0];
        int type = d[1];
        int unit = d[2];
        string desc = len > 13 ? Encoding.ASCII.GetString(d, 13, Math.Min(len - 13, 32)).TrimEnd('\0') : "";
        return $"Sensor {sensorNum}: type={type} unit={unit} \"{desc}\"";
    }

    private static string DecodeSensorValue(byte[] d, int len)
    {
        if (len < 7) return "Incomplete";
        int sensorNum = d[0];
        short present = (short)((d[1] << 8) | d[2]);
        short lowest = (short)((d[3] << 8) | d[4]);
        short highest = (short)((d[5] << 8) | d[6]);
        return $"Sensor {sensorNum}: current={present} low={lowest} high={highest}";
    }

    // ── NACK reason decoder (E1.20 Table A-17) ─────────────────────────
    private static string DecodeNackReason(int reason) => reason switch
    {
        0x0000 => "NR_UNKNOWN_PID",
        0x0001 => "NR_FORMAT_ERROR",
        0x0002 => "NR_HARDWARE_FAULT",
        0x0003 => "NR_PROXY_REJECT",
        0x0004 => "NR_WRITE_PROTECT",
        0x0005 => "NR_UNSUPPORTED_COMMAND_CLASS",
        0x0006 => "NR_DATA_OUT_OF_RANGE",
        0x0007 => "NR_BUFFER_FULL",
        0x0008 => "NR_PACKET_SIZE_UNSUPPORTED",
        0x0009 => "NR_SUB_DEVICE_OUT_OF_RANGE",
        0x000A => "NR_PROXY_BUFFER_FULL",
        _ => $"Unknown (0x{reason:X4})"
    };

    // ── Device Info decoder (PID 0x0060) ────────────────────────────────
    private string DecodeDeviceInfo(byte[] d, int len)
    {
        if (len < 19) return "Incomplete data";

        int protoMaj = d[0], protoMin = d[1];
        int model = (d[2] << 8) | d[3];
        int category = (d[4] << 8) | d[5];
        int swMaj = d[6], swMin = d[7], swBuild = (d[8] << 8) | d[9];
        int footprint = (d[10] << 8) | d[11];
        int curPers = d[12], numPers = d[13];
        int dmxAddr = (d[14] << 8) | d[15];
        int subCount = (d[16] << 8) | d[17];
        int sensorCount = d[18];

        // Auto-update fader count and address
        DmxFootprint = $"{footprint}";
        DmxStartAddress = $"{dmxAddr}";
        DmxAddressInput = $"{dmxAddr}";

        string catName = DecodeCategory(category);

        return $"Model: 0x{model:X4} | {catName}\n" +
               $"Protocol: {protoMaj}.{protoMin} | SW: {swMaj}.{swMin}.{swBuild}\n" +
               $"Footprint: {footprint}ch | Pers: {curPers}/{numPers}\n" +
               $"DMX Addr: {dmxAddr} | Sub-devs: {subCount} | Sensors: {sensorCount}";
    }

    private static string DecodeCategory(int cat) => cat switch
    {
        0x0100 => "Fixture",
        0x0101 => "Fixture Fixed",
        0x0102 => "Fixture Moving Yoke",
        0x0103 => "Fixture Moving Mirror",
        0x0200 => "Fixture Accessory",
        0x0301 => "Projector Fixed",
        0x0400 => "Atmospheric",
        0x0401 => "Atmospheric Hazer",
        0x0500 => "Dimmer",
        0x0600 => "Power",
        0x0700 => "Scenic",
        0x7FFF => "Other",
        _ => $"0x{cat:X4}"
    };

    // ── SUPPORTED_PARAMETERS decoder ────────────────────────────────────
    private static string DecodeSupportedPids(byte[] d, int len)
    {
        var sb = new StringBuilder();
        int count = len / 2;
        for (int i = 0; i < count; i++)
        {
            ushort pid = (ushort)((d[i * 2] << 8) | d[i * 2 + 1]);
            if (sb.Length > 0) sb.Append(", ");
            sb.Append($"0x{pid:X4}");
        }
        return $"{count} PIDs: {sb}";
    }

    private static string DecodeSupported(byte[] d, int len) => DecodeSupportedPids(d, len);
    private static string DecodeAscii(byte[] d, int len)
    {
        int end = Array.IndexOf(d, (byte)0, 0, len);
        if (end < 0) end = len;
        return Encoding.ASCII.GetString(d, 0, end);
    }

    private void UpdateScorecard()
    {
        PassCount = PidResults.Count(p => p.Status == NativeInterop.STATUS_ACK);
        WarnCount = PidResults.Count(p => p.Status == NativeInterop.STATUS_ACK_TIMER);
        FailCount = PidResults.Count(p => p.Status == NativeInterop.STATUS_NACK
                                       || p.Status == NativeInterop.STATUS_CHECKSUM_ERR
                                       || p.Status == NativeInterop.STATUS_INVALID);
        TimeoutCount = PidResults.Count(p => p.Status == NativeInterop.STATUS_TIMEOUT);
    }

    private static byte[]? ParseHexPayload(string hex)
    {
        if (string.IsNullOrWhiteSpace(hex)) return null;
        var parts = hex.Split(new[] { ' ', ',', '-' }, StringSplitOptions.RemoveEmptyEntries);
        var bytes = new byte[parts.Length];
        for (int i = 0; i < parts.Length; i++)
        {
            if (!byte.TryParse(parts[i], NumberStyles.HexNumber, null, out bytes[i]))
                return null;
        }
        return bytes;
    }

    // ── Flicker Finder (software-level timing jitter detection) ────────
    [ObservableProperty] private double _flickerDuration = 10.0; // seconds
    [ObservableProperty] private bool _flickerRunning;
    [ObservableProperty] private string _flickerResult = "";

    [RelayCommand]
    private async Task FlickerFinderAsync()
    {
        if (!IsConnected) return;
        _effectCts?.Cancel();
        _effectCts = new CancellationTokenSource();
        var token = _effectCts.Token;
        FlickerRunning = true;
        FlickerResult = "Analyzing...";

        try
        {
            int totalFrames = 0, failedSends = 0;
            double maxJitterMs = 0, sumJitterMs = 0;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            long lastTickUs = sw.ElapsedTicks;
            double tickFreq = System.Diagnostics.Stopwatch.Frequency / 1000.0;
            int durationMs = (int)(FlickerDuration * 1000);

            while (sw.ElapsedMilliseconds < durationMs && !token.IsCancellationRequested)
            {
                long now = sw.ElapsedTicks;
                double deltaMs = (now - lastTickUs) / tickFreq;
                lastTickUs = now;

                byte[] dmx = new byte[513];
                dmx[0] = 0x00;
                for (int i = 1; i < 513; i++) dmx[i] = (byte)DmxLevel;

                bool ok = NativeInterop.RDX_SendDMX(dmx, 513);
                totalFrames++;
                if (!ok) failedSends++;

                if (totalFrames > 1) // skip first delta (init)
                {
                    double expectedMs = 1000.0 / DmxRefreshRate;
                    double jitter = Math.Abs(deltaMs - expectedMs);
                    if (jitter > maxJitterMs) maxJitterMs = jitter;
                    sumJitterMs += jitter;
                }

                FlickerResult = $"Frame {totalFrames} | Fails: {failedSends} | MaxJitter: {maxJitterMs:F1}ms";
                await Task.Delay(1000 / DmxRefreshRate, token);
            }

            double avgJitter = totalFrames > 1 ? sumJitterMs / (totalFrames - 1) : 0;
            FlickerResult = $"✅ Done: {totalFrames} frames, {failedSends} failures\n" +
                            $"Avg jitter: {avgJitter:F1}ms | Max jitter: {maxJitterMs:F1}ms";
            if (failedSends > 0 || maxJitterMs > 10)
                FlickerResult = "⚠ " + FlickerResult.Substring(2);
        }
        catch (OperationCanceledException)
        {
            FlickerResult = "Stopped";
        }
        finally
        {
            FlickerRunning = false;
        }
    }

    // ── RDM Stress Test ─────────────────────────────────────────────────
    [ObservableProperty] private int _stressIterations = 100;
    [ObservableProperty] private bool _rdmStressRunning;
    [ObservableProperty] private string _rdmStressResult = "";

    [RelayCommand]
    private async Task RdmStressTestAsync()
    {
        if (!IsConnected || SelectedUID == null || SelectedPid == null) return;
        _effectCts?.Cancel();
        _effectCts = new CancellationTokenSource();
        var token = _effectCts.Token;
        RdmStressRunning = true;
        RdmStressResult = "Running...";
        // Stop DMX output entirely during stress test to prevent widget collisions
        _dmxTimer?.Stop();

        var destUID = SelectedUID.UID;
        ushort pid = SelectedPid.Pid;
        string pidName = SelectedPid.Name;

        try
        {
            int success = 0, nack = 0, timeout = 0, errors = 0;
            long minUs = long.MaxValue, maxUs = 0, sumUs = 0;

            for (int i = 0; i < StressIterations && !token.IsCancellationRequested; i++)
            {
                NativeInterop.RDX_Response response = default;
                await RunRdmAsync(() =>
                {
                    NativeInterop.RDX_SendGET(destUID, pid, null, 0, out response);
                    return 0; // dummy return for RunRdmAsync<T>
                });

                switch (response.Status)
                {
                    case NativeInterop.STATUS_ACK:
                        success++;
                        if (response.LatencyUs < minUs) minUs = response.LatencyUs;
                        if (response.LatencyUs > maxUs) maxUs = response.LatencyUs;
                        sumUs += response.LatencyUs;
                        break;
                    case NativeInterop.STATUS_NACK:
                        nack++;
                        break;
                    case NativeInterop.STATUS_TIMEOUT:
                        timeout++;
                        break;
                    default:
                        errors++;
                        break;
                }

                if (i % 10 == 0)
                {
                    RdmStressResult = $"Progress: {i + 1}/{StressIterations} | ACK: {success} | NACK: {nack} | TO: {timeout}";
                    await Task.Delay(1); // yield to UI
                }
            }

            double avgUs = success > 0 ? (double)sumUs / success : 0;
            double successPct = (double)success / StressIterations * 100;
            RdmStressResult = $"PID {pidName} (0x{pid:X4}): {StressIterations} iterations\n" +
                              $"✅ ACK: {success} ({successPct:F1}%) | ❌ NACK: {nack} | ⏱ TO: {timeout} | ⚠ Err: {errors}\n" +
                              $"Latency — Avg: {avgUs:F0}µs | Min: {(minUs == long.MaxValue ? 0 : minUs)}µs | Max: {maxUs}µs";
        }
        catch (OperationCanceledException)
        {
            RdmStressResult = "Stopped";
        }
        finally
        {
            _dmxTimer?.Start();
            RdmStressRunning = false;
        }
    }

    // ── DMX Stress Test ─────────────────────────────────────────────────
    [ObservableProperty] private double _dmxStressDuration = 5.0; // seconds
    [ObservableProperty] private bool _dmxStressRunning;
    [ObservableProperty] private string _dmxStressResult = "";

    [RelayCommand]
    private async Task DmxStressTestAsync()
    {
        if (!IsConnected) return;
        _effectCts?.Cancel();
        _effectCts = new CancellationTokenSource();
        var token = _effectCts.Token;
        DmxStressRunning = true;
        DmxStressResult = "Running...";

        try
        {
            int totalFrames = 0, failures = 0;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            int durationMs = (int)(DmxStressDuration * 1000);

            while (sw.ElapsedMilliseconds < durationMs && !token.IsCancellationRequested)
            {
                byte[] dmx = new byte[513];
                dmx[0] = 0x00;
                byte val = (totalFrames % 2 == 0) ? (byte)255 : (byte)0;
                for (int i = 1; i < 513; i++) dmx[i] = val;

                bool ok = NativeInterop.RDX_SendDMX(dmx, 513);
                totalFrames++;
                if (!ok) failures++;

                if (totalFrames % 50 == 0)
                {
                    double fps = totalFrames / (sw.ElapsedMilliseconds / 1000.0);
                    DmxStressResult = $"Frames: {totalFrames} | {fps:F0} fps | Fails: {failures}";
                    await Task.Delay(1); // yield to UI
                }
            }

            double elapsed = sw.ElapsedMilliseconds / 1000.0;
            double avgFps = totalFrames / elapsed;
            DmxStressResult = $"✅ Done: {totalFrames} frames in {elapsed:F1}s ({avgFps:F0} fps)\n" +
                              $"Failures: {failures} | Pattern: Alternating 0/255";
        }
        catch (OperationCanceledException)
        {
            DmxStressResult = "Stopped";
        }
        finally
        {
            DmxStressRunning = false;
        }
    }
}
