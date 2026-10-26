using Avalonia;
using Avalonia.Input;
using Avalonia.Media;
using SoloRig.Ui;

namespace SoloRig.Ui.Avalonia;

public sealed partial class PedalWorkshopView
{
    private sealed record SchematicWireHit(AnalogWireRouteDraft Wire, int Segment, Point Position);
    private readonly Dictionary<AnalogWireRouteDraft, IReadOnlyList<Point>> circuitWirePaths = [];
    private SchematicWireHit? pendingWireStart;
    private AnalogWireRouteDraft? selectedSchematicWire;
    private bool circuitWireHorizontalFirst = true;
    private SchematicWireHit? draggingSchematicSegment;
    private Point draggingSchematicOrigin;
    private Point[] draggingSchematicPoints = [];
    private bool draggingSchematicTransaction;
    private AnalogWirePointDraft[] draggingSchematicVertices = [];
    private IPointer? draggingSchematicPointer;

    private AnalogWirePointDraft? PendingWireEndpoint() => pendingWireStart is { } start
        ? new AnalogWirePointDraft(start.Position.X, start.Position.Y)
        : pendingCircuitPin is { } pin ? Document.AnalogPinEndpoint(pin) : null;

    private IReadOnlyList<Point> BuildSchematicPath(AnalogWirePointDraft source, Point end, string? endSide,
        IReadOnlyList<Point> fixedPoints)
    {
        var points = new List<Point> { new(source.X, source.Y) };
        if (fixedPoints.Count > 0) points.AddRange(fixedPoints);
        else if (source.Pin is { } pin) points.Add(OffsetCircuitPoint(points[0], pin.Side, CircuitGridSize * 2));
        var endLead = endSide is null ? end : OffsetCircuitPoint(end, endSide, CircuitGridSize * 2);
        var from = points[^1];
        var elbow = circuitWireHorizontalFirst ? new Point(endLead.X, from.Y) : new Point(from.X, endLead.Y);
        var opposite = circuitWireHorizontalFirst ? new Point(from.X, endLead.Y) : new Point(endLead.X, from.Y);
        var obstacles = CircuitRoutingObstacles();
        IReadOnlyList<Point> tail = [from, elbow, endLead];
        if (!RouteAvoidsObstacles(tail, obstacles))
            tail = RouteAvoidsObstacles([from, opposite, endLead], obstacles)
                ? [from, opposite, endLead] : BuildObstacleAvoidingRoute(from, endLead, obstacles);
        points.AddRange(tail.Skip(1));
        points.Add(end);
        return SimplifyOrthogonalRoute(DistinctCircuitPoints(points));
    }

    private void AddSchematicPath(string node, AnalogWirePointDraft start, AnalogWirePointDraft end, IReadOnlyList<Point> path)
    {
        Document.AddSchematicWire(node, new[] { start }.Concat(path.Skip(1).SkipLast(1)
            .Select(point => new AnalogWirePointDraft(point.X, point.Y))).Append(end));
    }

    private IReadOnlyList<Point> SchematicPath(AnalogWireRouteDraft wire)
    {
        var points = wire.Points.Select(point => new Point(point.X, point.Y)).ToArray();
        if (points.Length < 2) return points;
        points = RepairWireEndpoint(points, wire.Points[0]);
        points = RepairWireEndpoint(points.Reverse().ToArray(), wire.Points[^1]).Reverse().ToArray();
        var result = new List<Point> { points[0] };
        for (var index = 1; index < points.Length; index++)
        {
            var previous = result[^1];
            var next = points[index];
            if (Math.Abs(previous.X - next.X) > 0.01 && Math.Abs(previous.Y - next.Y) > 0.01)
            {
                var horizontal = index == 1 ? wire.Points[0].Pin?.Side is "left" or "right"
                    : index == points.Length - 1 ? wire.Points[^1].Pin?.Side is "top" or "bottom"
                    : true;
                result.Add(horizontal ? new Point(next.X, previous.Y) : new Point(previous.X, next.Y));
            }
            result.Add(next);
        }
        return SimplifyOrthogonalRoute(DistinctCircuitPoints(result));
    }

    private Point[] RepairWireEndpoint(Point[] points, AnalogWirePointDraft endpoint)
    {
        if (endpoint.Pin is not { } pin) return points;
        var start = points[0];
        var next = points[1];
        var aligned = pin.Side switch
        {
            "left" => next.Y == start.Y && next.X < start.X,
            "right" => next.Y == start.Y && next.X > start.X,
            "top" => next.X == start.X && next.Y < start.Y,
            _ => next.X == start.X && next.Y > start.Y,
        };
        if (aligned) return points;
        var lead = OffsetCircuitPoint(start, pin.Side, CircuitGridSize * 2);
        var obstacles = CircuitRoutingObstacles();
        var index = 1;
        while (index < points.Length - 1 && obstacles.Any(bounds => bounds.Contains(points[index]))) index++;
        return new[] { start, lead }.Concat(BuildObstacleAvoidingRoute(lead, points[index], obstacles).Skip(1))
            .Concat(points.Skip(index + 1)).ToArray();
    }

    private void RenderSchematicWires(string node, IReadOnlyList<AnalogWireRouteDraft> wires,
        IReadOnlyList<CircuitEndpoint> endpoints, IBrush stroke)
    {
        foreach (var wire in wires)
        {
            var path = SchematicPath(wire);
            circuitWirePaths[wire] = path;
            for (var index = 1; index < path.Count; index++)
                AddCircuitWireSegment(node, path[index - 1], path[index], stroke, wire, index - 1);
        }
        foreach (var junction in wires.SelectMany(wire => new[] { wire.Points[0], wire.Points[^1] })
                     .GroupBy(point => point).Where(group => group.Count() >= 3))
            AddJunctionDot(new Point(junction.Key.X, junction.Key.Y), stroke, 7, node);
        foreach (var endpoint in endpoints)
        {
            if (endpoint.Pin.GroundSymbol)
            {
                var lead = CircuitLeadPoint(endpoint);
                AddCircuitWireSegment(node, endpoint.Point, lead, stroke);
                AddGroundSymbol(lead, stroke);
            }
            if (endpoint.Pin.TerminalPortId.Length > 0)
            {
                var port = Document.AnalogCircuit.Ports.FirstOrDefault(port => port.Id == endpoint.Pin.TerminalPortId);
                if (port is not null) AddCircuitNodeLabel(node, CircuitLeadPoint(endpoint), stroke, port.Direction == "input" ? "IN" : "OUT");
            }
        }
    }

    private void EnsureSchematicNode(string node)
    {
        if (Document.AnalogCircuit.WireRoutes.Any(wire => wire.IsExplicit && wire.Node == node)) return;
        var pins = Document.AnalogCircuit.Components.SelectMany(component => component.Pins)
            .Where(pin => pin.Node == node).ToList();
        foreach (var pin in pins.Where(pin => node == Document.AnalogCircuit.ReferenceGround)) pin.GroundSymbol = true;
        foreach (var port in Document.AnalogCircuit.Ports.Where(port => port.Node == node))
            if (pins.FirstOrDefault() is { } pin && !pins.Any(pin => pin.TerminalPortId == port.Id)) pin.TerminalPortId = port.Id;
        var legacy = Document.AnalogCircuit.WireRoutes.Where(wire => !wire.IsExplicit && wire.Node == node).ToArray();
        foreach (var route in legacy) Document.AnalogCircuit.WireRoutes.Remove(route);
        if (node == Document.AnalogCircuit.ReferenceGround) return;
        // Freeze the legacy drawing as a segment graph so the first edit preserves its geometry.
        var segments = renderedCircuitSegments.Where(segment => segment.Node == node).ToArray();
        var vertices = new Dictionary<Point, AnalogWirePointDraft>();
        foreach (var pin in pins)
        {
            var endpoint = Document.AnalogPinEndpoint(pin);
            vertices.TryAdd(new Point(endpoint.X, endpoint.Y), endpoint);
        }
        foreach (var point in segments.SelectMany(segment => new[] { segment.Start, segment.End }))
            vertices.TryAdd(point, new AnalogWirePointDraft(point.X, point.Y));
        var edges = new HashSet<(Point, Point)>();
        foreach (var segment in segments)
        {
            var points = vertices.Keys.Where(point =>
                point.X >= Math.Min(segment.Start.X, segment.End.X) && point.X <= Math.Max(segment.Start.X, segment.End.X) &&
                point.Y >= Math.Min(segment.Start.Y, segment.End.Y) && point.Y <= Math.Max(segment.Start.Y, segment.End.Y))
                .OrderBy(point => DistanceSquared(segment.Start, point)).ToArray();
            for (var index = 1; index < points.Length; index++)
            {
                var first = points[index - 1];
                var last = points[index];
                if (!edges.Contains((last, first)) && edges.Add((first, last)))
                    Document.AddSchematicWire(node, [vertices[first], vertices[last]]);
            }
        }
    }

    private SchematicWireHit? FindSchematicWire(Point viewport, double tolerance = 10,
        AnalogWireRouteDraft? wire = null, string? node = null)
    {
        var position = CircuitViewportToWorld(viewport);
        var paths = circuitWirePaths.AsEnumerable();
        if (wire is null)
            paths = paths.Concat(renderedCircuitSegments.Where(segment => segment.Node != Document.AnalogCircuit.ReferenceGround &&
                !circuitWirePaths.Keys.Any(route => route.Node == segment.Node)).Select(segment =>
            {
                var legacy = new AnalogWireRouteDraft(segment.Node, true);
                legacy.Points.Add(new AnalogWirePointDraft(segment.Start.X, segment.Start.Y));
                legacy.Points.Add(new AnalogWirePointDraft(segment.End.X, segment.End.Y));
                return new KeyValuePair<AnalogWireRouteDraft, IReadOnlyList<Point>>(legacy, [segment.Start, segment.End]);
            }));
        return paths.Where(entry => (wire is null || ReferenceEquals(entry.Key, wire)) && (node is null || entry.Key.Node == node))
            .SelectMany(entry => Enumerable.Range(0, entry.Value.Count - 1).Select(index =>
        {
            var first = entry.Value[index];
            var second = entry.Value[index + 1];
            var hit = Math.Abs(first.X - second.X) < 0.01
                ? new Point(first.X, Math.Clamp(SnapCircuitGrid(position.Y), Math.Min(first.Y, second.Y), Math.Max(first.Y, second.Y)))
                : new Point(Math.Clamp(SnapCircuitGrid(position.X), Math.Min(first.X, second.X), Math.Max(first.X, second.X)), first.Y);
            return new SchematicWireHit(entry.Key, index, hit);
        })).Where(hit => DistanceSquared(CircuitWorldToViewport(hit.Position), viewport) <= tolerance * tolerance)
            .OrderBy(hit => DistanceSquared(CircuitWorldToViewport(hit.Position), viewport)).FirstOrDefault();
    }

    private AnalogWirePointDraft SplitSchematicWire(SchematicWireHit target)
    {
        if (!Document.AnalogCircuit.WireRoutes.Contains(target.Wire))
        {
            EnsureSchematicNode(target.Wire.Node);
            RenderCircuitConnections();
            target = FindSchematicWire(CircuitWorldToViewport(target.Position), node: target.Wire.Node)
                ?? throw new InvalidOperationException("The wire endpoint is no longer present.");
        }
        var wire = target.Wire;
        var path = SchematicPath(wire);
        if (DistanceSquared(path[0], target.Position) < 0.01) return wire.Points[0];
        if (DistanceSquared(path[^1], target.Position) < 0.01) return wire.Points[^1];
        var junction = new AnalogWirePointDraft(target.Position.X, target.Position.Y);
        Document.AnalogCircuit.WireRoutes.Remove(wire);
        AddSchematicPath(wire.Node, wire.Points[0], junction,
            path.Take(target.Segment + 1).Append(target.Position).ToArray());
        AddSchematicPath(wire.Node, junction, wire.Points[^1],
            new[] { target.Position }.Concat(path.Skip(target.Segment + 1)).ToArray());
        return junction;
    }

    private void CompleteSchematicBranch(SchematicWireHit target)
    {
        if (pendingCircuitPin is null || PendingWireEndpoint() is not { } start) return;
        if (DistanceSquared(new Point(start.X, start.Y), target.Position) < 0.01) return;
        var sourcePin = pendingCircuitPin;
        var targetPin = new AnalogPinDraft("wire", "wire", target.Wire.Node);
        var path = BuildSchematicPath(start, target.Position, null, pendingCircuitBends);
        Document.BeginEditTransaction();
        try
        {
            EnsureSchematicNode(sourcePin.Node);
            start = pendingWireStart is { } sourceWire ? SplitSchematicWire(sourceWire) : Document.AnalogPinEndpoint(sourcePin);
            var end = SplitSchematicWire(target);
            Document.ConnectAnalogPins(sourcePin, targetPin);
            AddSchematicPath(sourcePin.Node, start, end, path);
        }
        finally { Document.EndEditTransaction(); }
        ClearPendingCircuitPin();
        RenderCircuitConnections();
    }

    private void MoveSchematicSegment(Point pointer)
    {
        if (draggingSchematicSegment is not { } drag) return;
        var points = draggingSchematicPoints;
        var index = drag.Segment;
        var horizontal = Math.Abs(points[index].Y - points[index + 1].Y) < 0.01;
        var delta = horizontal ? SnapCircuitGrid(pointer.Y - draggingSchematicOrigin.Y)
            : SnapCircuitGrid(pointer.X - draggingSchematicOrigin.X);
        if (Math.Abs(delta) < 0.01 && !draggingSchematicTransaction) return;
        if (!draggingSchematicTransaction)
        {
            Document.BeginEditTransaction();
            draggingSchematicTransaction = true;
        }
        var first = drag.Wire.Points[0];
        var last = drag.Wire.Points[^1];
        var from = points[index];
        var to = points[index + 1];
        var updated = points.Take(index).ToList();
        if (index == 0)
        {
            updated.Add(points[0]);
            if (first.Pin is { } pin)
            {
                from = OffsetCircuitPoint(points[0], pin.Side, CircuitGridSize * 2);
                updated.Add(from);
            }
        }
        if (index + 1 == points.Length - 1 && last.Pin is { } endPin)
            to = OffsetCircuitPoint(points[^1], endPin.Side, CircuitGridSize * 2);
        updated.Add(horizontal ? new Point(from.X, points[index].Y + delta) : new Point(points[index].X + delta, from.Y));
        updated.Add(horizontal ? new Point(to.X, points[index + 1].Y + delta) : new Point(points[index + 1].X + delta, to.Y));
        if (index + 1 == points.Length - 1)
        {
            if (last.Pin is not null) updated.Add(to);
            updated.Add(points[^1]);
        }
        updated.AddRange(points.Skip(index + 2));
        var path = SimplifyOrthogonalRoute(DistinctCircuitPoints(updated));
        drag.Wire.Points.Clear();
        drag.Wire.Points.Add(first);
        foreach (var point in path.Skip(1).SkipLast(1)) drag.Wire.Points.Add(new AnalogWirePointDraft(point.X, point.Y));
        drag.Wire.Points.Add(last);
        RenderCircuitConnections();
    }

    private void FinishSchematicDrag(bool cancel)
    {
        if (cancel && draggingSchematicTransaction && draggingSchematicSegment is { } drag)
        {
            drag.Wire.Points.Clear();
            foreach (var point in draggingSchematicVertices) drag.Wire.Points.Add(point);
        }
        if (draggingSchematicTransaction) Document.EndEditTransaction();
        draggingSchematicTransaction = false;
        draggingSchematicSegment = null;
        draggingSchematicPointer?.Capture(null);
        draggingSchematicPointer = null;
        RenderCircuitConnections();
    }
}
