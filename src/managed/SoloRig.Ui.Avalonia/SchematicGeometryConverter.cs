using System.Globalization;
using Avalonia.Data.Converters;
using Avalonia.Media;

namespace SoloRig.Ui.Avalonia;

public sealed class SchematicGeometryConverter : IValueConverter
{
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not string path || string.IsNullOrWhiteSpace(path))
        {
            return null;
        }
        try
        {
            return StreamGeometry.Parse(path);
        }
        catch (FormatException)
        {
            return null;
        }
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
