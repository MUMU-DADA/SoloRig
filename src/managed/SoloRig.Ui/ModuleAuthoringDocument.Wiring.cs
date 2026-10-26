namespace SoloRig.Ui;

public sealed partial class ModuleAuthoringDocument
{
    private static void CloneAnalogWireRoutes(AnalogCircuitDraft source, AnalogCircuitDraft target)
    {
        var vertices = new Dictionary<AnalogWirePointDraft, AnalogWirePointDraft>();
        target.WireRoutes.Clear();
        foreach (var route in source.WireRoutes)
        {
            var copy = new AnalogWireRouteDraft(route.Node, route.IsExplicit);
            foreach (var point in route.Points)
            {
                if (!vertices.TryGetValue(point, out var vertex))
                {
                    var owner = target.Components.FirstOrDefault(component => component.Id == point.Component?.Id);
                    var pin = owner?.Pins.FirstOrDefault(pin => pin.Id == point.Pin?.Id);
                    vertex = new AnalogWirePointDraft(point.X, point.Y, owner, pin, point.Id);
                    vertices.Add(point, vertex);
                }
                copy.Points.Add(vertex);
            }
            target.WireRoutes.Add(copy);
        }
    }

    public AnalogWirePointDraft AnalogPinEndpoint(AnalogPinDraft pin)
    {
        var existing = AnalogCircuit.WireRoutes.Where(route => route.IsExplicit)
            .SelectMany(route => route.Points).FirstOrDefault(point => ReferenceEquals(point.Pin, pin));
        if (existing is not null) return existing;
        var owner = AnalogCircuit.Components.First(component => component.Pins.Contains(pin));
        return new AnalogWirePointDraft(0, 0, owner, pin);
    }

    public AnalogWireRouteDraft AddSchematicWire(string node, IEnumerable<AnalogWirePointDraft> points)
    {
        var wire = new AnalogWireRouteDraft(node, true);
        foreach (var point in points) wire.Points.Add(point);
        if (wire.Points.Count < 2) throw new ArgumentException("A wire needs two endpoints.", nameof(points));
        AnalogCircuit.WireRoutes.Add(wire);
        return wire;
    }

    public bool RemoveSchematicWire(AnalogWireRouteDraft wire)
    {
        BeginEditTransaction();
        try
        {
            if (!AnalogCircuit.WireRoutes.Remove(wire)) return false;
            RebuildSchematicNode(wire.Node);
            return true;
        }
        finally { EndEditTransaction(); }
    }

    private void RebuildSchematicNode(string node)
    {
        var wires = AnalogCircuit.WireRoutes.Where(wire => wire.IsExplicit && wire.Node == node && wire.Points.Count >= 2).ToArray();
        var pins = AnalogCircuit.Components.SelectMany(component => component.Pins).Where(pin => pin.Node == node).ToArray();
        var endpoints = pins.ToDictionary(pin => pin, AnalogPinEndpoint);
        var parent = wires.SelectMany(wire => new[] { wire.Points[0], wire.Points[^1] }).Concat(endpoints.Values)
            .Distinct().ToDictionary(point => point, point => point);
        AnalogWirePointDraft Root(AnalogWirePointDraft point)
        {
            while (!ReferenceEquals(parent[point], point)) point = parent[point];
            return point;
        }
        void Join(AnalogWirePointDraft first, AnalogWirePointDraft second) => parent[Root(second)] = Root(first);
        foreach (var wire in wires) Join(wire.Points[0], wire.Points[^1]);
        foreach (var samePin in parent.Keys.Where(point => point.Pin is not null).GroupBy(point => point.Pin))
            foreach (var point in samePin.Skip(1)) Join(samePin.First(), point);
        foreach (var terminals in pins.Where(pin => pin.GroundSymbol || pin.TerminalPortId.Length > 0)
                     .GroupBy(pin => pin.GroundSymbol ? "$ground" : pin.TerminalPortId))
            foreach (var pin in terminals.Skip(1)) Join(endpoints[terminals.First()], endpoints[pin]);
        var groups = parent.Keys.GroupBy(Root).OrderByDescending(group => group.Any(point => point.Pin?.GroundSymbol == true))
            .ThenByDescending(group => group.Any(point => point.Pin?.TerminalPortId.Length > 0))
            .ThenByDescending(group => group.Count(point => point.Pin is not null)).ToArray();
        var used = AnalogNodeIds().ToHashSet(StringComparer.Ordinal);
        for (var index = 0; index < groups.Length; index++)
        {
            var group = groups[index];
            var next = group.Any(point => point.Pin?.GroundSymbol == true) ? AnalogCircuit.ReferenceGround
                : index == 0 && node != AnalogCircuit.ReferenceGround &&
                  (!AnalogCircuit.Ports.Any(port => port.Node == node) || group.Any(point => point.Pin?.TerminalPortId.Length > 0))
                    ? node : UniqueId("net", used);
            used.Add(next);
            foreach (var point in group.Where(point => point.Pin is not null))
            {
                var pin = point.Pin!;
                pin.Node = next;
                var port = AnalogCircuit.Ports.FirstOrDefault(port => port.Id == pin.TerminalPortId);
                if (port is not null) port.Node = next;
            }
            foreach (var wire in wires.Where(wire => ReferenceEquals(Root(wire.Points[0]), group.Key))) wire.Node = next;
        }
        RemoveUnusedAnalogNode(node);
    }

    public static AnalogWireRouteDraft[] CopySchematicWires(IEnumerable<AnalogWireRouteDraft> sourceWires,
        IReadOnlyDictionary<AnalogComponentDraft, AnalogComponentDraft> components,
        Func<string, string> copiedNode, double offset)
    {
        var candidates = sourceWires.Where(wire => wire.IsExplicit && wire.Points.Count >= 2 && wire.Points.All(point =>
            point.Component is null || components.ContainsKey(point.Component))).ToArray();
        var selected = candidates.Where(wire => wire.Points.Any(point => point.Component is not null)).ToHashSet();
        bool added;
        do
        {
            var endpoints = selected.SelectMany(wire => new[] { wire.Points[0], wire.Points[^1] }).ToHashSet();
            added = false;
            foreach (var wire in candidates.Where(wire => endpoints.Contains(wire.Points[0]) || endpoints.Contains(wire.Points[^1])))
                added |= selected.Add(wire);
        } while (added);
        var vertices = new Dictionary<AnalogWirePointDraft, AnalogWirePointDraft>();
        var copies = new List<AnalogWireRouteDraft>();
        foreach (var wire in selected)
        {
            var points = wire.Points.Select(point =>
            {
                if (vertices.TryGetValue(point, out var existing)) return existing;
                var owner = point.Component is null ? null : components[point.Component];
                var pin = owner?.Pins.FirstOrDefault(pin => pin.Id == point.Pin?.Id);
                var copy = new AnalogWirePointDraft(point.X + offset, point.Y + offset, owner, pin);
                vertices.Add(point, copy);
                return copy;
            }).ToArray();
            var copy = new AnalogWireRouteDraft(copiedNode(wire.Node), true);
            foreach (var point in points) copy.Points.Add(point);
            copies.Add(copy);
        }
        return copies.ToArray();
    }
}
