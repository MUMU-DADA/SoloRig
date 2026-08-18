using System.Globalization;
using Avalonia;
using Avalonia.Data.Converters;
using Avalonia.Media;
using SoloRig.Ui;

namespace SoloRig.Ui.Avalonia;

/// <summary>
/// Draws an effect-chain connection as a readable curved route.
/// </summary>
public sealed class ConnectionGeometryConverter : IValueConverter
{
    public static StreamGeometry CreateRouteGeometry(Point start, Point end)
    {
        var direction = end.X >= start.X ? 1.0 : -1.0;
        var distance = Math.Abs(end.X - start.X);
        var handle = Math.Clamp(distance * 0.42, 28.0, 120.0);
        var controlStart = new Point(start.X + direction * handle, start.Y);
        var controlEnd = new Point(end.X - direction * handle, end.Y);
        var geometry = new StreamGeometry();
        using (var context = geometry.Open())
        {
            context.BeginFigure(start, false);
            context.CubicBezierTo(controlStart, controlEnd, end);
        }
        return geometry;
    }

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not CanvasConnectionSnapshot connection)
        {
            return null;
        }

        var start = new Point(connection.SourceX, connection.SourceY);
        var end = new Point(connection.DestinationX, connection.DestinationY);
        var direction = end.X >= start.X ? 1.0 : -1.0;
        var distance = Math.Abs(end.X - start.X);
        var handle = Math.Clamp(distance * 0.42, 28.0, 120.0);
        var controlStart = new Point(start.X + direction * handle, start.Y);
        var controlEnd = new Point(end.X - direction * handle, end.Y);

        if (string.Equals(parameter?.ToString(), "Arrow", StringComparison.OrdinalIgnoreCase))
        {
            return CreateArrowGeometry(connection, start, end, controlStart, controlEnd);
        }

        var geometry = new StreamGeometry();
        using (var context = geometry.Open())
        {
            context.BeginFigure(start, false);
            context.CubicBezierTo(controlStart, controlEnd, end);
        }
        return geometry;
    }

    private static StreamGeometry CreateArrowGeometry(CanvasConnectionSnapshot connection, Point start, Point end,
        Point controlStart, Point controlEnd)
    {
        const double t = 0.72;
        var point = Cubic(start, controlStart, controlEnd, end, t);
        var tangent = CubicDerivative(start, controlStart, controlEnd, end, t);
        var length = Math.Sqrt(tangent.X * tangent.X + tangent.Y * tangent.Y);
        var direction = length > 0.001 ? new Vector(tangent.X / length, tangent.Y / length) : new Vector(1, 0);
        var normal = new Vector(-direction.Y, direction.X);
        var size = connection.ArrowSize;
        var tip = point + direction * size;
        var baseCenter = point - direction * (size * 0.72);
        var left = baseCenter + normal * (size * 0.58);
        var right = baseCenter - normal * (size * 0.58);

        var geometry = new StreamGeometry();
        using (var context = geometry.Open())
        {
            context.BeginFigure(tip, true);
            context.LineTo(left);
            context.LineTo(right);
            context.EndFigure(true);
        }
        return geometry;
    }

    private static Point Cubic(Point p0, Point p1, Point p2, Point p3, double t)
    {
        var u = 1.0 - t;
        return new Point(
            u * u * u * p0.X + 3.0 * u * u * t * p1.X + 3.0 * u * t * t * p2.X + t * t * t * p3.X,
            u * u * u * p0.Y + 3.0 * u * u * t * p1.Y + 3.0 * u * t * t * p2.Y + t * t * t * p3.Y);
    }

    private static Vector CubicDerivative(Point p0, Point p1, Point p2, Point p3, double t)
    {
        var u = 1.0 - t;
        return new Vector(
            3.0 * u * u * (p1.X - p0.X) + 6.0 * u * t * (p2.X - p1.X) + 3.0 * t * t * (p3.X - p2.X),
            3.0 * u * u * (p1.Y - p0.Y) + 6.0 * u * t * (p2.Y - p1.Y) + 3.0 * t * t * (p3.Y - p2.Y));
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
