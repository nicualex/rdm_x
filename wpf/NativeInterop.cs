// ────────────────────────────────────────────────────────────────────────
// NativeInterop — P/Invoke wrapper for rdm_x_core.dll
// ────────────────────────────────────────────────────────────────────────
using System;
using System.Runtime.InteropServices;

namespace RDM_X;

public static class NativeInterop
{
    private const string Dll = "rdm_x_core.dll";

    // ── Driver Selection ─────────────────────────────────────────────────
    public const int DRIVER_ENTTEC   = 0;
    public const int DRIVER_PEPERONI = 1;

    [DllImport(Dll)] public static extern void RDX_SetDriver(int driverType);
    [DllImport(Dll)] public static extern int  RDX_GetDriver();
    [DllImport(Dll)] private static extern IntPtr RDX_GetDriverName(int driverType);
    public static string GetDriverName(int driverType)
        => Marshal.PtrToStringAnsi(RDX_GetDriverName(driverType)) ?? "Unknown";

    // ── Device ──────────────────────────────────────────────────────────
    [DllImport(Dll)] public static extern int  RDX_ListDevices();
    [DllImport(Dll)] public static extern bool RDX_Open(int deviceIndex);
    [DllImport(Dll)] public static extern void RDX_Close();
    [DllImport(Dll)] public static extern bool RDX_IsOpen();

    [DllImport(Dll)] private static extern IntPtr RDX_FirmwareString();
    public static string GetFirmwareString()
        => Marshal.PtrToStringAnsi(RDX_FirmwareString()) ?? "";

    [DllImport(Dll)] public static extern uint RDX_SerialNumber();

    // ── DMX ─────────────────────────────────────────────────────────────
    [DllImport(Dll)] public static extern bool RDX_SendDMX(byte[] data, int len);

    // ── Discovery ───────────────────────────────────────────────────────
    [DllImport(Dll)] public static extern int  RDX_Discover();
    [DllImport(Dll)] public static extern bool RDX_GetDiscoveredUID(int index, out ulong uid);

    // ── RDM Commands ────────────────────────────────────────────────────
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct RDX_Response
    {
        public int  Status;
        public int  NackReason;
        public int  DataLen;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 231)]
        public byte[] Data;
        public long LatencyUs;
        [MarshalAs(UnmanagedType.U1)]
        public bool ChecksumValid;
    }

    public const int STATUS_ACK          = 0;
    public const int STATUS_ACK_TIMER    = 1;
    public const int STATUS_NACK         = 2;
    public const int STATUS_TIMEOUT      = 3;
    public const int STATUS_CHECKSUM_ERR = 4;
    public const int STATUS_INVALID      = 5;

    [DllImport(Dll)]
    public static extern bool RDX_SendGET(ulong destUID, ushort pid,
                                          byte[]? paramData, int paramLen,
                                          out RDX_Response response);

    [DllImport(Dll)]
    public static extern bool RDX_SendSET(ulong destUID, ushort pid,
                                          byte[]? paramData, int paramLen,
                                          out RDX_Response response);

    // ── Parameters ──────────────────────────────────────────────────────
    [DllImport(Dll, CharSet = CharSet.Ansi)]
    public static extern int RDX_LoadParameters(string csvPath);

    [DllImport(Dll, CharSet = CharSet.Ansi)]
    public static extern bool RDX_GetParameterInfo(int index,
        out ushort pid,
        [MarshalAs(UnmanagedType.LPStr)] System.Text.StringBuilder name, int nameMax,
        [MarshalAs(UnmanagedType.LPStr)] System.Text.StringBuilder cmdClass, int cmdMax,
        out bool isMandatory);

    // ── Logging ─────────────────────────────────────────────────────────
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    public delegate void LogCallback(
        [MarshalAs(UnmanagedType.U1)] bool isTX,
        [MarshalAs(UnmanagedType.LPStr)] string hex,
        long timestampUs);

    [DllImport(Dll)]
    public static extern void RDX_SetLogCallback(LogCallback? cb);

    // Keep a reference to prevent GC collection of the delegate
    private static LogCallback? _pinnedCallback;
    public static void SetLogCallback(LogCallback? cb)
    {
        _pinnedCallback = cb;
        RDX_SetLogCallback(cb);
    }
}
