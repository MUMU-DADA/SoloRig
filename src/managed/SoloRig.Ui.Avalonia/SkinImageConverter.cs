using Avalonia.Data.Converters;
using Avalonia.Media.Imaging;
using System.Collections.Concurrent;
using System.Globalization;
using System.Threading;

namespace SoloRig.Ui.Avalonia;

public sealed class SkinImageConverter : IValueConverter
{
    private static readonly ConcurrentDictionary<string, Lazy<Bitmap?>> Cache = new(StringComparer.OrdinalIgnoreCase);

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not string path || string.IsNullOrWhiteSpace(path))
        {
            return null;
        }
        try
        {
            var key = Path.GetFullPath(path);
            var image = Cache.GetOrAdd(key, static item => new Lazy<Bitmap?>(() => Load(item),
                LazyThreadSafetyMode.ExecutionAndPublication));
            return image.Value;
        }
        catch
        {
            return null;
        }
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException("Skin images are one-way UI resources.");

    private static Bitmap? Load(string path)
    {
        try
        {
            return File.Exists(path) ? new Bitmap(path) : null;
        }
        catch
        {
            return null;
        }
    }
}
