using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Input.Raw;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.VisualTree;
using SoloRig.Ui;

namespace SoloRig.Ui.Avalonia;

public sealed partial class PedalWorkshopView
{
    public void ShowEdaExample()
    {
        Document.NewProjectFromTemplate("analog-blank");
        Document.DisplayName = "RC 低通滤波器";
        Document.ModuleId = "org.solorig.rc-lowpass";
        Document.BeginEditTransaction();
        try
        {
            var resistor = Document.AddAnalogComponent("resistor", 160, 160);
            resistor.Id = "R1";
            resistor.Value = 10000;
            resistor.PinA = "in";
            resistor.PinB = "out";
            var capacitor = Document.AddAnalogComponent("capacitor", 432, 208);
            capacitor.Id = "C1";
            capacitor.Value = 0.000000022;
            capacitor.Rotation = 90;
            capacitor.PinA = "out";
            capacitor.PinB = "0";
        }
        finally { Document.EndEditTransaction(); }
        SetCircuitTool("Select");
        ClearCircuitSelection();
        WorkshopTabs.SelectedIndex = 2;
        WorkshopStatusBand.IsVisible = false;
        UpdateLayout();
        RenderCircuitConnections();
        FitCircuitCanvas();
    }

    public void FitEdaView()
    {
        UpdateLayout();
        FitCircuitCanvas();
        RenderCircuitConnections();
    }

    public async Task<bool> ProbeEdaWorkflow(string outputDirectory)
    {
        static void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException("EDA: " + message);
        }

        Document.NewProjectFromTemplate("analog-blank");
        WorkshopTabs.SelectedIndex = 2;
        UpdateLayout();
        using var pointer = new Pointer(1, PointerType.Mouse, true);
        async Task Click(Control control, Point? local = null, Point? dragTo = null, bool middle = false, bool cancel = false)
        {
            UpdateLayout();
            // Hit testing consumes the composited scene from the previous frame.
            await Task.Delay(80);
            var position = control.TranslatePoint(local ?? new Point(control.Bounds.Width / 2, control.Bounds.Height / 2), this)
                ?? throw new InvalidOperationException("EDA control detached");
            var target = this.InputHitTest(position) as Interactive
                ?? throw new InvalidOperationException("EDA control cannot be hit");
            Require(ReferenceEquals(target, control) || control.IsVisualAncestorOf(target),
                $"hit target for {control.GetType().Name}: {target.GetType().Name}");
            var pressed = new PointerPointProperties(middle ? RawInputModifiers.MiddleMouseButton : RawInputModifiers.LeftMouseButton,
                middle ? PointerUpdateKind.MiddleButtonPressed : PointerUpdateKind.LeftButtonPressed);
            target.RaiseEvent(new PointerPressedEventArgs(target, pointer, this, position, 0,
                pressed, KeyModifiers.None, 1));
            if (dragTo is { } end)
            {
                position = control.TranslatePoint(end, this)!.Value;
                var moveTarget = pointer.Captured as Interactive ?? target;
                moveTarget.RaiseEvent(new PointerEventArgs(InputElement.PointerMovedEvent, moveTarget, pointer, this,
                    position, 1, pressed, KeyModifiers.None));
            }
            if (cancel) CircuitCanvasKeyDown(CircuitCanvasSurface, new KeyEventArgs { Key = Key.Escape });
            var releaseTarget = pointer.Captured as Interactive ?? target;
            releaseTarget.RaiseEvent(new PointerReleasedEventArgs(releaseTarget, pointer, this, position, 1,
                new PointerPointProperties(RawInputModifiers.None,
                    middle ? PointerUpdateKind.MiddleButtonReleased : PointerUpdateKind.LeftButtonReleased),
                KeyModifiers.None, middle ? MouseButton.Middle : MouseButton.Left));
        }

        CircuitComponentSearch.Text = "capacitor";
        FilterCircuitPalette();
        Require(CircuitComponentPalette.ItemCount == 1, "component search");
        CircuitComponentSearch.Text = string.Empty;
        FilterCircuitPalette();
        UpdateLayout();
        await Task.Delay(80);
        var center = new Point(CircuitCanvasSurface.Bounds.Width / 2, CircuitCanvasSurface.Bounds.Height / 2);
        var paletteItem = CircuitComponentPalette.GetVisualDescendants().OfType<Border>()
            .First(border => border.Tag is AnalogComponentDescriptor { Type: "resistor" });
        await Click(paletteItem);
        Require(placingCircuitComponent?.Type == "resistor", "palette click starts placement");
        await Click(CircuitCanvasSurface, center, center + new Point(32, 16), true);
        Require(circuitPanX == 32 && circuitPanY == 16 && placingCircuitComponent?.Type == "resistor",
            "middle-button pan preserves placement mode");
        await Click(CircuitCanvasSurface, center + new Point(32, 16), center, true);
        UpdateCircuitPlacementPreview(placingCircuitComponent!, center);
        Require(CircuitPaletteDragPreview.IsVisible && CircuitPlacementSymbol.Data is not null, "placement preview");
        await Click(CircuitCanvasSurface, center);
        var resistor = Document.AnalogCircuit.Components.Single();
        SetCircuitTool("Select");
        resistor.Id = "R9";
        var isolated = Document.AddAnalogComponent("resistor", 0, 0);
        Require(isolated.CanvasX == 0 && isolated.CanvasY == 0, "explicit origin placement");
        Require(!isolated.Pins.Select(pin => pin.Node).Intersect(resistor.Pins.Select(pin => pin.Node)).Any(),
            "new pins stay isolated after reference renaming");
        Document.RemoveAnalogComponent(isolated);
        resistor.Id = "R1";
        resistor.CanvasX = 160;
        resistor.CanvasY = 160;
        BeginCircuitPlacement(AnalogComponentCatalog.Get("capacitor"));
        UpdateCircuitPlacementPreview(placingCircuitComponent!, center);
        CircuitRotateClick(null, new RoutedEventArgs());
        Require(circuitPlacementRotation == 90 && CircuitPaletteDragPreview.Height > CircuitPaletteDragPreview.Width,
            "rotated placement preview");
        var capacitor = DropCircuitPaletteComponent(placingCircuitComponent!, center);
        Require(Document.Undo() && Document.AnalogCircuit.Components.Count == 1, "rotated placement atomic undo");
        RenderCircuitConnections();
        Require(selectedCircuitComponents.Count == 0 && !AnalogComponentList.IsVisible, "undo clears stale inspector");
        Require(Document.Redo(), "rotated placement redo");
        resistor = Document.AnalogCircuit.Components[0];
        capacitor = Document.AnalogCircuit.Components[1];
        SetCircuitTool("Select");
        capacitor.Id = "C1";
        capacitor.CanvasX = 432;
        capacitor.CanvasY = 208;
        SelectCircuitComponents([capacitor]);
        UpdateLayout();
        await Task.Delay(80);
        var capacitorCard = CircuitNodeLayer.GetVisualDescendants().OfType<Border>()
            .First(border => ReferenceEquals(border.Tag, capacitor));
        await Click(capacitorCard, new Point(48, 64), new Point(80, 80));
        Require(capacitor.CanvasX == 464 && capacitor.CanvasY == 224, "dragged symbol snaps to grid");
        Require(Document.Undo(), "component drag undo");
        resistor = Document.AnalogCircuit.Components[0];
        capacitor = Document.AnalogCircuit.Components[1];
        Require(capacitor.Rotation == 90 && capacitor.VisualWidth == capacitor.Descriptor.SymbolHeight,
            "rotation dimensions");
        UpdateLayout();
        await Task.Delay(80);
        for (var index = 0; index < capacitor.Pins.Count; index++)
        {
            var pin = capacitor.Pins[index];
            var visual = FindCircuitPinVisual(pin);
            var actual = visual?.TranslatePoint(new Point(7, 7), CircuitCanvasSurface);
            var expected = CircuitWorldToViewport(CircuitPinPoint(capacitor, index));
            Require(actual.HasValue && DistanceSquared(actual.Value, expected) < 1,
                $"rotated pin hit position: actual={actual}, expected={expected}, pin={pin.AnchorX},{pin.AnchorY}, " +
                $"nodeLayer={CircuitNodeLayer.Bounds}, canvas={CircuitCanvasSurface.Bounds}");
            foreach (var offset in new[] { new Point(-3, 0), new Point(3, 0), new Point(0, -3), new Point(0, 3) })
            {
                var hit = CircuitCanvasSurface.InputHitTest(expected + offset);
                Require(ReferenceEquals(hit, visual), $"pin hit area clipped at {offset}: {hit}; " +
                    string.Join(" / ", visual!.GetVisualAncestors().Take(7).Select(parent => $"{parent.GetType().Name}:clip={parent.ClipToBounds}")));
            }
        }
        SetCircuitTool("Input");
        await Click(FindCircuitPinVisual(resistor.Pins[0])!);
        SetCircuitTool("Ground");
        await Click(FindCircuitPinVisual(capacitor.Pins[1])!);
        await Click(FindCircuitPinVisual(resistor.Pins[1])!);
        await Click(CircuitCanvasSurface, CircuitWorldToViewport(new Point(384, 208)));
        await Click(FindCircuitPinVisual(capacitor.Pins[0])!);
        Require(resistor.PinB == capacitor.PinA && Document.AnalogCircuit.WireRoutes.Count == 1,
            "wire commit and bend");
        Require(Document.Undo(), "wire undo");
        Require(Document.AnalogCircuit.Components[0].PinB != Document.AnalogCircuit.Components[1].PinA &&
                Document.AnalogCircuit.WireRoutes.Count == 0, "atomic wire undo");
        Require(Document.Redo(), "wire redo");
        resistor = Document.AnalogCircuit.Components[0];
        capacitor = Document.AnalogCircuit.Components[1];
        SetCircuitTool("Output");
        ApplyCircuitTerminal(capacitor.Pins[0]);
        Require(resistor.PinB == "out" && capacitor.PinA == "out" && capacitor.PinB == "0", "terminal net merge");
        var project = Path.Combine(outputDirectory, "rc-workflow");
        Require(Document.SaveToDirectory(project), Document.Status);
        var reopened = new ModuleAuthoringDocument();
        Require(reopened.OpenFromDirectory(project), "reopen");
        Require(reopened.AnalogCircuit.Components[1].Rotation == 90 &&
                reopened.AnalogCircuit.Components[1].PinB == "0" && reopened.AnalogCircuit.WireRoutes.Count == 1,
            "rotation and wiring persistence");
        using (var backend = new BackendSession())
        {
            backend.ValidateModulePackage(project);
            Require(backend.AuditionModulePackage(project).OutputFinite, "native circuit processing");
        }
        SelectCircuitComponents([capacitor]);
        CircuitDuplicateClick(null, new RoutedEventArgs());
        Require(Document.AnalogCircuit.Components.Count == 3 &&
                Document.AnalogCircuit.Components[2].Rotation == 90 &&
                Document.AnalogCircuit.Components[2].PinA != capacitor.PinA, "duplicate rotation without shorting source");
        CircuitDeleteClick(null, new RoutedEventArgs());
        Require(Document.AnalogCircuit.Components.Count == 2, "delete component");
        var group = Document.DuplicateAnalogComponents(Document.AnalogCircuit.Components.ToArray());
        Require(group[0].PinB == group[1].PinA && group[0].PinB != resistor.PinB, "copied internal network is isolated");
        Require(Document.Undo() && Document.AnalogCircuit.Components.Count == 2, "group duplicate undo");
        var temporary = Document.AddAnalogComponent("resistor", 640, 320);
        Document.ConnectAnalogPins(temporary.Pins[0], temporary.Pins[1]);
        var temporaryNode = temporary.PinA;
        Document.GetOrCreateAnalogWireRoute(temporaryNode).Points.Add(new AnalogWirePointDraft(600, 320));
        Require(Document.RemoveAnalogComponent(temporary) && !Document.AnalogCircuit.Nodes.Contains(temporaryNode) &&
                !Document.AnalogCircuit.WireRoutes.Any(route => route.Node == temporaryNode),
            "deleting final pins removes unused solver node and route");
        resistor = Document.AnalogCircuit.Components[0];
        selectedCircuitNode = "out";
        CircuitDeleteClick(null, new RoutedEventArgs());
        Require(Document.AnalogCircuit.Components[0].PinB != Document.AnalogCircuit.Components[1].PinA,
            "delete network disconnects pins");
        Require(Document.Undo() && Document.AnalogCircuit.Components[0].PinB == "out" &&
                Document.AnalogCircuit.Components[1].PinA == "out", "network delete undo");
        resistor = Document.AnalogCircuit.Components[0];
        SetCircuitTool("Wire");
        pendingCircuitPin = resistor.Pins[0];
        pendingCircuitBends.Add(new Point(128, 128));
        var source = Document.LogicSource;
        CircuitCanvasKeyDown(CircuitCanvasSurface, new KeyEventArgs { Key = Key.Escape });
        Require(pendingCircuitPin is null && pendingCircuitBends.Count == 0 && Document.LogicSource == source,
            "cancel wire without editing circuit");
        await ProbeSchematicWiring(outputDirectory, Click);
        ShowEdaExample();
        Console.WriteLine("[PASS] EDA routed pointer input, placement, search, rotation, pin hit areas, wiring, terminals, history, save/reopen, network cleanup");
        return true;
    }

    private async Task ProbeSchematicWiring(string directory, Func<Control, Point?, Point?, bool, bool, Task> click)
    {
        static void Check(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException("Schematic wiring: " + message);
        }
        Task Click(Point point, Point? end = null, bool cancel = false) => click(CircuitCanvasSurface,
            CircuitWorldToViewport(point), end is { } target ? CircuitWorldToViewport(target) : null, false, cancel);
        void PressKey(Key key, KeyModifiers modifiers = KeyModifiers.None) =>
            CircuitCanvasKeyDown(CircuitCanvasSurface, new KeyEventArgs { Key = key, KeyModifiers = modifiers });
        Document.NewProjectFromTemplate("analog-blank");
        Document.DisplayName = "EDA wiring regression";
        SetCircuitTool("Select");
        circuitZoom = 1;
        circuitPanX = circuitPanY = 0;
        var capacitor = Document.AddAnalogComponent("capacitor", 224, 112);
        var load = Document.AddAnalogComponent("resistor", 592, 304);
        var source = Document.AddAnalogComponent("resistor", 128, 368);
        UpdateLayout();
        RenderCircuitConnections();

        AnalogWireRouteDraft Connect(AnalogPinDraft first, AnalogPinDraft last, params Point[] bends)
        {
            pendingCircuitPin = first;
            pendingCircuitBends.AddRange(bends);
            var endpoint = Document.AnalogPinEndpoint(last);
            var expected = BuildSchematicPath(PendingWireEndpoint()!, new Point(endpoint.X, endpoint.Y), last.Side, pendingCircuitBends);
            UpdateCircuitWirePreview(CircuitWorldToViewport(new Point(endpoint.X, endpoint.Y)));
            CompleteCircuitWire(last);
            var wire = Document.AnalogCircuit.WireRoutes.Last();
            Check(wire.IsExplicit && SchematicPath(wire).SequenceEqual(expected), "committed wire differs from preview");
            return wire;
        }

        var firstWire = Connect(source.Pins[1], capacitor.Pins[1], new Point(384, 416), new Point(384, 160));
        Check(SchematicPath(firstWire).SequenceEqual(new[]
        {
            new Point(256, 416), new Point(384, 416), new Point(384, 160), new Point(352, 160),
        }), "reference image path gained a loop or detached segment");
        var secondWire = Connect(capacitor.Pins[0], load.Pins[0],
            new Point(192, 160), new Point(192, 256), new Point(560, 256), new Point(560, 352));
        var unchanged = SchematicPath(secondWire).ToArray();
        SetCircuitTool("Input");
        ApplyCircuitTerminal(source.Pins[0]);
        SetCircuitTool("Output");
        ApplyCircuitTerminal(load.Pins[0]);
        SetCircuitTool("Ground");
        ApplyCircuitTerminal(load.Pins[1]);
        UpdateLayout();
        RenderCircuitConnections();
        Check(capacitor.PinA != capacitor.PinB && CircuitCrossingLayer.Children.Count > 0,
            "crossing wires were electrically joined or missing a crossing marker");
        await Task.Delay(80);
        CaptureWorkshopSurface(Path.Combine(directory, "wiring-reference"));

        var branchComponent = Document.AddAnalogComponent("resistor", 432, 464);
        SetCircuitTool("Ground");
        ApplyCircuitTerminal(branchComponent.Pins[1]);
        RenderCircuitConnections();
        var hit = FindSchematicWire(CircuitWorldToViewport(new Point(384, 320)));
        Check(hit is not null && ReferenceEquals(hit.Wire, firstWire), "wire segment snapping missed exact branch point");
        pendingCircuitPin = branchComponent.Pins[0];
        pendingCircuitBends.AddRange([new Point(416, 512), new Point(416, 320)]);
        CompleteSchematicBranch(hit!);
        var branch = Document.AnalogCircuit.WireRoutes.Last();
        var junction = branch.Points[^1];
        Check(junction.X == 384 && junction.Y == 320 && Document.AnalogCircuit.WireRoutes
            .SelectMany(wire => new[] { wire.Points[0], wire.Points[^1] }).Count(point => ReferenceEquals(point, junction)) == 3,
            "branch did not create one shared T junction at the selected point");
        Check(branchComponent.PinA == capacitor.PinB && capacitor.PinA != capacitor.PinB,
            "branch merged the wrong network at a crossing");
        Check(SchematicPath(secondWire).SequenceEqual(unchanged), "branch rerouted an unrelated wire");
        branchComponent.CanvasX += 64;
        branchComponent.CanvasY -= 32;
        RenderCircuitConnections();
        var moved = SchematicPath(branch);
        Check(moved[0] == new Point(branchComponent.CanvasX, branchComponent.CanvasY + 48) &&
            Enumerable.Range(1, moved.Count - 1).All(index => moved[index].X == moved[index - 1].X || moved[index].Y == moved[index - 1].Y),
            "moving a component detached the wire or introduced a diagonal");
        Check(SchematicPath(secondWire).SequenceEqual(unchanged), "moving a component changed an unrelated wire");
        UpdateLayout();
        await Task.Delay(80);
        CaptureWorkshopSurface(Path.Combine(directory, "wiring-branch"));
        var oldNode = capacitor.PinB;
        Check(Document.RemoveSchematicWire(branch) && branchComponent.PinA != oldNode && source.PinB == capacitor.PinB,
            "deleting a branch did not split only its electrical connection");
        Check(Document.Undo(), "branch deletion undo");
        Check(Document.AnalogCircuit.WireRoutes.SelectMany(wire => new[] { wire.Points[0], wire.Points[^1] })
            .GroupBy(point => point).Any(group => group.Count() == 3), "undo lost shared junction identity");
        var project = Path.Combine(directory, "wiring-project");
        Check(Document.SaveToDirectory(project), Document.Status);
        var reopened = new ModuleAuthoringDocument();
        Check(reopened.OpenFromDirectory(project) && reopened.AnalogCircuit.WireRoutes.Count == 4 &&
            reopened.AnalogCircuit.WireRoutes.All(wire => wire.IsExplicit), "wire persistence");
        Check(reopened.AnalogCircuit.WireRoutes.SelectMany(wire => new[] { wire.Points[0], wire.Points[^1] })
            .GroupBy(point => point).Any(group => group.Count() == 3), "reopen lost the T junction");
        using var backend = new BackendSession();
        backend.ValidateModulePackage(project);
        Check(backend.AuditionModulePackage(project).OutputFinite, "wired network cannot run");

        SetCircuitTool("Select");
        RenderCircuitConnections();
        await Click(new Point(464, 256), new Point(464, 288));
        var dragged = Document.AnalogCircuit.WireRoutes.First(wire => wire.Node == "out");
        Check(SchematicPath(dragged).Contains(new Point(560, 288)) && Document.IsDirty,
            $"routed segment drag did not move the selected horizontal line: {string.Join(";", SchematicPath(dragged))}, selected={selectedSchematicWire is not null}, dirty={Document.IsDirty}");
        Check(Document.Undo() && !Document.IsDirty, "segment drag undo did not restore the saved state");
        RenderCircuitConnections();
        await Click(new Point(464, 256), new Point(464, 288), true);
        Check(!Document.IsDirty && SchematicPath(Document.AnalogCircuit.WireRoutes.First(wire => wire.Node == "out"))
            .SequenceEqual(unchanged), "Escape changed a dragged line or its saved state");

        branchComponent = Document.AnalogCircuit.Components[3];
        branch = Document.AnalogCircuit.WireRoutes.Last();
        var rotationBefore = branchComponent.Rotation;
        foreach (var rotation in new[] { 90, 180, 270 })
        {
            branchComponent.Rotation = rotation;
            RenderCircuitConnections();
            var path = SchematicPath(branch);
            var pin = branchComponent.Pins[0];
            var first = CircuitPinPoint(branchComponent, 0);
            var body = new Rect(branchComponent.CanvasX + 1, branchComponent.CanvasY + 1,
                branchComponent.VisualWidth - 2, branchComponent.VisualHeight - 2);
            Check(path[0] == first && path[^1] == new Point(branch.Points[^1].X, branch.Points[^1].Y) &&
                RouteAvoidsObstacles(path, [body]), $"rotation {rotation} routed through its own component");
            Check(pin.Side switch
            {
                "left" => path[1].X < first.X && path[1].Y == first.Y,
                "right" => path[1].X > first.X && path[1].Y == first.Y,
                "top" => path[1].Y < first.Y && path[1].X == first.X,
                _ => path[1].Y > first.Y && path[1].X == first.X,
            }, $"rotation {rotation} ignored the pin direction");
        }
        branchComponent.Rotation = rotationBefore;
        RenderCircuitConnections();
        source = Document.AnalogCircuit.Components[2];
        capacitor = Document.AnalogCircuit.Components[0];
        var net = source.PinB;
        var extra = Connect(source.Pins[1], capacitor.Pins[1], new Point(320, 416), new Point(320, 208), new Point(384, 208));
        Check(Document.RemoveSchematicWire(extra) && source.PinB == net && capacitor.PinB == net,
            "removing a same-net alternate path disconnected the original path");

        var originalComponents = Document.AnalogCircuit.Components.ToArray();
        var originalWires = Document.AnalogCircuit.WireRoutes.ToArray();
        SelectCircuitComponents(originalComponents);
        PressKey(Key.D, KeyModifiers.Control);
        var duplicatedWires = Document.AnalogCircuit.WireRoutes.Skip(originalWires.Length).ToArray();
        Check(duplicatedWires.Length == originalWires.Length && duplicatedWires.SelectMany(wire => wire.Points)
            .Where(point => point.Component is not null).All(point => selectedCircuitComponents.Contains(point.Component!)) &&
            duplicatedWires.SelectMany(wire => new[] { wire.Points[0], wire.Points[^1] }).GroupBy(point => point)
                .Any(group => group.Count() == 3), "Ctrl+D lost wires, anchors or shared junctions");
        Check(Document.Undo(), "wired duplicate undo");
        SelectCircuitComponents(Document.AnalogCircuit.Components.ToArray());
        PressKey(Key.C, KeyModifiers.Control);
        var clipboardPaths = circuitClipboardWires.Select(wire => wire.Points.Select(point => new Point(point.X + 32, point.Y + 32)).ToArray()).ToArray();
        Document.NewProjectFromTemplate("analog-blank");
        PressKey(Key.V, KeyModifiers.Control);
        Check(Document.AnalogCircuit.Components.Count == 4 && Document.AnalogCircuit.WireRoutes.Count == 4 &&
            Document.AnalogCircuit.WireRoutes.Select((wire, index) => wire.Points.Select(point => new Point(point.X, point.Y))
                .SequenceEqual(clipboardPaths[index])).All(match => match), "Ctrl+C/V lost the geometry snapshot across projects");

        // Exercise line-source wiring and a click just outside the visible stroke at a different zoom.
        ClearCircuitSelection();
        SetCircuitTool("Wire");
        circuitZoom = 0.75;
        RenderCircuitConnections();
        await Click(new Point(416, 352));
        Check(pendingWireStart is not null, "wire tool did not start on an existing segment");
        var targetComponent = Document.AddAnalogComponent("resistor", 672, 432);
        UpdateLayout();
        RenderCircuitConnections();
        await click(FindCircuitPinVisual(targetComponent.Pins[0])!, null, null, false, false);
        Check(pendingCircuitPin is null && targetComponent.PinA == Document.AnalogCircuit.Components[0].PinB,
            "wire-source to pin did not attach to the selected branch");
        var newPin = targetComponent.Pins[1];
        await click(FindCircuitPinVisual(newPin)!, null, null, false, false);
        var edge = new Point(512, 288) + new Point(0, 9 / circuitZoom);
        UpdateCircuitWirePreview(CircuitWorldToViewport(edge));
        Check(CircuitWirePreviewTarget.IsVisible, "wire edge preview did not snap");
        await Click(edge);
        Check(pendingCircuitPin is null && newPin.Node == Document.AnalogCircuit.Components[0].PinA,
            "wire edge preview and click committed different targets");

        // Retain an isolated wire after its last pin is deleted, then reconnect it.
        Document.NewProjectFromTemplate("analog-blank");
        circuitZoom = 1;
        source = Document.AddAnalogComponent("resistor", 128, 112);
        var joint = new AnalogWirePointDraft(352, 160);
        Document.AddSchematicWire(source.PinB, [Document.AnalogPinEndpoint(source.Pins[1]), joint]);
        var floating = Document.AddSchematicWire(source.PinB, [joint, new AnalogWirePointDraft(448, 160)]);
        Document.RemoveAnalogComponent(source);
        UpdateLayout();
        RenderCircuitConnections();
        Check(circuitWirePaths.ContainsKey(floating), "a wire without remaining pins disappeared");
        SetCircuitTool("Wire");
        await Click(new Point(400, 160));
        targetComponent = Document.AddAnalogComponent("resistor", 576, 112);
        UpdateLayout();
        RenderCircuitConnections();
        await click(FindCircuitPinVisual(targetComponent.Pins[0])!, null, null, false, false);
        Check(pendingCircuitPin is null && floating.Node == targetComponent.PinA,
            "a floating wire could not be reconnected");

        ShowEdaExample();
        circuitZoom = 1;
        circuitPanX = circuitPanY = 0;
        RenderCircuitConnections();
        var legacySegment = renderedCircuitSegments.First(segment => segment.Node == "out" &&
            DistanceSquared(segment.Start, segment.End) > 4096);
        var legacyPoint = (legacySegment.Start + legacySegment.End) / 2;
        legacyPoint = new Point(SnapCircuitGrid(legacyPoint.X), SnapCircuitGrid(legacyPoint.Y));
        targetComponent = Document.AddAnalogComponent("resistor", 672, 432);
        UpdateLayout();
        RenderCircuitConnections();
        await click(FindCircuitPinVisual(targetComponent.Pins[0])!, null, null, false, false);
        await Click(legacyPoint);
        Check(pendingCircuitPin is null && targetComponent.PinA == "out" &&
            Document.AnalogCircuit.WireRoutes.All(wire => wire.IsExplicit), "branching from a legacy drawing failed");
        Check(Document.Undo() && Document.AnalogCircuit.WireRoutes.Count == 0,
            "legacy migration and branch were not one undo step");
        Console.WriteLine("[PASS] Explicit wires: reference layout, preview/commit, T junctions, crossings, drag/cancel, rotation, duplicate/clipboard, floating wires, legacy migration, history, persistence, native processing");
    }

    public void ShowEdaInspector(string type)
    {
        var component = Document.AnalogCircuit.Components.FirstOrDefault(component => component.Type == type)
            ?? Document.AddAnalogComponent(type, 656, 160);
        SelectCircuitComponents([component]);
        if (compactLayout) CircuitPalettePanel.IsVisible = false;
        CircuitInspectorPanel.IsVisible = true;
        UpdateLayout();
        CircuitInspectorPanel.Offset = new Vector(0, type == "bjt-npn" ? 440 : 0);
    }

    public bool ProbeEdaInspectorBounds()
    {
        var fields = AnalogComponentList.GetVisualDescendants().OfType<Control>()
            .Where(control => control.IsEffectivelyVisible && control.Classes.Contains("workshop-input")).ToArray();
        var viewport = CircuitVisibleViewport();
        return selectedCircuitComponent is { } selected &&
            viewport.Contains(CircuitWorldToViewport(new Point(selected.CanvasX + selected.VisualWidth / 2,
                selected.CanvasY + selected.VisualHeight / 2))) &&
            fields.Length > 0 && fields.All(field => field.Bounds.Width >= 90 &&
            field.TranslatePoint(default, CircuitInspectorPanel) is { } point && point.X >= 0 &&
            point.X + field.Bounds.Width <= CircuitInspectorPanel.Bounds.Width);
    }

    public bool ProbeEdaViewport()
    {
        UpdateLayout();
        var canvas = CircuitCanvasSurface.Bounds;
        var origin = CircuitCanvasSurface.TranslatePoint(default, this);
        var symbols = CircuitNodeLayer.GetVisualDescendants().OfType<Border>()
            .Where(border => border.Tag is AnalogComponentDraft).ToArray();
        Console.WriteLine($"[EDA geometry] canvas={canvas}, origin={origin}, root={Bounds}, zoom={circuitZoom}, " +
            string.Join("; ", symbols.Select(symbol => $"symbol={symbol.Tag}/{symbol.Bounds}, at={symbol.TranslatePoint(default, CircuitCanvasSurface)}, fill={symbol.Background}")));
        return canvas.Width >= 300 && canvas.Height >= 160 && origin is { } point &&
               point.Y + canvas.Height <= Bounds.Height && symbols.Length == 2 &&
               symbols.All(symbol => symbol.Background is ISolidColorBrush { Color.A: 0 }) &&
               symbols.All(symbol => symbol.TranslatePoint(default, CircuitCanvasSurface) is { } location &&
                   location.X >= 0 && location.Y >= 0 &&
                   location.X + symbol.Bounds.Width * circuitZoom <= canvas.Width + 1 &&
                   location.Y + symbol.Bounds.Height * circuitZoom <= canvas.Height + 1);
    }
}
