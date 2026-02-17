using System.Windows;

namespace RDM_X;

public partial class App : Application
{
    private static bool _cleaned;

    public App()
    {
        // Catch process termination (e.g. Ctrl+C in dotnet run, console close)
        AppDomain.CurrentDomain.ProcessExit += (_, _) => CleanupFtdi();
        try { Console.CancelKeyPress += (_, _) => CleanupFtdi(); } catch { }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        CleanupFtdi();
        base.OnExit(e);
    }

    private static void CleanupFtdi()
    {
        if (_cleaned) return;
        _cleaned = true;
        try { NativeInterop.RDX_Close(); } catch { }
    }
}
