using System.IO;
using System.Threading.Tasks;
using System.Windows;

namespace RDM_X;

public partial class App : Application
{
    private static bool _cleaned;

    public App()
    {
        // Global exception handling
        DispatcherUnhandledException += (s, e) => LogException(e.Exception, "Dispatcher");
        AppDomain.CurrentDomain.UnhandledException += (s, e) => LogException(e.ExceptionObject as Exception, "AppDomain");
        TaskScheduler.UnobservedTaskException += (s, e) => LogException(e.Exception, "TaskScheduler");

        try
        {
            // Catch process termination (e.g. Ctrl+C in dotnet run, console close)
            AppDomain.CurrentDomain.ProcessExit += (_, _) => CleanupFtdi();
            Console.CancelKeyPress += (_, _) => CleanupFtdi();
        }
        catch { }
    }

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        try
        {
            // Dummy call to force native DLL loading before XAML parsing
            NativeInterop.RDX_GetDriver();
        }
        catch (DllNotFoundException ex)
        {
            MessageBox.Show(
                "RDM_X requires hardware driver libraries to run, but they could not be found.\n\n" +
                "Please ensure all required files (rdm_x_core.dll, ftd2xx.dll, vusbdmx.dll) " +
                "are present in the application folder.\n\n" +
                $"Details: {ex.Message}",
                "RDM_X - Missing Dependencies",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            
            Shutdown(1);
            return;
        }

        // If DLL loaded successfully, launch the main window
        var mainWindow = new MainWindow();
        mainWindow.Show();
    }

    private static void LogException(Exception? ex, string source)
    {
        if (ex == null) return;
        try
        {
            var logPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RDM_X", "crash.log");
            Directory.CreateDirectory(Path.GetDirectoryName(logPath)!);
            File.AppendAllText(logPath, $"[{DateTime.Now}] ({source}) {ex}\n\n");
            MessageBox.Show($"App crashed! Log saved to:\n{logPath}\n\nError: {ex.Message}", "RDM_X Crash", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        catch { }
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
