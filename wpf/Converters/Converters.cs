using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;

namespace RDM_X.Converters;

/// <summary>Converts RDX_STATUS int to a colored text string.</summary>
public class StatusToTextConverter : IValueConverter
{
    public object Convert(object value, Type t, object p, CultureInfo c)
        => value is int s ? s switch
        {
            NativeInterop.STATUS_ACK => "ACK",
            NativeInterop.STATUS_ACK_TIMER => "ACK_TIMER",
            NativeInterop.STATUS_NACK => "NACK",
            NativeInterop.STATUS_TIMEOUT => "TIMEOUT",
            NativeInterop.STATUS_CHECKSUM_ERR => "CKSUM ERR",
            NativeInterop.STATUS_INVALID => "INVALID",
            _ => "?"
        } : "";

    public object ConvertBack(object value, Type t, object p, CultureInfo c)
        => throw new NotImplementedException();
}

public class StatusToBrushConverter : IValueConverter
{
    static readonly SolidColorBrush Green  = new((Color)ColorConverter.ConvertFromString("#FF66BB6A"));
    static readonly SolidColorBrush Yellow = new((Color)ColorConverter.ConvertFromString("#FFFFA726"));
    static readonly SolidColorBrush Red    = new((Color)ColorConverter.ConvertFromString("#FFEF5350"));
    static readonly SolidColorBrush Gray   = new((Color)ColorConverter.ConvertFromString("#FF9090A8"));

    public object Convert(object value, Type t, object p, CultureInfo c)
        => value is int s ? s switch
        {
            NativeInterop.STATUS_ACK => Green,
            NativeInterop.STATUS_ACK_TIMER => Yellow,
            NativeInterop.STATUS_NACK => Red,
            NativeInterop.STATUS_TIMEOUT => Gray,
            NativeInterop.STATUS_CHECKSUM_ERR => Red,
            _ => Gray
        } : Gray;

    public object ConvertBack(object value, Type t, object p, CultureInfo c)
        => throw new NotImplementedException();
}

public class LatencyToBrushConverter : IValueConverter
{
    static readonly SolidColorBrush Green  = new((Color)ColorConverter.ConvertFromString("#FF66BB6A"));
    static readonly SolidColorBrush Yellow = new((Color)ColorConverter.ConvertFromString("#FFFFA726"));
    static readonly SolidColorBrush Red    = new((Color)ColorConverter.ConvertFromString("#FFEF5350"));

    public object Convert(object value, Type t, object p, CultureInfo c)
    {
        if (value is long us)
        {
            if (us <= 1000) return Green;    // ≤ 1ms
            if (us <= 2800) return Yellow;   // ≤ 2.8ms (E1.20 limit)
            return Red;                       // > 2.8ms
        }
        return Green;
    }

    public object ConvertBack(object value, Type t, object p, CultureInfo c)
        => throw new NotImplementedException();
}

public class BoolToVisibilityConverter : IValueConverter
{
    public object Convert(object value, Type t, object p, CultureInfo c)
        => value is true ? Visibility.Visible : Visibility.Collapsed;

    public object ConvertBack(object value, Type t, object p, CultureInfo c)
        => throw new NotImplementedException();
}

public class InverseBoolToVisibilityConverter : IValueConverter
{
    public object Convert(object value, Type t, object p, CultureInfo c)
        => value is true ? Visibility.Collapsed : Visibility.Visible;

    public object ConvertBack(object value, Type t, object p, CultureInfo c)
        => throw new NotImplementedException();
}

public class InverseBoolConverter : IValueConverter
{
    public object Convert(object value, Type t, object p, CultureInfo c)
        => value is bool b ? !b : value;

    public object ConvertBack(object value, Type t, object p, CultureInfo c)
        => value is bool b ? !b : value;
}

/// <summary>Scales microseconds to bar width (max 5000μs → 400px).</summary>
public class LatencyToWidthConverter : IValueConverter
{
    private const double MaxUs   = 5000.0;
    private const double MaxPx   = 400.0;

    public object Convert(object value, Type t, object p, CultureInfo c)
    {
        if (value is long us && us > 0)
            return Math.Min(us / MaxUs * MaxPx, MaxPx);
        return 2.0;
    }

    public object ConvertBack(object value, Type t, object p, CultureInfo c)
        => throw new NotImplementedException();
}
