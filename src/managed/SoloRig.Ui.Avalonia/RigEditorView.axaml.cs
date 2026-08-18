using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using AvaloniaPath = Avalonia.Controls.Shapes.Path;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using Avalonia.Layout;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using SoloRig.Ui;
using System.Security.Cryptography;
using System.Text.Json;

namespace SoloRig.Ui.Avalonia;

public sealed partial class RigEditorView : UserControl
{
    private enum PendingChangesChoice
    {
        Cancel,
        Save,
        Discard,
    }

    private sealed record RenderedSurfaceFingerprint(int Width, int Height, string Sha256, int PngBytes);

    private sealed record ThemePalette(
        string Window,
        string Topbar,
        string Panel,
        string Canvas,
        string Overlay,
        string Control,
        string ControlHover,
        string Card,
        string InputPanel,
        string OutputPanel,
        string Border,
        string StrongBorder,
        string CardBorder,
        string Text,
        string Muted,
        string Warning,
        string Error,
        string Accent,
        string AccentHover,
        string AccentText,
        string Input,
        string InputOutline,
        string Output,
        string OutputOutline,
        string Track,
        string Knob,
        string Grid,
        string CanvasStart,
        string CanvasEnd,
        string AccentOutline,
        string ToolBorder);

    private static readonly ThemePalette DarkPalette = new(
        "#11151B", "#161B22", "#191F27", "#151C24", "#182029", "#222A34", "#2A3440",
        "#1B242D", "#1D2A38", "#282038", "#394655", "#3A4753", "#2C3946", "#EEF3F8",
        "#9AA8B8", "#FFD166", "#FF7B72", "#4DD6A4", "#69E2B5", "#10261F", "#66A7FF", "#E8F2FF", "#B889F5",
        "#FFF4FF", "#10151A", "#303C47", "#31414F", "#26313C", "#131820", "#D8FAED", "#637384");

    private static readonly ThemePalette LightPalette = new(
        "#F4F7FB", "#FFFFFF", "#EEF3F7", "#E6EDF4", "#DCE6EF", "#FFFFFF", "#E8F0F6",
        "#F8FAFC", "#EAF2F9", "#F1ECFB", "#C7D2DE", "#B5C2CF", "#C5D1DC", "#172033",
        "#5F6B78", "#A16207", "#B42318", "#0F766E", "#0B5F59", "#FFFFFF", "#2F80ED", "#EAF2FF", "#7C3AED",
        "#F5F3FF", "#CBD5E1", "#E2E8F0", "#B7C5D4", "#DDE7F0", "#F5F8FB", "#D1FAE5", "#94A3B8");

    private static readonly FilePickerFileType SrigFileType = new("SoloRig preset")
    {
        Patterns = ["*.srig"],
        MimeTypes = ["application/x-solorig-preset"]
    };
    private static readonly FilePickerFileType ResourceFileType = new("SoloRig resources")
    {
        Patterns = ["*.nam", "*.nam-a2", "*.ir", "*.wav", "*.json"]
    };

    private static readonly FilePickerFileType ThemeFileType = new("SoloRig theme")
    {
        Patterns = ["*.json"],
        MimeTypes = ["application/json"]
    };

    private RigEditorViewModel? viewModel;
    private DispatcherTimer? runtimeRefreshTimer;
    private bool dragging;
    private ModuleCatalogSnapshot? draggedModule;
    private Point moduleDragStart;
    private bool draggingModule;
    private bool moduleDragOverCanvas;
    private const double MinimumCanvasZoom = 0.6;
    private const double MaximumCanvasZoom = 1.8;
    private const double ConnectionSnapScreenRadius = 28.0;
    private double canvasZoom = 1.0;
    private double canvasPanX;
    private double canvasPanY;
    private bool panningCanvas;
    private Point canvasPanPointerStart;
    private double canvasPanOriginX;
    private double canvasPanOriginY;
    private bool draggingConnection;
    private string? draggingConnectionSource;
    private bool compactWorkspace;
    private bool moduleDrawerOpen;
    private bool inspectorDrawerOpen;
    private int workspaceFocusIndex = -1;
    private readonly Dictionary<TextBox, string> parameterEditBaselines = [];
    private readonly HashSet<string> pressedMomentaryTargets = [];

    public RigEditorView()
    {
        InitializeComponent();
        CanvasConnectionLayer.ContainerPrepared += ConnectionContainerPrepared;
        CanvasNodeLayer.ContainerPrepared += NodeContainerPrepared;
        ApplyTheme(ThemeSnapshot.Empty, true);
        ApplyCanvasTransform();
    }

    public RigEditorView(RigEditorViewModel editor)
        : this()
    {
        AttachEditor(editor);
    }

    public RigEditorViewModel? ViewModel => viewModel;
    public double CanvasZoom => canvasZoom;
    public Vector CanvasPan => new(canvasPanX, canvasPanY);
    public bool IsCompactWorkspace => compactWorkspace;

    public bool HasGraphVisualContent =>
        EditorCanvas is not null && CanvasConnectionLayer is not null && CanvasNodeLayer is not null && viewModel is not null &&
        viewModel.CanvasNodes.Count >= 2 && viewModel.CanvasConnections.Count >= 1 &&
        EditorCanvas.Bounds.Width > 0 && EditorCanvas.Bounds.Height > 0 &&
        CanvasConnectionLayer.Bounds.Width >= EditorCanvas.Bounds.Width - 0.5 &&
        CanvasConnectionLayer.Bounds.Height >= EditorCanvas.Bounds.Height - 0.5 &&
        CanvasNodeLayer.Bounds.Width >= EditorCanvas.Bounds.Width - 0.5 &&
        CanvasNodeLayer.Bounds.Height >= EditorCanvas.Bounds.Height - 0.5 &&
        CanvasNodeLayer.ItemCount == viewModel.CanvasNodes.Count &&
        Enumerable.Range(0, CanvasNodeLayer.ItemCount).All(index =>
            CanvasNodeLayer.ContainerFromIndex(index) is Control container &&
            container.Bounds.Width > 0 && container.Bounds.Height > 0 &&
            container.TranslatePoint(default, EditorCanvas) is { } origin &&
            origin.X + container.Bounds.Width > 0 && origin.Y + container.Bounds.Height > 0 &&
            origin.X < EditorCanvas.Bounds.Width && origin.Y < EditorCanvas.Bounds.Height);

    public bool HasEditorControlsVisualContent => HasGraphVisualContent &&
        ModuleCatalogList is not null && ModuleCatalogList.Bounds.Width > 0 &&
        ModuleCatalogList.ItemCount >= 2 && PerformanceText is not null &&
        PresetSelector is not null && PresetSelector.Bounds.Width > 0;

    public bool HasStandaloneVisualContent => HasEditorControlsVisualContent &&
        DocumentStatusText is not null && RealtimeStatusText is not null && WhiteboxStatusText is not null &&
        HasThemePalette &&
        !string.IsNullOrWhiteSpace(DocumentStatusText.Text) &&
        !string.IsNullOrWhiteSpace(RealtimeStatusText.Text) &&
        !string.IsNullOrWhiteSpace(WhiteboxStatusText.Text);

    public bool HasThemePalette => Resources["SrgWindowBrush"] is SolidColorBrush &&
        Resources["SrgTextBrush"] is SolidColorBrush &&
        Resources["SrgAccentBrush"] is SolidColorBrush;

    public string VisualRegressionFingerprint => BuildVisualRegressionFingerprint();

    public bool ProbeThemeSwitch()
    {
        if (viewModel is null || !HasThemePalette)
        {
            return false;
        }
        var originalDark = viewModel.DarkTheme;
        var originalWindow = (Resources["SrgWindowBrush"] as SolidColorBrush)?.Color;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        if (!viewModel.ToggleTheme(markDirty: false))
        {
            return false;
        }
        var switchedWindow = (Resources["SrgWindowBrush"] as SolidColorBrush)?.Color;
        var switchedFingerprint = BuildVisualRegressionFingerprint();
        var changed = viewModel.DarkTheme != originalDark && switchedWindow != originalWindow &&
                      switchedFingerprint != originalFingerprint;
        var restored = viewModel.ToggleTheme(markDirty: false) && viewModel.DarkTheme == originalDark &&
                       (Resources["SrgWindowBrush"] as SolidColorBrush)?.Color == originalWindow &&
                       BuildVisualRegressionFingerprint() == originalFingerprint;
        return changed && restored;
    }

    public bool ProbeExternalTheme()
    {
        if (viewModel is null || !HasThemePalette || string.IsNullOrWhiteSpace(viewModel.Theme.Id))
        {
            return false;
        }

        var originalTheme = viewModel.Theme;
        var originalWindow = (Resources["SrgWindowBrush"] as SolidColorBrush)?.Color;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var originalSelectedNode = viewModel.SelectedNode;
        (double X, double Y)? originalSelectedPoint = null;
        if (originalSelectedNode is not null &&
            viewModel.CanvasNodes.FirstOrDefault(node => node.InstanceId == originalSelectedNode.InstanceId) is { } selectedCanvasNode)
        {
            originalSelectedPoint = (selectedCanvasNode.CanvasX + 10.0, selectedCanvasNode.CanvasY + 10.0);
        }
        var customPath = Path.Combine(Path.GetTempPath(), $"solorig-theme-probe-{Guid.NewGuid():N}.json");
        var restorePath = Path.Combine(Path.GetTempPath(), $"solorig-theme-restore-{Guid.NewGuid():N}.json");
        try
        {
            File.WriteAllText(customPath, """
{
  "schemaVersion": 1,
  "id": "probe.external",
  "displayName": "Probe External",
  "colors": {
    "background": "#0F172A",
    "surface": "#1E293B",
    "text": "#F8FAFC",
    "accent": "#F97316",
    "window": "#0B1120",
    "canvas": "#111827"
  },
  "metrics": {
    "gridSize": 16,
    "cornerRadius": 8
  }
}
""");
            File.WriteAllText(restorePath, JsonSerializer.Serialize(new
            {
                schemaVersion = 1,
                id = originalTheme.Id,
                displayName = originalTheme.DisplayName,
                colors = originalTheme.Colors,
                metrics = new { gridSize = 16, cornerRadius = 8 }
            }));

            var loaded = viewModel.LoadTheme(customPath, markDirty: false);
            var applied = loaded && viewModel.Theme.Id == "probe.external" &&
                          (Resources["SrgWindowBrush"] as SolidColorBrush)?.Color == Color.Parse("#0B1120") &&
                          (Resources["SrgAccentBrush"] as SolidColorBrush)?.Color == Color.Parse("#F97316") &&
                          BuildVisualRegressionFingerprint() != originalFingerprint;
            var restoredTheme = viewModel.LoadTheme(restorePath, markDirty: false) && viewModel.Theme.Id == originalTheme.Id &&
                                (Resources["SrgWindowBrush"] as SolidColorBrush)?.Color == originalWindow;
            var restoredSelection = originalSelectedPoint is { } point
                ? viewModel.SelectAt(point.X, point.Y)
                : viewModel.ClearSelection();
            var restored = restoredTheme && restoredSelection && BuildVisualRegressionFingerprint() == originalFingerprint;
            return applied && restored;
        }
        catch
        {
            return false;
        }
        finally
        {
            try { File.Delete(customPath); } catch { }
            try { File.Delete(restorePath); } catch { }
        }
    }

    public bool ProbeParameterBinding()
    {
        if (viewModel is null)
        {
            return false;
        }
        var canvasNode = viewModel.CanvasNodes.FirstOrDefault();
        var sourceParameter = canvasNode?.Parameters.FirstOrDefault();
        if (canvasNode is null || sourceParameter is null || canvasNode.Controls.Count == 0 || !sourceParameter.IsValidRange)
        {
            return false;
        }
        if (!viewModel.SelectAt(canvasNode.CanvasX + 10.0, canvasNode.CanvasY + 10.0))
        {
            return false;
        }
        if (ParameterControlsList is null || ParameterControlsList.ItemCount == 0 || ParameterControlsList.Bounds.Width <= 0)
        {
            return false;
        }
        var selectedParameter = viewModel.SelectedParameters.FirstOrDefault(item => item.Id == sourceParameter.Id);
        if (selectedParameter is null || !selectedParameter.IsValidRange)
        {
            return false;
        }
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var original = selectedParameter.Value;
        var midpoint = (selectedParameter.Minimum + selectedParameter.Maximum) * 0.5F;
        var fraction = original <= midpoint ? 0.75F : 0.25F;
        var candidate = selectedParameter.Minimum +
                        (selectedParameter.Maximum - selectedParameter.Minimum) * fraction;
        if (Math.Abs(candidate - original) < 0.00001F)
        {
            candidate = midpoint;
        }
        if (Math.Abs(candidate - original) < 0.00001F ||
            !viewModel.SetParameter(selectedParameter.Target, candidate, markDirty: false))
        {
            return false;
        }
        var changed = viewModel.SelectedParameters.FirstOrDefault(item => item.Target == selectedParameter.Target);
        var changedControl = viewModel.CanvasNodes.SelectMany(item => item.Controls)
            .FirstOrDefault(item => item.Id == selectedParameter.Id);
        var changedCorrectly = changed is not null && changedControl is not null &&
                               Math.Abs(changed.Value - candidate) < 0.00001F &&
                               Math.Abs(changedControl.Value - candidate) < 0.00001F &&
                               BuildVisualRegressionFingerprint() != originalFingerprint;
        var restored = viewModel.SetParameter(selectedParameter.Target, original, markDirty: false);
        var restoredParameter = viewModel.SelectedParameters.FirstOrDefault(item => item.Target == selectedParameter.Target);
        var restoredControl = viewModel.CanvasNodes.SelectMany(item => item.Controls)
            .FirstOrDefault(item => item.Id == selectedParameter.Id);
        return changedCorrectly && restored && restoredParameter is not null && restoredControl is not null &&
               Math.Abs(restoredControl.Value - original) < 0.00001F &&
               Math.Abs(restoredParameter.Value - original) < 0.00001F &&
               BuildVisualRegressionFingerprint() == originalFingerprint;
    }

    public bool ProbeParameterEditingWorkflow()
    {
        if (viewModel is null || viewModel.CanvasNodes.Count == 0)
        {
            return false;
        }
        var originalSelectedId = viewModel.SelectedNode?.InstanceId;
        var originalTab = viewModel.InspectorTab;
        var originalDirty = viewModel.IsDirty;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var node = viewModel.CanvasNodes[0];
        var parameter = node.Parameters.FirstOrDefault(item => item.IsValidRange);
        if (parameter is null || !viewModel.SelectNode(node.InstanceId) || !viewModel.SelectInspectorTab("参数"))
        {
            return false;
        }
        UpdateLayout();
        var editor = viewModel.SelectedParameters.FirstOrDefault(item => item.Id == parameter.Id);
        if (editor is null || editor.CoarseStep <= 0 || editor.FineStep <= 0)
        {
            return false;
        }
        var originalValue = editor.Value;
        var target = editor.Target;
        var passed = false;
        try
        {
            var textBox = FindParameterTextBox(target);
            var slider = ParameterControlsList.GetVisualDescendants().OfType<Slider>()
                .FirstOrDefault(control => string.Equals(control.Tag as string, target, StringComparison.Ordinal));
            var nudgeButtons = ParameterControlsList.GetVisualDescendants().OfType<Button>()
                .Count(control => string.Equals(control.Tag as string, target, StringComparison.Ordinal));
            var chip = this.GetVisualDescendants().OfType<Button>().FirstOrDefault(control =>
                control.Classes.Contains("srg-parameter-chip") &&
                control.Tag is NodeControlSnapshot runtimeControl &&
                string.Equals(runtimeControl.Target, target, StringComparison.Ordinal));
            var capturePath = Environment.GetEnvironmentVariable("SOLO_RIG_PARAMETER_PROBE_CAPTURE");
            var captureReady = string.IsNullOrWhiteSpace(capturePath) || CaptureRenderedSurface(capturePath) is not null;
            var controlsReady = textBox is not null && slider is not null && nudgeButtons == 2 && chip is not null &&
                                Math.Abs(slider.SmallChange - editor.FineStep) < 0.000001 &&
                                Math.Abs(slider.LargeChange - editor.CoarseStep) < 0.000001 &&
                                FocusParameterEditor(target) && captureReady;

            var midpoint = editor.Minimum + (editor.Maximum - editor.Minimum) * 0.5F;
            var percentAccepted = viewModel.SetParameterText(target, "50%") &&
                                  Math.Abs(viewModel.SelectedParameters.First(item => item.Target == target).Value - midpoint) < 0.00001F;
            var fineNudge = viewModel.NudgeParameter(target, 1, fine: true);
            var nudged = viewModel.SelectedParameters.First(item => item.Target == target).Value;
            var nudgeCorrect = fineNudge && Math.Abs(nudged - Math.Min(midpoint + editor.FineStep, editor.Maximum)) < 0.00001F;
            var invalidRejected = !viewModel.SetParameterText(target, "not-a-number") &&
                                  Math.Abs(viewModel.SelectedParameters.First(item => item.Target == target).Value - nudged) < 0.00001F;
            passed = controlsReady && percentAccepted && nudgeCorrect && invalidRejected;
            if (!passed)
            {
                Console.Error.WriteLine($"[standalone-visual-probe] parameter editing detail: text={textBox is not null}, slider={slider is not null}, buttons={nudgeButtons}, chip={chip is not null}, small={slider?.SmallChange}, fine={editor.FineStep}, large={slider?.LargeChange}, coarse={editor.CoarseStep}, focus={controlsReady}, percent={percentAccepted}, nudge={nudgeCorrect}, invalid={invalidRejected}");
            }
        }
        finally
        {
            viewModel.SetParameter(target, originalValue, markDirty: false);
            if (viewModel.IsDirty != originalDirty)
            {
                viewModel.RestoreDocumentState(originalDirty);
            }
            viewModel.ClearHistory();
            if (originalSelectedId is null)
            {
                viewModel.ClearSelection();
            }
            else
            {
                viewModel.SelectNode(originalSelectedId);
            }
            viewModel.SelectInspectorTab(originalTab);
            UpdateLayout();
        }
        var restoredFingerprint = BuildVisualRegressionFingerprint();
        if (passed && restoredFingerprint != originalFingerprint)
        {
            Console.Error.WriteLine($"[standalone-visual-probe] parameter editing restore detail: original={originalFingerprint}; restored={restoredFingerprint}");
        }
        return passed && restoredFingerprint == originalFingerprint;
    }

    public bool ProbeDpiScaling()
    {
        if (viewModel is null || !viewModel.ProbeDpiScaling())
        {
            return false;
        }

        UpdateLayout();
        return HasGraphVisualContent;
    }

    public bool ProbeCanvasNavigation()
    {
        if (viewModel is null || CanvasNodeLayer is null || CanvasConnectionLayer is null ||
            viewModel.CanvasNodes.Count == 0 || EditorCanvas.Bounds.Width <= 0 || EditorCanvas.Bounds.Height <= 0)
        {
            return false;
        }
        var originalZoom = canvasZoom;
        var originalPanX = canvasPanX;
        var originalPanY = canvasPanY;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var originBefore = CanvasNodeLayer.ContainerFromIndex(0)?.TranslatePoint(default, EditorCanvas);
        var passed = false;
        try
        {
            ZoomCanvasAt(new Point(EditorCanvas.Bounds.Width * 0.5, EditorCanvas.Bounds.Height * 0.5), 1.25);
            UpdateLayout();
            var originAfterZoom = CanvasNodeLayer.ContainerFromIndex(0)?.TranslatePoint(default, EditorCanvas);
            var firstNode = viewModel.CanvasNodes[0];
            var zoomed = Math.Abs(canvasZoom - 1.25) < 0.00001 && CanvasZoomText.Text == "125%" &&
                         originBefore is { } before && originAfterZoom is { } after &&
                         (Math.Abs(after.X - before.X) > 0.5 || Math.Abs(after.Y - before.Y) > 0.5) &&
                         Math.Abs(after.X - (firstNode.CanvasX * canvasZoom + canvasPanX)) < 1.0 &&
                         Math.Abs(after.Y - (firstNode.CanvasY * canvasZoom + canvasPanY)) < 1.0;
            var zoomedLine = CanvasConnectionLayer.GetVisualDescendants().OfType<AvaloniaPath>()
                .FirstOrDefault(path => path.Classes.Contains("srg-connection-line"));
            var zoomedHit = CanvasConnectionLayer.GetVisualDescendants().OfType<AvaloniaPath>()
                .FirstOrDefault(path => path.Classes.Contains("srg-connection-hit"));
            var zoomedEndpoint = CanvasNodeLayer.GetVisualDescendants().OfType<Button>()
                .FirstOrDefault(button => button.Classes.Contains("srg-endpoint"));
            var zoomedCandidate = CanvasNodeLayer.GetVisualDescendants().OfType<Control>()
                .FirstOrDefault(control => control.Classes.Contains("srg-endpoint-candidate"));
            var renderScale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
            var zoomedEndpointScaleValue = (zoomedEndpoint?.RenderTransform as ScaleTransform)?.ScaleX ?? double.NaN;
            var zoomedCandidateScaleValue = (zoomedCandidate?.RenderTransform as ScaleTransform)?.ScaleX ?? double.NaN;
            var zoomedConnectionStable = zoomedLine is not null && zoomedHit is not null &&
                                         zoomedEndpoint?.RenderTransform is ScaleTransform zoomedEndpointScale &&
                                         zoomedCandidate?.RenderTransform is ScaleTransform zoomedCandidateScale &&
                                         Math.Abs(zoomedLine.StrokeThickness - 3.0 / 1.25) < 0.01 &&
                                         Math.Abs(zoomedHit.StrokeThickness - 18.0 / 1.25) < 0.01 &&
                                         Math.Abs(zoomedEndpointScale.ScaleX - 1.0 / 1.25) < 0.01 &&
                                         Math.Abs(zoomedCandidateScale.ScaleX - 1.0 / 1.25) < 0.01;
            var zoomedDpiInvariant = zoomedLine is not null && zoomedEndpoint is not null &&
                                     renderScale > 0.0 &&
                                     Math.Abs(zoomedLine.StrokeThickness * canvasZoom * renderScale - 3.0 * renderScale) < 0.05 &&
                                     Math.Abs(zoomedEndpoint.Bounds.Width * zoomedEndpointScaleValue *
                                              canvasZoom * renderScale - zoomedEndpoint.Bounds.Width * renderScale) < 0.05;
            SetCanvasViewport(MinimumCanvasZoom, canvasPanX, canvasPanY);
            var minimumEndpoint = CanvasNodeLayer.GetVisualDescendants().OfType<Button>()
                .FirstOrDefault(button => button.Classes.Contains("srg-endpoint"));
            var minimumCandidate = CanvasNodeLayer.GetVisualDescendants().OfType<Control>()
                .FirstOrDefault(control => control.Classes.Contains("srg-endpoint-candidate"));
            var minimumConnectionStable = CanvasConnectionLayer.GetVisualDescendants().OfType<AvaloniaPath>()
                .Where(path => path.Classes.Contains("srg-connection-line"))
                .All(path => Math.Abs(path.StrokeThickness - 3.0 / MinimumCanvasZoom) < 0.01) &&
                minimumEndpoint?.RenderTransform is ScaleTransform minimumEndpointScale &&
                minimumCandidate?.RenderTransform is ScaleTransform minimumCandidateScale &&
                Math.Abs(minimumEndpointScale.ScaleX - 1.0 / MinimumCanvasZoom) < 0.01 &&
                Math.Abs(minimumCandidateScale.ScaleX - 1.0 / MinimumCanvasZoom) < 0.01;
            SetCanvasViewport(MaximumCanvasZoom, canvasPanX, canvasPanY);
            var maximumEndpoint = CanvasNodeLayer.GetVisualDescendants().OfType<Button>()
                .FirstOrDefault(button => button.Classes.Contains("srg-endpoint"));
            var maximumCandidate = CanvasNodeLayer.GetVisualDescendants().OfType<Control>()
                .FirstOrDefault(control => control.Classes.Contains("srg-endpoint-candidate"));
            var maximumConnectionStable = CanvasConnectionLayer.GetVisualDescendants().OfType<AvaloniaPath>()
                .Where(path => path.Classes.Contains("srg-connection-line"))
                .All(path => Math.Abs(path.StrokeThickness - 3.0 / MaximumCanvasZoom) < 0.01) &&
                maximumEndpoint?.RenderTransform is ScaleTransform maximumEndpointScale &&
                maximumCandidate?.RenderTransform is ScaleTransform maximumCandidateScale &&
                Math.Abs(maximumEndpointScale.ScaleX - 1.0 / MaximumCanvasZoom) < 0.01 &&
                Math.Abs(maximumCandidateScale.ScaleX - 1.0 / MaximumCanvasZoom) < 0.01;
            SetCanvasViewport(1.25, canvasPanX, canvasPanY);
            var panBeforeX = canvasPanX;
            var panBeforeY = canvasPanY;
            PanCanvasBy(24, 18);
            var panned = Math.Abs(canvasPanX - panBeforeX - 24) < 0.001 &&
                         Math.Abs(canvasPanY - panBeforeY - 18) < 0.001;
            FitCanvasToNodes();
            var fitted = canvasZoom is >= MinimumCanvasZoom and <= MaximumCanvasZoom &&
                          string.Equals(CanvasZoomText.Text, $"{canvasZoom * 100:0}%", StringComparison.Ordinal);
            passed = zoomed && zoomedConnectionStable && zoomedDpiInvariant && minimumConnectionStable && maximumConnectionStable &&
                     panned && fitted;
            if (!passed)
            {
                Console.Error.WriteLine($"[standalone-visual-probe] canvas navigation detail: " +
                                        $"zoomed={zoomed}, connection={zoomedConnectionStable}, dpi={zoomedDpiInvariant}, " +
                                        $"minimum={minimumConnectionStable}, maximum={maximumConnectionStable}, " +
                                        $"scale={renderScale:0.###}, line={zoomedLine?.StrokeThickness:0.###}, hit={zoomedHit?.StrokeThickness:0.###}, " +
                                        $"endpoint={zoomedEndpoint?.Bounds.Width:0.###}/{zoomedEndpointScaleValue:0.###}, " +
                                        $"candidate={zoomedCandidate?.Bounds.Width:0.###}/{zoomedCandidateScaleValue:0.###}, canvasZoom={canvasZoom:0.###}");
            }
        }
        finally
        {
            SetCanvasViewport(originalZoom, originalPanX, originalPanY);
            UpdateLayout();
        }
        return passed && BuildVisualRegressionFingerprint() == originalFingerprint;
    }

    public bool ProbeResponsiveWorkspace()
    {
        if (Bounds.Width <= 0 || TopbarGrid.ColumnDefinitions.Count < 1 ||
            WorkspaceGrid.ColumnDefinitions.Count < 3)
        {
            return false;
        }

        var originalWidth = Bounds.Width;
        var originalCompact = compactWorkspace;
        var originalModuleDrawerOpen = moduleDrawerOpen;
        var originalInspectorDrawerOpen = inspectorDrawerOpen;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var passed = false;
        try
        {
            moduleDrawerOpen = false;
            inspectorDrawerOpen = false;
            UpdateResponsiveLayout(900);
            UpdateLayout();
            var compact = compactWorkspace &&
                          WorkspaceGrid.ColumnDefinitions[0].Width.Value == 0 &&
                          WorkspaceGrid.ColumnDefinitions[2].Width.Value == 0 &&
                          ModulePanelToggleButton.IsVisible && InspectorPanelToggleButton.IsVisible &&
                          !ModuleLibraryPanel.IsVisible && !InspectorPanel.IsVisible &&
                          Grid.GetColumn(ModuleLibraryPanel) == 1 && Grid.GetColumn(InspectorPanel) == 1;
            var compactFingerprint = BuildVisualRegressionFingerprint();

            ToggleModulePanel(900);
            UpdateLayout();
            var moduleDrawer = moduleDrawerOpen && !inspectorDrawerOpen &&
                               ModuleLibraryPanel.IsVisible && !InspectorPanel.IsVisible &&
                               ModuleLibraryPanel.HorizontalAlignment == HorizontalAlignment.Left &&
                               Math.Abs(ModuleLibraryPanel.Width - 286) < 0.01 &&
                               ModuleLibraryPanel.GetValue(Panel.ZIndexProperty) == 40;
            NodeNavigatorList.BringIntoView();
            UpdateLayout();
            var navigatorOrigin = NodeNavigatorList.TranslatePoint(default, ModuleLibraryPanel);
            var navigatorDrawerFits = navigatorOrigin is { } origin &&
                                      NodeNavigatorList.Bounds.Width > 0 && NodeNavigatorList.Bounds.Height > 0 &&
                                      origin.X >= 0 && origin.X + NodeNavigatorList.Bounds.Width <= ModuleLibraryPanel.Bounds.Width + 0.01 &&
                                      NodeNavigatorBypassButton.Bounds.Width > 0 && NodeNavigatorEnableButton.Bounds.Width > 0;
            var compactCapturePath = Environment.GetEnvironmentVariable("SOLO_RIG_NODE_NAVIGATOR_COMPACT_CAPTURE");
            var compactCaptureReady = string.IsNullOrWhiteSpace(compactCapturePath) ||
                                      CaptureRenderedSurface(compactCapturePath) is not null;
            var moduleFingerprint = BuildVisualRegressionFingerprint();

            ToggleInspectorPanel(900);
            UpdateLayout();
            var inspectorDrawer = !moduleDrawerOpen && inspectorDrawerOpen &&
                                  !ModuleLibraryPanel.IsVisible && InspectorPanel.IsVisible &&
                                  InspectorPanel.HorizontalAlignment == HorizontalAlignment.Right &&
                                  Math.Abs(InspectorPanel.Width - 320) < 0.01 &&
                                  InspectorPanel.GetValue(Panel.ZIndexProperty) == 40;
            var inspectorFingerprint = BuildVisualRegressionFingerprint();

            passed = compact && moduleDrawer && navigatorDrawerFits && compactCaptureReady && inspectorDrawer &&
                     compactFingerprint != originalFingerprint &&
                     moduleFingerprint != compactFingerprint &&
                     inspectorFingerprint != moduleFingerprint;
        }
        finally
        {
            moduleDrawerOpen = originalModuleDrawerOpen;
            inspectorDrawerOpen = originalInspectorDrawerOpen;
            UpdateResponsiveLayout(originalWidth);
            if (originalCompact)
            {
                moduleDrawerOpen = originalModuleDrawerOpen;
                inspectorDrawerOpen = originalInspectorDrawerOpen;
                UpdateResponsiveLayout(originalWidth);
            }
            UpdateLayout();
        }

        return passed && compactWorkspace == originalCompact &&
               moduleDrawerOpen == originalModuleDrawerOpen &&
               inspectorDrawerOpen == originalInspectorDrawerOpen &&
               BuildVisualRegressionFingerprint() == originalFingerprint;
    }

    public bool ProbeRenderedSurface()
    {
        if (viewModel is null || !HasStandaloneVisualContent || Bounds.Width <= 0 || Bounds.Height <= 0)
        {
            var containers = CanvasNodeLayer is null
                ? "missing"
                : string.Join(";", Enumerable.Range(0, CanvasNodeLayer.ItemCount).Select(index =>
                {
                    var container = CanvasNodeLayer.ContainerFromIndex(index) as Control;
                    var origin = container?.TranslatePoint(default, EditorCanvas);
                    return $"{index}:bounds={container?.Bounds},origin={origin}";
                }));
            Console.Error.WriteLine($"[standalone-visual-probe] rendered surface precondition: viewModel={viewModel is not null}, standalone={HasStandaloneVisualContent}, bounds={Bounds}, canvas={EditorCanvas?.Bounds}, connectionLayer={CanvasConnectionLayer?.Bounds}, nodeLayer={CanvasNodeLayer?.Bounds}, items={CanvasNodeLayer?.ItemCount}, containers={containers}");
            return false;
        }

        var original = CaptureRenderedSurface();
        var repeated = CaptureRenderedSurface();
        if (original is null || repeated is null || original != repeated)
        {
            Console.Error.WriteLine($"[standalone-visual-probe] rendered surface detail: original={original}, repeated={repeated}");
            return false;
        }

        var originalDark = viewModel.DarkTheme;
        if (!viewModel.ToggleTheme(markDirty: false))
        {
            return false;
        }

        try
        {
            UpdateLayout();
            var switched = CaptureRenderedSurface();
            if (switched is null || switched == original || !viewModel.ToggleTheme(markDirty: false))
            {
                Console.Error.WriteLine($"[standalone-visual-probe] rendered surface theme detail: original={original}, switched={switched}, dark={viewModel.DarkTheme}, expected={originalDark}");
                return false;
            }
            UpdateLayout();
            if (viewModel.DarkTheme != originalDark)
            {
                Console.Error.WriteLine($"[standalone-visual-probe] rendered surface theme restore state: dark={viewModel.DarkTheme}, expected={originalDark}");
                return false;
            }
            var restored = CaptureRenderedSurface();
            if (restored != original)
            {
                Console.Error.WriteLine($"[standalone-visual-probe] rendered surface restore detail: original={original}, restored={restored}");
                return false;
            }
            var capturePath = Environment.GetEnvironmentVariable("SOLO_RIG_VISUAL_PROBE_CAPTURE");
            if (!string.IsNullOrWhiteSpace(capturePath) && CaptureRenderedSurface(capturePath) is null)
            {
                Console.Error.WriteLine($"[standalone-visual-probe] failed to save rendered surface: {capturePath}");
                return false;
            }
            return true;
        }
        finally
        {
            if (viewModel.DarkTheme != originalDark)
            {
                viewModel.ToggleTheme(markDirty: false);
            }
        }
    }

    public bool ProbeKeyboardShortcuts()
    {
        if (viewModel is null)
        {
            return false;
        }
        var originalDark = viewModel.DarkTheme;
        var handled = HandleShortcut(Key.T, KeyModifiers.None, markDirty: false);
        var toggled = handled && viewModel.DarkTheme != originalDark;
        var modifierBaseline = viewModel.DarkTheme;
        var modifiedHandled = HandleShortcut(Key.T, KeyModifiers.Control);
        var modifierIgnored = !modifiedHandled && viewModel.DarkTheme == modifierBaseline;
        var originalFocusIndex = workspaceFocusIndex;
        var expectedForwardIndex = (originalFocusIndex + 1 + 5) % 5;
        var expectedBackwardIndex = (expectedForwardIndex - 1 + 5) % 5;
        var focusForward = HandleShortcut(Key.F6, KeyModifiers.None, markDirty: false) &&
                           workspaceFocusIndex == expectedForwardIndex;
        var focusBackward = HandleShortcut(Key.F6, KeyModifiers.Shift, markDirty: false) &&
                            workspaceFocusIndex == expectedBackwardIndex;
        workspaceFocusIndex = originalFocusIndex;
        if (viewModel.DarkTheme != originalDark)
        {
            viewModel.ToggleTheme(markDirty: false);
        }
        return toggled && modifierIgnored && focusForward && focusBackward && viewModel.DarkTheme == originalDark;
    }

    public bool ProbeKeyboardAccessibility()
    {
        if (viewModel is null || viewModel.CanvasNodes.Count == 0 || CanvasNodeLayer.ItemCount == 0)
        {
            return false;
        }

        UpdateLayout();
        var firstNode = viewModel.CanvasNodes[0];
        var nodeControl = FindCanvasNodeControl(firstNode.InstanceId);
        var controlsReady = CanvasSurface.Focusable && ConnectionToolPanel.Focusable &&
                            nodeControl?.Focusable == true &&
                            !string.IsNullOrWhiteSpace(CanvasSurface.GetValue(AutomationProperties.NameProperty)) &&
                            !string.IsNullOrWhiteSpace(ConnectionToolPanel.GetValue(AutomationProperties.NameProperty)) &&
                            !string.IsNullOrWhiteSpace(nodeControl.GetValue(AutomationProperties.NameProperty));
        if (!controlsReady)
        {
            return false;
        }

        var originalSelectedId = viewModel.SelectedNode?.InstanceId;
        var originalDirty = viewModel.IsDirty;
        var originalSource = viewModel.ConnectionSourceNodeId;
        var originalDestination = viewModel.ConnectionDestinationNodeId;
        var originalX = firstNode.CanvasX;
        var originalY = firstNode.CanvasY;
        var moved = false;
        var restoredPosition = false;
        try
        {
            moved = MoveCanvasNodeByKeyboard(firstNode.InstanceId, 8, 0);
            var shifted = viewModel.CanvasNodes.First(node => node.InstanceId == firstNode.InstanceId);
            moved = moved && Math.Abs(shifted.CanvasX - originalX - 8) < 0.01 &&
                    Math.Abs(shifted.CanvasY - originalY) < 0.01;
            restoredPosition = MoveCanvasNodeByKeyboard(firstNode.InstanceId, -8, 0);
            var restored = viewModel.CanvasNodes.First(node => node.InstanceId == firstNode.InstanceId);
            restoredPosition = restoredPosition && Math.Abs(restored.CanvasX - originalX) < 0.01 &&
                               Math.Abs(restored.CanvasY - originalY) < 0.01;

            SelectConnectionSource(firstNode.InstanceId);
            var endpointKeyboardState = viewModel.ConnectionSourceNodeId == firstNode.InstanceId;
            return moved && restoredPosition && endpointKeyboardState;
        }
        finally
        {
            viewModel.ConnectionSourceNodeId = originalSource;
            viewModel.ConnectionDestinationNodeId = originalDestination;
            if (originalSelectedId is null)
            {
                viewModel.ClearSelection();
            }
            else
            {
                viewModel.SelectNode(originalSelectedId);
            }
            if (viewModel.IsDirty != originalDirty)
            {
                viewModel.RestoreDocumentState(originalDirty);
            }
            viewModel.ClearHistory();
            UpdateLayout();
        }
    }

    public bool ProbeSelectionState()
    {
        if (viewModel is null)
        {
            return false;
        }

        var originalFilter = viewModel.ModuleCatalogFilter;
        var originalTab = viewModel.InspectorTab;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var probePassed = false;
        try
        {
            var filterChanged = viewModel.SetModuleCatalogFilter("模拟") &&
                                viewModel.ModuleCatalogFilter == "模拟" &&
                                AnalogFilterButton.Classes.Contains("selected") &&
                                !DigitalFilterButton.Classes.Contains("selected") &&
                                !ExternalFilterButton.Classes.Contains("selected");
            var tabChanged = viewModel.SelectInspectorTab("端口") &&
                             viewModel.InspectorTab == "端口" &&
                             PortInspectorButton.Classes.Contains("selected") &&
                             !ParameterInspectorButton.Classes.Contains("selected") &&
                             !ResourceInspectorButton.Classes.Contains("selected");
            probePassed = filterChanged && tabChanged;
        }
        finally
        {
            viewModel.SetModuleCatalogFilter(originalFilter);
            viewModel.SelectInspectorTab(originalTab);
            UpdateSelectionStyles();
        }
        var filterRestored = AnalogFilterButton.Classes.Contains("selected") == (originalFilter == "模拟") &&
                             DigitalFilterButton.Classes.Contains("selected") == (originalFilter == "数字") &&
                             ExternalFilterButton.Classes.Contains("selected") == (originalFilter == "外部");
        var tabRestored = ParameterInspectorButton.Classes.Contains("selected") == (originalTab == "参数") &&
                          PortInspectorButton.Classes.Contains("selected") == (originalTab == "端口") &&
                          ResourceInspectorButton.Classes.Contains("selected") == (originalTab == "资源");
        return probePassed && BuildVisualRegressionFingerprint() == originalFingerprint && filterRestored && tabRestored;
    }

    public bool ProbeInspectorDetails()
    {
        if (viewModel is null)
        {
            return false;
        }

        var originalSelected = viewModel.SelectedNode?.InstanceId;
        var originalTab = viewModel.InspectorTab;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var passed = false;
        try
        {
            if (!viewModel.SelectNode("drive-1") || !viewModel.SelectInspectorTab("端口"))
            {
                return false;
            }
            UpdateSelectionStyles();
            UpdateLayout();
            var portsVisible = PortInspectorList.IsVisible && PortInspectorList.ItemCount == 2 &&
                               viewModel.SelectedPorts.Count == 2 &&
                               Enumerable.Range(0, PortInspectorList.ItemCount).All(index =>
                                   PortInspectorList.ContainerFromIndex(index) is Control container &&
                                   container.Bounds.Width > 0 && container.Bounds.Height > 0);
            if (!viewModel.SelectInspectorTab("资源"))
            {
                return false;
            }
            UpdateSelectionStyles();
            UpdateLayout();
            var resourceVisible = ResourceKindText.IsVisible && ResourceKindText.Text == "内置模块" &&
                                  ResourcePathText.Text == "Embedded runtime" &&
                                  ResourceStateText.Text == "就绪";
            passed = portsVisible && resourceVisible;
        }
        finally
        {
            if (originalSelected is null)
            {
                viewModel.ClearSelection();
            }
            else
            {
                viewModel.SelectNode(originalSelected);
            }
            viewModel.SelectInspectorTab(originalTab);
            UpdateSelectionStyles();
            UpdateLayout();
        }
        return passed && BuildVisualRegressionFingerprint() == originalFingerprint;
    }

    public bool ProbeConnectionSelection()
    {
        if (viewModel is null || viewModel.CanvasConnections.Count == 0)
        {
            return false;
        }

        var original = viewModel.SelectedConnection;
        var originalSource = viewModel.ConnectionSourceNodeId;
        var originalDestination = viewModel.ConnectionDestinationNodeId;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var target = viewModel.CanvasConnections[0];
        if (!viewModel.SelectConnection(target.SourceInstanceId, target.DestinationInstanceId))
        {
            return false;
        }

        var selected = viewModel.SelectedConnection is { } selectedConnection &&
                       selectedConnection.SourceInstanceId == target.SourceInstanceId &&
                       selectedConnection.DestinationInstanceId == target.DestinationInstanceId &&
                       selectedConnection.Selected &&
                       viewModel.ConnectionSelectionText.Contains(target.SourceInstanceId, StringComparison.Ordinal);
        UpdateLayout();
        var renderedConnectionPaths = CanvasConnectionLayer?.GetVisualDescendants().OfType<AvaloniaPath>().ToArray() ?? [];
        var hasArrowAndHitTarget = renderedConnectionPaths.Any(path => path.Data is StreamGeometry) &&
                                   renderedConnectionPaths.Any(path => path.StrokeThickness >= target.HitTestThickness - 0.01);
        var restored = original is null
            ? viewModel.ClearConnectionSelection()
            : viewModel.SelectConnection(original.SourceInstanceId, original.DestinationInstanceId);
        viewModel.ConnectionSourceNodeId = originalSource;
        viewModel.ConnectionDestinationNodeId = originalDestination;
        return selected && hasArrowAndHitTarget && restored &&
               BuildVisualRegressionFingerprint() == originalFingerprint &&
               (viewModel.SelectedConnection?.SourceInstanceId ?? string.Empty) == (original?.SourceInstanceId ?? string.Empty);
    }

    public bool ProbeTopologyEditor()
    {
        if (viewModel is null || viewModel.ConnectionNodeIds.Count < 2)
        {
            return false;
        }
        var originalSource = viewModel.ConnectionSourceNodeId;
        var originalDestination = viewModel.ConnectionDestinationNodeId;
        var originalDragging = draggingConnection;
        var originalDraggingSource = draggingConnectionSource;
        var originalZoom = canvasZoom;
        var originalPanX = canvasPanX;
        var originalPanY = canvasPanY;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var passed = false;
        try
        {
            viewModel.ConnectionSourceNodeId = "drive-1";
            viewModel.ConnectionDestinationNodeId = "delay-1";
            UpdateLayout();
            var duplicateTarget = viewModel.CanvasNodes.FirstOrDefault(node => node.InstanceId == "delay-1");
            var duplicateRejected = ConnectionSourceSelector.SelectedItem as string == "drive-1" &&
                                    ConnectionDestinationSelector.SelectedItem as string == "delay-1" &&
                                    !CanvasConnectButton.IsEnabled &&
                                    ConnectionCandidateStatusText.Text?.Contains("已存在", StringComparison.Ordinal) == true &&
                                    duplicateTarget?.IsConnectionDestinationInvalid == true;
            draggingConnection = true;
            draggingConnectionSource = "drive-1";
            UpdateConnectionPreview(GetInputEndpointPoint("delay-1"));
            var duplicatePreviewMarker = ConnectionPreviewTarget.IsVisible &&
                                          ConnectionPreviewTarget.Stroke == (Resources["SrgErrorBrush"] as IBrush) &&
                                          ConnectionPreviewPath.IsVisible &&
                                          ConnectionPreviewPath.Stroke == (Resources["SrgErrorBrush"] as IBrush) &&
                                          ConnectionPreviewHalo.IsVisible &&
                                          ConnectionPreviewHalo.Data is StreamGeometry;
            SetCanvasViewport(MinimumCanvasZoom, canvasPanX, canvasPanY);
            var targetPoint = GetInputEndpointPoint("delay-1");
            var minimumRadius = ConnectionScreenScale(ConnectionSnapScreenRadius);
            var minimumSnap = FindInputEndpoint(new Point(targetPoint.X + minimumRadius * 0.8, targetPoint.Y)) == "delay-1";
            var minimumOutside = FindInputEndpoint(new Point(targetPoint.X + minimumRadius * 1.2, targetPoint.Y)) != "delay-1";
            SetCanvasViewport(MaximumCanvasZoom, canvasPanX, canvasPanY);
            targetPoint = GetInputEndpointPoint("delay-1");
            var maximumRadius = ConnectionScreenScale(ConnectionSnapScreenRadius);
            var maximumSnap = FindInputEndpoint(new Point(targetPoint.X + maximumRadius * 0.8, targetPoint.Y)) == "delay-1";
            var maximumOutside = FindInputEndpoint(new Point(targetPoint.X + maximumRadius * 1.2, targetPoint.Y)) != "delay-1";
            var snapInvariant = minimumSnap && minimumOutside && maximumSnap && maximumOutside;
            SetCanvasViewport(1.0, canvasPanX, canvasPanY);
            viewModel.ConnectionSourceNodeId = "delay-1";
            viewModel.ConnectionDestinationNodeId = "drive-1";
            UpdateLayout();
            var cycleRejected = !CanvasConnectButton.IsEnabled &&
                                ConnectionCandidateStatusText.Text?.Contains("环路", StringComparison.Ordinal) == true;
            passed = duplicateRejected && duplicatePreviewMarker && snapInvariant && cycleRejected;
        }
        finally
        {
            draggingConnection = originalDragging;
            draggingConnectionSource = originalDraggingSource;
            if (!originalDragging)
            {
                HideConnectionPreview();
            }
            SetCanvasViewport(originalZoom, originalPanX, originalPanY);
            viewModel.ConnectionSourceNodeId = originalSource;
            viewModel.ConnectionDestinationNodeId = originalDestination;
            UpdateLayout();
        }
        return passed && BuildVisualRegressionFingerprint() == originalFingerprint;
    }

    public bool ProbeDocumentStatus()
    {
        if (viewModel is null || DocumentStatusText is null)
        {
            return false;
        }

        var canvasNode = viewModel.CanvasNodes.FirstOrDefault();
        var sourceParameter = canvasNode?.Parameters.FirstOrDefault(parameter => parameter.IsValidRange);
        if (canvasNode is null || sourceParameter is null)
        {
            return false;
        }

        if (!viewModel.SelectAt(canvasNode.CanvasX + 10.0, canvasNode.CanvasY + 10.0))
        {
            return false;
        }

        var originalValue = sourceParameter.Value;
        var originalDirty = viewModel.IsDirty;
        var originalFileStatus = viewModel.FileStatus;
        var originalWindowTitle = viewModel.WindowTitle;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        (double X, double Y)? originalPoint = null;
        if (viewModel.CanvasNodes.FirstOrDefault(node => node.InstanceId == canvasNode.InstanceId) is { } originalCanvasNode)
        {
            originalPoint = (originalCanvasNode.CanvasX + 10.0, originalCanvasNode.CanvasY + 10.0);
        }
        var midpoint = (sourceParameter.Minimum + sourceParameter.Maximum) * 0.5F;
        var fraction = originalValue <= midpoint ? 0.75F : 0.25F;
        var candidate = sourceParameter.Minimum +
                        (sourceParameter.Maximum - sourceParameter.Minimum) * fraction;
        if (Math.Abs(candidate - originalValue) < 0.00001F)
        {
            candidate = midpoint;
        }

        if (Math.Abs(candidate - originalValue) < 0.00001F ||
            !viewModel.SetParameter($"{canvasNode.InstanceId}.{sourceParameter.Id}", candidate))
        {
            return false;
        }

        var dirtyMarkerVisible = viewModel.IsDirty &&
                                 DocumentStatusText.Text?.Contains("*", StringComparison.Ordinal) == true &&
                                 viewModel.WindowTitle.Contains("*", StringComparison.Ordinal);
        var restored = viewModel.SetParameter($"{canvasNode.InstanceId}.{sourceParameter.Id}", originalValue, markDirty: false) &&
                       viewModel.RestoreDocumentState(originalDirty);
        if (restored)
        {
            restored = originalPoint is { } point
                ? viewModel.SelectAt(point.X, point.Y)
                : viewModel.SelectAt(5.0, 5.0);
        }
        return dirtyMarkerVisible && restored &&
               viewModel.IsDirty == originalDirty &&
               viewModel.FileStatus == originalFileStatus &&
               viewModel.WindowTitle == originalWindowTitle &&
               BuildVisualRegressionFingerprint() == originalFingerprint;
    }

    public bool ProbeActionGates()
    {
        if (viewModel is null || CatalogAddButton is null || CatalogDuplicateButton is null ||
            CatalogDeleteButton is null || CanvasAddModuleButton is null || CanvasConnectButton is null ||
            CanvasDisconnectButton is null || InspectorDuplicateButton is null || InspectorDeleteButton is null)
        {
            return false;
        }

        var originalNode = viewModel.SelectedNode;
        (double X, double Y)? originalPoint = null;
        if (originalNode is not null &&
            viewModel.CanvasNodes.FirstOrDefault(node => node.InstanceId == originalNode.InstanceId) is { } node)
        {
            originalPoint = (node.CanvasX + 10.0, node.CanvasY + 10.0);
        }
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var probePassed = false;
        var cleared = false;
        var graphActions = false;
        var selected = false;
        var clearButtonDuplicate = true;
        var clearButtonDelete = true;
        try
        {
            viewModel.ClearSelection();
            clearButtonDuplicate = CatalogDuplicateButton.IsEnabled;
            clearButtonDelete = CatalogDeleteButton.IsEnabled;
            cleared = viewModel.SelectedNode is null &&
                      !clearButtonDuplicate && !clearButtonDelete &&
                      !InspectorDuplicateButton.IsEnabled && !InspectorDeleteButton.IsEnabled;
            graphActions = viewModel.CanAddModule && CatalogAddButton.IsEnabled && CanvasAddModuleButton.IsEnabled &&
                           CanvasConnectButton.IsEnabled == viewModel.CanConnectNodes &&
                           CanvasDisconnectButton.IsEnabled == viewModel.CanDisconnectNodes;
            var probeNode = viewModel.CanvasNodes.FirstOrDefault();
            selected = probeNode is not null && viewModel.SelectAt(probeNode.CanvasX + 10.0, probeNode.CanvasY + 10.0) &&
                       viewModel.SelectedNode?.InstanceId == probeNode.InstanceId &&
                       viewModel.CanDuplicateSelected && viewModel.CanDeleteSelected &&
                       CatalogDuplicateButton.IsEnabled && CatalogDeleteButton.IsEnabled &&
                       InspectorDuplicateButton.IsEnabled && InspectorDeleteButton.IsEnabled;
            probePassed = cleared && graphActions && selected;
        }
        finally
        {
            if (originalPoint is { } point)
            {
                viewModel.SelectAt(point.X, point.Y);
            }
            else
            {
                viewModel.ClearSelection();
            }
        }

        var restored = BuildVisualRegressionFingerprint() == originalFingerprint &&
                       (viewModel.SelectedNode?.InstanceId ?? string.Empty) == (originalNode?.InstanceId ?? string.Empty);
        if (!probePassed || !restored)
        {
            Console.Error.WriteLine($"[standalone-visual-probe] action gates detail: cleared={cleared}, graph={graphActions}, selected={selected}, restored={restored}");
        }
        return probePassed && restored;
    }

    public bool ProbeNodeNavigatorBatchWorkflow()
    {
        if (viewModel is null || NodeNavigatorList is null || NodeNavigatorSearchBox is null ||
            NodeNavigatorSelectAllButton is null || NodeNavigatorClearButton is null ||
            NodeNavigatorBypassButton is null || NodeNavigatorEnableButton is null ||
            viewModel.NodeNavigatorEntries.Count < 2)
        {
            return false;
        }

        var originalFingerprint = BuildVisualRegressionFingerprint();
        var originalSearch = viewModel.NodeNavigatorSearchText;
        var originalSelectedNodeId = viewModel.SelectedNode?.InstanceId;
        var originalCanvasZoom = canvasZoom;
        var originalCanvasPanX = canvasPanX;
        var originalCanvasPanY = canvasPanY;
        var originalBatchSelection = viewModel.NodeNavigatorEntries
            .Where(entry => entry.IsBatchSelected)
            .Select(entry => entry.InstanceId)
            .ToHashSet(StringComparer.Ordinal);
        var originalBypass = viewModel.NodeNavigatorEntries.ToDictionary(
            entry => entry.InstanceId, entry => entry.Node.Bypassed, StringComparer.Ordinal);
        var originalDirty = viewModel.IsDirty;
        var batchApplied = false;
        var batchUndone = false;
        var passed = false;
        try
        {
            viewModel.ClearBatchNodeSelection();
            viewModel.NodeNavigatorSearchText = "drive-1";
            NodeNavigatorList.BringIntoView();
            UpdateLayout();
            var filtered = viewModel.FilteredNodeNavigatorEntries.Count == 1 &&
                           viewModel.FilteredNodeNavigatorEntries[0].InstanceId == "drive-1";
            var materialized = NodeNavigatorList.ItemCount == 1 &&
                               NodeNavigatorList.ContainerFromIndex(0) is Control container &&
                               container.Bounds.Width > 0 && container.Bounds.Height > 0;
            var navigatorItem = NodeNavigatorList.ContainerFromIndex(0) as Control;
            var navigatorCheckBox = navigatorItem?.GetVisualDescendants().OfType<CheckBox>().FirstOrDefault();
            var navigatorLocateButton = navigatorItem?.GetVisualDescendants().OfType<Button>().FirstOrDefault(button =>
                string.Equals(button.Tag as string, "drive-1", StringComparison.Ordinal));
            var navigatorCheckBoxName = navigatorCheckBox?.GetValue(AutomationProperties.NameProperty) ?? string.Empty;
            var navigatorLocateName = navigatorLocateButton?.GetValue(AutomationProperties.NameProperty) ?? string.Empty;
            var automationNamed = navigatorCheckBoxName.Contains("drive-1", StringComparison.Ordinal) &&
                                  navigatorLocateName.Contains("drive-1", StringComparison.Ordinal);
            viewModel.ClearSelection();
            UpdateLayout();
            var activeNavigatorItem = NodeNavigatorList.ContainerFromIndex(0) as Control;
            var activeLocateButton = activeNavigatorItem?.GetVisualDescendants().OfType<Button>().FirstOrDefault(button =>
                string.Equals(button.Tag as string, "drive-1", StringComparison.Ordinal));
            if (activeLocateButton is not null)
            {
                NodeNavigatorLocateClick(activeLocateButton, new RoutedEventArgs());
            }
            var located = viewModel.SelectedNode?.InstanceId == "drive-1";
            var mediumLayoutFits = NodeNavigatorList.Bounds.Width > 0 &&
                                   NodeNavigatorList.Bounds.Width <= ModuleLibraryPanel.Bounds.Width + 0.01 &&
                                   NodeNavigatorSelectAllButton.Bounds.Width > 0 && NodeNavigatorClearButton.Bounds.Width > 0;
            var capturePath = Environment.GetEnvironmentVariable("SOLO_RIG_NODE_NAVIGATOR_PROBE_CAPTURE");
            var captureReady = string.IsNullOrWhiteSpace(capturePath) ||
                               CaptureRenderedSurface(capturePath) is not null;
            var emptyGates = !NodeNavigatorClearButton.IsEnabled &&
                             !NodeNavigatorBypassButton.IsEnabled && !NodeNavigatorEnableButton.IsEnabled;

            viewModel.SelectAllFilteredNodes();
            UpdateLayout();
            var oneSelected = viewModel.BatchSelectedNodeCount == 1 &&
                              NodeNavigatorClearButton.IsEnabled &&
                              NodeNavigatorBypassButton.IsEnabled == viewModel.CanBatchBypassNodes &&
                              NodeNavigatorEnableButton.IsEnabled == viewModel.CanBatchEnableNodes;

            viewModel.NodeNavigatorSearchText = string.Empty;
            viewModel.SelectAllFilteredNodes();
            var beforeBatch = viewModel.NodeNavigatorEntries.ToDictionary(
                entry => entry.InstanceId, entry => entry.Node.Bypassed, StringComparer.Ordinal);
            var bypass = viewModel.NodeNavigatorEntries.Any(entry => !entry.Node.Bypassed);
            UpdateLayout();
            var targetGate = bypass ? NodeNavigatorBypassButton.IsEnabled : NodeNavigatorEnableButton.IsEnabled;
            batchApplied = targetGate && viewModel.SetBatchNodesBypassed(bypass);
            NodeNavigatorList.BringIntoView();
            UpdateLayout();
            var projected = batchApplied &&
                            NodeNavigatorList.ItemCount == viewModel.NodeNavigatorEntries.Count &&
                            viewModel.NodeNavigatorEntries.All(entry =>
                                entry.IsBatchSelected && entry.Node.Bypassed == bypass) &&
                            viewModel.BatchSelectedNodeCount == viewModel.NodeNavigatorEntries.Count;
            batchUndone = batchApplied && viewModel.Undo();
            var restoredByUndo = batchUndone && viewModel.NodeNavigatorEntries.All(entry =>
                beforeBatch.TryGetValue(entry.InstanceId, out var original) && entry.Node.Bypassed == original);
            passed = filtered && materialized && automationNamed && located && mediumLayoutFits && captureReady && emptyGates &&
                     oneSelected && targetGate && projected && restoredByUndo;
            if (!passed)
            {
                Console.Error.WriteLine($"[standalone-visual-probe] node navigator assertions: filtered={filtered}, materialized={materialized}, automation={automationNamed}, located={located}, layout={mediumLayoutFits}, capture={captureReady}, emptyGates={emptyGates}, oneSelected={oneSelected}, targetGate={targetGate}, projected={projected}, undo={restoredByUndo}");
            }
        }
        finally
        {
            if (batchApplied && !batchUndone)
            {
                viewModel.Undo();
            }
            viewModel.NodeNavigatorSearchText = originalSearch;
            viewModel.ClearBatchNodeSelection();
            foreach (var entry in viewModel.NodeNavigatorEntries.Where(entry =>
                         originalBatchSelection.Contains(entry.InstanceId)))
            {
                entry.IsBatchSelected = true;
            }
            viewModel.RestoreDocumentState(originalDirty);
            viewModel.ClearHistory();
            if (originalSelectedNodeId is null)
            {
                viewModel.ClearSelection();
            }
            else
            {
                viewModel.SelectNode(originalSelectedNodeId);
            }
            SetCanvasViewport(originalCanvasZoom, originalCanvasPanX, originalCanvasPanY);
            UpdateLayout();
        }

        var currentFingerprint = BuildVisualRegressionFingerprint();
        var restored = viewModel.NodeNavigatorEntries.All(entry =>
                           originalBypass.TryGetValue(entry.InstanceId, out var bypassed) &&
                           entry.Node.Bypassed == bypassed) &&
                       currentFingerprint == originalFingerprint;
        if (!passed || !restored)
        {
            Console.Error.WriteLine($"[standalone-visual-probe] node navigator detail: passed={passed}, restored={restored}, items={NodeNavigatorList.ItemCount}, selected={viewModel.BatchSelectedNodeCount}");
        }
        return passed && restored;
    }

    public bool ProbeModuleInsertion()
    {
        if (viewModel is null)
        {
            return false;
        }

        UpdateLayout();
        if (!HasGraphVisualContent || CaptureRenderedSurface() is not { } originalSurface)
        {
            Console.Error.WriteLine($"[standalone-visual-probe] module insertion precondition: graph={HasGraphVisualContent}, canvas={EditorCanvas.Bounds}, nodeLayer={CanvasNodeLayer.Bounds}, items={CanvasNodeLayer.ItemCount}");
            return false;
        }

        var originalDirty = viewModel.IsDirty;
        var originalSelectedCatalogModule = viewModel.SelectedCatalogModule;
        var originalNodeIds = viewModel.CanvasNodes.Select(node => node.InstanceId).ToHashSet(StringComparer.Ordinal);
        ModuleCatalogSnapshot? probeModule = viewModel.FilteredModuleCatalog.FirstOrDefault();
        CanvasNodeSnapshot? addedNode = null;
        var added = false;
        try
        {
            if (probeModule is null)
            {
                return false;
            }
            viewModel.SelectedCatalogModule = probeModule;
            viewModel.AddModuleCommand.Execute(null);
            addedNode = viewModel.CanvasNodes.FirstOrDefault(node => !originalNodeIds.Contains(node.InstanceId));
            if (addedNode is null)
            {
                return false;
            }
            added = true;
            var scale = Math.Max(viewModel.Snapshot.DpiScale, 1.0);
            var visibleWidth = viewModel.Snapshot.ViewportWidth / scale;
            var visibleHeight = viewModel.Snapshot.ViewportHeight / scale;
            var projectedInsideCanvas = addedNode.CanvasX >= 0.0 && addedNode.CanvasY >= 0.0 &&
                                         addedNode.CanvasX + addedNode.Width <= visibleWidth + 0.001 &&
                                         addedNode.CanvasY + addedNode.Height <= visibleHeight + 0.001;
            var linkedToChain = viewModel.CanvasConnections.Any(connection =>
                connection.DestinationInstanceId == addedNode.InstanceId);
            var projectedIntoChain = viewModel.EffectChainNodes.Any(node => node.InstanceId == addedNode.InstanceId);
            UpdateLayout();
            var addedIndex = viewModel.CanvasNodes
                .Select((node, index) => (node, index))
                .First(item => item.node.InstanceId == addedNode.InstanceId).index;
            var addedContainer = CanvasNodeLayer?.ContainerFromIndex(addedIndex) as Control;
            var addedOrigin = addedContainer?.TranslatePoint(default, EditorCanvas);
            var projectedCanvasControl = addedContainer is not null && addedOrigin is { } origin &&
                                         addedContainer.Bounds.Width > 0 && addedContainer.Bounds.Height > 0 &&
                                         origin.X + addedContainer.Bounds.Width > 0 &&
                                         origin.Y + addedContainer.Bounds.Height > 0 &&
                                         origin.X < EditorCanvas.Bounds.Width && origin.Y < EditorCanvas.Bounds.Height;
            var afterSurface = CaptureRenderedSurface();
            var passed = projectedInsideCanvas && linkedToChain && projectedIntoChain &&
                         projectedCanvasControl &&
                         afterSurface is not null && afterSurface != originalSurface;
            if (!passed)
            {
                Console.Error.WriteLine($"[standalone-visual-probe] module insertion detail: inside={projectedInsideCanvas}, linked={linkedToChain}, chain={projectedIntoChain}, canvasControl={projectedCanvasControl}, container={addedContainer?.Bounds}, origin={addedOrigin}, surfaceChanged={afterSurface is not null && afterSurface != originalSurface}");
            }
            return passed;
        }
        finally
        {
            if (added && addedNode is not null)
            {
                viewModel.DeleteNode(addedNode.InstanceId);
            }
            viewModel.RestoreDocumentState(originalDirty);
            viewModel.ClearHistory();
            viewModel.SelectedCatalogModule = originalSelectedCatalogModule;
            UpdateLayout();
        }
    }

    public bool ProbeRuntimeControlInteraction()
    {
        if (viewModel is null || CanvasNodeLayer is null)
        {
            return false;
        }
        var originalNodeId = viewModel.SelectedNode?.InstanceId;
        var originalDirty = viewModel.IsDirty;
        var node = viewModel.CanvasNodes.FirstOrDefault(candidate =>
            candidate.Parameters.Any(parameter => parameter.IsValidRange));
        if (node is null)
        {
            return false;
        }
        var parameter = node.Parameters.First(parameterValue => parameterValue.IsValidRange);
        var target = $"{node.InstanceId}.{parameter.Id}";
        var originalValue = parameter.Value;
        try
        {
            if (!viewModel.SelectNode(node.InstanceId))
            {
                return false;
            }
            UpdateLayout();
            var nodeControl = FindCanvasNodeControl(node.InstanceId);
            var runtimeButtons = nodeControl?.GetVisualDescendants().OfType<Button>()
                .Where(button => button.Tag is NodeControlSnapshot)
                .ToArray() ?? [];
            var runtimeControl = viewModel.CanvasNodes.Single(candidate => candidate.InstanceId == node.InstanceId)
                .Controls.FirstOrDefault(control => control.Target == target);
            var materialized = runtimeControl is not null && runtimeButtons.Any(button =>
                button.Tag is NodeControlSnapshot control && control.Target == target &&
                !string.IsNullOrWhiteSpace(button.GetValue(AutomationProperties.NameProperty)));
            var toggled = runtimeControl is not null && viewModel.ToggleParameter(target);
            var changed = toggled && Math.Abs(viewModel.CanvasNodes.Single(candidate => candidate.InstanceId == node.InstanceId)
                .Controls.First(control => control.Target == target).Value - originalValue) > 0.00001F;
            var restored = viewModel.SetParameter(target, originalValue, markDirty: false) &&
                           viewModel.RestoreDocumentState(originalDirty);
            var passed = materialized && changed && restored;
            if (!passed)
            {
                Console.Error.WriteLine($"[standalone-visual-probe] runtime control detail: buttons={runtimeButtons.Length}, materialized={materialized}, changed={changed}, restored={restored}, element={runtimeControl?.IsElement}");
            }
            return passed;
        }
        finally
        {
            viewModel.SetParameter(target, originalValue, markDirty: false);
            viewModel.RestoreDocumentState(originalDirty);
            viewModel.ClearHistory();
            if (originalNodeId is null)
            {
                viewModel.ClearSelection();
            }
            else
            {
                viewModel.SelectNode(originalNodeId);
            }
            UpdateLayout();
        }
    }

    public bool ProbeModuleDragPreview()
    {
        if (viewModel is null || ModuleDragPreview is null || ModuleDragPreviewTitle is null ||
            EditorCanvas.Bounds.Width <= 0 || EditorCanvas.Bounds.Height <= 0)
        {
            return false;
        }

        var module = viewModel.FilteredModuleCatalog.FirstOrDefault();
        if (module is null)
        {
            return false;
        }

        var originalTitle = ModuleDragPreviewTitle.Text;
        try
        {
            UpdateModuleDragPreview(new Point(EditorCanvas.Bounds.Width * 0.5, EditorCanvas.Bounds.Height * 0.5), module.DisplayName);
            var shown = ModuleDragPreview.IsVisible &&
                        moduleDragOverCanvas &&
                        ModuleDragPreviewTitle.Text == module.DisplayName;
            HideModuleDragPreview();
            return shown && !ModuleDragPreview.IsVisible && !moduleDragOverCanvas;
        }
        finally
        {
            HideModuleDragPreview();
            ModuleDragPreviewTitle.Text = originalTitle;
        }
    }

    public bool ProbeDeviceAvailabilityState()
    {
        if (viewModel is null || AsioDeviceStatusText is null || MidiDeviceStatusText is null)
        {
            return false;
        }

        var statusVisible = !string.IsNullOrWhiteSpace(AsioDeviceStatusText.Text) &&
                            !string.IsNullOrWhiteSpace(MidiDeviceStatusText.Text);
        var asioGateSafe = viewModel.HasAsioDrivers || !viewModel.CanOpenAsio;
        var midiGateSafe = viewModel.HasMidiDevices ||
                           (!viewModel.CanSelectMidiDevice && !viewModel.CanOpenMidi);
        return statusVisible && asioGateSafe && midiGateSafe;
    }

    public bool ProbePresetLibrary()
    {
        if (viewModel is null || PresetLibraryList is null || PresetLibraryLoadButton is null)
        {
            return false;
        }

        var originalDirectory = viewModel.PresetLibraryDirectory;
        var originalIndex = viewModel.SelectedPresetLibraryIndex;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var probeDirectory = Path.Combine(Path.GetTempPath(), $"solorig-preset-library-probe-{Guid.NewGuid():N}");
        Directory.CreateDirectory(probeDirectory);
        var firstPath = Path.Combine(probeDirectory, "01-clean.srig");
        var secondPath = Path.Combine(probeDirectory, "02-edited.srig");
        var invalidPath = Path.Combine(probeDirectory, "99-invalid.srig");
        var probePassed = false;
        try
        {
            using (var seed = new BackendSession())
            {
                seed.Save(firstPath);
                seed.SetParameter("drive-1.drive", 5.0F);
                seed.Save(secondPath);
            }
            File.WriteAllText(invalidPath, "{not-json");

            if (!viewModel.LoadPresetLibrary(probeDirectory) || PresetLibraryList.ItemCount != 3)
            {
                return false;
            }
            var invalidIndex = viewModel.PresetLibraryEntries
                .Select((entry, index) => (entry, index))
                .FirstOrDefault(item => !item.entry.IsValid).index;
            viewModel.SelectedPresetLibraryIndex = invalidIndex;
            var invalidGate = !viewModel.CanLoadSelectedPresetLibraryEntry && !PresetLibraryLoadButton.IsEnabled;
            var validIndex = viewModel.PresetLibraryEntries
                .Select((entry, index) => (entry, index))
                .FirstOrDefault(item => item.entry.IsValid).index;
            viewModel.SelectedPresetLibraryIndex = validIndex;
            var validGate = viewModel.CanLoadSelectedPresetLibraryEntry && PresetLibraryLoadButton.IsEnabled;
            probePassed = invalidGate && validGate && viewModel.PresetLibraryEntries.Count(entry => entry.IsValid) == 2;
        }
        catch
        {
            probePassed = false;
        }
        finally
        {
            if (string.IsNullOrWhiteSpace(originalDirectory))
            {
                viewModel.ClearPresetLibrary();
            }
            else if (viewModel.LoadPresetLibrary(originalDirectory))
            {
                viewModel.SelectedPresetLibraryIndex = originalIndex;
            }
            try { Directory.Delete(probeDirectory, true); } catch { }
        }
        return probePassed && BuildVisualRegressionFingerprint() == originalFingerprint &&
               viewModel.SelectedPresetLibraryIndex == originalIndex &&
               string.Equals(viewModel.PresetLibraryDirectory, originalDirectory, StringComparison.OrdinalIgnoreCase);
    }

    public bool ProbeUndoRedo()
    {
        if (viewModel is null || UndoButton is null || RedoButton is null)
        {
            return false;
        }

        var canvasNode = viewModel.CanvasNodes.FirstOrDefault(node => node.Parameters.Any(parameter => parameter.IsValidRange));
        var parameter = canvasNode?.Parameters.FirstOrDefault(item => item.IsValidRange);
        if (canvasNode is null || parameter is null)
        {
            return false;
        }

        var originalValue = parameter.Value;
        var originalDirty = viewModel.IsDirty;
        var originalFingerprint = BuildVisualRegressionFingerprint();
        var candidate = parameter.Minimum + (parameter.Maximum - parameter.Minimum) *
            (originalValue <= (parameter.Minimum + parameter.Maximum) * 0.5F ? 0.75F : 0.25F);
        if (Math.Abs(candidate - originalValue) < 0.00001F)
        {
            candidate = (parameter.Minimum + parameter.Maximum) * 0.5F;
        }
        var target = $"{canvasNode.InstanceId}.{parameter.Id}";
        var passed = false;
        try
        {
            if (!viewModel.SetParameter(target, candidate) ||
                viewModel.CanUndo != true || viewModel.CanRedo ||
                !UndoButton.IsEnabled || RedoButton.IsEnabled)
            {
                return false;
            }

            var changed = viewModel.Snapshot.Nodes.First(node => node.InstanceId == canvasNode.InstanceId)
                .Parameters.First(item => item.Id == parameter.Id).Value;
            var undo = viewModel.Undo();
            var undone = viewModel.Snapshot.Nodes.First(node => node.InstanceId == canvasNode.InstanceId)
                .Parameters.First(item => item.Id == parameter.Id).Value;
            var undoGate = undo && Math.Abs(undone - originalValue) < 0.00001F &&
                           viewModel.CanRedo && RedoButton.IsEnabled;

            var redo = viewModel.Redo();
            var redone = viewModel.Snapshot.Nodes.First(node => node.InstanceId == canvasNode.InstanceId)
                .Parameters.First(item => item.Id == parameter.Id).Value;
            var redoGate = redo && Math.Abs(redone - candidate) < 0.00001F &&
                           viewModel.CanUndo && !viewModel.CanRedo && UndoButton.IsEnabled && !RedoButton.IsEnabled;

            passed = Math.Abs(changed - candidate) < 0.00001F && undoGate && redoGate && viewModel.Undo();
        }
        catch
        {
            passed = false;
        }
        finally
        {
            viewModel.SetParameter(target, originalValue, markDirty: false);
            if (viewModel.IsDirty != originalDirty)
            {
                viewModel.RestoreDocumentState(originalDirty);
            }
            viewModel.ClearHistory();
        }

        var restored = Math.Abs(viewModel.Snapshot.Nodes.First(node => node.InstanceId == canvasNode.InstanceId)
                           .Parameters.First(item => item.Id == parameter.Id).Value - originalValue) < 0.00001F &&
                       viewModel.IsDirty == originalDirty &&
                       !viewModel.CanUndo && !viewModel.CanRedo &&
                       BuildVisualRegressionFingerprint() == originalFingerprint;
        return passed && restored;
    }

    private RenderedSurfaceFingerprint? CaptureRenderedSurface(string? outputPath = null)
    {
        var topLevel = TopLevel.GetTopLevel(this);
        var renderScale = topLevel?.RenderScaling ?? 1.0;
        if (!double.IsFinite(renderScale) || renderScale <= 0 ||
            !double.IsFinite(Bounds.Width) || !double.IsFinite(Bounds.Height))
        {
            return null;
        }

        var pixelSize = new PixelSize(
            Math.Max(1, (int)Math.Ceiling(Bounds.Width * renderScale)),
            Math.Max(1, (int)Math.Ceiling(Bounds.Height * renderScale)));
        try
        {
            using var bitmap = new RenderTargetBitmap(pixelSize, new Vector(96 * renderScale, 96 * renderScale));
            bitmap.Render(this);
            using var stream = new MemoryStream();
            bitmap.Save(stream);
            var png = stream.ToArray();
            if (png.Length < 32 || png[0] != 0x89 || png[1] != 0x50 || png[2] != 0x4E || png[3] != 0x47)
            {
                return null;
            }
            if (!string.IsNullOrWhiteSpace(outputPath))
            {
                var directory = Path.GetDirectoryName(outputPath);
                if (!string.IsNullOrWhiteSpace(directory))
                {
                    Directory.CreateDirectory(directory);
                }
                File.WriteAllBytes(outputPath, png);
            }
            return new RenderedSurfaceFingerprint(
                pixelSize.Width,
                pixelSize.Height,
                Convert.ToHexString(SHA256.HashData(png)),
                png.Length);
        }
        catch
        {
            return null;
        }
    }

    private string BuildVisualRegressionFingerprint()
    {
        if (viewModel is null)
        {
            return string.Empty;
        }
        var resourceKeys = new[] { "SrgWindowBrush", "SrgCanvasBrush", "SrgTextBrush", "SrgAccentBrush", "SrgKnobBrush" };
        var resources = string.Join(",", resourceKeys.Select(key =>
            $"{key}={(Resources[key] as SolidColorBrush)?.Color.ToString() ?? "missing"}"));
        var nodes = string.Join("|", viewModel.CanvasNodes.Select(node =>
            $"{node.InstanceId}:{node.Selected}:{node.Unresolved}:{node.Bypassed}:{node.FootswitchText}:" +
            $"candidate={node.IsConnectionSourceCandidate},{node.IsConnectionDestinationValid},{node.IsConnectionDestinationInvalid}:skin={node.Skin.Id}:" +
            $"images={node.Skin.HasKnobImage},{node.Skin.HasToggleImage},{node.Skin.HasFootswitchImage}:" +
            string.Join(",", node.Controls.Select(control =>
                $"{control.Id}:{control.Value:0.######}:{control.KnobAngle:0.###}:{control.IsToggle}"))));
        var library = string.Join("|", viewModel.PresetLibraryEntries.Select(entry =>
            $"{entry.DisplayName}:{entry.IsValid}:{entry.SourcePath}"));
        var chain = string.Join("|", viewModel.EffectChainNodes.Select(node =>
            $"{node.InstanceId}:{node.Selected}:{node.Bypassed}"));
        var navigator = string.Join("|", viewModel.NodeNavigatorEntries.Select(entry =>
            $"{entry.InstanceId}:{entry.IsBatchSelected}:{entry.Node.Bypassed}"));
        var connections = string.Join("|", viewModel.CanvasConnections.Select(connection =>
            $"{connection.SourceInstanceId}>{connection.DestinationInstanceId}:{connection.Selected}:" +
            $"stroke={connection.StrokeThickness:0.###}:halo={connection.HaloThickness:0.###}:hit={connection.HitTestThickness:0.###}"));
        var inspectorDetails = viewModel.SelectedNode is null
            ? "none"
            : $"{viewModel.SelectedNode.InstanceId}:ports={string.Join(",", viewModel.SelectedPorts.Select(port => port.DisplayText))}:" +
              $"resource={viewModel.SelectedResource?.Kind}:{viewModel.SelectedResource?.Path}:{viewModel.SelectedResource?.Status}";
        var workspace = $"{compactWorkspace}:{moduleDrawerOpen}:{inspectorDrawerOpen}:" +
                        $"columns={WorkspaceGrid.ColumnDefinitions[0].Width.Value:0.###},{WorkspaceGrid.ColumnDefinitions[2].Width.Value:0.###}:" +
                        $"visible={ModuleLibraryPanel.IsVisible},{InspectorPanel.IsVisible}";
        return $"theme={viewModel.Theme.Id};dark={viewModel.DarkTheme};file={viewModel.FileStatus};filter={viewModel.ModuleCatalogFilter};" +
               $"canvas={canvasZoom:0.###}:{canvasPanX:0.###},{canvasPanY:0.###};" +
               $"workspace={workspace};" +
               $"inspector={viewModel.InspectorTab}:{inspectorDetails};" +
               $"topology={viewModel.ConnectionSourceNodeId}>{viewModel.ConnectionDestinationNodeId}:{viewModel.ConnectionCandidateStatusText};" +
               $"navigator={viewModel.NodeNavigatorSearchText}:{navigator};" +
               $"library={viewModel.PresetLibraryDirectory}:{viewModel.SelectedPresetLibraryIndex}:{library};resources={resources};" +
               $"nodes={nodes};chain={chain};connections={connections}";
    }

    private void NodeFootswitchClick(object? sender, RoutedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not string instanceId)
        {
            return;
        }
        viewModel.ToggleNodeBypass(instanceId);
        args.Handled = true;
    }

    public void AttachEditor(RigEditorViewModel editor)
    {
        ArgumentNullException.ThrowIfNull(editor);
        if (ReferenceEquals(viewModel, editor))
        {
            return;
        }

        if (viewModel is not null)
        {
            viewModel.PropertyChanged -= ViewModelPropertyChanged;
        }

        viewModel = editor;
        DataContext = editor;
        editor.PropertyChanged += ViewModelPropertyChanged;
        ApplyTheme(editor.Theme, editor.DarkTheme);
        UpdateSelectionStyles();
        UpdateConnectionCandidateFeedbackVisual();
        ResetCanvasViewport();
        StartRuntimeRefreshTimer();
    }

    public void HandleWindowKeyDown(KeyEventArgs args)
    {
        if (HandleShortcut(args.Key, args.KeyModifiers))
        {
            args.Handled = true;
        }
    }

    public bool HandleShortcut(Key key, KeyModifiers modifiers) => HandleShortcut(key, modifiers, markDirty: true);

    private bool HandleShortcut(Key key, KeyModifiers modifiers, bool markDirty)
    {
        if (viewModel is null)
        {
            return false;
        }
        if ((modifiers & (KeyModifiers.Control | KeyModifiers.Alt)) == 0 && key == Key.F6)
        {
            CycleWorkspaceFocus((modifiers & KeyModifiers.Shift) != 0);
            return true;
        }
        var hasModifier = (modifiers & (KeyModifiers.Control | KeyModifiers.Shift | KeyModifiers.Alt)) != KeyModifiers.None;
        if (!hasModifier && key == Key.T)
        {
            viewModel.ToggleTheme(markDirty);
            return true;
        }

        if ((modifiers & KeyModifiers.Control) != 0 && (modifiers & KeyModifiers.Alt) == 0 && key == Key.Z)
        {
            if ((modifiers & KeyModifiers.Shift) != 0)
            {
                viewModel.Redo();
            }
            else
            {
                viewModel.Undo();
            }
            return true;
        }

        if ((modifiers & KeyModifiers.Control) != 0 && (modifiers & KeyModifiers.Alt) == 0 && key == Key.Y)
        {
            viewModel.Redo();
            return true;
        }

        if ((modifiers & KeyModifiers.Control) != 0 && (key == Key.PageUp || key == Key.PageDown))
        {
            _ = SelectAdjacentPresetAsync(key == Key.PageUp ? -1 : 1);
            return true;
        }

        if ((modifiers & KeyModifiers.Control) != 0 && key == Key.S)
        {
            if ((modifiers & KeyModifiers.Shift) != 0)
            {
                _ = SaveAsAsync();
            }
            else
            {
                _ = SaveAsync();
            }
            return true;
        }
        if ((modifiers & KeyModifiers.Control) != 0 && key == Key.O)
        {
            _ = OpenAsync();
            return true;
        }
        if ((modifiers & KeyModifiers.Control) != 0 && key == Key.D1)
        {
            _ = SelectDefaultPresetAsync();
            return true;
        }
        return false;
    }

    private void CycleWorkspaceFocus(bool reverse)
    {
        Control[] targets = [PresetSelector, ModuleCatalogList, CanvasSurface, ConnectionToolPanel, ParameterInspectorButton];
        if (workspaceFocusIndex < 0)
        {
            workspaceFocusIndex = reverse ? targets.Length - 1 : 0;
        }
        else
        {
            var direction = reverse ? -1 : 1;
            workspaceFocusIndex = (workspaceFocusIndex + direction + targets.Length) % targets.Length;
        }
        var target = targets[workspaceFocusIndex];

        if (compactWorkspace)
        {
            if (ReferenceEquals(target, ModuleCatalogList))
            {
                moduleDrawerOpen = true;
                inspectorDrawerOpen = false;
                UpdateResponsiveLayout(Bounds.Width);
            }
            else if (ReferenceEquals(target, ParameterInspectorButton))
            {
                moduleDrawerOpen = false;
                inspectorDrawerOpen = true;
                UpdateResponsiveLayout(Bounds.Width);
            }
            else
            {
                CloseCompactPanels();
            }
        }

        target.Focus();
    }

    private Control? FindCanvasNodeControl(string instanceId)
    {
        var index = viewModel?.CanvasNodes.ToList().FindIndex(node =>
            string.Equals(node.InstanceId, instanceId, StringComparison.Ordinal)) ?? -1;
        if (index < 0 || CanvasNodeLayer.ContainerFromIndex(index) is not Control container)
        {
            return null;
        }
        return container.GetVisualDescendants().OfType<Control>().FirstOrDefault(control =>
            control.Classes.Contains("srg-canvas-node"));
    }

    private bool SelectAdjacentCanvasNode(int direction, bool focusNode)
    {
        if (viewModel is null || viewModel.CanvasNodes.Count == 0)
        {
            return false;
        }
        var currentId = viewModel.SelectedNode?.InstanceId;
        var currentIndex = viewModel.CanvasNodes.ToList().FindIndex(node =>
            string.Equals(node.InstanceId, currentId, StringComparison.Ordinal));
        var nextIndex = currentIndex < 0
            ? (direction < 0 ? viewModel.CanvasNodes.Count - 1 : 0)
            : (currentIndex + direction + viewModel.CanvasNodes.Count) % viewModel.CanvasNodes.Count;
        var next = viewModel.CanvasNodes[nextIndex];
        if (!viewModel.SelectNode(next.InstanceId))
        {
            return false;
        }
        FocusSelectedCanvasNode();
        if (focusNode)
        {
            UpdateLayout();
            FindCanvasNodeControl(next.InstanceId)?.Focus();
        }
        return true;
    }

    private bool MoveCanvasNodeByKeyboard(string instanceId, double deltaX, double deltaY)
    {
        if (viewModel is null || !viewModel.SelectNode(instanceId))
        {
            return false;
        }
        var node = viewModel.CanvasNodes.FirstOrDefault(candidate =>
            string.Equals(candidate.InstanceId, instanceId, StringComparison.Ordinal));
        if (node is null)
        {
            return false;
        }
        var centerX = node.CanvasX + node.Width * 0.5;
        var centerY = node.CanvasY + node.Height * 0.5;
        if (!viewModel.BeginDrag(centerX, centerY))
        {
            return false;
        }
        try
        {
            return viewModel.DragTo(centerX + deltaX, centerY + deltaY, snapToGrid: false);
        }
        finally
        {
            viewModel.EndDrag();
        }
    }

    private void ViewModelPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if ((args.PropertyName == nameof(RigEditorViewModel.DarkTheme) ||
             args.PropertyName == nameof(RigEditorViewModel.Theme)) && viewModel is not null)
        {
            ApplyTheme(viewModel.Theme, viewModel.DarkTheme);
        }
        if (viewModel is not null &&
            (args.PropertyName == nameof(RigEditorViewModel.ModuleCatalogFilter) ||
             args.PropertyName == nameof(RigEditorViewModel.InspectorTab)))
        {
            UpdateSelectionStyles();
        }
        if (args.PropertyName == nameof(RigEditorViewModel.CanvasConnections) ||
            args.PropertyName == nameof(RigEditorViewModel.SelectedConnection))
        {
            UpdateConnectionVisualState();
        }
        if (args.PropertyName is nameof(RigEditorViewModel.ConnectionSourceNodeId) or
            nameof(RigEditorViewModel.ConnectionDestinationNodeId) or
            nameof(RigEditorViewModel.ConnectionCandidateFeedbackKind) or
            nameof(RigEditorViewModel.Theme))
        {
            UpdateConnectionCandidateFeedbackVisual();
        }
    }

    private void UpdateSelectionStyles()
    {
        if (viewModel is null)
        {
            return;
        }
        AnalogFilterButton.Classes.Set("selected", viewModel.ModuleCatalogFilter == "模拟");
        DigitalFilterButton.Classes.Set("selected", viewModel.ModuleCatalogFilter == "数字");
        ExternalFilterButton.Classes.Set("selected", viewModel.ModuleCatalogFilter == "外部");
        ParameterInspectorButton.Classes.Set("selected", viewModel.InspectorTab == "参数");
        PortInspectorButton.Classes.Set("selected", viewModel.InspectorTab == "端口");
        ResourceInspectorButton.Classes.Set("selected", viewModel.InspectorTab == "资源");
    }

    private void UpdateConnectionCandidateFeedbackVisual()
    {
        if (viewModel is null || ConnectionCandidateStatusText is null)
        {
            return;
        }
        ConnectionCandidateStatusText.Foreground = ResolveConnectionFeedbackBrush(
            viewModel.ConnectionCandidateFeedbackKind);
    }

    private IBrush ResolveConnectionFeedbackBrush(ConnectionFeedbackKind kind) => kind switch
    {
        ConnectionFeedbackKind.Allowed => Resources["SrgAccentHoverBrush"] as IBrush ?? Brushes.White,
        ConnectionFeedbackKind.Blocked => Resources["SrgErrorBrush"] as IBrush ?? Brushes.Red,
        ConnectionFeedbackKind.Diagnostic => Resources["SrgWarningBrush"] as IBrush ?? Brushes.Gold,
        _ => Resources["SrgMutedBrush"] as IBrush ?? Brushes.Gray,
    };

    private void StartRuntimeRefreshTimer()
    {
        if (runtimeRefreshTimer is not null)
        {
            return;
        }
        runtimeRefreshTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
        runtimeRefreshTimer.Tick += RuntimeRefreshTimerTick;
        runtimeRefreshTimer.Start();
    }

    private void StopRuntimeRefreshTimer()
    {
        if (runtimeRefreshTimer is null)
        {
            return;
        }
        runtimeRefreshTimer.Stop();
        runtimeRefreshTimer.Tick -= RuntimeRefreshTimerTick;
        runtimeRefreshTimer = null;
    }

    private void RuntimeRefreshTimerTick(object? sender, EventArgs args)
    {
        if (viewModel is { AudioRunning: true } || viewModel?.TunerEnabled == true)
        {
            viewModel.RefreshRuntimeStatus();
        }
    }

    private void ApplyTheme(ThemeSnapshot theme, bool dark)
    {
        var palette = ResolvePalette(theme, dark);
        SetBrush("SrgWindowBrush", palette.Window);
        SetBrush("SrgTopbarBrush", palette.Topbar);
        SetBrush("SrgPanelBrush", palette.Panel);
        SetBrush("SrgCanvasBrush", palette.Canvas);
        SetBrush("SrgOverlayBrush", palette.Overlay);
        SetBrush("SrgControlBrush", palette.Control);
        SetBrush("SrgControlHoverBrush", palette.ControlHover);
        SetBrush("SrgCardBrush", palette.Card);
        SetBrush("SrgInputPanelBrush", palette.InputPanel);
        SetBrush("SrgOutputPanelBrush", palette.OutputPanel);
        SetBrush("SrgBorderBrush", palette.Border);
        SetBrush("SrgStrongBorderBrush", palette.StrongBorder);
        SetBrush("SrgCardBorderBrush", palette.CardBorder);
        SetBrush("SrgTextBrush", palette.Text);
        SetBrush("SrgMutedBrush", palette.Muted);
        SetBrush("SrgWarningBrush", palette.Warning);
        SetBrush("SrgErrorBrush", palette.Error);
        SetBrush("SrgAccentBrush", palette.Accent);
        SetBrush("SrgAccentHoverBrush", palette.AccentHover);
        SetBrush("SrgAccentTextBrush", palette.AccentText);
        SetBrush("SrgInputBrush", palette.Input);
        SetBrush("SrgInputOutlineBrush", palette.InputOutline);
        SetBrush("SrgOutputBrush", palette.Output);
        SetBrush("SrgOutputOutlineBrush", palette.OutputOutline);
        SetBrush("SrgTrackBrush", palette.Track);
        SetBrush("SrgKnobBrush", palette.Knob);
        SetBrush("SrgGridBrush", palette.Grid);
        SetBrush("SrgAccentOutlineBrush", palette.AccentOutline);
        SetBrush("SrgToolBorderBrush", palette.ToolBorder);
        Resources["SrgCanvasBackgroundBrush"] = new LinearGradientBrush
        {
            StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
            EndPoint = new RelativePoint(1, 1, RelativeUnit.Relative),
            GradientStops =
            [
                new GradientStop(Color.Parse(palette.CanvasStart), 0),
                new GradientStop(Color.Parse(palette.Overlay), 0.55),
                new GradientStop(Color.Parse(palette.CanvasEnd), 1)
            ]
        };
        if (TopLevel.GetTopLevel(this) is TopLevel topLevel)
        {
            topLevel.RequestedThemeVariant = dark ? ThemeVariant.Dark : ThemeVariant.Light;
        }
    }

    private static ThemePalette ResolvePalette(ThemeSnapshot theme, bool dark)
    {
        var fallback = dark ? DarkPalette : LightPalette;
        var background = ThemeColor(theme, "background", fallback.Window);
        var surface = ThemeColor(theme, "surface", fallback.Control);
        var control = ThemeColor(theme, "control", surface);
        var accent = ThemeColor(theme, "accent", fallback.Accent);
        return fallback with
        {
            Window = ThemeColor(theme, "window", background),
            Topbar = ThemeColor(theme, "topbar", surface),
            Panel = ThemeColor(theme, "panel", surface),
            Canvas = ThemeColor(theme, "canvas", background),
            Overlay = ThemeColor(theme, "overlay", surface),
            Control = control,
            ControlHover = ThemeColor(theme, "controlHover", fallback.ControlHover),
            Card = ThemeColor(theme, "card", surface),
            InputPanel = ThemeColor(theme, "inputPanel", control),
            OutputPanel = ThemeColor(theme, "outputPanel", control),
            Border = ThemeColor(theme, "border", fallback.Border),
            StrongBorder = ThemeColor(theme, "strongBorder", fallback.StrongBorder),
            CardBorder = ThemeColor(theme, "cardBorder", fallback.CardBorder),
            Text = ThemeColor(theme, "text", fallback.Text),
            Muted = ThemeColor(theme, "muted", fallback.Muted),
            Warning = ThemeColor(theme, "warning", fallback.Warning),
            Error = ThemeColor(theme, "error", fallback.Error),
            Accent = accent,
            AccentHover = ThemeColor(theme, "accentHover", accent),
            AccentText = ThemeColor(theme, "accentText", fallback.AccentText),
            Input = ThemeColor(theme, "input", fallback.Input),
            InputOutline = ThemeColor(theme, "inputOutline", fallback.InputOutline),
            Output = ThemeColor(theme, "output", fallback.Output),
            OutputOutline = ThemeColor(theme, "outputOutline", fallback.OutputOutline),
            Track = ThemeColor(theme, "track", fallback.Track),
            Knob = ThemeColor(theme, "knob", fallback.Knob),
            Grid = ThemeColor(theme, "grid", fallback.Grid),
            CanvasStart = ThemeColor(theme, "canvasStart", background),
            CanvasEnd = ThemeColor(theme, "canvasEnd", fallback.CanvasEnd),
            AccentOutline = ThemeColor(theme, "accentOutline", fallback.AccentOutline),
            ToolBorder = ThemeColor(theme, "toolBorder", fallback.ToolBorder)
        };
    }

    private static string ThemeColor(ThemeSnapshot theme, string key, string fallback) =>
        theme.GetColor(key) ?? fallback;

    private void SetBrush(string key, string color) => Resources[key] = new SolidColorBrush(Color.Parse(color));

    private void CanvasSizeChanged(object? sender, SizeChangedEventArgs args)
    {
        if (viewModel is not null && args.NewSize.Width > 0 && args.NewSize.Height > 0)
        {
            var dpiScale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
            viewModel.SetViewport(args.NewSize.Width, args.NewSize.Height, dpiScale);
        }
    }

    private void RigEditorSizeChanged(object? sender, SizeChangedEventArgs args) =>
        UpdateResponsiveLayout(args.NewSize.Width);

    private void UpdateResponsiveLayout(double width)
    {
        if (TopbarGrid is null || TopbarGrid.ColumnDefinitions.Count < 1 ||
            WorkspaceGrid is null || WorkspaceGrid.ColumnDefinitions.Count < 3 ||
            ModuleLibraryPanel is null || InspectorPanel is null ||
            ModulePanelToggleButton is null || InspectorPanelToggleButton is null)
        {
            return;
        }
        compactWorkspace = width > 0 && width < 980.0;
        if (compactWorkspace)
        {
            BrandPanel.IsVisible = false;
            TopbarGrid.ColumnDefinitions[0].Width = new GridLength(0);
            PresetSelector.Width = 138;
            TopbarActions.Width = 360;
            WorkspaceGrid.ColumnDefinitions[0].Width = new GridLength(0);
            WorkspaceGrid.ColumnDefinitions[2].Width = new GridLength(0);
            ModulePanelToggleButton.IsVisible = true;
            InspectorPanelToggleButton.IsVisible = true;
            Grid.SetColumn(ModuleLibraryPanel, 1);
            Grid.SetColumn(InspectorPanel, 1);
            ModuleLibraryPanel.Width = 286;
            InspectorPanel.Width = 320;
            ModuleLibraryPanel.HorizontalAlignment = global::Avalonia.Layout.HorizontalAlignment.Left;
            InspectorPanel.HorizontalAlignment = global::Avalonia.Layout.HorizontalAlignment.Right;
            ModuleLibraryPanel.IsVisible = moduleDrawerOpen;
            InspectorPanel.IsVisible = inspectorDrawerOpen;
            ModuleLibraryPanel.SetValue(Panel.ZIndexProperty, 40);
            InspectorPanel.SetValue(Panel.ZIndexProperty, 40);
        }
        else
        {
            moduleDrawerOpen = false;
            inspectorDrawerOpen = false;
            BrandPanel.IsVisible = true;
            TopbarGrid.ColumnDefinitions[0].Width = GridLength.Auto;
            PresetSelector.Width = 178;
            TopbarActions.Width = double.NaN;
            var medium = width > 0 && width < 1250.0;
            WorkspaceGrid.ColumnDefinitions[0].Width = new GridLength(medium ? 190 : 226);
            WorkspaceGrid.ColumnDefinitions[2].Width = new GridLength(medium ? 240 : 286);
            ModulePanelToggleButton.IsVisible = false;
            InspectorPanelToggleButton.IsVisible = false;
            Grid.SetColumn(ModuleLibraryPanel, 0);
            Grid.SetColumn(InspectorPanel, 2);
            ModuleLibraryPanel.Width = double.NaN;
            InspectorPanel.Width = double.NaN;
            ModuleLibraryPanel.HorizontalAlignment = global::Avalonia.Layout.HorizontalAlignment.Stretch;
            InspectorPanel.HorizontalAlignment = global::Avalonia.Layout.HorizontalAlignment.Stretch;
            ModuleLibraryPanel.IsVisible = true;
            InspectorPanel.IsVisible = true;
            ModuleLibraryPanel.SetValue(Panel.ZIndexProperty, 0);
            InspectorPanel.SetValue(Panel.ZIndexProperty, 0);
        }
    }

    private void ToggleModulePanel(double? layoutWidth = null)
    {
        if (!compactWorkspace) return;
        moduleDrawerOpen = !moduleDrawerOpen;
        inspectorDrawerOpen = false;
        UpdateResponsiveLayout(layoutWidth ?? Bounds.Width);
    }

    private void ToggleInspectorPanel(double? layoutWidth = null)
    {
        if (!compactWorkspace) return;
        inspectorDrawerOpen = !inspectorDrawerOpen;
        moduleDrawerOpen = false;
        UpdateResponsiveLayout(layoutWidth ?? Bounds.Width);
    }

    private void CloseCompactPanels()
    {
        if (!compactWorkspace || (!moduleDrawerOpen && !inspectorDrawerOpen)) return;
        moduleDrawerOpen = false;
        inspectorDrawerOpen = false;
        UpdateResponsiveLayout(Bounds.Width);
    }

    private void ApplyCanvasTransform()
    {
        if (CanvasConnectionLayer is null || CanvasNodeLayer is null)
        {
            return;
        }
        var transform = new MatrixTransform(new Matrix(canvasZoom, 0.0, 0.0, canvasZoom, canvasPanX, canvasPanY));
        CanvasConnectionLayer.RenderTransform = transform;
        CanvasNodeLayer.RenderTransform = new MatrixTransform(transform.Matrix);
        if (ConnectionPreviewPath is not null)
        {
            ConnectionPreviewPath.RenderTransform = new MatrixTransform(transform.Matrix);
            ConnectionPreviewPath.StrokeThickness = 4.0 / Math.Max(canvasZoom, MinimumCanvasZoom);
        }
        if (ConnectionPreviewHalo is not null)
        {
            ConnectionPreviewHalo.RenderTransform = new MatrixTransform(transform.Matrix);
            ConnectionPreviewHalo.StrokeThickness = 10.0 / Math.Max(canvasZoom, MinimumCanvasZoom);
        }
        UpdateConnectionVisualState();
        if (CanvasZoomText is not null)
        {
            CanvasZoomText.Text = $"{canvasZoom * 100:0}%";
        }
    }

    private double ConnectionScreenScale(double screenSize) =>
        screenSize / Math.Max(canvasZoom, MinimumCanvasZoom);

    private void ConnectionContainerPrepared(object? sender, ContainerPreparedEventArgs args)
    {
        var container = args.Container;
        void ContainerLayoutUpdated(object? layoutSender, EventArgs layoutArgs)
        {
            container.LayoutUpdated -= ContainerLayoutUpdated;
            UpdateConnectionPathVisualState(container);
        }
        container.LayoutUpdated += ContainerLayoutUpdated;
    }

    private void NodeContainerPrepared(object? sender, ContainerPreparedEventArgs args)
    {
        var container = args.Container;
        void ContainerLayoutUpdated(object? layoutSender, EventArgs layoutArgs)
        {
            container.LayoutUpdated -= ContainerLayoutUpdated;
            UpdateConnectionEndpointVisualState(container);
        }
        container.LayoutUpdated += ContainerLayoutUpdated;
    }

    private void UpdateConnectionVisualState()
    {
        UpdateConnectionPathVisualState();
        UpdateConnectionEndpointVisualState();
    }

    private void UpdateConnectionPathVisualState(Control? visualRoot = null)
    {
        visualRoot ??= CanvasConnectionLayer;
        if (visualRoot is null)
        {
            return;
        }
        foreach (var path in visualRoot.GetVisualDescendants().OfType<AvaloniaPath>())
        {
            if (path.Classes.Contains("srg-connection-halo"))
            {
                var selected = path.DataContext is CanvasConnectionSnapshot { Selected: true };
                path.StrokeThickness = ConnectionScreenScale(selected ? 11.0 : 8.0);
            }
            else if (path.Classes.Contains("srg-connection-line"))
            {
                var selected = path.DataContext is CanvasConnectionSnapshot { Selected: true };
                path.StrokeThickness = ConnectionScreenScale(selected ? 5.0 : 3.0);
            }
            else if (path.Classes.Contains("srg-connection-hover-line"))
            {
                path.StrokeThickness = ConnectionScreenScale(5.0);
            }
            else if (path.Classes.Contains("srg-connection-hit"))
            {
                var selected = path.DataContext is CanvasConnectionSnapshot { Selected: true };
                path.StrokeThickness = ConnectionScreenScale(selected ? 22.0 : 18.0);
            }
            else if (path.Classes.Contains("srg-connection-arrow"))
            {
                path.StrokeThickness = ConnectionScreenScale(1.0);
            }
        }
    }

    private void UpdateConnectionEndpointVisualState(Control? visualRoot = null)
    {
        visualRoot ??= CanvasNodeLayer;
        if (visualRoot is not null)
        {
            var inverseScale = 1.0 / Math.Max(canvasZoom, MinimumCanvasZoom);
            foreach (var endpoint in visualRoot.GetVisualDescendants().OfType<Button>()
                         .Where(button => button.Classes.Contains("srg-endpoint")))
            {
                endpoint.RenderTransform = new ScaleTransform(inverseScale, inverseScale);
                endpoint.RenderTransformOrigin = RelativePoint.Center;
            }
            foreach (var candidate in visualRoot.GetVisualDescendants().OfType<Control>()
                         .Where(control => control.Classes.Contains("srg-endpoint-candidate")))
            {
                candidate.RenderTransform = new ScaleTransform(inverseScale, inverseScale);
                candidate.RenderTransformOrigin = RelativePoint.Center;
            }
        }
    }

    private void SetCanvasViewport(double zoom, double panX, double panY)
    {
        canvasZoom = Math.Clamp(zoom, MinimumCanvasZoom, MaximumCanvasZoom);
        canvasPanX = double.IsFinite(panX) ? panX : 0.0;
        canvasPanY = double.IsFinite(panY) ? panY : 0.0;
        ApplyCanvasTransform();
    }

    private void ResetCanvasViewport() => SetCanvasViewport(1.0, 0.0, 0.0);

    private void ZoomCanvasAt(Point viewportPoint, double requestedZoom)
    {
        var nextZoom = Math.Clamp(requestedZoom, MinimumCanvasZoom, MaximumCanvasZoom);
        if (Math.Abs(nextZoom - canvasZoom) < 0.00001)
        {
            return;
        }
        var worldX = (viewportPoint.X - canvasPanX) / canvasZoom;
        var worldY = (viewportPoint.Y - canvasPanY) / canvasZoom;
        canvasZoom = nextZoom;
        canvasPanX = viewportPoint.X - worldX * canvasZoom;
        canvasPanY = viewportPoint.Y - worldY * canvasZoom;
        ApplyCanvasTransform();
    }

    private void PanCanvasBy(double deltaX, double deltaY)
    {
        canvasPanX += deltaX;
        canvasPanY += deltaY;
        ApplyCanvasTransform();
    }

    private void FitCanvasToNodes()
    {
        if (viewModel is null || viewModel.CanvasNodes.Count == 0 || EditorCanvas.Bounds.Width <= 0 ||
            EditorCanvas.Bounds.Height <= 0)
        {
            ResetCanvasViewport();
            return;
        }
        var minX = viewModel.CanvasNodes.Min(node => node.CanvasX);
        var minY = viewModel.CanvasNodes.Min(node => node.CanvasY);
        var maxX = viewModel.CanvasNodes.Max(node => node.CanvasX + node.Width);
        var maxY = viewModel.CanvasNodes.Max(node => node.CanvasY + node.Height);
        var contentWidth = Math.Max(maxX - minX, 1.0);
        var contentHeight = Math.Max(maxY - minY, 1.0);
        var availableWidth = Math.Max(EditorCanvas.Bounds.Width - 48.0, 1.0);
        var availableHeight = Math.Max(EditorCanvas.Bounds.Height - 48.0, 1.0);
        var zoom = Math.Clamp(Math.Min(availableWidth / contentWidth, availableHeight / contentHeight),
            MinimumCanvasZoom, MaximumCanvasZoom);
        var contentCenterX = (minX + maxX) * 0.5;
        var contentCenterY = (minY + maxY) * 0.5;
        canvasZoom = zoom;
        canvasPanX = EditorCanvas.Bounds.Width * 0.5 - contentCenterX * zoom;
        canvasPanY = EditorCanvas.Bounds.Height * 0.5 - contentCenterY * zoom;
        ApplyCanvasTransform();
    }

    private void FocusSelectedCanvasNode()
    {
        var selected = viewModel?.CanvasNodes.FirstOrDefault(node => node.Selected);
        if (selected is null || EditorCanvas.Bounds.Width <= 0 || EditorCanvas.Bounds.Height <= 0)
        {
            return;
        }
        var centerX = selected.CanvasX + selected.Width * 0.5;
        var centerY = selected.CanvasY + selected.Height * 0.5;
        canvasPanX = EditorCanvas.Bounds.Width * 0.5 - centerX * canvasZoom;
        canvasPanY = EditorCanvas.Bounds.Height * 0.5 - centerY * canvasZoom;
        ApplyCanvasTransform();
    }

    private void CanvasSurfaceKeyDown(object? sender, KeyEventArgs args)
    {
        if (viewModel is null || sender is not Control control || !ReferenceEquals(args.Source, control))
        {
            return;
        }

        var handled = args.Key switch
        {
            Key.Left => SelectAdjacentCanvasNode(-1, focusNode: false),
            Key.Right => SelectAdjacentCanvasNode(1, focusNode: false),
            Key.Home => SelectCanvasNodeAtIndex(0, focusNode: false),
            Key.End => SelectCanvasNodeAtIndex(viewModel.CanvasNodes.Count - 1, focusNode: false),
            Key.Enter => FocusSelectedCanvasNodeControl(),
            Key.Delete => viewModel.SelectedNode is { } selected && viewModel.DeleteNode(selected.InstanceId),
            Key.Escape => ClearCanvasKeyboardState(),
            _ => false,
        };
        args.Handled = handled;
    }

    private void CanvasNodeKeyDown(object? sender, KeyEventArgs args)
    {
        if (viewModel is null || sender is not Control control || !ReferenceEquals(args.Source, control) ||
            control.DataContext is not CanvasNodeSnapshot node)
        {
            return;
        }

        var step = (args.KeyModifiers & KeyModifiers.Shift) != 0 ? 1.0 : 8.0;
        var handled = args.Key switch
        {
            Key.Enter or Key.Space => viewModel.SelectNode(node.InstanceId),
            Key.Left => MoveCanvasNodeByKeyboard(node.InstanceId, -step, 0),
            Key.Right => MoveCanvasNodeByKeyboard(node.InstanceId, step, 0),
            Key.Up => MoveCanvasNodeByKeyboard(node.InstanceId, 0, -step),
            Key.Down => MoveCanvasNodeByKeyboard(node.InstanceId, 0, step),
            Key.PageUp => SelectAdjacentCanvasNode(-1, focusNode: true),
            Key.PageDown => SelectAdjacentCanvasNode(1, focusNode: true),
            Key.Delete => viewModel.DeleteNode(node.InstanceId),
            Key.Escape => FocusCanvasSurface(),
            _ => false,
        };
        args.Handled = handled;
    }

    private void ConnectionToolKeyDown(object? sender, KeyEventArgs args)
    {
        if (viewModel is null || sender is not Control control || !ReferenceEquals(args.Source, control))
        {
            return;
        }
        var handled = args.Key switch
        {
            Key.Enter => viewModel.ConnectConnectionCandidate(),
            Key.Delete => DisconnectSelectedConnectionFromKeyboard(),
            Key.Escape => ClearConnectionCandidate(),
            _ => false,
        };
        args.Handled = handled;
    }

    private bool SelectCanvasNodeAtIndex(int index, bool focusNode)
    {
        if (viewModel is null || index < 0 || index >= viewModel.CanvasNodes.Count)
        {
            return false;
        }
        var node = viewModel.CanvasNodes[index];
        if (!viewModel.SelectNode(node.InstanceId))
        {
            return false;
        }
        FocusSelectedCanvasNode();
        if (focusNode)
        {
            UpdateLayout();
            FindCanvasNodeControl(node.InstanceId)?.Focus();
        }
        return true;
    }

    private bool FocusSelectedCanvasNodeControl()
    {
        var instanceId = viewModel?.SelectedNode?.InstanceId;
        if (instanceId is null)
        {
            return SelectCanvasNodeAtIndex(0, focusNode: true);
        }
        FocusSelectedCanvasNode();
        UpdateLayout();
        return FindCanvasNodeControl(instanceId)?.Focus() == true;
    }

    private bool FocusCanvasSurface() => CanvasSurface.Focus();

    private bool ClearCanvasKeyboardState()
    {
        CloseCompactPanels();
        return viewModel?.ClearSelection() == true;
    }

    private bool ClearConnectionCandidate()
    {
        if (viewModel is null)
        {
            return false;
        }
        draggingConnection = false;
        draggingConnectionSource = null;
        HideConnectionPreview();
        viewModel.ConnectionSourceNodeId = null;
        viewModel.ConnectionDestinationNodeId = null;
        return true;
    }

    private bool DisconnectSelectedConnectionFromKeyboard()
    {
        var connection = viewModel?.SelectedConnection;
        return connection is not null &&
               viewModel!.DisconnectNodes(connection.SourceInstanceId, connection.DestinationInstanceId);
    }

    private void CanvasPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (viewModel is null || sender is not Control source || !IsEditorCanvas(source))
        {
            return;
        }
        CloseCompactPanels();
        var viewportPoint = args.GetPosition(EditorCanvas);
        if (args.GetCurrentPoint(EditorCanvas).Properties.IsMiddleButtonPressed)
        {
            panningCanvas = true;
            canvasPanPointerStart = viewportPoint;
            canvasPanOriginX = canvasPanX;
            canvasPanOriginY = canvasPanY;
            args.Pointer.Capture(EditorCanvas);
            args.Handled = true;
            return;
        }
        var point = CanvasViewportToWorld(viewportPoint);
        if (viewModel.BeginDrag(point.X, point.Y))
        {
            dragging = true;
            args.Pointer.Capture(EditorCanvas);
            args.Handled = true;
            return;
        }
        viewModel.ClearSelection();
        args.Handled = true;
    }

    private void CanvasPointerMoved(object? sender, PointerEventArgs args)
    {
        if (draggingConnection && viewModel is not null && args.Pointer.Captured == EditorCanvas)
        {
            var point = CanvasViewportToWorld(args.GetPosition(EditorCanvas));
            var target = FindInputEndpoint(point);
            viewModel.ConnectionDestinationNodeId = target;
            UpdateConnectionPreview(target is null ? point : GetInputEndpointPoint(target));
            args.Handled = true;
            return;
        }
        if (panningCanvas && args.Pointer.Captured == EditorCanvas)
        {
            var panPoint = args.GetPosition(EditorCanvas);
            canvasPanX = canvasPanOriginX + panPoint.X - canvasPanPointerStart.X;
            canvasPanY = canvasPanOriginY + panPoint.Y - canvasPanPointerStart.Y;
            ApplyCanvasTransform();
            args.Handled = true;
            return;
        }
        if (!dragging || viewModel is null || args.Pointer.Captured != EditorCanvas)
        {
            return;
        }
        var dragPoint = CanvasViewportToWorld(args.GetPosition(EditorCanvas));
        viewModel.DragTo(dragPoint.X, dragPoint.Y, true);
        args.Handled = true;
    }

    private void CanvasPointerReleased(object? sender, PointerReleasedEventArgs args)
    {
        if (draggingConnection && viewModel is not null)
        {
            var point = CanvasViewportToWorld(args.GetPosition(EditorCanvas));
            var target = FindInputEndpoint(point);
            var source = draggingConnectionSource;
            draggingConnection = false;
            draggingConnectionSource = null;
            HideConnectionPreview();
            args.Pointer.Capture(null);
            if (!string.IsNullOrWhiteSpace(source) && !string.IsNullOrWhiteSpace(target))
            {
                viewModel.ConnectionSourceNodeId = source;
                viewModel.ConnectionDestinationNodeId = target;
                viewModel.ConnectConnectionCandidate();
            }
            else
            {
                viewModel.ReportInteractionStatus("连接未完成：请将线拖到目标效果器的输入端点");
            }
            args.Handled = true;
            return;
        }
        if (panningCanvas)
        {
            panningCanvas = false;
            args.Pointer.Capture(null);
            args.Handled = true;
            return;
        }
        if (!dragging || viewModel is null)
        {
            return;
        }
        dragging = false;
        viewModel.EndDrag();
        args.Pointer.Capture(null);
        args.Handled = true;
    }

    private void NodeRuntimeControlPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not NodeControlSnapshot runtimeControl ||
            !runtimeControl.IsMomentary || runtimeControl.IsBypass ||
            !args.GetCurrentPoint(control).Properties.IsLeftButtonPressed)
        {
            return;
        }
        if (pressedMomentaryTargets.Add(runtimeControl.Target))
        {
            viewModel.SetParameter(runtimeControl.Target, runtimeControl.Maximum);
        }
        args.Pointer.Capture(control);
        args.Handled = true;
    }

    private void NodeRuntimeControlPointerReleased(object? sender, PointerReleasedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not NodeControlSnapshot runtimeControl ||
            !runtimeControl.IsMomentary || runtimeControl.IsBypass)
        {
            return;
        }
        if (pressedMomentaryTargets.Remove(runtimeControl.Target))
        {
            viewModel.SetParameter(runtimeControl.Target, runtimeControl.Minimum);
        }
        args.Pointer.Capture(null);
        args.Handled = true;
    }

    private void NodeRuntimeControlWheelChanged(object? sender, PointerWheelEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not NodeControlSnapshot runtimeControl ||
            runtimeControl.IsBypass || Math.Abs(args.Delta.Y) < double.Epsilon)
        {
            return;
        }
        viewModel.NudgeParameter(runtimeControl.Target, args.Delta.Y > 0.0 ? 1 : -1,
            (args.KeyModifiers & KeyModifiers.Shift) != 0);
        args.Handled = true;
    }

    private void NodeRuntimeControlClick(object? sender, RoutedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not NodeControlSnapshot runtimeControl)
        {
            return;
        }
        if (runtimeControl.IsBypass)
        {
            var suffix = ".__bypass";
            var instanceId = runtimeControl.Target.EndsWith(suffix, StringComparison.Ordinal)
                ? runtimeControl.Target[..^suffix.Length]
                : string.Empty;
            if (!string.IsNullOrWhiteSpace(instanceId))
            {
                viewModel.ToggleNodeBypass(instanceId);
            }
        }
        else if (runtimeControl.IsMomentary)
        {
            // Pointer interaction pulses high on press and returns low on release.
            // Keyboard activation has no pointer release, so leave it at the safe low state.
            if (!pressedMomentaryTargets.Contains(runtimeControl.Target))
            {
                viewModel.SetParameter(runtimeControl.Target, runtimeControl.Minimum);
            }
        }
        else if (runtimeControl.IsToggle || runtimeControl.IsButton)
        {
            viewModel.ToggleParameter(runtimeControl.Target);
        }
        else
        {
            FocusParameterEditor(runtimeControl.Target);
        }
        args.Handled = true;
    }

    private void CanvasPointerWheelChanged(object? sender, PointerWheelEventArgs args)
    {
        if ((args.KeyModifiers & KeyModifiers.Control) != 0)
        {
            var direction = Math.Sign(args.Delta.Y);
            if (direction != 0)
            {
                ZoomCanvasAt(args.GetPosition(EditorCanvas), canvasZoom + direction * 0.1);
            }
        }
        else
        {
            PanCanvasBy(args.Delta.X * 36.0, args.Delta.Y * 36.0);
        }
        args.Handled = true;
    }

    private Point CanvasViewportToWorld(Point point) =>
        new((point.X - canvasPanX) / canvasZoom, (point.Y - canvasPanY) / canvasZoom);

    private Point CanvasWorldToViewport(Point point) =>
        new(point.X * canvasZoom + canvasPanX, point.Y * canvasZoom + canvasPanY);

    private void ModuleCatalogItemPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (viewModel is null || sender is not Control control ||
            control.DataContext is not ModuleCatalogSnapshot module ||
            !args.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
        {
            return;
        }
        viewModel.SelectedCatalogModule = module;
        draggedModule = module;
        moduleDragStart = args.GetPosition(this);
        draggingModule = false;
        HideModuleDragPreview();
        args.Pointer.Capture(this);
        args.Handled = true;
    }

    private void RootPointerMoved(object? sender, PointerEventArgs args)
    {
        if (draggedModule is null || args.Pointer.Captured != this)
        {
            return;
        }
        var point = args.GetPosition(this);
        var deltaX = point.X - moduleDragStart.X;
        var deltaY = point.Y - moduleDragStart.Y;
        if (!draggingModule && Math.Sqrt(deltaX * deltaX + deltaY * deltaY) >= 6.0)
        {
            draggingModule = true;
            viewModel?.ReportInteractionStatus($"拖动 {draggedModule.DisplayName} 到画布中放置");
        }
        if (draggingModule)
        {
            var canvasPoint = args.GetPosition(EditorCanvas);
            var insideCanvas = canvasPoint.X >= 0 && canvasPoint.Y >= 0 &&
                               canvasPoint.X <= EditorCanvas.Bounds.Width &&
                               canvasPoint.Y <= EditorCanvas.Bounds.Height;
            if (insideCanvas)
            {
                if (!moduleDragOverCanvas)
                {
                    viewModel?.ReportInteractionStatus($"松开以添加 {draggedModule.DisplayName}");
                }
                UpdateModuleDragPreview(canvasPoint, draggedModule.DisplayName);
            }
            else
            {
                HideModuleDragPreview();
            }
            args.Handled = true;
        }
    }

    private void RootPointerReleased(object? sender, PointerReleasedEventArgs args)
    {
        if (draggedModule is null || args.Pointer.Captured != this)
        {
            return;
        }
        var module = draggedModule;
        var wasDragging = draggingModule;
        var canvasPoint = args.GetPosition(EditorCanvas);
        draggedModule = null;
        draggingModule = false;
        HideModuleDragPreview();
        args.Pointer.Capture(null);
        if (wasDragging && viewModel is not null &&
            canvasPoint.X >= 0 && canvasPoint.Y >= 0 &&
            canvasPoint.X <= EditorCanvas.Bounds.Width && canvasPoint.Y <= EditorCanvas.Bounds.Height)
        {
            var worldPoint = CanvasViewportToWorld(canvasPoint);
            viewModel.AddModuleAtCanvasPoint(module.ModuleId, worldPoint.X, worldPoint.Y);
        }
        args.Handled = true;
    }

    private bool IsEditorCanvas(Control source) => source == EditorCanvas || source is Border || source is ItemsControl;

    private void ParameterGotFocus(object? sender, GotFocusEventArgs args)
    {
        if (sender is TextBox textBox && textBox.Tag is string target)
        {
            parameterEditBaselines[textBox] = viewModel?.SelectedParameters
                .FirstOrDefault(item => item.Target == target)?.ValueText ?? textBox.Text ?? string.Empty;
            textBox.Classes.Set("invalid", false);
            textBox.SetValue(AutomationProperties.HelpTextProperty, null);
        }
    }

    private void ParameterLostFocus(object? sender, RoutedEventArgs args)
    {
        if (sender is TextBox textBox)
        {
            var baseline = parameterEditBaselines.GetValueOrDefault(textBox, textBox.Text ?? string.Empty);
            if (!string.Equals(textBox.Text, baseline, StringComparison.Ordinal))
            {
                CommitParameterText(textBox, selectOnFailure: false);
            }
            parameterEditBaselines.Remove(textBox);
        }
    }

    private void ParameterTextBoxKeyDown(object? sender, KeyEventArgs args)
    {
        if (viewModel is null || sender is not TextBox textBox || textBox.Tag is not string target)
        {
            return;
        }
        if (args.Key == Key.Enter)
        {
            CommitParameterText(textBox, selectOnFailure: true);
            args.Handled = true;
        }
        else if (args.Key == Key.Escape)
        {
            RestoreParameterText(textBox, target);
            textBox.SelectAll();
            args.Handled = true;
        }
        else if (args.Key is Key.Up or Key.Down)
        {
            var direction = args.Key == Key.Up ? 1 : -1;
            var fine = (args.KeyModifiers & KeyModifiers.Shift) != 0;
            if (viewModel.NudgeParameter(target, direction, fine))
            {
                RestoreParameterText(textBox, target);
                textBox.SelectAll();
            }
            args.Handled = true;
        }
    }

    private bool CommitParameterText(TextBox textBox, bool selectOnFailure)
    {
        if (viewModel is null || textBox.Tag is not string target)
        {
            return false;
        }
        var valid = viewModel.SetParameterText(target, textBox.Text ?? string.Empty);
        textBox.Classes.Set("invalid", !valid);
        textBox.SetValue(AutomationProperties.HelpTextProperty, valid ? null : viewModel.Status);
        if (valid)
        {
            RestoreParameterText(textBox, target);
            parameterEditBaselines[textBox] = textBox.Text ?? string.Empty;
        }
        else if (selectOnFailure)
        {
            textBox.SelectAll();
        }
        return valid;
    }

    private void RestoreParameterText(TextBox textBox, string target)
    {
        var parameter = viewModel?.SelectedParameters.FirstOrDefault(item => item.Target == target);
        if (parameter is not null)
        {
            textBox.Text = parameter.ValueText;
        }
        textBox.Classes.Set("invalid", false);
        textBox.SetValue(AutomationProperties.HelpTextProperty, null);
        parameterEditBaselines[textBox] = textBox.Text ?? string.Empty;
    }

    private void ParameterDecreaseClick(object? sender, RoutedEventArgs args) => NudgeParameterFromButton(sender, -1);

    private void ParameterIncreaseClick(object? sender, RoutedEventArgs args) => NudgeParameterFromButton(sender, 1);

    private void NudgeParameterFromButton(object? sender, int direction)
    {
        if (viewModel is not null && sender is Control control && control.Tag is string target)
        {
            viewModel.NudgeParameter(target, direction, fine: false);
        }
    }

    private void NodeParameterChipClick(object? sender, RoutedEventArgs args)
    {
        if (sender is Control control && control.Tag is string target)
        {
            FocusParameterEditor(target);
            args.Handled = true;
        }
    }

    private bool FocusParameterEditor(string target)
    {
        if (viewModel is null)
        {
            return false;
        }
        var separator = target.LastIndexOf('.');
        if (separator <= 0 || !viewModel.SelectNode(target[..separator]) || !viewModel.SelectInspectorTab("参数"))
        {
            return false;
        }
        if (compactWorkspace)
        {
            moduleDrawerOpen = false;
            inspectorDrawerOpen = true;
            UpdateResponsiveLayout(Bounds.Width);
        }
        UpdateLayout();
        var textBox = FindParameterTextBox(target);
        if (textBox is null)
        {
            return false;
        }
        textBox.Focus();
        textBox.SelectAll();
        return true;
    }

    private TextBox? FindParameterTextBox(string target) =>
        ParameterControlsList.GetVisualDescendants().OfType<TextBox>().FirstOrDefault(textBox =>
            string.Equals(textBox.Tag as string, target, StringComparison.Ordinal));

    private void ParameterSliderChanged(object? sender, RangeBaseValueChangedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not string target ||
            !double.IsFinite(args.NewValue))
        {
            return;
        }
        var current = viewModel.SelectedParameters.FirstOrDefault(item => item.Target == target);
        if (current is null || Math.Abs(current.Value - args.NewValue) < 0.00001)
        {
            return;
        }
        viewModel.SetParameter(target, (float)args.NewValue);
    }

    private void OpenClick(object? sender, RoutedEventArgs args) => _ = OpenAsync();
    private void SaveClick(object? sender, RoutedEventArgs args) => _ = SaveAsync();
    private void SaveAsClick(object? sender, RoutedEventArgs args) => _ = SaveAsAsync();
    private void UndoClick(object? sender, RoutedEventArgs args) => viewModel?.Undo();
    private void RedoClick(object? sender, RoutedEventArgs args) => viewModel?.Redo();
    private void NewPresetClick(object? sender, RoutedEventArgs args) => _ = SelectDefaultPresetAsync();
    private void DefaultPresetClick(object? sender, RoutedEventArgs args) => _ = SelectDefaultPresetAsync();
    private void PresetSelectionChanged(object? sender, SelectionChangedEventArgs args)
    {
        if (viewModel is null || args.AddedItems.Count == 0 || args.AddedItems[0] is not string presetName ||
            string.Equals(presetName, viewModel.SelectedPresetName, StringComparison.Ordinal))
        {
            return;
        }
        _ = SelectPresetAsync(presetName);
    }
    private void LoadThemeClick(object? sender, RoutedEventArgs args) => _ = LoadThemeAsync();
    private void ImportResourceClick(object? sender, RoutedEventArgs args) => _ = ImportResourceAsync();
    private void ZoomOutClick(object? sender, RoutedEventArgs args) =>
        ZoomCanvasAt(new Point(EditorCanvas.Bounds.Width * 0.5, EditorCanvas.Bounds.Height * 0.5), canvasZoom - 0.1);
    private void ZoomInClick(object? sender, RoutedEventArgs args) =>
        ZoomCanvasAt(new Point(EditorCanvas.Bounds.Width * 0.5, EditorCanvas.Bounds.Height * 0.5), canvasZoom + 0.1);
    private void FitCanvasClick(object? sender, RoutedEventArgs args) => FitCanvasToNodes();
    private void FocusSelectedClick(object? sender, RoutedEventArgs args) => FocusSelectedCanvasNode();
    private void ToggleModulePanelClick(object? sender, RoutedEventArgs args) => ToggleModulePanel();
    private void ToggleInspectorPanelClick(object? sender, RoutedEventArgs args) => ToggleInspectorPanel();

    private void ModuleCatalogFilterClick(object? sender, RoutedEventArgs args)
    {
        if (viewModel is not null && sender is Control control && control.Tag is string filter)
        {
            viewModel.SetModuleCatalogFilter(filter);
        }
    }

    private void NodeNavigatorLocateClick(object? sender, RoutedEventArgs args)
    {
        if (viewModel is not null && sender is Control control && control.Tag is string instanceId &&
            viewModel.SelectNode(instanceId))
        {
            FocusSelectedCanvasNode();
            args.Handled = true;
        }
    }

    private void NodeNavigatorSelectAllClick(object? sender, RoutedEventArgs args) =>
        viewModel?.SelectAllFilteredNodes();

    private void NodeNavigatorClearClick(object? sender, RoutedEventArgs args) =>
        viewModel?.ClearBatchNodeSelection();

    private void NodeNavigatorBypassClick(object? sender, RoutedEventArgs args) =>
        viewModel?.SetBatchNodesBypassed(true);

    private void NodeNavigatorEnableClick(object? sender, RoutedEventArgs args) =>
        viewModel?.SetBatchNodesBypassed(false);

    private void InspectorTabClick(object? sender, RoutedEventArgs args)
    {
        if (viewModel is not null && sender is Control control && control.Tag is string tab)
        {
            viewModel.SelectInspectorTab(tab);
        }
    }

    private void RefreshAsioClick(object? sender, RoutedEventArgs args) => viewModel?.RefreshAsioDevices();

    private void OpenAsioClick(object? sender, RoutedEventArgs args)
    {
        var nativeWindow = TopLevel.GetTopLevel(this)?.TryGetPlatformHandle()?.Handle ?? IntPtr.Zero;
        viewModel?.OpenSelectedAsio(nativeWindow);
    }

    private void StartAsioClick(object? sender, RoutedEventArgs args) => viewModel?.StartAsio();
    private void StopAsioClick(object? sender, RoutedEventArgs args) => viewModel?.StopAsio();
    private void CloseAsioClick(object? sender, RoutedEventArgs args) => viewModel?.CloseAsio();
    private void RecoverAsioClick(object? sender, RoutedEventArgs args) => viewModel?.RecoverAsio();

    private void ToggleTunerClick(object? sender, RoutedEventArgs args) =>
        viewModel?.SetTunerEnabled(!viewModel.TunerEnabled);

    private void RefreshMidiClick(object? sender, RoutedEventArgs args) => viewModel?.RefreshMidiDevices();
    private void OpenMidiClick(object? sender, RoutedEventArgs args) => viewModel?.OpenSelectedMidi();
    private void CloseMidiClick(object? sender, RoutedEventArgs args) => viewModel?.CloseMidi();
    private void DrainMidiClick(object? sender, RoutedEventArgs args) => viewModel?.DrainMidi();
    private void ArmMidiLearnClick(object? sender, RoutedEventArgs args) => viewModel?.ArmSelectedMidiLearn();
    private void CancelMidiLearnClick(object? sender, RoutedEventArgs args) => viewModel?.CancelMidiLearn();
    private void DeleteMidiBindingClick(object? sender, RoutedEventArgs args) => viewModel?.DeleteSelectedMidiBinding();
    private void ClearMidiBindingsClick(object? sender, RoutedEventArgs args) => viewModel?.ClearMidiBindings();
    private void ImportMidiBankClick(object? sender, RoutedEventArgs args) => _ = ImportMidiBankAsync();
    private void LoadPresetLibraryClick(object? sender, RoutedEventArgs args) => _ = LoadPresetLibraryAsync();
    private void RefreshPresetLibraryClick(object? sender, RoutedEventArgs args) => viewModel?.RefreshPresetLibrary();
    private void LoadSelectedPresetLibraryClick(object? sender, RoutedEventArgs args) => _ = LoadSelectedPresetLibraryAsync();

    private void NodeOutputEndpointClick(object? sender, RoutedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not string nodeId)
        {
            return;
        }
        SelectConnectionSource(nodeId);
        args.Handled = true;
    }

    private void NodeOutputEndpointPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not string nodeId ||
            !args.GetCurrentPoint(control).Properties.IsLeftButtonPressed)
        {
            return;
        }
        SelectConnectionSource(nodeId);
        draggingConnection = true;
        draggingConnectionSource = nodeId;
        var source = viewModel.CanvasNodes.FirstOrDefault(node => node.InstanceId == nodeId);
        if (source is not null)
        {
            var start = new Point(source.CanvasX + source.Width, source.CanvasY + source.Height * 0.5);
            UpdateConnectionPreview(start);
        }
        args.Pointer.Capture(EditorCanvas);
        args.Handled = true;
    }

    private void CanvasConnectionPointerEntered(object? sender, PointerEventArgs args) =>
        SetConnectionHoverVisual(sender, true);

    private void CanvasConnectionPointerExited(object? sender, PointerEventArgs args) =>
        SetConnectionHoverVisual(sender, false);

    private static void SetConnectionHoverVisual(object? sender, bool visible)
    {
        if (sender is not Control hit || hit.Parent is not Panel parent)
        {
            return;
        }
        var hoverPath = parent.Children.FirstOrDefault(child => child.Classes.Contains("srg-connection-hover-line"));
        if (hoverPath is not null)
        {
            hoverPath.Opacity = visible ? 0.92 : 0.0;
        }
    }

    private void UpdateConnectionPreview(Point end)
    {
        if (ConnectionPreviewPath is null || viewModel is null || string.IsNullOrWhiteSpace(draggingConnectionSource))
        {
            return;
        }
        var source = viewModel.CanvasNodes.FirstOrDefault(node => node.InstanceId == draggingConnectionSource);
        if (source is null)
        {
            return;
        }
        var start = new Point(source.CanvasX + source.Width, source.CanvasY + source.Height * 0.5);
        var feedbackKind = viewModel.ConnectionCandidateFeedbackKind;
        var feedbackBrush = ResolveConnectionFeedbackBrush(feedbackKind);
        var route = ConnectionGeometryConverter.CreateRouteGeometry(start, end);
        if (ConnectionPreviewHalo is not null)
        {
            ConnectionPreviewHalo.Data = route;
            ConnectionPreviewHalo.Stroke = Resources["SrgCanvasBrush"] as IBrush ?? Brushes.Black;
            ConnectionPreviewHalo.Opacity = feedbackKind == ConnectionFeedbackKind.Blocked ? 0.96 : 0.9;
            ConnectionPreviewHalo.IsVisible = true;
        }
        ConnectionPreviewPath.Data = route;
        ConnectionPreviewPath.Stroke = feedbackBrush;
        ConnectionPreviewPath.IsVisible = true;
        if (ConnectionPreviewTarget is not null)
        {
            var targetId = viewModel.ConnectionDestinationNodeId;
            var target = string.IsNullOrWhiteSpace(targetId)
                ? null
                : viewModel.CanvasNodes.FirstOrDefault(node => node.InstanceId == targetId);
            if (target is null)
            {
                ConnectionPreviewTarget.IsVisible = false;
            }
            else
            {
                var targetPoint = CanvasWorldToViewport(GetInputEndpointPoint(target.InstanceId));
                Canvas.SetLeft(ConnectionPreviewTarget, targetPoint.X - 9.0);
                Canvas.SetTop(ConnectionPreviewTarget, targetPoint.Y - 9.0);
                ConnectionPreviewTarget.Stroke = feedbackBrush;
                ConnectionPreviewTarget.StrokeThickness = 3.0;
                ConnectionPreviewTarget.IsVisible = true;
            }
        }
    }

    private Point GetInputEndpointPoint(string nodeId)
    {
        var node = viewModel?.CanvasNodes.FirstOrDefault(candidate => candidate.InstanceId == nodeId);
        return node is null
            ? new Point(0, 0)
            : new Point(node.CanvasX, node.CanvasY + node.Height * 0.5);
    }

    private void HideConnectionPreview()
    {
        if (ConnectionPreviewPath is not null)
        {
            ConnectionPreviewPath.Data = new StreamGeometry();
            ConnectionPreviewPath.IsVisible = false;
        }
        if (ConnectionPreviewHalo is not null)
        {
            ConnectionPreviewHalo.Data = new StreamGeometry();
            ConnectionPreviewHalo.IsVisible = false;
        }
        if (ConnectionPreviewTarget is not null)
        {
            ConnectionPreviewTarget.Stroke = Resources["SrgAccentHoverBrush"] as IBrush ?? Brushes.White;
            ConnectionPreviewTarget.IsVisible = false;
        }
    }

    private string? FindInputEndpoint(Point point)
    {
        if (viewModel is null)
        {
            return null;
        }
        var snapRadius = ConnectionScreenScale(ConnectionSnapScreenRadius);
        return viewModel.CanvasNodes
            .Where(node => node.InstanceId != draggingConnectionSource)
            .Select(node => new
            {
                node.InstanceId,
                Distance = Math.Sqrt(Math.Pow(point.X - node.CanvasX, 2) +
                                     Math.Pow(point.Y - (node.CanvasY + node.Height * 0.5), 2))
            })
            .Where(candidate => candidate.Distance <= snapRadius)
            .OrderBy(candidate => candidate.Distance)
            .Select(candidate => candidate.InstanceId)
            .FirstOrDefault();
    }

    private void SelectConnectionSource(string nodeId)
    {
        if (viewModel is null)
        {
            return;
        }
        viewModel.ConnectionSourceNodeId = nodeId;
        viewModel.ReportInteractionStatus($"已选择 {nodeId} 的输出端点，请点击目标效果器的输入端点");
    }

    private void CanvasConnectionPressed(object? sender, PointerPressedEventArgs args)
    {
        if (viewModel is null || sender is not Control control ||
            control.DataContext is not CanvasConnectionSnapshot connection)
        {
            return;
        }
        viewModel.SelectConnection(connection.SourceInstanceId, connection.DestinationInstanceId);
        args.Handled = true;
    }

    private void UpdateModuleDragPreview(Point canvasPoint, string displayName)
    {
        if (ModuleDragPreview is null || ModuleDragPreviewTitle is null)
        {
            return;
        }

        ModuleDragPreviewTitle.Text = displayName;
        var previewWidth = ModuleDragPreview.Bounds.Width > 0 ? ModuleDragPreview.Bounds.Width : 176.0;
        var previewHeight = ModuleDragPreview.Bounds.Height > 0 ? ModuleDragPreview.Bounds.Height : 42.0;
        var maxLeft = Math.Max(8.0, EditorCanvas.Bounds.Width - previewWidth - 8.0);
        var maxTop = Math.Max(8.0, EditorCanvas.Bounds.Height - previewHeight - 8.0);
        Canvas.SetLeft(ModuleDragPreview, Math.Clamp(canvasPoint.X - previewWidth * 0.5, 8.0, maxLeft));
        Canvas.SetTop(ModuleDragPreview, Math.Clamp(canvasPoint.Y - previewHeight * 0.5, 8.0, maxTop));
        ModuleDragPreview.IsVisible = true;
        moduleDragOverCanvas = true;
    }

    private void HideModuleDragPreview()
    {
        if (ModuleDragPreview is not null)
        {
            ModuleDragPreview.IsVisible = false;
        }
        moduleDragOverCanvas = false;
    }

    private void NodeInputEndpointClick(object? sender, RoutedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not string nodeId)
        {
            return;
        }
        SelectConnectionDestination(nodeId);
        args.Handled = true;
    }

    private void SelectConnectionDestination(string nodeId)
    {
        if (viewModel is null)
        {
            return;
        }
        if (viewModel.ConnectionSourceNodeId is null)
        {
            viewModel.ReportInteractionStatus($"已点击 {nodeId} 的输入端点；请先点击来源效果器的输出端点");
        }
        else
        {
            viewModel.ConnectionDestinationNodeId = nodeId;
            viewModel.ConnectConnectionCandidate();
        }
    }

    private void FixedEndpointClick(object? sender, RoutedEventArgs args)
    {
        viewModel?.ReportInteractionStatus("INPUT / OUTPUT 是当前 ASIO 设备的固定物理端口，不能作为效果器节点移动");
        args.Handled = true;
    }

    private async Task ImportResourceAsync()
    {
        if (viewModel is null)
        {
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            viewModel.ReportFileDialogCancelled("Resource import");
            return;
        }
        var files = await storage.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "导入 NAM / IR / circuit 资源",
            AllowMultiple = false,
            FileTypeFilter = [ResourceFileType]
        });
        var path = files.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            viewModel.ReportFileDialogCancelled("Resource import");
            return;
        }
        viewModel.ImportResource(path);
    }

    private async Task LoadThemeAsync()
    {
        if (viewModel is null)
        {
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            viewModel.ReportFileDialogCancelled("Theme load");
            return;
        }
        var files = await storage.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "加载 SoloRig 主题 JSON",
            AllowMultiple = false,
            FileTypeFilter = [ThemeFileType]
        });
        var path = files.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            viewModel.ReportFileDialogCancelled("Theme load");
            return;
        }
        viewModel.LoadTheme(path);
    }

    private async Task ImportMidiBankAsync()
    {
        if (viewModel is null)
        {
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            viewModel.ReportFileDialogCancelled("MIDI Bank import");
            return;
        }
        var folders = await storage.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "选择 MIDI Bank 目录",
            AllowMultiple = false
        });
        var path = folders.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            viewModel.ReportFileDialogCancelled("MIDI Bank import");
            return;
        }
        viewModel.ImportMidiBank(path);
    }

    private async Task LoadPresetLibraryAsync()
    {
        if (viewModel is null)
        {
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            viewModel.ReportFileDialogCancelled("Preset library load");
            return;
        }
        var folders = await storage.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "选择 SoloRig 预设库目录",
            AllowMultiple = false
        });
        var path = folders.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            viewModel.ReportFileDialogCancelled("Preset library load");
            return;
        }
        viewModel.LoadPresetLibrary(path);
    }

    private async Task LoadSelectedPresetLibraryAsync()
    {
        if (viewModel is null || !viewModel.CanLoadSelectedPresetLibraryEntry)
        {
            return;
        }
        var entry = viewModel.SelectedPresetLibraryEntry;
        if (!await ConfirmPendingChangesAsync())
        {
            return;
        }
        if (!viewModel.OpenPresetLibraryEntry(entry))
        {
            PresetLibraryList.SelectedIndex = viewModel.SelectedPresetLibraryIndex;
        }
    }

    private async Task OpenAsync()
    {
        if (viewModel is null)
        {
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            viewModel.ReportFileDialogCancelled("Open");
            return;
        }
        var files = await storage.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "打开 SoloRig 预设",
            AllowMultiple = false,
            FileTypeFilter = [SrigFileType]
        });
        var path = files.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            viewModel.ReportFileDialogCancelled("Open");
            return;
        }
        if (!await ConfirmPendingChangesAsync())
        {
            return;
        }
        viewModel.OpenFromPath(path);
    }

    private async Task SelectDefaultPresetAsync()
    {
        if (viewModel is not null && await ConfirmPendingChangesAsync())
        {
            viewModel.SelectPreset("SoloRig Default");
        }
    }

    private async Task SelectPresetAsync(string presetName)
    {
        if (viewModel is null)
        {
            return;
        }
        if (!await ConfirmPendingChangesAsync() || !viewModel.SelectPreset(presetName))
        {
            PresetSelector.SelectedItem = viewModel.SelectedPresetName;
        }
    }

    private async Task SelectAdjacentPresetAsync(int direction)
    {
        if (viewModel is null || viewModel.PresetLibraryEntries.Count == 0)
        {
            return;
        }
        var validIndexes = viewModel.PresetLibraryEntries
            .Select((entry, index) => (entry, index))
            .Where(item => item.entry.IsValid)
            .Select(item => item.index)
            .ToArray();
        if (validIndexes.Length == 0)
        {
            viewModel.ReportInteractionStatus("预设库中没有可加载的预设");
            return;
        }
        var currentPosition = Array.IndexOf(validIndexes, viewModel.SelectedPresetLibraryIndex);
        var nextPosition = currentPosition < 0
            ? (direction > 0 ? 0 : validIndexes.Length - 1)
            : (currentPosition + direction + validIndexes.Length) % validIndexes.Length;
        var nextIndex = validIndexes[nextPosition];
        if (!await ConfirmPendingChangesAsync())
        {
            return;
        }
        viewModel.SelectedPresetLibraryIndex = nextIndex;
        if (!viewModel.OpenPresetLibraryEntry(viewModel.SelectedPresetLibraryEntry))
        {
            viewModel.SelectedPresetLibraryIndex = currentPosition >= 0 ? validIndexes[currentPosition] : -1;
        }
    }

    public async Task<bool> ConfirmPendingChangesAsync()
    {
        if (viewModel is null || !viewModel.IsDirty)
        {
            return true;
        }
        var owner = TopLevel.GetTopLevel(this) as Window;
        if (owner is null)
        {
            return false;
        }
        var dialog = new Window
        {
            Title = "未保存的更改",
            Width = 440,
            SizeToContent = SizeToContent.Height,
            CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
        };
        var cancel = new Button { Content = "取消", MinWidth = 88 };
        var discard = new Button { Content = "不保存", MinWidth = 96 };
        var save = new Button { Content = "保存", MinWidth = 88 };
        cancel.Click += (_, _) => dialog.Close(PendingChangesChoice.Cancel);
        discard.Click += (_, _) => dialog.Close(PendingChangesChoice.Discard);
        save.Click += (_, _) => dialog.Close(PendingChangesChoice.Save);
        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Spacing = 8,
        };
        buttons.Children.Add(cancel);
        buttons.Children.Add(discard);
        buttons.Children.Add(save);
        dialog.Content = new StackPanel
        {
            Margin = new global::Avalonia.Thickness(24),
            Spacing = 16,
            Children =
            {
                new TextBlock
                {
                    Text = "当前预设有未保存的更改。保存后继续，或放弃更改继续操作。",
                    TextWrapping = TextWrapping.Wrap,
                },
                buttons,
            },
        };
        var choice = await dialog.ShowDialog<PendingChangesChoice>(owner);
        return choice switch
        {
            PendingChangesChoice.Save => await SaveForCloseAsync(),
            PendingChangesChoice.Discard => true,
            _ => false,
        };
    }

    public Task<bool> SaveForCloseAsync() => SaveAsync();

    private async Task<bool> SaveAsync()
    {
        if (viewModel is null)
        {
            return false;
        }
        if (!string.IsNullOrWhiteSpace(viewModel.Snapshot.SourcePath))
        {
            return viewModel.SaveToPath(viewModel.Snapshot.SourcePath);
        }
        return await SaveAsAsync();
    }

    private async Task<bool> SaveAsAsync()
    {
        if (viewModel is null)
        {
            return false;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            viewModel.ReportFileDialogCancelled("Save As");
            return false;
        }
        var file = await storage.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "保存 SoloRig 预设",
            SuggestedFileName = "SoloRig.srig",
            DefaultExtension = "srig",
            FileTypeChoices = [SrigFileType]
        });
        var path = file?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            viewModel.ReportFileDialogCancelled("Save As");
            return false;
        }
        return viewModel.SaveToPath(path);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        StopRuntimeRefreshTimer();
        if (viewModel is not null)
        {
            viewModel.PropertyChanged -= ViewModelPropertyChanged;
        }
        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        if (viewModel is not null)
        {
            ApplyTheme(viewModel.Theme, viewModel.DarkTheme);
            StartRuntimeRefreshTimer();
        }
    }
}
