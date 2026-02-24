// tests/cs/RDM_X.Tests/ConverterTests.cs
// Tests for all 7 WPF value converters.
// No rdm_x_core.dll is loaded — converters are pure value logic.
using System.Globalization;
using System.Windows;
using System.Windows.Media;
using RDM_X;
using RDM_X.Converters;
using Xunit;

namespace RDM_X.Tests;

public class ConverterTests
{
    // ── Helpers ──────────────────────────────────────────────────────────

    private static Color ParseColor(string argb)
        => (Color)ColorConverter.ConvertFromString(argb);

    private static object Conv(System.Windows.Data.IValueConverter c, object value)
        => c.Convert(value, typeof(object), null!, CultureInfo.InvariantCulture);

    private static SolidColorBrush Brush(System.Windows.Data.IValueConverter c, object value)
        => Assert.IsType<SolidColorBrush>(Conv(c, value));

    // ═════════════════════════════════════════════════════════════════════
    // StatusToTextConverter
    // ═════════════════════════════════════════════════════════════════════

    [Fact] public void StatusToText_Ack()          => Assert.Equal("ACK",       Conv(new StatusToTextConverter(), NativeInterop.STATUS_ACK));
    [Fact] public void StatusToText_AckTimer()     => Assert.Equal("ACK_TIMER", Conv(new StatusToTextConverter(), NativeInterop.STATUS_ACK_TIMER));
    [Fact] public void StatusToText_Nack()         => Assert.Equal("NACK",      Conv(new StatusToTextConverter(), NativeInterop.STATUS_NACK));
    [Fact] public void StatusToText_Timeout()      => Assert.Equal("TIMEOUT",   Conv(new StatusToTextConverter(), NativeInterop.STATUS_TIMEOUT));
    [Fact] public void StatusToText_ChecksumErr()  => Assert.Equal("CKSUM ERR", Conv(new StatusToTextConverter(), NativeInterop.STATUS_CHECKSUM_ERR));
    [Fact] public void StatusToText_Invalid()      => Assert.Equal("INVALID",   Conv(new StatusToTextConverter(), NativeInterop.STATUS_INVALID));
    [Fact] public void StatusToText_Unknown()      => Assert.Equal("?",         Conv(new StatusToTextConverter(), 999));
    [Fact] public void StatusToText_NonInt()       => Assert.Equal("",          Conv(new StatusToTextConverter(), "not an int"));

    // ═════════════════════════════════════════════════════════════════════
    // StatusToBrushConverter
    // ═════════════════════════════════════════════════════════════════════

    static readonly Color Green  = ParseColor("#FF66BB6A");
    static readonly Color Yellow = ParseColor("#FFFFA726");
    static readonly Color Red    = ParseColor("#FFEF5350");
    static readonly Color Gray   = ParseColor("#FF9090A8");

    [Fact] public void StatusToBrush_Ack()         => Assert.Equal(Green,  Brush(new StatusToBrushConverter(), NativeInterop.STATUS_ACK).Color);
    [Fact] public void StatusToBrush_AckTimer()    => Assert.Equal(Yellow, Brush(new StatusToBrushConverter(), NativeInterop.STATUS_ACK_TIMER).Color);
    [Fact] public void StatusToBrush_Nack()        => Assert.Equal(Red,    Brush(new StatusToBrushConverter(), NativeInterop.STATUS_NACK).Color);
    [Fact] public void StatusToBrush_Timeout()     => Assert.Equal(Gray,   Brush(new StatusToBrushConverter(), NativeInterop.STATUS_TIMEOUT).Color);
    [Fact] public void StatusToBrush_ChecksumErr() => Assert.Equal(Red,    Brush(new StatusToBrushConverter(), NativeInterop.STATUS_CHECKSUM_ERR).Color);
    [Fact] public void StatusToBrush_Unknown()     => Assert.Equal(Gray,   Brush(new StatusToBrushConverter(), 999).Color);
    [Fact] public void StatusToBrush_NonInt()      => Assert.Equal(Gray,   Brush(new StatusToBrushConverter(), "bad").Color);

    // ═════════════════════════════════════════════════════════════════════
    // LatencyToBrushConverter — thresholds: ≤1000μs=green, ≤2800=yellow, >2800=red
    // ═════════════════════════════════════════════════════════════════════

    [Fact] public void LatencyToBrush_Zero_IsGreen()            => Assert.Equal(Green,  Brush(new LatencyToBrushConverter(), 0L).Color);
    [Fact] public void LatencyToBrush_1000us_IsGreen()          => Assert.Equal(Green,  Brush(new LatencyToBrushConverter(), 1000L).Color);
    [Fact] public void LatencyToBrush_1001us_IsYellow()         => Assert.Equal(Yellow, Brush(new LatencyToBrushConverter(), 1001L).Color);
    [Fact] public void LatencyToBrush_2000us_IsYellow()         => Assert.Equal(Yellow, Brush(new LatencyToBrushConverter(), 2000L).Color);
    [Fact] public void LatencyToBrush_2800us_IsYellow()         => Assert.Equal(Yellow, Brush(new LatencyToBrushConverter(), 2800L).Color);
    [Fact] public void LatencyToBrush_2801us_IsRed()            => Assert.Equal(Red,    Brush(new LatencyToBrushConverter(), 2801L).Color);
    [Fact] public void LatencyToBrush_VeryHigh_IsRed()          => Assert.Equal(Red,    Brush(new LatencyToBrushConverter(), 100_000L).Color);
    [Fact] public void LatencyToBrush_NonLong_IsGreen()         => Assert.Equal(Green,  Brush(new LatencyToBrushConverter(), "bad").Color);

    // ═════════════════════════════════════════════════════════════════════
    // BoolToVisibilityConverter
    // ═════════════════════════════════════════════════════════════════════

    [Fact] public void BoolToVis_True_IsVisible()    => Assert.Equal(Visibility.Visible,   Conv(new BoolToVisibilityConverter(), true));
    [Fact] public void BoolToVis_False_IsCollapsed() => Assert.Equal(Visibility.Collapsed, Conv(new BoolToVisibilityConverter(), false));
    [Fact] public void BoolToVis_NonBool_IsCollapsed()=> Assert.Equal(Visibility.Collapsed, Conv(new BoolToVisibilityConverter(), "nope"));

    // ═════════════════════════════════════════════════════════════════════
    // InverseBoolToVisibilityConverter
    // ═════════════════════════════════════════════════════════════════════

    [Fact] public void InvBoolToVis_True_IsCollapsed() => Assert.Equal(Visibility.Collapsed, Conv(new InverseBoolToVisibilityConverter(), true));
    [Fact] public void InvBoolToVis_False_IsVisible()  => Assert.Equal(Visibility.Visible,   Conv(new InverseBoolToVisibilityConverter(), false));
    [Fact] public void InvBoolToVis_NonBool_IsVisible() => Assert.Equal(Visibility.Visible,  Conv(new InverseBoolToVisibilityConverter(), "nope"));

    // ═════════════════════════════════════════════════════════════════════
    // InverseBoolConverter
    // ═════════════════════════════════════════════════════════════════════

    [Fact] public void InvBool_True_ReturnsFalse()  => Assert.Equal(false, Conv(new InverseBoolConverter(), true));
    [Fact] public void InvBool_False_ReturnsTrue()  => Assert.Equal(true,  Conv(new InverseBoolConverter(), false));
    [Fact] public void InvBool_NonBool_Passthrough()=> Assert.Equal("hi",  Conv(new InverseBoolConverter(), "hi"));

    [Fact]
    public void InvBool_ConvertBack_InvertsValue()
    {
        var c = new InverseBoolConverter();
        Assert.Equal(false, c.ConvertBack(true,  typeof(bool), null!, CultureInfo.InvariantCulture));
        Assert.Equal(true,  c.ConvertBack(false, typeof(bool), null!, CultureInfo.InvariantCulture));
    }

    // ═════════════════════════════════════════════════════════════════════
    // LatencyToWidthConverter — maps 0..5000μs → 0..400px
    //   us <= 0 or non-long → 2.0 (minimum visible width)
    //   us > 5000            → clamped to 400.0
    // ═════════════════════════════════════════════════════════════════════

    [Fact] public void LatencyToWidth_Zero_Returns2px()      => Assert.Equal(2.0,   Conv(new LatencyToWidthConverter(), 0L));
    [Fact] public void LatencyToWidth_Negative_Returns2px()  => Assert.Equal(2.0,   Conv(new LatencyToWidthConverter(), -1L));
    [Fact] public void LatencyToWidth_NonLong_Returns2px()   => Assert.Equal(2.0,   Conv(new LatencyToWidthConverter(), "bad"));
    [Fact] public void LatencyToWidth_5000us_Returns400px()  => Assert.Equal(400.0, Conv(new LatencyToWidthConverter(), 5000L));
    [Fact] public void LatencyToWidth_Over5000_Clamped()     => Assert.Equal(400.0, Conv(new LatencyToWidthConverter(), 10_000L));

    [Fact]
    public void LatencyToWidth_2500us_Returns200px()
    {
        double result = (double)Conv(new LatencyToWidthConverter(), 2500L);
        Assert.Equal(200.0, result, precision: 5);
    }

    [Fact]
    public void LatencyToWidth_1000us_Returns80px()
    {
        double result = (double)Conv(new LatencyToWidthConverter(), 1000L);
        Assert.Equal(80.0, result, precision: 5);
    }

    [Fact]
    public void LatencyToWidth_1us_IsPositiveAndSmall()
    {
        // 1/5000 * 400 = 0.08px — positive but below 2.0 minimum display threshold
        double result = (double)Conv(new LatencyToWidthConverter(), 1L);
        Assert.True(result > 0.0);
        Assert.True(result < 2.0);
    }
}
