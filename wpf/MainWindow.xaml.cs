using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace RDM_X;

public partial class MainWindow : Window
{
    // DWM dark title bar (Windows 10 1809+ / Windows 11)
    [DllImport("dwmapi.dll", PreserveSig = true)]
    private static extern int DwmSetWindowAttribute(
        IntPtr hwnd, int attr, ref int attrValue, int attrSize);

    private const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;

    public MainWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        // Force dark title bar
        if (PresentationSource.FromVisual(this) is HwndSource source)
        {
            int value = 1;
            DwmSetWindowAttribute(source.Handle,
                DWMWA_USE_IMMERSIVE_DARK_MODE, ref value, sizeof(int));
        }
    }
}