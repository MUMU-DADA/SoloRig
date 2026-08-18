using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using SoloRig.Ui;
using Avalonia.Media.Imaging;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using Ellipse = Avalonia.Controls.Shapes.Ellipse;
using Line = Avalonia.Controls.Shapes.Line;
using AvaloniaPath = Avalonia.Controls.Shapes.Path;

namespace SoloRig.Ui.Avalonia;

public sealed partial class PedalWorkshopView : UserControl
{
    private enum CircuitWireVisualPart
    {
        Halo,
        Wire,
        HitTarget,
    }

    private sealed record CircuitWireVisualTag(string Node, CircuitWireVisualPart Part, IBrush BaseStroke);
    private sealed record CircuitCrossingVisualTag(string Node, bool IsHalo, IBrush BaseStroke);
    private sealed record CircuitJunctionVisualTag(string Node);
    private sealed record CircuitLabelLeaderVisualTag(string Node, IBrush BaseStroke);

    private sealed record CircuitEndpoint(AnalogPinDraft Pin, Point Point);

    private readonly record struct CircuitGridPoint(int X, int Y);

    private sealed record CircuitWireSegment(string Node, Point Start, Point End, IBrush Stroke);

    private sealed class CircuitRoutingGrid
    {
        private readonly Dictionary<double, IReadOnlyList<Rect>> horizontalObstacles = [];
        private readonly Dictionary<double, IReadOnlyList<Rect>> verticalObstacles = [];

        public CircuitRoutingGrid(
            IReadOnlyList<double> xCoordinates,
            IReadOnlyList<double> yCoordinates,
            IReadOnlyDictionary<double, int> xIndex,
            IReadOnlyDictionary<double, int> yIndex,
            IReadOnlyList<Rect> obstacles)
        {
            XCoordinates = xCoordinates;
            YCoordinates = yCoordinates;
            XIndex = xIndex;
            YIndex = yIndex;
            Obstacles = obstacles;
        }

        public IReadOnlyList<double> XCoordinates { get; }
        public IReadOnlyList<double> YCoordinates { get; }
        public IReadOnlyDictionary<double, int> XIndex { get; }
        public IReadOnlyDictionary<double, int> YIndex { get; }
        public IReadOnlyList<Rect> Obstacles { get; }

        public IReadOnlyList<Rect> ObstaclesAtY(double y) =>
            horizontalObstacles.TryGetValue(y, out var cached)
                ? cached
                : horizontalObstacles[y] = Obstacles.Where(obstacle =>
                    y > obstacle.Top + 0.5 && y < obstacle.Bottom - 0.5).ToArray();

        public IReadOnlyList<Rect> ObstaclesAtX(double x) =>
            verticalObstacles.TryGetValue(x, out var cached)
                ? cached
                : verticalObstacles[x] = Obstacles.Where(obstacle =>
                    x > obstacle.Left + 0.5 && x < obstacle.Right - 0.5).ToArray();
    }

    private sealed record WorkshopSurfaceFingerprint(int Width, int Height, string Sha256, int PngBytes);

    private readonly record struct CircuitRouteCacheKey(double StartX, double StartY, double EndX, double EndY);

    private sealed record WorkshopThemePalette(
        string Window,
        string Topbar,
        string Panel,
        string Canvas,
        string Control,
        string ControlHover,
        string Border,
        string Text,
        string Muted,
        string Accent,
        string AccentText,
        string Warning,
        string Error,
        string GridMinor,
        string GridMajor);

    private static readonly WorkshopThemePalette DarkPalette = new(
        "#11151B", "#161B22", "#191F27", "#10151A", "#222A34", "#2A3440", "#394655",
        "#EEF3F8", "#9AA8B8", "#4DD6A4", "#10261F", "#FFD166", "#FF7B72", "#1B2530", "#263542");

    private static readonly WorkshopThemePalette LightPalette = new(
        "#F4F7FB", "#FFFFFF", "#EEF3F7", "#F8FAFC", "#FFFFFF", "#E8F0F6", "#C7D2DE",
        "#172033", "#5F6B78", "#0F766E", "#FFFFFF", "#9A6700", "#B42318", "#DDE7F0", "#C7D2DE");

    private const double CircuitCanvasWidth = 2400.0;
    private const double CircuitCanvasHeight = 1600.0;
    private const double CircuitGridSize = 16.0;
    private const double CircuitPinPreviewSnapTolerance = 28.0;
    private const double CircuitPinCommitSnapTolerance = 32.0;
    private const double CircuitNormalWireThickness = 3.0;
    private const double CircuitHoverWireThickness = 5.0;
    private const double CircuitSelectedWireThickness = 5.0;
    private const double CircuitNormalHaloThickness = 8.0;
    private const double CircuitHoverHaloThickness = 9.0;
    private const double CircuitSelectedHaloThickness = 11.0;
    private const double CircuitNormalHitThickness = 18.0;
    private const double CircuitHoverHitThickness = 20.0;
    private const double CircuitSelectedHitThickness = 22.0;
    private static readonly string[] CircuitElectricalRoles = ["positive", "negative", "control", "passive"];
    private static readonly IReadOnlySet<(string First, string Second)> CircuitRoleCompatibility =
        (from first in CircuitElectricalRoles
         from second in CircuitElectricalRoles
         select (first, second)).ToHashSet();
    private const double MinimumCircuitZoom = 0.35;
    private const double MaximumCircuitZoom = 2.5;
    private static readonly IBrush AuditionBaselineBrush = new SolidColorBrush(Color.Parse("#394655"));
    private static readonly IBrush AuditionInputBrush = new SolidColorBrush(Color.Parse("#4DD6A4"));
    private static readonly IBrush AuditionOutputBrush = new SolidColorBrush(Color.Parse("#FFB454"));
    private static readonly IBrush FrequencyMagnitudeBrush = new SolidColorBrush(Color.Parse("#FFD166"));
    private static readonly IBrush FrequencyPhaseBrush = new SolidColorBrush(Color.Parse("#61AFEF"));
    private static readonly IBrush ComparisonABrush = new SolidColorBrush(Color.Parse("#4DD6A4"));
    private static readonly IBrush ComparisonBBrush = new SolidColorBrush(Color.Parse("#61AFEF"));
    private static readonly IBrush CircuitPositivePinBrush = new SolidColorBrush(Color.Parse("#F2B84B"));
    private static readonly IBrush CircuitNegativePinBrush = new SolidColorBrush(Color.Parse("#61AFEF"));
    private static readonly IBrush CircuitControlPinBrush = new SolidColorBrush(Color.Parse("#9CD67A"));
    private bool compactLayout;
    private ThemeSnapshot workshopTheme = ThemeSnapshot.Empty;
    private bool workshopDark = true;
    private bool CircuitSnapEnabled => CircuitSnapToggle?.IsChecked != false;
    private IBrush circuitMinorGridBrush = new SolidColorBrush(Color.Parse("#1B2530"));
    private IBrush circuitMajorGridBrush = new SolidColorBrush(Color.Parse("#263542"));
    private IBrush circuitWireHaloBrush = new SolidColorBrush(Color.Parse("#10151A"));
    private IBrush circuitAccentBrush = new SolidColorBrush(Color.Parse("#4DD6A4"));
    private IBrush circuitHoverBrush = new SolidColorBrush(Color.Parse("#9CF0D1"));
    private IBrush circuitSelectedBrush = new SolidColorBrush(Color.Parse("#FFD166"));
    private IBrush circuitWarningBrush = new SolidColorBrush(Color.Parse("#FFD166"));
    private IBrush circuitErrorBrush = new SolidColorBrush(Color.Parse("#FF7B72"));
    private double circuitZoom = 1.0;
    private double circuitPanX;
    private double circuitPanY;
    private bool panningCircuit;
    private bool selectingCircuitBox;
    private bool circuitSelectionAdditive;
    private bool draggingCircuitPalette;
    private Point circuitPointerStart;
    private Point circuitSelectionStart;
    private Point circuitPreviewPointer;
    private double circuitPanStartX;
    private double circuitPanStartY;
    private AnalogComponentDraft? draggingCircuitComponent;
    private AnalogWirePointDraft? draggingCircuitRoutePoint;
    private AnalogWirePointDraft? selectedCircuitRoutePoint;
    private AnalogWireRouteDraft? draggingCircuitRoute;
    private AnalogComponentDraft? selectedCircuitComponent;
    private readonly HashSet<AnalogComponentDraft> selectedCircuitComponents = [];
    private readonly Dictionary<AnalogComponentDraft, Border> selectedCircuitCards = [];
    private readonly Dictionary<CircuitRouteCacheKey, IReadOnlyList<Point>> circuitRouteCache = [];
    private IReadOnlyList<Rect>? circuitRoutingObstacleSnapshot;
    private CircuitRoutingGrid? circuitRoutingGridSnapshot;
    private readonly Dictionary<AnalogComponentDraft, Point> draggingCircuitStarts = [];
    private readonly List<AnalogComponentDraft> circuitClipboard = [];
    private double circuitClipboardPasteOffset;
    private AnalogPinDraft? pendingCircuitPin;
    private AnalogComponentDescriptor? circuitPaletteDescriptor;
    private string? selectedCircuitNode;
    private string? hoveredCircuitNode;
    private AnalogPinDraft? hoveredCircuitPin;
    private Ellipse? hoveredCircuitPinVisual;
    private Ellipse? pendingCircuitTargetVisual;
    private readonly HashSet<AnalogPinDraft> circuitDiagnosticPins = [];
    private readonly HashSet<AnalogPinDraft> circuitDiagnosticWarningPins = [];
    private readonly HashSet<AnalogPinDraft> circuitDiagnosticErrorPins = [];
    private readonly Dictionary<AnalogPinDraft, string> circuitDiagnosticMessages = [];
    private readonly HashSet<AnalogWireRouteDraft> circuitDiagnosticRoutes = [];
    private readonly HashSet<AnalogWireRouteDraft> circuitDiagnosticWarningRoutes = [];
    private readonly HashSet<AnalogWireRouteDraft> circuitDiagnosticErrorRoutes = [];
    private readonly Dictionary<AnalogWireRouteDraft, string> circuitDiagnosticRouteMessages = [];
    private readonly List<Rect> circuitLabelBounds = [];
    private bool draggingCircuitWire;
    private Point circuitWirePointerStart;
    private readonly List<CircuitWireSegment> renderedCircuitSegments = [];
    private Point draggingCircuitPointerStart;
    private Point circuitPalettePointerStart;
    private Border? selectedCircuitCard;
    private Ellipse? pendingCircuitPinVisual;
    private PedalSkinParameterPlacementDraft? selectedSkinPlacement;
    private PedalSkinElementDraft? selectedSkinElement;
    private bool updatingSkinInspector;
    private bool skinFootswitchSelected;
    private bool draggingSkinElement;
    private Point skinDragPointerStart;
    private Point skinDragElementStart;

    private enum PendingChangesChoice
    {
        Cancel,
        Save,
        Discard,
    }

    private static readonly FilePickerFileType NamFileType = new("NAM model")
    {
        Patterns = ["*.nam", "*.nam-a2"],
    };
    private static readonly FilePickerFileType WaveFileType = new("Wave impulse response")
    {
        Patterns = ["*.wav"],
        MimeTypes = ["audio/wav"],
    };
    private static readonly FilePickerFileType PedalSkinFileType = new("SoloRig pedal appearance")
    {
        Patterns = ["*.json"],
        MimeTypes = ["application/json"],
    };
    private static readonly FilePickerFileType SkinImageFileType = new("Pedal control image")
    {
        Patterns = ["*.png", "*.jpg", "*.jpeg", "*.bmp"],
        MimeTypes = ["image/png", "image/jpeg", "image/bmp"],
    };
    private static readonly FilePickerFileType ModulePackageFileType = new("SoloRig module package")
    {
        Patterns = ["*.srmodule"],
        MimeTypes = ["application/zip"],
    };

    public PedalWorkshopView()
    {
        InitializeComponent();
        ApplyTheme(ThemeSnapshot.Empty, true);
        Document = new ModuleAuthoringDocument();
        RecentProjects = new ModuleAuthoringRecentProjects();
        DataContext = Document;
        ProjectKindSelector.ItemsSource = ModuleAuthoringDocument.KindOptions;
        SelectProjectCreationKind(Document.Kind);
        RefreshRecentProjects();
        Document.AnalogCircuitChanged += (_, _) =>
            Dispatcher.UIThread.Post(RenderCircuitConnections, DispatcherPriority.Background);
        Document.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName == nameof(ModuleAuthoringDocument.AuditionAnalysis))
            {
                Dispatcher.UIThread.Post(RenderAuditionWaveforms, DispatcherPriority.Background);
            }
            if (args.PropertyName == nameof(ModuleAuthoringDocument.FrequencyResponse))
            {
                Dispatcher.UIThread.Post(RenderFrequencyResponse, DispatcherPriority.Background);
            }
            if (args.PropertyName is nameof(ModuleAuthoringDocument.ParameterSweep) or
                nameof(ModuleAuthoringDocument.AuditionComparison))
            {
                Dispatcher.UIThread.Post(RenderParameterAnalysis, DispatcherPriority.Background);
            }
        };
        BuildCircuitGrid();
        CircuitNodeLayer.ContainerPrepared += CircuitNodeContainerPrepared;
        UpdateCircuitInspectorState();
        AttachedToVisualTree += (_, _) =>
        {
            ApplyTheme(workshopTheme, workshopDark);
            Dispatcher.UIThread.Post(RenderCircuitConnections, DispatcherPriority.Background);
            Dispatcher.UIThread.Post(RenderAuditionWaveforms, DispatcherPriority.Background);
            Dispatcher.UIThread.Post(RenderFrequencyResponse, DispatcherPriority.Background);
            Dispatcher.UIThread.Post(RenderParameterAnalysis, DispatcherPriority.Background);
        };
    }

    public ModuleAuthoringDocument Document { get; }
    public ModuleAuthoringRecentProjects RecentProjects { get; }
    public event EventHandler<string>? InstallRequested;
    public bool IsCompactLayout => compactLayout;

    public void ApplyTheme(ThemeSnapshot theme, bool dark)
    {
        workshopTheme = theme;
        workshopDark = dark;
        var palette = ResolvePalette(theme, dark);
        SetBrush("WorkshopWindowBrush", palette.Window);
        SetBrush("WorkshopTopbarBrush", palette.Topbar);
        SetBrush("WorkshopPanelBrush", palette.Panel);
        SetBrush("WorkshopCanvasBrush", palette.Canvas);
        SetBrush("WorkshopControlBrush", palette.Control);
        SetBrush("WorkshopControlHoverBrush", palette.ControlHover);
        SetBrush("WorkshopBorderBrush", palette.Border);
        SetBrush("WorkshopTextBrush", palette.Text);
        SetBrush("WorkshopMutedBrush", palette.Muted);
        SetBrush("WorkshopAccentBrush", palette.Accent);
        SetBrush("WorkshopAccentTextBrush", palette.AccentText);
        SetBrush("WorkshopWarningBrush", palette.Warning);
        SetBrush("WorkshopErrorBrush", palette.Error);
        circuitMinorGridBrush = new SolidColorBrush(Color.Parse(palette.GridMinor));
        circuitMajorGridBrush = new SolidColorBrush(Color.Parse(palette.GridMajor));
        circuitWireHaloBrush = new SolidColorBrush(Color.Parse(palette.Canvas));
        circuitAccentBrush = new SolidColorBrush(Color.Parse(palette.Accent));
        circuitHoverBrush = new SolidColorBrush(Color.Parse(dark ? "#9CF0D1" : "#0B5F58"));
        circuitSelectedBrush = new SolidColorBrush(Color.Parse("#FFD166"));
        circuitWarningBrush = new SolidColorBrush(Color.Parse(palette.Warning));
        circuitErrorBrush = new SolidColorBrush(Color.Parse(palette.Error));
        if (CircuitGridLayer is not null)
        {
            CircuitGridLayer.Children.Clear();
            BuildCircuitGrid();
        }
        if (DataContext is ModuleAuthoringDocument)
        {
            RenderCircuitConnections();
        }
        if (TopLevel.GetTopLevel(this) is TopLevel topLevel)
        {
            topLevel.RequestedThemeVariant = dark ? ThemeVariant.Dark : ThemeVariant.Light;
        }
    }

    public bool ProbeThemePalette()
    {
        var originalTheme = workshopTheme;
        var originalDark = workshopDark;
        try
        {
            ApplyTheme(ThemeSnapshot.Empty, true);
            var darkWindow = (Resources["WorkshopWindowBrush"] as SolidColorBrush)?.Color;
            var darkText = (Resources["WorkshopTextBrush"] as SolidColorBrush)?.Color;
            ApplyTheme(ThemeSnapshot.Empty, false);
            var lightWindow = (Resources["WorkshopWindowBrush"] as SolidColorBrush)?.Color;
            var lightText = (Resources["WorkshopTextBrush"] as SolidColorBrush)?.Color;
            return darkWindow.HasValue && lightWindow.HasValue && darkText.HasValue && lightText.HasValue &&
                   darkWindow.Value != lightWindow.Value && darkText.Value != lightText.Value;
        }
        finally
        {
            ApplyTheme(originalTheme, originalDark);
        }
    }

    public bool HasWorkshopVisualContent =>
        WorkshopTabs is not null && WorkshopTabs.Bounds.Width > 0 && WorkshopTabs.Bounds.Height > 0 &&
        ProjectKindSelector is not null && ProjectKindSelector.Bounds.Width > 0 &&
        TemplateSelector is not null && TemplateSelector.Bounds.Width > 0 &&
        ModuleIdEditor is not null && PortList is not null && ParameterList is not null && ValidationList is not null &&
        AuditionInputWaveform is not null && AuditionOutputWaveform is not null &&
        FrequencyMagnitudePlot is not null && FrequencyPhasePlot is not null &&
        ParameterSweepPlot is not null && AuditionComparisonPlot is not null;

    public bool ProbeProjectWorkflow()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"solorig-workshop-visual-{Guid.NewGuid():N}");
        try
        {
            var probe = new ModuleAuthoringDocument();
            probe.ModuleId = "org.mumu-dada.solorig.workshop-probe";
            probe.DisplayName = "Workshop Probe";
            var parameter = probe.AddParameter();
            parameter.Id = "tone";
            return probe.IsDirty && probe.IsValid && probe.SaveToDirectory(directory) && !probe.IsDirty &&
                   new ModuleAuthoringDocument().OpenFromDirectory(directory);
        }
        catch
        {
            return false;
        }
        finally
        {
            try
            {
                if (Directory.Exists(directory))
                {
                    Directory.Delete(directory, true);
                }
            }
            catch
            {
            }
        }
    }

    public bool ProbeValidationStates()
    {
        static bool UsesBrush(Border border, object? resource) =>
            border.BorderBrush is SolidColorBrush actual && resource is SolidColorBrush expected &&
            actual.Color == expected.Color;

        Document.NewProject(ModuleAuthoringKind.DigitalAlgorithm);
        WorkshopTabs.SelectedIndex = 4;
        Document.RefreshValidation();
        UpdateLayout();
        var emptyVisible = Document.HasNoValidationIssues && ValidationEmptyState.IsVisible &&
                           !ValidationList.IsVisible;

        Document.NewProject(ModuleAuthoringKind.AnalogCircuit);
        var warningRoute = new AnalogWireRouteDraft("out");
        warningRoute.Points.Add(new AnalogWirePointDraft(480.0, 256.0));
        var duplicateRoute = new AnalogWireRouteDraft("out");
        duplicateRoute.Points.Add(new AnalogWirePointDraft(560.0, 256.0));
        Document.AnalogCircuit.WireRoutes.Add(warningRoute);
        Document.AnalogCircuit.WireRoutes.Add(duplicateRoute);
        Document.RefreshValidation();
        UpdateLayout();
        var warningIssue = Document.ValidationIssues.FirstOrDefault(issue =>
            issue.IsWarning && ReferenceEquals(issue.TargetObject, warningRoute));
        var warningBorder = ValidationList.GetVisualDescendants().OfType<Border>()
            .FirstOrDefault(border => ReferenceEquals(border.Tag, warningIssue));
        var warningVisible = warningIssue is not null && Document.IsValid && Document.HasValidationIssues &&
                             !ValidationEmptyState.IsVisible && ValidationList.IsVisible &&
                             warningBorder is not null && UsesBrush(warningBorder, Resources["WorkshopWarningBrush"]);

        Document.ModuleId = string.Empty;
        Document.RefreshValidation();
        UpdateLayout();
        var errorIssue = Document.ValidationIssues.FirstOrDefault(issue =>
            issue is { IsError: true, FieldId: "moduleId" });
        var errorBorder = ValidationList.GetVisualDescendants().OfType<Border>()
            .FirstOrDefault(border => ReferenceEquals(border.Tag, errorIssue));
        var errorVisible = errorIssue is not null && !Document.IsValid &&
                           errorBorder is not null && UsesBrush(errorBorder, Resources["WorkshopErrorBrush"]);
        var validationLayout = ValidationList.Bounds.Width > 0 &&
                               ValidationList.Bounds.Right <= ValidationStateGrid.Bounds.Right + 1.0 &&
                               ValidationList.GetVisualDescendants().OfType<TextBlock>()
                                   .Where(text => text.IsVisible)
                                   .All(text => text.Bounds.Width > 0 &&
                                                text.Bounds.Right <= ValidationList.Bounds.Right + 1.0);

        var result = emptyVisible && warningVisible && errorVisible && validationLayout;
        if (!result)
        {
            Console.Error.WriteLine($"[standalone-visual-probe] validation states detail: " +
                                    $"empty={emptyVisible}/{Document.HasNoValidationIssues}, " +
                                    $"warning={warningVisible}/{warningIssue is not null}/{warningBorder?.BorderBrush}, " +
                                    $"error={errorVisible}/{errorIssue is not null}/{errorBorder?.BorderBrush}, " +
                                    $"layout={validationLayout}/{ValidationList.Bounds}/{ValidationStateGrid.Bounds}");
        }
        Document.NewProject(ModuleAuthoringKind.DigitalAlgorithm);
        return result;
    }

    public bool ProbeResponsiveLayout()
    {
        var originalWidth = Bounds.Width > 0 ? Bounds.Width : 1180.0;
        var originalCompact = compactLayout;
        try
        {
            UpdateResponsiveLayout(860.0);
            UpdateLayout();
            var compactBase = compactLayout && Grid.GetRow(WorkshopActions) == 1 && Grid.GetColumn(WorkshopActions) == 0 &&
                   WorkshopActions.HorizontalAlignment == HorizontalAlignment.Left &&
                   WorkshopStatusGrid.ColumnDefinitions.Count == 4 &&
                   WorkshopStatusGrid.RowDefinitions.Count == 2 &&
                   Grid.GetRow(RecentProjectSearchBox) == 1 && Grid.GetColumn(RecentProjectSearchBox) == 0 &&
                   Grid.GetRow(RecentProjectSelector) == 1 && Grid.GetColumn(RecentProjectSelector) == 1 &&
                   Grid.GetRow(OpenRecentProjectButton) == 1 && Grid.GetColumn(OpenRecentProjectButton) == 2 &&
                   Grid.GetRow(ValidateProjectButton) == 1 && Grid.GetColumn(ValidateProjectButton) == 3 &&
                   SkinWorkspaceGrid.ColumnDefinitions.Count == 1 &&
                   SkinWorkspaceGrid.RowDefinitions.Count == 2 &&
                   Grid.GetRow(SkinPreviewPanel) == 1 && Grid.GetColumn(SkinPreviewPanel) == 0 &&
                   CircuitWorkspaceGrid.ColumnDefinitions.Count == 1 &&
                   CircuitWorkspaceGrid.RowDefinitions.Count == 1 &&
                   Grid.GetColumn(CircuitCanvasSurface) == 0 &&
                   !CircuitPalettePanel.IsVisible && !CircuitInspectorPanel.IsVisible &&
                   CircuitPaletteToggleButton.IsVisible && CircuitInspectorToggleButton.IsVisible &&
                   double.IsNaN(AnalogConfigurationTabs.Height);
            CircuitPaletteToggleClick(null, new RoutedEventArgs());
            UpdateLayout();
            var paletteDrawer = CircuitPalettePanel.IsVisible && !CircuitInspectorPanel.IsVisible &&
                                CircuitPalettePanel.Bounds.Width <= CircuitWorkspaceGrid.Bounds.Width + 0.5;
            CircuitInspectorToggleClick(null, new RoutedEventArgs());
            UpdateLayout();
            var inspectorDrawer = !CircuitPalettePanel.IsVisible && CircuitInspectorPanel.IsVisible &&
                                  CircuitInspectorPanel.Bounds.Width <= CircuitWorkspaceGrid.Bounds.Width + 0.5;
            CircuitInspectorPanel.IsVisible = false;
            return compactBase && paletteDrawer && inspectorDrawer;
        }
        finally
        {
            UpdateResponsiveLayout(originalWidth);
            if (originalCompact != compactLayout)
            {
                UpdateResponsiveLayout(originalCompact ? 860.0 : 1180.0);
            }
            UpdateLayout();
        }
    }

    public bool ProbeNarrowLayoutBounds()
    {
        var layoutWidth = Bounds.Width;
        if (TopLevel.GetTopLevel(this) is Window window && window.Width > 0 && window.Width < 1120.0)
        {
            layoutWidth = window.Width;
        }
        UpdateResponsiveLayout(layoutWidth);
        UpdateLayout();
        var circuitCanvasName = CircuitCanvasSurface.GetValue(AutomationProperties.NameProperty) ?? string.Empty;
        var skinCanvasName = SkinDesignCanvas.GetValue(AutomationProperties.NameProperty) ?? string.Empty;
        var circuitToolbarChildrenFit = CircuitToolbarPanel.Bounds.Width <= 0 ||
            CircuitToolbarPanel.Bounds.Height <= 0 ||
            CircuitToolbarPanel.Children.OfType<Control>().Where(child => child.IsVisible).All(child =>
                child.TranslatePoint(default, CircuitToolbarPanel) is { } origin &&
                origin.X >= -0.5 && origin.Y >= -0.5 &&
                origin.X + child.Bounds.Width <= CircuitToolbarPanel.Bounds.Width + 0.5 &&
                origin.Y + child.Bounds.Height <= CircuitToolbarPanel.Bounds.Height + 0.5);
        // Bounds are device pixels on a scaled desktop; compactLayout is based
        // on the logical width passed through the responsive layout policy.
        var result = Bounds.Width > 0 && compactLayout &&
               WorkshopTopbarGrid.Bounds.Width > 0 && WorkshopActions.Bounds.Width > 0 &&
               WorkshopActions.Bounds.Right <= WorkshopTopbarGrid.Bounds.Width + 0.5 &&
               WorkshopStatusGrid.Bounds.Width > 0 && RecentProjectSearchBox.Bounds.Width > 0 &&
               RecentProjectSelector.Bounds.Width > 0 &&
               RecentProjectSearchBox.Bounds.Right <= WorkshopStatusGrid.Bounds.Width + 0.5 &&
               RecentProjectSelector.Bounds.Right <= WorkshopStatusGrid.Bounds.Width + 0.5 &&
               OpenRecentProjectButton.Bounds.Right <= WorkshopStatusGrid.Bounds.Width + 0.5 &&
               ValidateProjectButton.Bounds.Right <= WorkshopStatusGrid.Bounds.Width + 0.5 &&
               RecentProjectSearchBox.Bounds.Top >= WorkshopStatusText.Bounds.Bottom - 0.5 &&
               CircuitCanvasSurface.Bounds.Width >= CircuitWorkspaceGrid.Bounds.Width - 1.0 &&
               circuitToolbarChildrenFit &&
               CircuitPaletteToggleButton.Bounds.Right <= CircuitWorkspaceGrid.Bounds.Right + 0.5 &&
               CircuitInspectorToggleButton.Bounds.Right <= CircuitWorkspaceGrid.Bounds.Right + 0.5 &&
               !string.IsNullOrWhiteSpace(circuitCanvasName) && !string.IsNullOrWhiteSpace(skinCanvasName);
        if (!result)
        {
            Console.Error.WriteLine($"[standalone-visual-probe] narrow detail: bounds={Bounds.Width:0.##}, compact={compactLayout}, " +
                                    $"topbar={WorkshopTopbarGrid.Bounds}, actions={WorkshopActions.Bounds}, status={WorkshopStatusGrid.Bounds}, " +
                                    $"search={RecentProjectSearchBox.Bounds}/{RecentProjectSearchBox.Bounds.Right:0.##}, recent={RecentProjectSelector.Bounds}, " +
                                    $"open={OpenRecentProjectButton.Bounds}, validate={ValidateProjectButton.Bounds}, circuit={CircuitWorkspaceGrid.Bounds}, " +
                                    $"toolbar={CircuitToolbarPanel.Bounds}/{circuitToolbarChildrenFit}, " +
                                    $"canvas={CircuitCanvasSurface.Bounds}, paletteToggle={CircuitPaletteToggleButton.Bounds}, inspectorToggle={CircuitInspectorToggleButton.Bounds}, " +
                                    $"names={circuitCanvasName}/{skinCanvasName}");
        }
        return result;
    }

    public bool ProbeResponsiveDpiSnapshots()
    {
        if (TopLevel.GetTopLevel(this) is not Window window)
        {
            return false;
        }

        var originalWidth = window.Width;
        var originalHeight = window.Height;
        var originalCompact = compactLayout;
        var samples = new List<(double LogicalWidth, double ViewWidth, double ViewHeight, double RenderScale, WorkshopSurfaceFingerprint? Fingerprint)>();
        try
        {
            foreach (var logicalWidth in new[] { 820.0, 1000.0, 1280.0 })
            {
                window.Width = logicalWidth;
                window.Height = Math.Max(originalHeight > 0 ? originalHeight : 640.0, 640.0);
                UpdateResponsiveLayout(logicalWidth);
                UpdateLayout();
                var scale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
                samples.Add((logicalWidth, Bounds.Width, Bounds.Height, scale, CaptureWorkshopSurface()));
            }

            var valid = samples.All(sample =>
                sample.RenderScale > 0 && sample.Fingerprint is { Width: > 0, Height: > 0 } fingerprint &&
                fingerprint.Width == Math.Max(1, (int)Math.Ceiling(sample.ViewWidth * sample.RenderScale)) &&
                fingerprint.Height == Math.Max(1, (int)Math.Ceiling(sample.ViewHeight * sample.RenderScale)));
            var widthsDistinct = samples.Select(sample => sample.Fingerprint?.Width).Distinct().Count() == samples.Count;
            var compactPolicy = samples[0].LogicalWidth < 1120.0 && samples[1].LogicalWidth < 1120.0 &&
                                 samples[2].LogicalWidth >= 1120.0;
            var result = valid && widthsDistinct && compactPolicy;
            if (!result)
            {
                Console.Error.WriteLine($"[standalone-visual-probe] workshop DPI snapshots: " +
                                        string.Join("; ", samples.Select(sample =>
                                            $"{sample.LogicalWidth:0}=>{sample.Fingerprint?.Width}x{sample.Fingerprint?.Height}@{sample.RenderScale:0.##}")) +
                                        $"; bounds={Bounds.Width:0.##}x{Bounds.Height:0.##}, valid={valid}, distinct={widthsDistinct}, compactPolicy={compactPolicy}");
            }
            return result;
        }
        finally
        {
            window.Width = originalWidth;
            window.Height = originalHeight;
            UpdateResponsiveLayout(originalCompact ? 860.0 : (originalWidth > 0 ? originalWidth : 1180.0));
            UpdateLayout();
        }
    }

    public bool ProbeCircuitRoutingStress()
    {
        var obstacles = new List<Rect>();
        for (var column = 0; column < 8; column++)
        {
            var x = 120.0 + column * 76.0;
            var gapRow = column % 3;
            for (var row = 0; row < 4; row++)
            {
                if (row == gapRow) continue;
                obstacles.Add(new Rect(x, 64.0 + row * 96.0, 52.0, 56.0));
            }
        }

        var stopwatch = Stopwatch.StartNew();
        var routingGrid = BuildCircuitRoutingGrid(obstacles);
        var routes = new List<IReadOnlyList<Point>>();
        const int stressRouteCount = 192;
        for (var index = 0; index < stressRouteCount; index++)
        {
            var y = 44.0 + (index % 24) * 28.0;
            var route = BuildObstacleAvoidingRoute(new Point(40.0, y),
                new Point(CircuitCanvasWidth - 40.0, CircuitCanvasHeight - y), routingGrid);
            routes.Add(route);
        }
        stopwatch.Stop();

        var validRoutes = routes.All(route => route.Count > 1 && RouteAvoidsObstacles(route, obstacles));
        var result = validRoutes && stopwatch.ElapsedMilliseconds < 3000;
        if (!result)
        {
            Console.Error.WriteLine($"[standalone-visual-probe] circuit routing stress: routes={routes.Count}, " +
                                    $"valid={routes.Count(route => route.Count > 1 && RouteAvoidsObstacles(route, obstacles))}, " +
                                    $"elapsedMs={stopwatch.Elapsed.TotalMilliseconds:0.##}");
        }
        return result;
    }

    public bool ProbeSkinDesigner()
    {
        Document.NewProject(ModuleAuthoringKind.DigitalAlgorithm);
        Document.CustomSkinEnabled = true;
        var tone = Document.AddParameter();
        tone.Id = "tone";
        tone.ControlKind = "toggle";
        var first = Document.AddSkinParameterPlacement();
        SelectSkinPlacement(first);
        DuplicateSkinPlacementClick(null, new RoutedEventArgs());
        var second = Document.Skin.ParameterPlacements.LastOrDefault();
        if (second is null || ReferenceEquals(first, second)) return false;
        SelectSkinPlacement(second);
        MoveSelectedSkinElement(Document.Skin.Width * 0.5, 42.0);
        SelectSkinPlacement(first);
        BringSkinPlacementFrontClick(null, new RoutedEventArgs());
        Document.Skin.UseCustomFootswitchPosition = true;
        Document.Skin.FootswitchX = 96.0;
        Document.Skin.FootswitchY = 128.0;
        var elementCountBeforePalette = Document.Skin.Elements.Count;
        AddSkinElementClick(new Button { Tag = PedalControlCatalog.Controls.First(item => item.Type == "fader.detented") },
            new RoutedEventArgs());
        var paletteElement = selectedSkinElement;
        var paletteAdded = Document.Skin.Elements.Count == elementCountBeforePalette + 1 &&
                           paletteElement?.Type == "fader.detented" && SkinElementInspector.IsVisible;
        if (paletteElement is not null)
        {
            paletteElement.Binding = "tone";
            paletteElement.Detents = 7;
        }
        WorkshopTabs.SelectedIndex = 3;
        UpdateLayout();
        var result = Document.Skin.ParameterPlacements.Count == 2 &&
                     Document.Skin.ParameterPlacements[^1] == first &&
                     second.IsToggle && Math.Abs(second.X - 96.0) < 0.001 &&
                     Math.Abs(second.Y - 40.0) < 0.001 &&
                     Document.Skin.UseCustomFootswitchPosition &&
                     paletteAdded && paletteElement?.Detents == 7 &&
                     SkinDesignCanvas.Bounds.Width > 0 && SkinDesignCanvas.Bounds.Height > 0 &&
                     SkinPlacementLayer.ItemCount == 2 &&
                     SkinElementLayer.ItemCount >= Document.Skin.Elements.Count;
        if (!result)
        {
            Console.Error.WriteLine($"[standalone-visual-probe] skin designer detail: placements={Document.Skin.ParameterPlacements.Count}, " +
                                    $"firstLast={Document.Skin.ParameterPlacements[^1] == first}, toggle={second.IsToggle}, " +
                                    $"second={second.X:0.##},{second.Y:0.##}, foot={Document.Skin.UseCustomFootswitchPosition}, " +
                                    $"palette={paletteAdded}/{paletteElement?.Type}/{paletteElement?.Detents}, inspector={SkinElementInspector.IsVisible}, " +
                                    $"canvas={SkinDesignCanvas.Bounds}, placementItems={SkinPlacementLayer.ItemCount}, elementItems={SkinElementLayer.ItemCount}/{Document.Skin.Elements.Count}");
        }
        ClearSkinSelection();
        return result;
    }

    public bool ProbeCircuitCanvas()
    {
        Document.NewProject(ModuleAuthoringKind.AnalogCircuit);
        circuitZoom = 1.0;
        circuitPanX = 0.0;
        circuitPanY = 0.0;
        selectedCircuitNode = null;
        hoveredCircuitNode = null;
        selectedCircuitRoutePoint = null;
        WorkshopTabs.SelectedIndex = 2;
        UpdateLayout();
        RenderCircuitConnections();
        var normalSurfaceFingerprint = CaptureWorkshopSurface();
        var component = Document.AnalogCircuit.Components[0];
        var inspectorWasVisible = CircuitInspectorPanel.IsVisible;
        CircuitInspectorPanel.IsVisible = true;
        ClearCircuitSelection();
        UpdateLayout();
        var inspectorEmpty = CircuitInspectorEmptyState.IsVisible && !AnalogComponentList.IsVisible;
        SelectCircuitComponents([component]);
        UpdateLayout();
        var inspectorSingle = !CircuitInspectorEmptyState.IsVisible && AnalogComponentList.IsVisible &&
                              AnalogComponentList.ItemCount == 1;
        SelectCircuitComponents(Document.AnalogCircuit.Components.Take(2));
        UpdateLayout();
        var inspectorMulti = CircuitInspectorMultiState.IsVisible && !AnalogComponentList.IsVisible &&
                             CircuitInspectorMultiSelectionText.Text?.Contains("2", StringComparison.Ordinal) == true;
        ClearCircuitSelection();
        selectedCircuitNode = "out";
        UpdateCircuitSelectionStatus();
        UpdateLayout();
        var inspectorNetwork = CircuitInspectorNetworkState.IsVisible && !AnalogComponentList.IsVisible &&
                               CircuitInspectorNetworkText.Text?.Contains("out", StringComparison.Ordinal) == true;
        var selectedNetworkPins = Document.AnalogCircuit.Components
            .SelectMany(candidate => candidate.Pins)
            .Where(pin => string.Equals(pin.Node?.Trim(), "out", StringComparison.Ordinal))
            .ToArray();
        var selectedNetworkPinsMarked = selectedNetworkPins.Length > 0 &&
                                        selectedNetworkPins.All(pin =>
                                            FindCircuitPinVisual(pin)?.Stroke == circuitSelectedBrush);
        selectedCircuitNode = null;
        hoveredCircuitNode = "out";
        UpdateCircuitWireVisualState();
        var hoveredNetworkPinsMarked = selectedNetworkPins.Length > 0 &&
                                       selectedNetworkPins.All(pin =>
                                           FindCircuitPinVisual(pin)?.Stroke == circuitHoverBrush);
        hoveredCircuitNode = null;
        UpdateCircuitWireVisualState();
        ClearCircuitSelection();
        CircuitInspectorPanel.IsVisible = inspectorWasVisible;
        var roleColorsVisible = FindCircuitPinVisual(component.Pins[0])?.Stroke == CircuitPositivePinBrush &&
                                FindCircuitPinVisual(component.Pins[1])?.Stroke == CircuitNegativePinBrush;
        var roleMatrixComplete = CircuitElectricalRoles.Length == 4 &&
                                 CircuitRoleCompatibility.Count == 16 &&
                                 CircuitElectricalRoles.All(first =>
                                     CircuitElectricalRoles.All(second =>
                                         CircuitRoleCompatibility.Contains((first, second))));
        var componentCard = CircuitNodeLayer.GetVisualDescendants().OfType<Border>()
            .FirstOrDefault(candidate => ReferenceEquals(candidate.Tag, component));
        var pinVisual = FindCircuitPinVisual(component.Pins[0]);
        var accessibilityVisible = CircuitCanvasSurface.Focusable &&
                                    componentCard?.IsTabStop == true &&
                                    !string.IsNullOrWhiteSpace(componentCard?.GetValue(AutomationProperties.NameProperty)) &&
                                    pinVisual?.IsTabStop == true &&
                                    !string.IsNullOrWhiteSpace(pinVisual?.GetValue(AutomationProperties.NameProperty));
        var originalX = component.CanvasX;
        Document.BeginEditTransaction();
        component.CanvasX += CircuitGridSize;
        Document.EndEditTransaction();
        var moved = component.CanvasX != originalX && Document.CanUndo;
        var undone = Document.Undo() && Math.Abs(Document.AnalogCircuit.Components[0].CanvasX - originalX) < 0.001;
        UpdateLayout();
        RenderCircuitConnections();
        var dropped = DropCircuitPaletteComponent(AnalogComponentCatalog.Get("capacitor"),
            CircuitWorldToViewport(new Point(800.0, 400.0)));
        var paletteDrop = dropped.Type == "capacitor" && Document.AnalogCircuit.Components.Count == 3 &&
                          dropped.CanvasX > 600.0 && Document.Undo() &&
                          Document.AnalogCircuit.Components.Count == 2;
        UpdateLayout();
        RenderCircuitConnections();
        CompleteCircuitBoxSelection(CircuitWorldToViewport(new Point(40.0, 80.0)),
            CircuitWorldToViewport(new Point(620.0, 320.0)));
        var boxSelected = selectedCircuitComponents.Count == 2;
        var additiveTarget = Document.AnalogCircuit.Components[1];
        SelectCircuitComponents([Document.AnalogCircuit.Components[0]]);
        circuitSelectionAdditive = true;
        CompleteCircuitBoxSelection(
            CircuitWorldToViewport(new Point(additiveTarget.CanvasX - 4.0, additiveTarget.CanvasY - 4.0)),
            CircuitWorldToViewport(new Point(additiveTarget.CanvasX + additiveTarget.Descriptor.SymbolWidth + 4.0,
                additiveTarget.CanvasY + additiveTarget.Descriptor.SymbolHeight + 4.0)));
        var additiveBoxSelected = selectedCircuitComponents.Count == 2;
        var previewSource = Document.AnalogCircuit.Components[0].Pins[1];
        var previewTarget = Document.AnalogCircuit.Components[1].Pins[1];
        pendingCircuitPin = previewSource;
        var previewTargetPoint = CircuitWorldToViewport(CircuitPinPoint(Document.AnalogCircuit.Components[1], 1));
        UpdateCircuitWirePreview(previewTargetPoint);
        var previewVisible = CircuitWirePreview.IsVisible &&
                             CircuitWirePreview.Data is StreamGeometry &&
                             CircuitWirePreviewTarget.IsVisible &&
                             CircuitWirePreviewTarget.Stroke is not null &&
                             pendingCircuitTargetVisual is not null &&
                             ReferenceEquals(pendingCircuitTargetVisual.Tag, previewTarget);
        UpdateCircuitWirePreview(new Point(previewTargetPoint.X + CircuitPinPreviewSnapTolerance - 1.0,
            previewTargetPoint.Y));
        var previewSnapBoundary = pendingCircuitTargetVisual is not null &&
                                  ReferenceEquals(pendingCircuitTargetVisual.Tag, previewTarget);
        UpdateCircuitWirePreview(new Point(previewTargetPoint.X + CircuitPinPreviewSnapTolerance + 2.0,
            previewTargetPoint.Y));
        var previewSnapOutside = pendingCircuitTargetVisual is null && !CircuitWirePreviewTarget.IsVisible;
        ClearPendingCircuitPin();
        pendingCircuitPin = previewSource;
        var invalidTargetPoint = CircuitWorldToViewport(CircuitPinPoint(Document.AnalogCircuit.Components[1], 0));
        UpdateCircuitWirePreview(invalidTargetPoint);
        var invalidPreviewVisible = CircuitWirePreviewTarget.IsVisible &&
                                    pendingCircuitTargetVisual is not null &&
                                    pendingCircuitTargetVisual.Stroke == circuitErrorBrush &&
                                    (CircuitSelectionText.Text?.Contains("不能连接", StringComparison.Ordinal) == true ||
                                     CircuitSelectionText.Text?.Contains("无法连接", StringComparison.Ordinal) == true);
        ClearPendingCircuitPin();
        selectedCircuitNode = "out";
        RenderCircuitConnections();
        var wireSelected = CircuitSelectionText.Text?.Contains("out", StringComparison.Ordinal) == true &&
                           CircuitWireLayer.Children.OfType<Line>().Any(line =>
                               line.Tag is CircuitWireVisualTag { Node: "out", Part: CircuitWireVisualPart.HitTarget } &&
                               line.StrokeThickness >= 16.0) &&
                           CircuitWireLayer.Children.OfType<Line>().Any(line =>
                               line.Tag is CircuitWireVisualTag { Node: "out", Part: CircuitWireVisualPart.Wire } &&
                               line.StrokeThickness >= 4.0);
        var zoomBefore = circuitZoom;
        circuitZoom = MinimumCircuitZoom;
        ApplyCircuitCanvasTransform();
        var minimumZoomWire = CircuitWireLayer.Children.OfType<Line>()
            .FirstOrDefault(line => line.Tag is CircuitWireVisualTag { Node: "out", Part: CircuitWireVisualPart.Wire });
        var minimumZoomHit = CircuitWireLayer.Children.OfType<Line>()
            .FirstOrDefault(line => line.Tag is CircuitWireVisualTag { Node: "out", Part: CircuitWireVisualPart.HitTarget });
        var minimumZoomPin = FindCircuitPinVisual(previewSource);
        var minimumZoomStable = minimumZoomWire is not null && minimumZoomHit is not null &&
                                minimumZoomPin?.RenderTransform is ScaleTransform minimumPinScale &&
                                Math.Abs(minimumZoomWire.StrokeThickness -
                                         CircuitSelectedWireThickness / MinimumCircuitZoom) < 0.01 &&
                                Math.Abs(minimumZoomHit.StrokeThickness -
                                         CircuitSelectedHitThickness / MinimumCircuitZoom) < 0.01 &&
                                Math.Abs(minimumPinScale.ScaleX - 1.0 / MinimumCircuitZoom) < 0.01;
        circuitZoom = MaximumCircuitZoom;
        ApplyCircuitCanvasTransform();
        var maximumZoomWire = CircuitWireLayer.Children.OfType<Line>()
            .FirstOrDefault(line => line.Tag is CircuitWireVisualTag { Node: "out", Part: CircuitWireVisualPart.Wire });
        var maximumZoomLabel = CircuitJunctionLayer.Children.OfType<Border>()
            .FirstOrDefault(label => Equals(label.Tag, "out"));
        var maximumZoomPin = FindCircuitPinVisual(previewSource);
        var maximumZoomStable = maximumZoomWire is not null && maximumZoomLabel is not null &&
                                maximumZoomPin?.RenderTransform is ScaleTransform maximumPinScale &&
                                Math.Abs(maximumZoomWire.StrokeThickness -
                                         CircuitSelectedWireThickness / MaximumCircuitZoom) < 0.01 &&
                                Math.Abs(maximumPinScale.ScaleX - 1.0 / MaximumCircuitZoom) < 0.01 &&
                                maximumZoomLabel.IsVisible;
        circuitZoom = zoomBefore;
        ApplyCircuitCanvasTransform();
        var selectedSurfaceFingerprint = CaptureWorkshopSurface();
        var routeForDelete = Document.GetOrCreateAnalogWireRoute("out");
        routeForDelete.Points.Add(new AnalogWirePointDraft(480.0, 320.0));
        selectedCircuitNode = "out";
        RenderCircuitConnections();
        var routeDeleteHandled = RemoveSelectedCircuitRoute();
        var routeDeleted = routeDeleteHandled && selectedCircuitNode is null &&
                           Document.AnalogCircuit.WireRoutes.All(route => route.Node != "out");
        var routeForPointDelete = Document.GetOrCreateAnalogWireRoute("out");
        var firstRoutePoint = new AnalogWirePointDraft(480.0, 320.0);
        var secondRoutePoint = new AnalogWirePointDraft(560.0, 320.0);
        routeForPointDelete.Points.Add(firstRoutePoint);
        routeForPointDelete.Points.Add(secondRoutePoint);
        selectedCircuitNode = "out";
        selectedCircuitRoutePoint = secondRoutePoint;
        RenderCircuitConnections();
        var routePointDeleteHandled = RemoveSelectedCircuitRoutePoint();
        var routePointDeleted = routePointDeleteHandled && routeForPointDelete.Points.Count == 1 &&
                                ReferenceEquals(routeForPointDelete.Points[0], firstRoutePoint);
        RemoveSelectedCircuitRoute();
        var snapBefore = CircuitSnapToggle.IsChecked;
        CircuitSnapToggle.IsChecked = false;
        var freeformSnap = SnapCircuitEdit(17.0);
        CircuitSnapToggle.IsChecked = true;
        var snapToggleWorked = Math.Abs(freeformSnap - 17.0) < 0.001 && CircuitSnapEnabled;
        CircuitSnapToggle.IsChecked = snapBefore;
        renderedCircuitSegments.Clear();
        renderedCircuitSegments.Add(new CircuitWireSegment("cross-a", new Point(120.0, 120.0),
            new Point(320.0, 120.0), circuitAccentBrush));
        renderedCircuitSegments.Add(new CircuitWireSegment("cross-b", new Point(220.0, 40.0),
            new Point(220.0, 200.0), circuitHoverBrush));
        CircuitCrossingLayer.Children.Clear();
        RenderCircuitCrossings();
        var crossoverMarked = CircuitCrossingLayer.Children.OfType<AvaloniaPath>().Count() >= 2;
        var crossoverTagged = CircuitCrossingLayer.Children.OfType<AvaloniaPath>()
            .Select(path => path.Tag)
            .OfType<CircuitCrossingVisualTag>()
            .Count(tag => tag.Node == "cross-a") >= 2 &&
            CircuitCrossingLayer.Children.OfType<AvaloniaPath>()
                .Any(path => path.Tag is CircuitCrossingVisualTag { Node: "cross-a", IsHalo: false });
        var originalSelectedCircuitNode = selectedCircuitNode;
        selectedCircuitNode = "cross-a";
        UpdateCircuitWireVisualState();
        var crossoverSelection = CircuitCrossingLayer.Children.OfType<AvaloniaPath>()
            .Any(path => path.Tag is CircuitCrossingVisualTag { Node: "cross-a", IsHalo: false } &&
                        path.Stroke == circuitSelectedBrush);
        selectedCircuitNode = originalSelectedCircuitNode;
        UpdateCircuitWireVisualState();
        var obstacleRoute = BuildObstacleAvoidingRoute(new Point(64.0, 300.0), new Point(400.0, 300.0),
            [new Rect(176.0, 240.0, 128.0, 120.0)]);
        var obstacleRouteAvoids = obstacleRoute.Count >= 4 &&
                                  RouteAvoidsObstacles(obstacleRoute,
                                      [new Rect(176.0, 240.0, 128.0, 120.0)]);
        var denseObstacles = new[]
        {
            new Rect(144.0, 48.0, 128.0, 128.0),
            new Rect(320.0, 176.0, 128.0, 128.0),
            new Rect(496.0, 48.0, 128.0, 128.0),
        };
        var denseRoute = BuildObstacleAvoidingRoute(new Point(80.0, 112.0), new Point(720.0, 112.0),
            denseObstacles);
        var denseRouteAvoids = denseRoute.Count >= 6 && RouteAvoidsObstacles(denseRoute, denseObstacles);

        var denseLabelComponents = new[]
        {
            Document.AddAnalogComponent("resistor", 120.0, 420.0),
            Document.AddAnalogComponent("resistor", 300.0, 420.0),
            Document.AddAnalogComponent("resistor", 480.0, 420.0),
        };
        denseLabelComponents[0].Pins[0].Node = "dense-label-a";
        denseLabelComponents[1].Pins[0].Node = "dense-label-b";
        denseLabelComponents[2].Pins[0].Node = "dense-label-c";
        denseLabelComponents[0].Pins[1].Node = "dense-label-b";
        denseLabelComponents[1].Pins[1].Node = "dense-label-c";
        denseLabelComponents[2].Pins[1].Node = "dense-label-a";
        Document.AnalogCircuit.Nodes.Add("dense-label-a");
        Document.AnalogCircuit.Nodes.Add("dense-label-b");
        Document.AnalogCircuit.Nodes.Add("dense-label-c");
        foreach (var (node, x) in new[]
                 { ("dense-label-a", 278.0), ("dense-label-b", 282.0), ("dense-label-c", 286.0) })
        {
            var route = Document.GetOrCreateAnalogWireRoute(node);
            route.Points.Add(new AnalogWirePointDraft(x, 220.0));
            route.Points.Add(new AnalogWirePointDraft(x + 12.0, 260.0));
        }
        Document.RefreshValidation();
        RenderCircuitConnections();
        UpdateLayout();
        var denseLabels = CircuitJunctionLayer.Children.OfType<Border>()
            .Where(label => label.Tag is string node && node.StartsWith("dense-label-", StringComparison.Ordinal))
            .ToArray();
        var denseLabelLeaders = CircuitJunctionLayer.Children.OfType<Line>()
            .Where(line => line.Tag is CircuitLabelLeaderVisualTag { Node: var node } &&
                           node.StartsWith("dense-label-", StringComparison.Ordinal))
            .ToArray();
        var denseLabelLeaderLayout = denseLabelLeaders.Length > 0 &&
                                     denseLabelLeaders.All(line =>
                                         double.IsFinite(line.StartPoint.X) && double.IsFinite(line.StartPoint.Y) &&
                                         double.IsFinite(line.EndPoint.X) && double.IsFinite(line.EndPoint.Y) &&
                                         line.StrokeThickness > 0.0);
        var circuitRenderScale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
        var dpiWire = CircuitWireLayer.Children.OfType<Line>()
            .FirstOrDefault(line => line.Tag is CircuitWireVisualTag { Part: CircuitWireVisualPart.Wire });
        var dpiPin = FindCircuitPinVisual(previewSource);
        var dpiPinScale = (dpiPin?.RenderTransform as ScaleTransform)?.ScaleX ?? double.NaN;
        var dpiLeader = denseLabelLeaders.FirstOrDefault();
        var circuitDpiInvariant = dpiWire is not null && dpiPin is not null && dpiLeader is not null &&
                                  circuitRenderScale > 0.0 &&
                                  Math.Abs(dpiWire.StrokeThickness * circuitZoom * circuitRenderScale -
                                           CircuitNormalWireThickness * circuitRenderScale) < 0.05 &&
                                  Math.Abs(dpiPin.Bounds.Width * dpiPinScale * circuitZoom * circuitRenderScale -
                                           14.0 * circuitRenderScale) < 0.05 &&
                                  Math.Abs(dpiLeader.StrokeThickness * circuitZoom * circuitRenderScale -
                                           1.5 * circuitRenderScale) < 0.05;
        var denseLabelLayout = denseLabels.Length >= 3 &&
                               denseLabels.Select(label => label.Bounds).All(bounds =>
                                   bounds.Width > 0 && bounds.Height > 0 &&
                                   bounds.Left >= 0.0 && bounds.Top >= 0.0 &&
                                   bounds.Right <= CircuitCanvasWidth && bounds.Bottom <= CircuitCanvasHeight) &&
                               !denseLabels.SelectMany((label, index) => denseLabels.Skip(index + 1)
                                   .Select(other => (label, other)))
                                   .Any(pair => CircuitRectIntersects(pair.label.Bounds, pair.other.Bounds));
        var labelZoomBefore = circuitZoom;
        circuitZoom = 0.65;
        selectedCircuitNode = null;
        hoveredCircuitNode = null;
        UpdateCircuitWireVisualState();
        var simpleNetworkLabel = CircuitJunctionLayer.Children.OfType<Border>()
            .FirstOrDefault(label => Equals(label.Tag, "out"));
        var complexNetworkLabelsVisible = denseLabels.All(label => label.IsVisible);
        var labelDensityPolicy = simpleNetworkLabel is not null && !simpleNetworkLabel.IsVisible &&
                                 complexNetworkLabelsVisible;
        circuitZoom = labelZoomBefore;
        UpdateCircuitWireVisualState();
        selectedCircuitNode = "dense-label-a";
        UpdateCircuitWireVisualState();
        var denseLeaderSelection = denseLabelLeaders.Any(line =>
            line.Tag is CircuitLabelLeaderVisualTag { Node: "dense-label-a" } &&
            line.Stroke == circuitSelectedBrush);
        var denseJunctionSelection = CircuitJunctionLayer.Children.OfType<Ellipse>()
            .Any(dot => dot.Tag is CircuitJunctionVisualTag { Node: "dense-label-a" } &&
                        dot.Fill == circuitSelectedBrush);
        selectedCircuitNode = null;
        UpdateCircuitWireVisualState();
        Document.NewProject(ModuleAuthoringKind.AnalogCircuit);
        RenderCircuitConnections();
        ClearCircuitSelection();
        var diagnosticComponent = Document.AnalogCircuit.Components[0];
        var diagnosticNode = diagnosticComponent.Pins[0].Node;
        diagnosticComponent.Pins[0].Node = "orphan-probe";
        Document.RefreshValidation();
        RenderCircuitConnections();
        UpdateLayout();
        var diagnosticIssue = Document.ValidationIssues.FirstOrDefault(issue =>
            issue.Target == ModuleAuthoringIssueTarget.AnalogPin &&
            ReferenceEquals(issue.TargetObject, diagnosticComponent));
        var diagnosticBrush = diagnosticIssue?.IsWarning == true ? circuitWarningBrush : circuitErrorBrush;
        var diagnosticPinMarked = diagnosticIssue is not null &&
                                  circuitDiagnosticPins.Contains(diagnosticComponent.Pins[0]) &&
                                  FindCircuitPinVisual(diagnosticComponent.Pins[0])?.Stroke == diagnosticBrush;
        if (diagnosticIssue is not null)
        {
            NavigateToValidationIssue(diagnosticIssue);
            UpdateLayout();
        }
        var diagnosticNavigation = diagnosticIssue is not null && WorkshopTabs.SelectedIndex == 2 &&
                                   selectedCircuitComponents.Contains(diagnosticComponent);
        diagnosticComponent.Pins[0].Node = diagnosticNode;
        Document.RefreshValidation();
        RenderCircuitConnections();
        var diagnosticRoutePoint = new AnalogWirePointDraft(480.0, 256.0);
        var diagnosticRoute = new AnalogWireRouteDraft("out");
        diagnosticRoute.Points.Add(diagnosticRoutePoint);
        var duplicateDiagnosticRoute = new AnalogWireRouteDraft("out");
        duplicateDiagnosticRoute.Points.Add(new AnalogWirePointDraft(560.0, 256.0));
        Document.AnalogCircuit.WireRoutes.Add(diagnosticRoute);
        Document.AnalogCircuit.WireRoutes.Add(duplicateDiagnosticRoute);
        Document.RefreshValidation();
        selectedCircuitNode = "out";
        RenderCircuitConnections();
        var diagnosticRouteMarked = circuitDiagnosticRoutes.Contains(diagnosticRoute) &&
                                    circuitDiagnosticRouteMessages.TryGetValue(diagnosticRoute, out var routeMessage) &&
                                    routeMessage.Contains("重复人工路由", StringComparison.Ordinal) &&
                                    CircuitJunctionLayer.Children.OfType<Ellipse>().Any(ellipse =>
                                        ReferenceEquals(ellipse.Tag, diagnosticRoutePoint) &&
                                        ellipse.Stroke == circuitWarningBrush);
        var diagnosticSurfaceFingerprint = CaptureWorkshopSurface();
        Document.AnalogCircuit.WireRoutes.Remove(diagnosticRoute);
        Document.AnalogCircuit.WireRoutes.Remove(duplicateDiagnosticRoute);
        Document.NewProject(ModuleAuthoringKind.AnalogCircuit);
        ClearCircuitSelection();
        RenderCircuitConnections();
        UpdateLayout();
        var restoredSurfaceFingerprint = CaptureWorkshopSurface();
        var visualStateBaselines = normalSurfaceFingerprint is not null &&
                                    selectedSurfaceFingerprint is not null &&
                                    diagnosticSurfaceFingerprint is not null &&
                                    restoredSurfaceFingerprint is not null &&
                                    normalSurfaceFingerprint.Sha256 != selectedSurfaceFingerprint.Sha256 &&
                                    selectedSurfaceFingerprint.Sha256 != diagnosticSurfaceFingerprint.Sha256 &&
                                    normalSurfaceFingerprint.Sha256 == restoredSurfaceFingerprint.Sha256;
        if (!visualStateBaselines)
        {
            Console.Error.WriteLine($"[standalone-visual-probe] circuit surface detail: " +
                                    $"normal={normalSurfaceFingerprint?.Width}x{normalSurfaceFingerprint?.Height}/{normalSurfaceFingerprint?.Sha256}, " +
                                    $"selected={selectedSurfaceFingerprint?.Width}x{selectedSurfaceFingerprint?.Height}/{selectedSurfaceFingerprint?.Sha256}, " +
                                    $"diagnostic={diagnosticSurfaceFingerprint?.Width}x{diagnosticSurfaceFingerprint?.Height}/{diagnosticSurfaceFingerprint?.Sha256}, " +
                                    $"restored={restoredSurfaceFingerprint?.Width}x{restoredSurfaceFingerprint?.Height}/{restoredSurfaceFingerprint?.Sha256}");
        }
        var circuitResult = CircuitCanvasSurface.Bounds.Width > 0 && CircuitCanvasSurface.Bounds.Height > 0 &&
               CircuitNodeLayer.ItemCount == Document.AnalogCircuit.Components.Count &&
               CircuitWireLayer.Children.Count > 0 && moved && undone && paletteDrop && boxSelected &&
               previewVisible && invalidPreviewVisible && wireSelected && routeDeleted && routePointDeleted &&
                previewSnapBoundary && previewSnapOutside &&
                additiveBoxSelected && snapToggleWorked &&
                minimumZoomStable && maximumZoomStable &&
                crossoverMarked && crossoverTagged && crossoverSelection && obstacleRouteAvoids && denseRouteAvoids && denseLabelLayout && denseLabelLeaderLayout && denseLeaderSelection && labelDensityPolicy && circuitDpiInvariant && denseJunctionSelection &&
               roleColorsVisible && roleMatrixComplete && selectedNetworkPinsMarked && hoveredNetworkPinsMarked && accessibilityVisible && visualStateBaselines && inspectorEmpty && inspectorSingle && inspectorMulti &&
                inspectorNetwork && diagnosticPinMarked && diagnosticNavigation && diagnosticRouteMarked;
        if (!circuitResult)
        {
            Console.Error.WriteLine($"[standalone-visual-probe] circuit detail: " +
                                    $"surface={CircuitCanvasSurface.Bounds.Width:0.##}x{CircuitCanvasSurface.Bounds.Height:0.##}, " +
                                    $"items={CircuitNodeLayer.ItemCount}/{Document.AnalogCircuit.Components.Count}, wires={CircuitWireLayer.Children.Count}, " +
                                    $"moved={moved}, undone={undone}, palette={paletteDrop}, box={boxSelected}, preview={previewVisible}, invalid={invalidPreviewVisible}, " +
                                    $"wire={wireSelected}, route={routeDeleted}, point={routePointDeleted}, boundary={previewSnapBoundary}, outside={previewSnapOutside}, " +
                                    $"additive={additiveBoxSelected}, snap={snapToggleWorked}, minZoom={minimumZoomStable}, maxZoom={maximumZoomStable}, " +
                                    $"cross={crossoverMarked}/{crossoverTagged}/{crossoverSelection}, routeAvoid={obstacleRouteAvoids}/{denseRouteAvoids}, " +
                                    $"labels={denseLabelLayout}/{denseLabelLeaderLayout}/{denseLeaderSelection}/{labelDensityPolicy}/{denseJunctionSelection}, " +
                                    $"dpi={circuitDpiInvariant}, role={roleColorsVisible}/{roleMatrixComplete}, pins={selectedNetworkPinsMarked}/{hoveredNetworkPinsMarked}, access={accessibilityVisible}, " +
                                    $"fingerprints={visualStateBaselines}, inspectors={inspectorEmpty}/{inspectorSingle}/{inspectorMulti}/{inspectorNetwork}, " +
                                    $"diagnostics={diagnosticPinMarked}/{diagnosticNavigation}/{diagnosticRouteMarked}");
        }
        return circuitResult;
    }

    private WorkshopSurfaceFingerprint? CaptureWorkshopSurface()
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
            return new WorkshopSurfaceFingerprint(pixelSize.Width, pixelSize.Height,
                Convert.ToHexString(SHA256.HashData(png)), png.Length);
        }
        catch
        {
            return null;
        }
    }

    public bool ProbeAuditionWorkspace()
    {
        Document.NewProject(ModuleAuthoringKind.AnalogCircuit);
        var fileInput = Enumerable.Range(0, 128)
            .Select(index => (float)(0.18 * Math.Sin(index * Math.PI / 16.0))).ToArray();
        Document.SetAuditionWaveFile(new AuditionWaveFileSnapshot(
            "C:\\probe\\workshop-input.wav", 48000U, 1U, 128U, "PCM 16", fileInput));
        Document.AuditionStimulusIndex = 2;
        Document.AuditionStartFrequencyHz = 100.0;
        Document.AuditionEndFrequencyHz = 5000.0;
        var input = Enumerable.Range(0, 128)
            .Select(index => (float)(0.2 * Math.Sin(index * Math.PI / 16.0))).ToArray();
        var output = input.Select(sample => sample * 0.5F).ToArray();
        Document.SetAuditionResult(new ModuleAuditionAnalysisSnapshot(
            2U, 48000U, 128U, 1U, 0U,
            0.2, 0.14, 0.0, 0.1, 0.07, 0.0, 1.0, true, input, output));
        Document.SetFrequencyResponseResult(new ModuleFrequencyResponseSnapshot(
            48000U, 4096U, "input", "output", 0.2,
            Enumerable.Range(0, 16).Select(index => new ModuleFrequencyResponsePoint(
                100.0 * Math.Pow(50.0, index / 15.0), -3.0 * index / 15.0, -90.0 * index / 15.0)).ToArray(),
            1.0, 1.5));
        var comparisonA = new ModuleAuditionAnalysisSnapshot(
            3U, 48000U, 128U, 1U, 0U, 0.2, 0.14, 0.0, 0.1, 0.07, 0.0, 1.0, true,
            input, output);
        var comparisonBOutput = input.Select(sample => sample * 0.8F).ToArray();
        var comparisonB = comparisonA with { OutputPeak = 0.16, OutputRms = 0.112, OutputWaveform = comparisonBOutput };
        Document.SetParameterSweepResult(new ModuleParameterSweepSnapshot("gain", 0.0, 2.0,
            Enumerable.Range(0, 12).Select(index => new ModuleParameterSweepPoint(
                index * 2.0 / 11.0, 0.1 + index * 0.01, 0.07 + index * 0.007,
                -6.0 + index * 0.7, 1.0)).ToArray()));
        Document.SetAuditionComparisonResult(new ModuleAuditionComparisonSnapshot(
            "gain", 0.5, 1.5, comparisonA, comparisonB));
        WorkshopTabs.SelectedIndex = 4;
        UpdateLayout();
        RenderAuditionWaveforms();
        RenderFrequencyResponse();
        RenderParameterAnalysis();
        return !Document.IsDirty && Document.HasAuditionFile &&
               Document.AuditionFileSummary.Contains("workshop-input.wav", StringComparison.Ordinal) &&
               Document.CreateAuditionRequest() is
                   { Stimulus: ModuleAuditionStimulus.LogarithmicSweep } &&
               AuditionInputWaveform.Bounds.Height > 0 && AuditionOutputWaveform.Bounds.Height > 0 &&
               AuditionInputWaveform.Children.Count > 2 && AuditionOutputWaveform.Children.Count > 2 &&
               FrequencyMagnitudePlot.Children.Count > 4 && FrequencyPhasePlot.Children.Count > 4 &&
               ParameterSweepPlot.Children.Count > 4 && AuditionComparisonPlot.Children.Count > 4;
    }

    public void HandleWindowKeyDown(KeyEventArgs args)
    {
        if (args.Key == Key.Delete && WorkshopTabs.SelectedIndex == 3)
        {
            DeleteSelectedSkinElement();
            args.Handled = true;
            return;
        }
        if ((args.KeyModifiers & KeyModifiers.Control) == 0)
        {
            return;
        }
        if (args.Key == Key.Z)
        {
            if ((args.KeyModifiers & KeyModifiers.Shift) != 0)
            {
                ClearSkinSelection();
                Document.Redo();
            }
            else
            {
                ClearSkinSelection();
                Document.Undo();
            }
            args.Handled = true;
        }
        else if (args.Key == Key.Y)
        {
            ClearSkinSelection();
            Document.Redo();
            args.Handled = true;
        }
        else if (args.Key == Key.S)
        {
            _ = (args.KeyModifiers & KeyModifiers.Shift) != 0 ? SaveAsAsync() : SaveAsync();
            args.Handled = true;
        }
        else if (args.Key == Key.O)
        {
            _ = OpenProjectAsync();
            args.Handled = true;
        }
        else if (args.Key == Key.N)
        {
            _ = NewProjectAsync();
            args.Handled = true;
        }
    }

    private void WorkshopSizeChanged(object? sender, SizeChangedEventArgs args) =>
        UpdateResponsiveLayout(args.NewSize.Width);

    private static WorkshopThemePalette ResolvePalette(ThemeSnapshot theme, bool dark)
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
            Control = control,
            ControlHover = ThemeColor(theme, "controlHover", fallback.ControlHover),
            Border = ThemeColor(theme, "border", fallback.Border),
            Text = ThemeColor(theme, "text", fallback.Text),
            Muted = ThemeColor(theme, "muted", fallback.Muted),
            Accent = accent,
            AccentText = ThemeColor(theme, "accentText", fallback.AccentText),
            Warning = ThemeColor(theme, "warning", fallback.Warning),
            Error = ThemeColor(theme, "error", fallback.Error),
            GridMinor = ThemeColor(theme, "grid", fallback.GridMinor),
            GridMajor = ThemeColor(theme, "strongBorder", fallback.GridMajor),
        };
    }

    private static string ThemeColor(ThemeSnapshot theme, string key, string fallback) =>
        theme.GetColor(key) ?? fallback;

    private void SetBrush(string key, string color) =>
        Resources[key] = new SolidColorBrush(Color.Parse(color));

    private void UpdateResponsiveLayout(double width)
    {
        var compact = width > 0 && width < 1120.0;
        compactLayout = compact;
        WorkshopTopbarGrid.ColumnDefinitions.Clear();
        WorkshopTopbarGrid.RowDefinitions.Clear();
        WorkshopStatusGrid.ColumnDefinitions.Clear();
        WorkshopStatusGrid.RowDefinitions.Clear();
        SkinWorkspaceGrid.ColumnDefinitions.Clear();
        SkinWorkspaceGrid.RowDefinitions.Clear();
        CircuitWorkspaceGrid.ColumnDefinitions.Clear();
        CircuitWorkspaceGrid.RowDefinitions.Clear();
        if (compact)
        {
            WorkshopTopbarGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            WorkshopTopbarGrid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            WorkshopTopbarGrid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            Grid.SetColumn(WorkshopTitlePanel, 0);
            Grid.SetRow(WorkshopTitlePanel, 0);
            Grid.SetColumn(WorkshopActions, 0);
            Grid.SetRow(WorkshopActions, 1);
            WorkshopActions.HorizontalAlignment = HorizontalAlignment.Left;
            WorkshopActions.Margin = new Thickness(0, 8, 0, 0);

            WorkshopStatusGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            WorkshopStatusGrid.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(1.2, GridUnitType.Star)));
            WorkshopStatusGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
            WorkshopStatusGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
            WorkshopStatusGrid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            WorkshopStatusGrid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            Grid.SetRow(WorkshopKindBadge, 0);
            Grid.SetColumn(WorkshopKindBadge, 0);
            Grid.SetRow(WorkshopStatusText, 0);
            Grid.SetColumn(WorkshopStatusText, 1);
            Grid.SetColumnSpan(WorkshopStatusText, 3);
            Grid.SetRow(RecentProjectSearchBox, 1);
            Grid.SetColumn(RecentProjectSearchBox, 0);
            RecentProjectSearchBox.Margin = new Thickness(0, 8, 6, 0);
            Grid.SetRow(RecentProjectSelector, 1);
            Grid.SetColumn(RecentProjectSelector, 1);
            RecentProjectSelector.Margin = new Thickness(0, 8, 6, 0);
            Grid.SetRow(OpenRecentProjectButton, 1);
            Grid.SetColumn(OpenRecentProjectButton, 2);
            OpenRecentProjectButton.Margin = new Thickness(0, 8, 6, 0);
            Grid.SetRow(ValidateProjectButton, 1);
            Grid.SetColumn(ValidateProjectButton, 3);
            ValidateProjectButton.Margin = new Thickness(0, 8, 0, 0);

            SkinWorkspaceGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            SkinWorkspaceGrid.RowDefinitions.Add(new RowDefinition(GridLength.Star));
            SkinWorkspaceGrid.RowDefinitions.Add(new RowDefinition(new GridLength(260)));
            Grid.SetColumn(SkinEditorScroll, 0);
            Grid.SetRow(SkinEditorScroll, 0);
            SkinEditorScroll.Margin = new Thickness(0, 0, 0, 14);
            Grid.SetColumn(SkinPreviewPanel, 0);
            Grid.SetRow(SkinPreviewPanel, 1);
            SkinPreviewPanel.MinHeight = 240;

            CircuitWorkspaceGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            CircuitWorkspaceGrid.RowDefinitions.Add(new RowDefinition(GridLength.Star));
            Grid.SetColumn(CircuitCanvasSurface, 0);
            Grid.SetRow(CircuitCanvasSurface, 0);
            CircuitCanvasSurface.Margin = default;
            Grid.SetColumn(CircuitPalettePanel, 0);
            Grid.SetRow(CircuitPalettePanel, 0);
            CircuitPalettePanel.Width = 240.0;
            CircuitPalettePanel.HorizontalAlignment = HorizontalAlignment.Left;
            CircuitPalettePanel.Margin = default;
            CircuitPalettePanel.IsVisible = false;
            CircuitPalettePanel.SetValue(Panel.ZIndexProperty, 10);
            Grid.SetColumn(CircuitInspectorPanel, 0);
            Grid.SetRow(CircuitInspectorPanel, 0);
            CircuitInspectorPanel.Width = Math.Clamp(width - 96.0, 280.0, 380.0);
            CircuitInspectorPanel.HorizontalAlignment = HorizontalAlignment.Right;
            CircuitInspectorPanel.Margin = default;
            CircuitInspectorPanel.IsVisible = false;
            CircuitInspectorPanel.SetValue(Panel.ZIndexProperty, 10);
            CircuitPaletteToggleButton.IsVisible = true;
            CircuitInspectorToggleButton.IsVisible = true;
            AnalogConfigurationTabs.MaxHeight = 320.0;
        }
        else
        {
            WorkshopTopbarGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            WorkshopTopbarGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
            WorkshopTopbarGrid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            Grid.SetColumn(WorkshopTitlePanel, 0);
            Grid.SetRow(WorkshopTitlePanel, 0);
            Grid.SetColumn(WorkshopActions, 1);
            Grid.SetRow(WorkshopActions, 0);
            WorkshopActions.HorizontalAlignment = HorizontalAlignment.Right;
            WorkshopActions.Margin = default;

            WorkshopStatusGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
            WorkshopStatusGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            WorkshopStatusGrid.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(180)));
            WorkshopStatusGrid.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(220)));
            WorkshopStatusGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
            WorkshopStatusGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
            WorkshopStatusGrid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            Grid.SetRow(WorkshopKindBadge, 0);
            Grid.SetColumn(WorkshopKindBadge, 0);
            Grid.SetRow(WorkshopStatusText, 0);
            Grid.SetColumn(WorkshopStatusText, 1);
            Grid.SetColumnSpan(WorkshopStatusText, 1);
            Grid.SetRow(RecentProjectSearchBox, 0);
            Grid.SetColumn(RecentProjectSearchBox, 2);
            RecentProjectSearchBox.Margin = new Thickness(0, 0, 6, 0);
            Grid.SetRow(RecentProjectSelector, 0);
            Grid.SetColumn(RecentProjectSelector, 3);
            RecentProjectSelector.Margin = new Thickness(0, 0, 6, 0);
            Grid.SetRow(OpenRecentProjectButton, 0);
            Grid.SetColumn(OpenRecentProjectButton, 4);
            OpenRecentProjectButton.Margin = new Thickness(0, 0, 6, 0);
            Grid.SetRow(ValidateProjectButton, 0);
            Grid.SetColumn(ValidateProjectButton, 5);
            ValidateProjectButton.Margin = default;

            SkinWorkspaceGrid.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(1.35, GridUnitType.Star)));
            SkinWorkspaceGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            SkinWorkspaceGrid.RowDefinitions.Add(new RowDefinition(GridLength.Star));
            Grid.SetColumn(SkinEditorScroll, 0);
            Grid.SetRow(SkinEditorScroll, 0);
            SkinEditorScroll.Margin = new Thickness(0, 0, 18, 0);
            Grid.SetColumn(SkinPreviewPanel, 1);
            Grid.SetRow(SkinPreviewPanel, 0);
            SkinPreviewPanel.MinHeight = 280;

            CircuitWorkspaceGrid.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(190)));
            CircuitWorkspaceGrid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            CircuitWorkspaceGrid.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(340)));
            CircuitWorkspaceGrid.RowDefinitions.Add(new RowDefinition(GridLength.Star));
            Grid.SetColumn(CircuitPalettePanel, 0);
            Grid.SetRow(CircuitPalettePanel, 0);
            CircuitPalettePanel.Width = double.NaN;
            CircuitPalettePanel.HorizontalAlignment = HorizontalAlignment.Stretch;
            CircuitPalettePanel.Margin = new Thickness(0, 0, 12, 0);
            CircuitPalettePanel.IsVisible = true;
            CircuitPalettePanel.SetValue(Panel.ZIndexProperty, 0);
            Grid.SetColumn(CircuitCanvasSurface, 1);
            Grid.SetRow(CircuitCanvasSurface, 0);
            CircuitCanvasSurface.Margin = default;
            Grid.SetColumn(CircuitInspectorPanel, 2);
            Grid.SetRow(CircuitInspectorPanel, 0);
            CircuitInspectorPanel.Width = double.NaN;
            CircuitInspectorPanel.HorizontalAlignment = HorizontalAlignment.Stretch;
            CircuitInspectorPanel.Margin = new Thickness(12, 0, 0, 0);
            CircuitInspectorPanel.IsVisible = true;
            CircuitInspectorPanel.SetValue(Panel.ZIndexProperty, 0);
            CircuitPaletteToggleButton.IsVisible = false;
            CircuitInspectorToggleButton.IsVisible = false;
            AnalogConfigurationTabs.MaxHeight = 280.0;
        }
    }

    private void CircuitPaletteToggleClick(object? sender, RoutedEventArgs args)
    {
        if (!compactLayout)
        {
            return;
        }
        var show = !CircuitPalettePanel.IsVisible;
        CircuitInspectorPanel.IsVisible = false;
        CircuitPalettePanel.IsVisible = show;
    }

    private void CircuitInspectorToggleClick(object? sender, RoutedEventArgs args)
    {
        if (!compactLayout)
        {
            return;
        }
        var show = !CircuitInspectorPanel.IsVisible;
        CircuitPalettePanel.IsVisible = false;
        CircuitInspectorPanel.IsVisible = show;
    }

    public async Task<bool> ConfirmPendingChangesAsync()
    {
        if (!Document.IsDirty)
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
            Title = "未保存的模块项目",
            Width = 460,
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
            Orientation = global::Avalonia.Layout.Orientation.Horizontal,
            HorizontalAlignment = global::Avalonia.Layout.HorizontalAlignment.Right,
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
                    Text = "当前效果器项目有未保存的更改。保存后继续，或放弃更改继续操作。",
                    TextWrapping = global::Avalonia.Media.TextWrapping.Wrap,
                },
                buttons,
            },
        };
        var choice = await dialog.ShowDialog<PendingChangesChoice>(owner);
        return choice switch
        {
            PendingChangesChoice.Save => await SaveAsync(),
            PendingChangesChoice.Discard => true,
            _ => false,
        };
    }

    public Task<bool> SaveForCloseAsync() => SaveAsync();

    private void BuildCircuitGrid()
    {
        if (CircuitGridLayer is null || CircuitGridLayer.Children.Count > 0)
        {
            return;
        }
        for (var x = 0.0; x <= CircuitCanvasWidth; x += CircuitGridSize)
        {
            CircuitGridLayer.Children.Add(new Line
            {
                StartPoint = new Point(x, 0.0),
                EndPoint = new Point(x, CircuitCanvasHeight),
                Stroke = ((int)(x / CircuitGridSize) % 4 == 0) ? circuitMajorGridBrush : circuitMinorGridBrush,
                StrokeThickness = 1.0,
            });
        }
        for (var y = 0.0; y <= CircuitCanvasHeight; y += CircuitGridSize)
        {
            CircuitGridLayer.Children.Add(new Line
            {
                StartPoint = new Point(0.0, y),
                EndPoint = new Point(CircuitCanvasWidth, y),
                Stroke = ((int)(y / CircuitGridSize) % 4 == 0) ? circuitMajorGridBrush : circuitMinorGridBrush,
                StrokeThickness = 1.0,
            });
        }
        ApplyCircuitCanvasTransform();
    }

    private void RenderCircuitConnections()
    {
        if (CircuitWireLayer is null || CircuitCrossingLayer is null || CircuitJunctionLayer is null)
        {
            return;
        }
        CircuitWireLayer.Children.Clear();
        CircuitCrossingLayer.Children.Clear();
        CircuitJunctionLayer.Children.Clear();
        renderedCircuitSegments.Clear();
        circuitLabelBounds.Clear();
        circuitRouteCache.Clear();
        circuitRoutingObstacleSnapshot = null;
        circuitRoutingGridSnapshot = null;
        circuitRoutingObstacleSnapshot = CircuitRoutingObstacles();
        circuitRoutingGridSnapshot = BuildCircuitRoutingGrid(circuitRoutingObstacleSnapshot);
        RefreshCircuitDiagnosticPins();
        var endpoints = Document.AnalogCircuit.Components
            .SelectMany(component => component.Pins.Select((pin, index) => new
            {
                Pin = pin,
                Point = CircuitPinPoint(component, index),
            }))
            .Where(endpoint => !string.IsNullOrWhiteSpace(endpoint.Pin.Node))
            .GroupBy(endpoint => endpoint.Pin.Node.Trim(), StringComparer.Ordinal);
        foreach (var node in endpoints)
        {
            var circuitEndpoints = node.Select(endpoint => new CircuitEndpoint(endpoint.Pin, endpoint.Point)).ToArray();
            if (circuitEndpoints.Length < 2)
            {
                continue;
            }
            var stroke = CircuitNodeBaseBrush(node.Key);
            var leadPoints = circuitEndpoints.Select(CircuitLeadPoint).ToArray();
            var route = Document.AnalogCircuit.WireRoutes.FirstOrDefault(candidate =>
                string.Equals(candidate.Node, node.Key, StringComparison.Ordinal));
            foreach (var endpoint in circuitEndpoints)
            {
                AddCircuitWireSegment(node.Key, endpoint.Point, CircuitLeadPoint(endpoint), stroke);
            }
            if (route is not null && route.Points.Count > 0)
            {
                var routeStroke = circuitDiagnosticRoutes.Contains(route) ? CircuitRouteDiagnosticBrush(route) : stroke;
                var waypoints = route.Points.Select(point => new Point(point.X, point.Y)).ToArray();
                foreach (var lead in leadPoints)
                {
                    var nearest = waypoints.OrderBy(point => DistanceSquared(point, lead)).First();
                    AddRoutedManhattanWire(node.Key, lead, nearest, routeStroke);
                }
                for (var index = 1; index < waypoints.Length; ++index)
                {
                    AddRoutedManhattanWire(node.Key, waypoints[index - 1], waypoints[index], routeStroke);
                }
                foreach (var point in route.Points)
                {
                    if (string.Equals(node.Key, selectedCircuitNode, StringComparison.Ordinal))
                    {
                        AddRoutePointHandle(route, point, routeStroke);
                    }
                    else
                    {
                        AddJunctionDot(new Point(point.X, point.Y), routeStroke, 6.0, node.Key);
                    }
                }
                AddCircuitNodeLabel(node.Key, RouteLabelPoint(waypoints), routeStroke);
            }
            else
            {
                var minimumY = leadPoints.Min(point => point.Y);
                var maximumY = leadPoints.Max(point => point.Y);
                var trunkX = FindClearCircuitTrunkX(leadPoints, minimumY, maximumY);
                if (Math.Abs(maximumY - minimumY) > 0.5)
                {
                    AddCircuitWireSegment(node.Key, new Point(trunkX, minimumY),
                        new Point(trunkX, maximumY), stroke);
                }
                foreach (var point in leadPoints)
                {
                    if (Math.Abs(point.X - trunkX) > 0.5)
                    {
                        AddRoutedManhattanWire(node.Key, point, new Point(trunkX, point.Y), stroke);
                    }
                }
                var branchRows = leadPoints.GroupBy(point => Math.Round(point.Y, 3)).ToArray();
                foreach (var branch in branchRows.Where(branch =>
                             branch.Any(point => Math.Abs(point.X - trunkX) > 0.5) &&
                             branch.Key > minimumY + 0.5 && branch.Key < maximumY - 0.5))
                {
                    AddJunctionDot(new Point(trunkX, branch.Key), stroke, 8.0, node.Key);
                }
                AddCircuitNodeLabel(node.Key, new Point(trunkX + 6.0, minimumY - 18.0), stroke);
                if (string.Equals(node.Key, Document.AnalogCircuit.ReferenceGround, StringComparison.Ordinal))
                {
                    AddGroundSymbol(new Point(trunkX, maximumY), stroke);
                }
            }
        }
        RenderCircuitCrossings();
        if (selectedCircuitNode is not null)
        {
            CircuitSelectionText.Text = CircuitNetworkStatusText(selectedCircuitNode);
        }
        CircuitAddRoutePointButton.IsVisible = selectedCircuitNode is not null;
        CircuitClearRouteButton.IsVisible = selectedCircuitNode is not null;
        CircuitAddRoutePointButton.IsEnabled = selectedCircuitNode is not null;
        CircuitClearRouteButton.IsEnabled = selectedCircuitNode is not null &&
                                             Document.AnalogCircuit.WireRoutes.Any(route =>
                                                 string.Equals(route.Node, selectedCircuitNode, StringComparison.Ordinal) &&
                                                 route.Points.Count > 0);
        UpdateCircuitWireVisualState();
        ApplyCircuitCanvasTransform();
    }

    private void RefreshCircuitDiagnosticPins()
    {
        circuitDiagnosticPins.Clear();
        circuitDiagnosticWarningPins.Clear();
        circuitDiagnosticErrorPins.Clear();
        circuitDiagnosticMessages.Clear();
        circuitDiagnosticRoutes.Clear();
        circuitDiagnosticWarningRoutes.Clear();
        circuitDiagnosticErrorRoutes.Clear();
        circuitDiagnosticRouteMessages.Clear();
        foreach (var issue in Document.ValidationIssues.Where(issue =>
                     issue.Target == ModuleAuthoringIssueTarget.AnalogPin && issue.TargetObject is AnalogComponentDraft))
        {
            if (issue.TargetObject is not AnalogComponentDraft component)
            {
                continue;
            }
            var pin = component.Pins.FirstOrDefault(candidate =>
                string.Equals(candidate.Id, issue.FieldId, StringComparison.Ordinal));
            if (pin is null)
            {
                continue;
            }
            circuitDiagnosticPins.Add(pin);
            if (issue.IsWarning)
            {
                circuitDiagnosticWarningPins.Add(pin);
            }
            else
            {
                circuitDiagnosticErrorPins.Add(pin);
            }
            circuitDiagnosticMessages[pin] = circuitDiagnosticMessages.TryGetValue(pin, out var existing)
                ? $"{existing}；{issue.Message}"
                : issue.Message;
        }
        foreach (var issue in Document.ValidationIssues.Where(issue =>
                     issue.Target == ModuleAuthoringIssueTarget.AnalogCircuit &&
                     issue.FieldId == "wireRoutes" && issue.TargetObject is AnalogWireRouteDraft))
        {
            var route = (AnalogWireRouteDraft)issue.TargetObject!;
            circuitDiagnosticRoutes.Add(route);
            if (issue.IsWarning)
            {
                circuitDiagnosticWarningRoutes.Add(route);
            }
            else
            {
                circuitDiagnosticErrorRoutes.Add(route);
            }
            circuitDiagnosticRouteMessages[route] = circuitDiagnosticRouteMessages.TryGetValue(route, out var existing)
                ? $"{existing}；{issue.Message}"
                : issue.Message;
        }
        UpdateCircuitPinVisualStates();
    }

    private static double DistanceSquared(Point first, Point second)
    {
        var x = first.X - second.X;
        var y = first.Y - second.Y;
        return x * x + y * y;
    }

    private IBrush CircuitNodeBaseBrush(string node) =>
        string.Equals(node, Document.AnalogCircuit.ReferenceGround, StringComparison.Ordinal)
            ? new SolidColorBrush(Color.Parse("#82909F"))
            : circuitAccentBrush;

    private static Point CircuitLeadPoint(CircuitEndpoint endpoint)
    {
        const double lead = 20.0;
        return endpoint.Pin.Side switch
        {
            "right" => new Point(endpoint.Point.X + lead, endpoint.Point.Y),
            "bottom" => new Point(endpoint.Point.X, endpoint.Point.Y + lead),
            "top" => new Point(endpoint.Point.X, endpoint.Point.Y - lead),
            _ => new Point(endpoint.Point.X - lead, endpoint.Point.Y),
        };
    }

    private static Point RouteLabelPoint(IReadOnlyList<Point> points)
    {
        var point = points.OrderBy(candidate => candidate.Y).ThenBy(candidate => candidate.X).FirstOrDefault();
        return new Point(point.X + 6.0, point.Y - 18.0);
    }

    private void RenderCircuitCrossings()
    {
        if (CircuitCrossingLayer is null)
        {
            return;
        }
        var seen = new HashSet<(long X, long Y)>();
        for (var firstIndex = 0; firstIndex < renderedCircuitSegments.Count; ++firstIndex)
        {
            var first = renderedCircuitSegments[firstIndex];
            for (var secondIndex = firstIndex + 1; secondIndex < renderedCircuitSegments.Count; ++secondIndex)
            {
                var second = renderedCircuitSegments[secondIndex];
                if (string.Equals(first.Node, second.Node, StringComparison.Ordinal) ||
                    !TryAxisIntersection(first, second, out var intersection) ||
                    DistanceSquared(intersection, first.Start) < 100.0 ||
                    DistanceSquared(intersection, first.End) < 100.0 ||
                    DistanceSquared(intersection, second.Start) < 100.0 ||
                    DistanceSquared(intersection, second.End) < 100.0)
                {
                    continue;
                }
                var key = ((long)Math.Round(intersection.X), (long)Math.Round(intersection.Y));
                if (seen.Add(key))
                {
                    var bridgeSegment = IsHorizontal(first) ? first : second;
                    AddCircuitCrossingBridge(intersection, bridgeSegment.Node, bridgeSegment.Stroke);
                }
            }
        }
    }

    private static bool IsHorizontal(CircuitWireSegment segment) =>
        Math.Abs(segment.Start.Y - segment.End.Y) < 0.5;

    private static bool TryAxisIntersection(CircuitWireSegment first, CircuitWireSegment second, out Point point)
    {
        point = default;
        var firstHorizontal = IsHorizontal(first);
        var secondHorizontal = IsHorizontal(second);
        if (firstHorizontal == secondHorizontal)
        {
            return false;
        }
        var horizontal = firstHorizontal ? first : second;
        var vertical = firstHorizontal ? second : first;
        var x = vertical.Start.X;
        var y = horizontal.Start.Y;
        if (x < Math.Min(horizontal.Start.X, horizontal.End.X) + 0.5 ||
            x > Math.Max(horizontal.Start.X, horizontal.End.X) - 0.5 ||
            y < Math.Min(vertical.Start.Y, vertical.End.Y) + 0.5 ||
            y > Math.Max(vertical.Start.Y, vertical.End.Y) - 0.5)
        {
            return false;
        }
        point = new Point(x, y);
        return true;
    }

    private void AddCircuitCrossingBridge(Point point, string node, IBrush stroke)
    {
        var radius = 8.0;
        var haloGeometry = new StreamGeometry();
        using (var context = haloGeometry.Open())
        {
            context.BeginFigure(new Point(point.X - radius, point.Y), false);
            context.CubicBezierTo(new Point(point.X - radius * 0.45, point.Y - radius * 1.5),
                new Point(point.X + radius * 0.45, point.Y - radius * 1.5),
                new Point(point.X + radius, point.Y));
            context.EndFigure(false);
        }
        CircuitCrossingLayer.Children.Add(new AvaloniaPath
        {
            Data = haloGeometry,
            Stroke = circuitWireHaloBrush,
            StrokeThickness = 9.0,
            StrokeLineCap = PenLineCap.Round,
            StrokeJoin = PenLineJoin.Round,
            IsHitTestVisible = false,
            Tag = new CircuitCrossingVisualTag(node, true, stroke),
        });
        var bridgeGeometry = new StreamGeometry();
        using (var context = bridgeGeometry.Open())
        {
            context.BeginFigure(new Point(point.X - radius, point.Y), false);
            context.CubicBezierTo(new Point(point.X - radius * 0.45, point.Y - radius * 1.5),
                new Point(point.X + radius * 0.45, point.Y - radius * 1.5),
                new Point(point.X + radius, point.Y));
            context.EndFigure(false);
        }
        CircuitCrossingLayer.Children.Add(new AvaloniaPath
        {
            Data = bridgeGeometry,
            Stroke = stroke,
            StrokeThickness = 3.2,
            StrokeLineCap = PenLineCap.Round,
            StrokeJoin = PenLineJoin.Round,
            IsHitTestVisible = false,
            Tag = new CircuitCrossingVisualTag(node, false, stroke),
        });
    }

    private void UpdateCircuitWireVisualState()
    {
        if (CircuitWireLayer is null)
        {
            return;
        }
        foreach (var line in CircuitWireLayer.Children.OfType<Line>())
        {
            if (line.Tag is not CircuitWireVisualTag tag)
            {
                continue;
            }
            var selected = string.Equals(tag.Node, selectedCircuitNode, StringComparison.Ordinal);
            var hovered = string.Equals(tag.Node, hoveredCircuitNode, StringComparison.Ordinal);
            var diagnostic = circuitDiagnosticRoutes.Any(route =>
                string.Equals(route.Node, tag.Node, StringComparison.Ordinal));
            var diagnosticBrush = CircuitNodeDiagnosticBrush(tag.Node);
            switch (tag.Part)
            {
                case CircuitWireVisualPart.Halo:
                    line.Stroke = circuitWireHaloBrush;
                    line.StrokeThickness = CircuitScreenScale(selected ? CircuitSelectedHaloThickness :
                        hovered ? CircuitHoverHaloThickness : CircuitNormalHaloThickness);
                    line.Opacity = selected ? 0.95 : hovered ? 0.88 : 0.82;
                    break;
                case CircuitWireVisualPart.Wire:
                    line.Stroke = selected ? circuitSelectedBrush : hovered ? circuitHoverBrush :
                        diagnostic ? diagnosticBrush! : tag.BaseStroke;
                    line.StrokeThickness = CircuitScreenScale(selected ? CircuitSelectedWireThickness :
                        hovered ? CircuitHoverWireThickness : CircuitNormalWireThickness);
                    line.Opacity = selected || hovered ? 1.0 : 0.9;
                    break;
                case CircuitWireVisualPart.HitTarget:
                    line.Stroke = Brushes.Transparent;
                    line.StrokeThickness = CircuitScreenScale(selected ? CircuitSelectedHitThickness :
                        hovered ? CircuitHoverHitThickness : CircuitNormalHitThickness);
                    break;
            }
        }
        if (CircuitCrossingLayer is not null)
        {
            foreach (var bridge in CircuitCrossingLayer.Children.OfType<AvaloniaPath>())
            {
                if (bridge.Tag is not CircuitCrossingVisualTag tag)
                {
                    continue;
                }
                var selected = string.Equals(tag.Node, selectedCircuitNode, StringComparison.Ordinal);
                var hovered = string.Equals(tag.Node, hoveredCircuitNode, StringComparison.Ordinal);
                var diagnosticBrush = CircuitNodeDiagnosticBrush(tag.Node);
                bridge.Stroke = tag.IsHalo
                    ? circuitWireHaloBrush
                    : selected ? circuitSelectedBrush
                    : hovered ? circuitHoverBrush
                    : diagnosticBrush ?? tag.BaseStroke;
                bridge.StrokeThickness = tag.IsHalo
                    ? CircuitScreenScale(selected ? CircuitSelectedHaloThickness : hovered ? CircuitHoverHaloThickness : 9.0)
                    : CircuitScreenScale(selected ? CircuitSelectedWireThickness : hovered ? CircuitHoverWireThickness : 3.2);
                bridge.Opacity = selected || hovered ? 1.0 : 0.92;
            }
        }
        if (CircuitJunctionLayer is null)
        {
            return;
        }
        foreach (var leader in CircuitJunctionLayer.Children.OfType<Line>())
        {
            if (leader.Tag is not CircuitLabelLeaderVisualTag tag)
            {
                continue;
            }
            var selected = string.Equals(tag.Node, selectedCircuitNode, StringComparison.Ordinal);
            var hovered = string.Equals(tag.Node, hoveredCircuitNode, StringComparison.Ordinal);
            var diagnosticBrush = CircuitNodeDiagnosticBrush(tag.Node);
            leader.Stroke = selected ? circuitSelectedBrush : hovered ? circuitHoverBrush :
                diagnosticBrush ?? tag.BaseStroke;
            leader.StrokeThickness = CircuitScreenScale(selected || hovered ? 2.0 : 1.5);
            leader.Opacity = selected || hovered ? 1.0 : 0.82;
        }
        foreach (var junction in CircuitJunctionLayer.Children.OfType<Ellipse>())
        {
            if (junction.Tag is not CircuitJunctionVisualTag junctionTag)
            {
                continue;
            }
            var node = junctionTag.Node;
            var selected = string.Equals(node, selectedCircuitNode, StringComparison.Ordinal);
            var hovered = string.Equals(node, hoveredCircuitNode, StringComparison.Ordinal);
            var stroke = selected ? circuitSelectedBrush : hovered ? circuitHoverBrush :
                CircuitNodeDiagnosticBrush(node) ?? CircuitNodeBaseBrush(node);
            junction.Fill = stroke;
            junction.Stroke = stroke;
            junction.StrokeThickness = selected ? 2.0 : 1.0;
            junction.Opacity = selected || hovered ? 1.0 : 0.9;
            var inverseScale = 1.0 / Math.Max(circuitZoom, MinimumCircuitZoom);
            junction.RenderTransform = new ScaleTransform(inverseScale, inverseScale);
            junction.RenderTransformOrigin = RelativePoint.Center;
        }
        foreach (var label in CircuitJunctionLayer.Children.OfType<Border>())
        {
            if (label.Tag is not string node)
            {
                continue;
            }
            var diagnostic = circuitDiagnosticRoutes.Any(route =>
                string.Equals(route.Node, node, StringComparison.Ordinal));
            var diagnosticBrush = CircuitNodeDiagnosticBrush(node);
            var stroke = string.Equals(node, selectedCircuitNode, StringComparison.Ordinal)
                ? circuitSelectedBrush
                : string.Equals(node, hoveredCircuitNode, StringComparison.Ordinal)
                    ? circuitHoverBrush
                    : diagnostic ? diagnosticBrush! : CircuitNodeBaseBrush(node);
            label.BorderBrush = stroke;
            var selected = string.Equals(node, selectedCircuitNode, StringComparison.Ordinal);
            var hovered = string.Equals(node, hoveredCircuitNode, StringComparison.Ordinal);
            var focused = selected || hovered || diagnostic;
            var pinCount = Document.AnalogCircuit.Components.SelectMany(component => component.Pins)
                .Count(pin => string.Equals(pin.Node?.Trim(), node, StringComparison.Ordinal));
            var hasManualRoute = Document.AnalogCircuit.WireRoutes.Any(route =>
                string.Equals(route.Node, node, StringComparison.Ordinal) && route.Points.Count > 0);
            var complexNetwork = pinCount >= 3 || hasManualRoute;
            label.IsVisible = focused || circuitZoom >= 0.72 ||
                              (circuitZoom >= 0.55 && complexNetwork);
            var labelScale = circuitZoom < 0.55
                ? 1.0 / Math.Max(circuitZoom, MinimumCircuitZoom)
                : circuitZoom > 1.5
                    ? 1.5 / circuitZoom
                    : 1.0;
            label.RenderTransform = new ScaleTransform(labelScale, labelScale);
            label.RenderTransformOrigin = RelativePoint.Center;
            if (label.Child is TextBlock text)
            {
                text.Foreground = stroke;
            }
        }
        UpdateCircuitPinVisualStates();
    }

    private static double SnapCircuitGrid(double value) => Math.Round(value / CircuitGridSize) * CircuitGridSize;

    private double SnapCircuitEdit(double value) => CircuitSnapEnabled ? SnapCircuitGrid(value) : value;

    private void AddRoutedManhattanWire(string node, Point start, Point end, IBrush stroke)
    {
        var key = new CircuitRouteCacheKey(start.X, start.Y, end.X, end.Y);
        if (!circuitRouteCache.TryGetValue(key, out var route))
        {
            route = circuitRoutingGridSnapshot is { } grid
                ? BuildObstacleAvoidingRoute(start, end, grid)
                : BuildObstacleAvoidingRoute(start, end, CircuitRoutingObstacles());
            circuitRouteCache[key] = route;
        }
        for (var index = 1; index < route.Count; ++index)
        {
            AddCircuitWireSegment(node, route[index - 1], route[index], stroke);
        }
    }

    private double FindClearCircuitTrunkX(IReadOnlyList<Point> points, double minimumY, double maximumY)
    {
        var preferred = SnapCircuitGrid(points.Select(point => point.X).OrderBy(value => value)
            .ElementAt(points.Count / 2));
        var obstacles = CircuitRoutingObstacles();
        var candidates = new List<double> { preferred };
        foreach (var obstacle in obstacles)
        {
            candidates.Add(SnapCircuitGrid(obstacle.Left - CircuitGridSize));
            candidates.Add(SnapCircuitGrid(obstacle.Right + CircuitGridSize));
        }
        return candidates
            .Where(candidate => candidate >= CircuitGridSize && candidate <= CircuitCanvasWidth - CircuitGridSize)
            .Distinct()
            .Where(candidate => !SegmentIntersectsAnyObstacle(
                new Point(candidate, minimumY), new Point(candidate, maximumY), obstacles))
            .OrderBy(candidate => Math.Abs(candidate - preferred))
            .FirstOrDefault(preferred);
    }

    private IReadOnlyList<Rect> CircuitRoutingObstacles()
    {
        if (circuitRoutingObstacleSnapshot is not null)
        {
            return circuitRoutingObstacleSnapshot;
        }
        return Document.AnalogCircuit.Components
        .Select(component => new Rect(
            component.CanvasX - CircuitGridSize,
            component.CanvasY - CircuitGridSize,
            component.Descriptor.SymbolWidth + CircuitGridSize * 2.0,
            component.Descriptor.SymbolHeight + CircuitGridSize * 2.0))
        .ToArray();
    }

    private static IReadOnlyList<Point> BuildObstacleAvoidingRoute(
        Point start, Point end, IReadOnlyList<Rect> obstacles)
    {
        var grid = BuildCircuitRoutingGrid(obstacles);
        return BuildObstacleAvoidingRoute(start, end, grid);
    }

    private static IReadOnlyList<Point> BuildObstacleAvoidingRoute(
        Point start, Point end, CircuitRoutingGrid grid)
    {
        var gridRoute = BuildGridObstacleRoute(start, end, grid);
        if (gridRoute.Count > 1 && RouteAvoidsObstacles(gridRoute, grid))
        {
            return gridRoute;
        }

        // Keep a deterministic, explainable fallback for a saturated canvas or
        // malformed endpoint. Manual route points remain untouched by this path.
        return BuildCandidateFallbackRoute(start, end, grid.Obstacles);
    }

    private static IReadOnlyList<Point> BuildGridObstacleRoute(
        Point start, Point end, IReadOnlyList<Rect> obstacles)
    {
        return BuildGridObstacleRoute(start, end, BuildCircuitRoutingGrid(obstacles));
    }

    private static IReadOnlyList<Point> BuildGridObstacleRoute(
        Point start, Point end, CircuitRoutingGrid grid)
    {
        var xCoordinates = EnsureRouteEndpoints(grid.XCoordinates, start.X, end.X, CircuitCanvasWidth);
        var yCoordinates = EnsureRouteEndpoints(grid.YCoordinates, start.Y, end.Y, CircuitCanvasHeight);
        var xIndex = xCoordinates.Count == grid.XCoordinates.Count
            ? grid.XIndex
            : xCoordinates.Select((value, index) => (value, index))
                .ToDictionary(entry => entry.value, entry => entry.index);
        var yIndex = yCoordinates.Count == grid.YCoordinates.Count
            ? grid.YIndex
            : yCoordinates.Select((value, index) => (value, index))
                .ToDictionary(entry => entry.value, entry => entry.index);
        var obstacles = grid.Obstacles;
        if (!xIndex.TryGetValue(start.X, out var startX) || !xIndex.TryGetValue(end.X, out var endX) ||
            !yIndex.TryGetValue(start.Y, out var startY) || !yIndex.TryGetValue(end.Y, out var endY))
        {
            return [];
        }

        var startNode = new CircuitGridPoint(startX, startY);
        var endNode = new CircuitGridPoint(endX, endY);
        if (IsRoutePointBlocked(start, grid) || IsRoutePointBlocked(end, grid))
        {
            return [];
        }

        var open = new PriorityQueue<CircuitGridPoint, double>();
        var scores = new Dictionary<CircuitGridPoint, double> { [startNode] = 0.0 };
        var previous = new Dictionary<CircuitGridPoint, CircuitGridPoint>();
        open.Enqueue(startNode, ManhattanDistance(start, end));
        while (open.TryDequeue(out var current, out _))
        {
            if (current == endNode)
            {
                var path = new List<Point>();
                var cursor = current;
                path.Add(new Point(xCoordinates[cursor.X], yCoordinates[cursor.Y]));
                while (previous.TryGetValue(cursor, out var parent))
                {
                    cursor = parent;
                    path.Add(new Point(xCoordinates[cursor.X], yCoordinates[cursor.Y]));
                }
                path.Reverse();
                return SimplifyOrthogonalRoute(path);
            }

            if (!scores.TryGetValue(current, out var currentScore))
            {
                continue;
            }
            foreach (var neighbor in RouteNeighbors(current, xCoordinates.Count, yCoordinates.Count))
            {
                var nextPoint = new Point(xCoordinates[neighbor.X], yCoordinates[neighbor.Y]);
                if (neighbor != endNode && IsRoutePointBlocked(nextPoint, grid))
                {
                    continue;
                }
                var currentPoint = new Point(xCoordinates[current.X], yCoordinates[current.Y]);
                if (SegmentIntersectsAnyObstacle(currentPoint, nextPoint, grid))
                {
                    continue;
                }
                var stepCost = Math.Abs(nextPoint.X - currentPoint.X) + Math.Abs(nextPoint.Y - currentPoint.Y);
                var nextScore = currentScore + stepCost;
                if (scores.TryGetValue(neighbor, out var knownScore) && nextScore >= knownScore)
                {
                    continue;
                }
                scores[neighbor] = nextScore;
                previous[neighbor] = current;
                var heuristic = ManhattanDistance(nextPoint, end);
                open.Enqueue(neighbor, nextScore + heuristic);
            }
        }
        return [];
    }

    private static IReadOnlyList<double> EnsureRouteEndpoints(
        IReadOnlyList<double> axis, double start, double end, double maximum)
    {
        var clampedStart = Math.Clamp(start, 0.0, maximum);
        var clampedEnd = Math.Clamp(end, 0.0, maximum);
        if (axis.Contains(clampedStart) && axis.Contains(clampedEnd))
        {
            return axis;
        }
        return axis.Concat([clampedStart, clampedEnd])
            .Distinct()
            .OrderBy(value => value)
            .ToArray();
    }

    private static CircuitRoutingGrid BuildCircuitRoutingGrid(IReadOnlyList<Rect> obstacles)
    {
        var xCoordinates = BuildRouteAxisCoordinates(0.0, CircuitCanvasWidth, CircuitCanvasWidth,
            obstacles.SelectMany(obstacle => new[] { obstacle.Left, obstacle.Right }));
        var yCoordinates = BuildRouteAxisCoordinates(0.0, CircuitCanvasHeight, CircuitCanvasHeight,
            obstacles.SelectMany(obstacle => new[] { obstacle.Top, obstacle.Bottom }));
        return new CircuitRoutingGrid(
            xCoordinates,
            yCoordinates,
            xCoordinates.Select((value, index) => (value, index))
                .ToDictionary(entry => entry.value, entry => entry.index),
            yCoordinates.Select((value, index) => (value, index))
                .ToDictionary(entry => entry.value, entry => entry.index),
            obstacles);
    }

    private static IReadOnlyList<double> BuildRouteAxisCoordinates(
        double start, double end, double maximum, IEnumerable<double> obstacleEdges)
    {
        var values = new SortedSet<double>();
        for (var value = 0.0; value <= maximum + 0.001; value += CircuitGridSize)
        {
            values.Add(value);
        }
        values.Add(Math.Clamp(start, 0.0, maximum));
        values.Add(Math.Clamp(end, 0.0, maximum));
        foreach (var edge in obstacleEdges.Where(double.IsFinite))
        {
            if (edge >= 0.0 && edge <= maximum)
            {
                values.Add(edge);
            }
        }
        return values.ToArray();
    }

    private static IEnumerable<CircuitGridPoint> RouteNeighbors(
        CircuitGridPoint point, int width, int height)
    {
        if (point.X > 0) yield return new CircuitGridPoint(point.X - 1, point.Y);
        if (point.X + 1 < width) yield return new CircuitGridPoint(point.X + 1, point.Y);
        if (point.Y > 0) yield return new CircuitGridPoint(point.X, point.Y - 1);
        if (point.Y + 1 < height) yield return new CircuitGridPoint(point.X, point.Y + 1);
    }

    private static bool IsRoutePointBlocked(Point point, IReadOnlyList<Rect> obstacles) =>
        obstacles.Any(obstacle => point.X > obstacle.Left + 0.5 && point.X < obstacle.Right - 0.5 &&
                                  point.Y > obstacle.Top + 0.5 && point.Y < obstacle.Bottom - 0.5);

    private static bool IsRoutePointBlocked(Point point, CircuitRoutingGrid grid) =>
        grid.ObstaclesAtY(point.Y).Any(obstacle =>
            point.X > obstacle.Left + 0.5 && point.X < obstacle.Right - 0.5);

    private static double ManhattanDistance(Point first, Point second) =>
        Math.Abs(first.X - second.X) + Math.Abs(first.Y - second.Y);

    private static IReadOnlyList<Point> SimplifyOrthogonalRoute(IReadOnlyList<Point> route)
    {
        var distinct = DistinctCircuitPoints(route);
        if (distinct.Count < 3)
        {
            return distinct;
        }
        var simplified = new List<Point> { distinct[0] };
        for (var index = 1; index < distinct.Count - 1; ++index)
        {
            var previous = simplified[^1];
            var current = distinct[index];
            var next = distinct[index + 1];
            var sameHorizontal = Math.Abs(previous.Y - current.Y) < 0.5 &&
                                 Math.Abs(current.Y - next.Y) < 0.5;
            var sameVertical = Math.Abs(previous.X - current.X) < 0.5 &&
                               Math.Abs(current.X - next.X) < 0.5;
            if (!sameHorizontal && !sameVertical)
            {
                simplified.Add(current);
            }
        }
        simplified.Add(distinct[^1]);
        return simplified;
    }

    private static IReadOnlyList<Point> BuildCandidateFallbackRoute(
        Point start, Point end, IReadOnlyList<Rect> obstacles)
    {
        var candidates = new List<IReadOnlyList<Point>>();
        if (Math.Abs(start.X - end.X) < 0.5 || Math.Abs(start.Y - end.Y) < 0.5)
        {
            candidates.Add(DistinctCircuitPoints([start, end]));
        }
        var middleXs = new List<double> { SnapCircuitGrid((start.X + end.X) * 0.5) };
        var middleYs = new List<double> { SnapCircuitGrid((start.Y + end.Y) * 0.5) };
        foreach (var obstacle in obstacles)
        {
            middleXs.Add(SnapCircuitGrid(obstacle.Left - CircuitGridSize));
            middleXs.Add(SnapCircuitGrid(obstacle.Right + CircuitGridSize));
            middleYs.Add(SnapCircuitGrid(obstacle.Top - CircuitGridSize));
            middleYs.Add(SnapCircuitGrid(obstacle.Bottom + CircuitGridSize));
        }
        foreach (var middleX in middleXs.Distinct())
        {
            candidates.Add(DistinctCircuitPoints([start, new Point(middleX, start.Y),
                new Point(middleX, end.Y), end]));
        }
        foreach (var middleY in middleYs.Distinct())
        {
            candidates.Add(DistinctCircuitPoints([start, new Point(start.X, middleY),
                new Point(end.X, middleY), end]));
        }
        var clear = candidates.Where(route => RouteAvoidsObstacles(route, obstacles)).ToArray();
        IEnumerable<IReadOnlyList<Point>> available = clear.Length > 0 ? clear : candidates;
        return available
            .OrderBy(RouteLength)
            .ThenBy(route => route.Count)
            .FirstOrDefault(DistinctCircuitPoints([start, end]));
    }

    private static bool RouteAvoidsObstacles(IReadOnlyList<Point> route, IReadOnlyList<Rect> obstacles) =>
        Enumerable.Range(1, route.Count - 1).All(index =>
            !SegmentIntersectsAnyObstacle(route[index - 1], route[index], obstacles));

    private static bool RouteAvoidsObstacles(IReadOnlyList<Point> route, CircuitRoutingGrid grid) =>
        Enumerable.Range(1, route.Count - 1).All(index =>
            !SegmentIntersectsAnyObstacle(route[index - 1], route[index], grid));

    private static bool SegmentIntersectsAnyObstacle(Point start, Point end, IReadOnlyList<Rect> obstacles) =>
        obstacles.Any(obstacle => SegmentIntersectsObstacle(start, end, obstacle));

    private static bool SegmentIntersectsAnyObstacle(Point start, Point end, CircuitRoutingGrid grid)
    {
        if (Math.Abs(start.Y - end.Y) < 0.5)
        {
            return grid.ObstaclesAtY(start.Y).Any(obstacle =>
                Math.Max(start.X, end.X) > obstacle.Left + 0.5 &&
                Math.Min(start.X, end.X) < obstacle.Right - 0.5);
        }
        if (Math.Abs(start.X - end.X) < 0.5)
        {
            return grid.ObstaclesAtX(start.X).Any(obstacle =>
                Math.Max(start.Y, end.Y) > obstacle.Top + 0.5 &&
                Math.Min(start.Y, end.Y) < obstacle.Bottom - 0.5);
        }
        return true;
    }

    private static bool SegmentIntersectsObstacle(Point start, Point end, Rect obstacle)
    {
        if (Math.Abs(start.Y - end.Y) < 0.5)
        {
            return start.Y > obstacle.Top + 0.5 && start.Y < obstacle.Bottom - 0.5 &&
                   Math.Max(start.X, end.X) > obstacle.Left + 0.5 &&
                   Math.Min(start.X, end.X) < obstacle.Right - 0.5;
        }
        if (Math.Abs(start.X - end.X) < 0.5)
        {
            return start.X > obstacle.Left + 0.5 && start.X < obstacle.Right - 0.5 &&
                   Math.Max(start.Y, end.Y) > obstacle.Top + 0.5 &&
                   Math.Min(start.Y, end.Y) < obstacle.Bottom - 0.5;
        }
        return true;
    }

    private static double RouteLength(IReadOnlyList<Point> route) => Enumerable.Range(1, route.Count - 1)
        .Sum(index => Math.Abs(route[index].X - route[index - 1].X) +
                      Math.Abs(route[index].Y - route[index - 1].Y));

    private void AddJunctionDot(Point point, IBrush stroke, double diameter = 9.0, string? node = null)
    {
        var dot = new Ellipse
        {
            Width = diameter,
            Height = diameter,
            Fill = stroke,
            Stroke = stroke,
            StrokeThickness = 1.0,
            IsHitTestVisible = false,
            Tag = node is null ? null : new CircuitJunctionVisualTag(node),
        };
        Canvas.SetLeft(dot, point.X - diameter * 0.5);
        Canvas.SetTop(dot, point.Y - diameter * 0.5);
        CircuitJunctionLayer.Children.Add(dot);
    }

    private void AddCircuitNodeLabel(string node, Point point, IBrush stroke)
    {
        var displayStroke = string.Equals(node, selectedCircuitNode, StringComparison.Ordinal)
            ? circuitSelectedBrush
            : string.Equals(node, hoveredCircuitNode, StringComparison.Ordinal)
                ? circuitHoverBrush
                : stroke;
        var labelWidth = Math.Clamp(14.0 + node.Length * 7.0, 48.0, 188.0);
        const double labelHeight = 24.0;
        var labelBounds = FindCircuitLabelBounds(point, labelWidth, labelHeight);
        circuitLabelBounds.Add(labelBounds);
        var leaderEnd = new Point(
            Math.Clamp(point.X, labelBounds.Left, labelBounds.Right),
            Math.Clamp(point.Y, labelBounds.Top, labelBounds.Bottom));
        if (DistanceSquared(point, leaderEnd) > 4.0)
        {
            CircuitJunctionLayer.Children.Add(new Line
            {
                StartPoint = point,
                EndPoint = leaderEnd,
                Stroke = displayStroke,
                StrokeThickness = CircuitScreenScale(1.5),
                StrokeLineCap = PenLineCap.Round,
                Tag = new CircuitLabelLeaderVisualTag(node, displayStroke),
                IsHitTestVisible = false,
            });
        }
        var label = new Border
        {
            Width = labelWidth,
            Height = labelHeight,
            Tag = node,
            Background = circuitWireHaloBrush,
            BorderBrush = displayStroke,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(3),
            Padding = new Thickness(4, 2),
            IsHitTestVisible = false,
            Child = new TextBlock
            {
                Text = node,
                Foreground = displayStroke,
                FontSize = 10.0,
                TextTrimming = TextTrimming.CharacterEllipsis,
                VerticalAlignment = VerticalAlignment.Center,
            },
        };
        AutomationProperties.SetName(label, $"网络标签 {node}");
        AutomationProperties.SetHelpText(label, $"网络 {node}，点击画布可选择该网络");
        Canvas.SetLeft(label, labelBounds.X);
        Canvas.SetTop(label, labelBounds.Y);
        CircuitJunctionLayer.Children.Add(label);
    }

    private Rect FindCircuitLabelBounds(Point anchor, double width, double height)
    {
        bool IsClear(Rect bounds) =>
            bounds.Left >= 4.0 && bounds.Top >= 4.0 &&
            bounds.Right <= CircuitCanvasWidth - 4.0 && bounds.Bottom <= CircuitCanvasHeight - 4.0 &&
            !(circuitRoutingObstacleSnapshot ?? []).Any(obstacle => CircuitRectIntersects(obstacle, bounds)) &&
            !circuitLabelBounds.Any(existing => CircuitRectIntersects(existing, bounds));

        var candidates = new[]
        {
            new Point(anchor.X, anchor.Y),
            new Point(anchor.X, anchor.Y + 26.0),
            new Point(anchor.X - width - 8.0, anchor.Y),
            new Point(anchor.X - width - 8.0, anchor.Y + 26.0),
            new Point(anchor.X, anchor.Y - height - 8.0),
            new Point(anchor.X - width - 8.0, anchor.Y - height - 8.0),
        };
        foreach (var candidate in candidates)
        {
            var bounds = new Rect(candidate.X, candidate.Y, width, height);
            if (!IsClear(bounds))
            {
                continue;
            }
            return bounds;
        }
        for (var row = -3; row <= 8; ++row)
        {
            for (var column = -4; column <= 8; ++column)
            {
                var bounds = new Rect(anchor.X + column * 12.0, anchor.Y + row * 26.0, width, height);
                if (IsClear(bounds))
                {
                    return bounds;
                }
            }
        }
        var fallback = new Rect(
            Math.Clamp(anchor.X, 4.0, CircuitCanvasWidth - width - 4.0),
            Math.Clamp(anchor.Y, 4.0, CircuitCanvasHeight - height - 4.0),
            width, height);
        return fallback;
    }

    private static bool CircuitRectIntersects(Rect first, Rect second) =>
        first.Left < second.Right && first.Right > second.Left &&
        first.Top < second.Bottom && first.Bottom > second.Top;

    private void AddGroundSymbol(Point point, IBrush stroke)
    {
        foreach (var (width, offset) in new[] { (22.0, 0.0), (14.0, 6.0), (6.0, 12.0) })
        {
            CircuitJunctionLayer.Children.Add(new Line
            {
                StartPoint = new Point(point.X - width * 0.5, point.Y + offset),
                EndPoint = new Point(point.X + width * 0.5, point.Y + offset),
                Stroke = stroke,
                StrokeThickness = 2.0,
                IsHitTestVisible = false,
            });
        }
    }

    private void AddRoutePointHandle(AnalogWireRouteDraft route, AnalogWirePointDraft point, IBrush stroke)
    {
        var diagnostic = circuitDiagnosticRoutes.Contains(route);
        var handle = new Ellipse
        {
            Width = 14.0,
            Height = 14.0,
            Fill = new SolidColorBrush(Color.Parse("#10151A")),
            Stroke = diagnostic ? CircuitRouteDiagnosticBrush(route) : string.Equals(route.Node, selectedCircuitNode, StringComparison.Ordinal)
                ? new SolidColorBrush(Color.Parse("#FFD166")) : stroke,
            StrokeThickness = 2.0,
            Tag = point,
            Cursor = new Cursor(StandardCursorType.Hand),
            Focusable = true,
            IsTabStop = true,
        };
        AutomationProperties.SetName(handle, $"网络 {route.Node} 布线折点");
        ToolTip.SetTip(handle, circuitDiagnosticRouteMessages.TryGetValue(route, out var message)
            ? $"网络 {route.Node} 折点\n{message}" : $"网络 {route.Node} 折点");
        handle.PointerPressed += CircuitRoutePointPointerPressed;
        Canvas.SetLeft(handle, point.X - 7.0);
        Canvas.SetTop(handle, point.Y - 7.0);
        CircuitJunctionLayer.Children.Add(handle);
    }

    private static Point CircuitPinPoint(AnalogComponentDraft component, int pinIndex)
    {
        var pin = component.Pins.ElementAtOrDefault(pinIndex);
        return new Point(
            component.CanvasX + (pin?.AnchorX ?? 0.0),
            component.CanvasY + (pin?.AnchorY ?? 0.0));
    }

    private void AddCircuitWire(string node, Point start, Point end, IBrush stroke)
    {
        var middleX = (start.X + end.X) * 0.5;
        AddCircuitWireSegment(node, start, new Point(middleX, start.Y), stroke);
        AddCircuitWireSegment(node, new Point(middleX, start.Y), new Point(middleX, end.Y), stroke);
        AddCircuitWireSegment(node, new Point(middleX, end.Y), end, stroke);
    }

    private void AddCircuitWireSegment(string node, Point start, Point end, IBrush stroke)
    {
        if (DistanceSquared(start, end) < 0.01)
        {
            return;
        }
        renderedCircuitSegments.Add(new CircuitWireSegment(node, start, end, stroke));
        var halo = new Line
        {
            StartPoint = start,
            EndPoint = end,
            Stroke = circuitWireHaloBrush,
            StrokeThickness = 6.5,
            StrokeLineCap = PenLineCap.Round,
            Tag = new CircuitWireVisualTag(node, CircuitWireVisualPart.Halo, stroke),
            IsHitTestVisible = false,
        };
        CircuitWireLayer.Children.Add(halo);

        var line = new Line
        {
            StartPoint = start,
            EndPoint = end,
            Stroke = stroke,
            StrokeThickness = 3.0,
            StrokeLineCap = PenLineCap.Round,
            Tag = new CircuitWireVisualTag(node, CircuitWireVisualPart.Wire, stroke),
            IsHitTestVisible = false,
        };
        ToolTip.SetTip(line, $"网络 {node}");
        CircuitWireLayer.Children.Add(line);

        var hitTarget = new Line
        {
            StartPoint = start,
            EndPoint = end,
            Stroke = Brushes.Transparent,
            StrokeThickness = 16.0,
            StrokeLineCap = PenLineCap.Round,
            Tag = new CircuitWireVisualTag(node, CircuitWireVisualPart.HitTarget, stroke),
            Cursor = new Cursor(StandardCursorType.Hand),
            Focusable = true,
            IsTabStop = true,
        };
        AutomationProperties.SetName(hitTarget, $"网络 {node} 连线");
        ToolTip.SetTip(hitTarget, $"网络 {node}");
        hitTarget.PointerEntered += CircuitWirePointerEntered;
        hitTarget.PointerExited += CircuitWirePointerExited;
        hitTarget.PointerPressed += CircuitWirePointerPressed;
        CircuitWireLayer.Children.Add(hitTarget);
    }

    private double CircuitScreenScale(double screenSize) =>
        screenSize / Math.Max(circuitZoom, MinimumCircuitZoom);

    private void ApplyCircuitCanvasTransform()
    {
        if (CircuitGridLayer is null || CircuitWireLayer is null || CircuitCrossingLayer is null || CircuitNodeLayer is null)
        {
            return;
        }
        var matrix = new Matrix(circuitZoom, 0.0, 0.0, circuitZoom, circuitPanX, circuitPanY);
        CircuitGridLayer.RenderTransform = new MatrixTransform(matrix);
        CircuitWireLayer.RenderTransform = new MatrixTransform(matrix);
        CircuitCrossingLayer.RenderTransform = new MatrixTransform(matrix);
        CircuitJunctionLayer.RenderTransform = new MatrixTransform(matrix);
        CircuitNodeLayer.RenderTransform = new MatrixTransform(matrix);
        CircuitZoomText.Content = $"{circuitZoom * 100:0}%";
        UpdateCircuitWireVisualState();
        UpdateCircuitWirePreview(circuitPreviewPointer);
    }

    private Point CircuitViewportToWorld(Point point) =>
        new((point.X - circuitPanX) / circuitZoom, (point.Y - circuitPanY) / circuitZoom);

    private Point CircuitWorldToViewport(Point point) =>
        new(point.X * circuitZoom + circuitPanX, point.Y * circuitZoom + circuitPanY);

    private void CircuitPaletteItemPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (sender is not Border { Tag: AnalogComponentDescriptor descriptor } ||
            !args.GetCurrentPoint(WorkshopRoot).Properties.IsLeftButtonPressed)
        {
            return;
        }
        circuitPaletteDescriptor = descriptor;
        circuitPalettePointerStart = args.GetPosition(WorkshopRoot);
        draggingCircuitPalette = false;
        AnalogAddTypeSelector.SelectedItem = descriptor;
        args.Pointer.Capture(WorkshopRoot);
        args.Handled = true;
    }

    private void CircuitRootPointerMoved(object? sender, PointerEventArgs args)
    {
        if (circuitPaletteDescriptor is null || args.Pointer.Captured != WorkshopRoot)
        {
            return;
        }
        var rootPoint = args.GetPosition(WorkshopRoot);
        if (!draggingCircuitPalette &&
            Math.Abs(rootPoint.X - circuitPalettePointerStart.X) +
            Math.Abs(rootPoint.Y - circuitPalettePointerStart.Y) < 6.0)
        {
            return;
        }
        draggingCircuitPalette = true;
        var canvasPoint = args.GetPosition(CircuitCanvasSurface);
        var inside = canvasPoint.X >= 0.0 && canvasPoint.Y >= 0.0 &&
                     canvasPoint.X <= CircuitCanvasSurface.Bounds.Width &&
                     canvasPoint.Y <= CircuitCanvasSurface.Bounds.Height;
        CircuitPaletteDragPreview.IsVisible = inside;
        if (!inside)
        {
            return;
        }
        CircuitPaletteDragPreviewText.Text = circuitPaletteDescriptor.DisplayName;
        Canvas.SetLeft(CircuitPaletteDragPreview, Math.Clamp(canvasPoint.X - 75.0, 0.0,
            Math.Max(CircuitCanvasSurface.Bounds.Width - 150.0, 0.0)));
        Canvas.SetTop(CircuitPaletteDragPreview, Math.Clamp(canvasPoint.Y - 27.0, 0.0,
            Math.Max(CircuitCanvasSurface.Bounds.Height - 54.0, 0.0)));
        args.Handled = true;
    }

    private void CircuitRootPointerReleased(object? sender, PointerReleasedEventArgs args)
    {
        if (circuitPaletteDescriptor is null)
        {
            return;
        }
        if (draggingCircuitPalette)
        {
            var canvasPoint = args.GetPosition(CircuitCanvasSurface);
            if (canvasPoint.X >= 0.0 && canvasPoint.Y >= 0.0 &&
                canvasPoint.X <= CircuitCanvasSurface.Bounds.Width &&
                canvasPoint.Y <= CircuitCanvasSurface.Bounds.Height)
            {
                DropCircuitPaletteComponent(circuitPaletteDescriptor, canvasPoint);
            }
        }
        circuitPaletteDescriptor = null;
        draggingCircuitPalette = false;
        CircuitPaletteDragPreview.IsVisible = false;
        if (args.Pointer.Captured == WorkshopRoot)
        {
            args.Pointer.Capture(null);
        }
        args.Handled = true;
    }

    private AnalogComponentDraft DropCircuitPaletteComponent(
        AnalogComponentDescriptor descriptor, Point viewportPoint)
    {
        var world = CircuitViewportToWorld(viewportPoint);
            var x = Math.Clamp(SnapCircuitEdit(world.X - descriptor.SymbolWidth * 0.5),
                0.0, CircuitCanvasWidth - descriptor.SymbolWidth);
        var y = Math.Clamp(SnapCircuitEdit(world.Y - descriptor.SymbolHeight * 0.5),
                0.0, CircuitCanvasHeight - descriptor.SymbolHeight);
        var component = Document.AddAnalogComponent(descriptor.Type, x, y);
        UpdateLayout();
        SelectCircuitComponents([component]);
        if (compactLayout)
        {
            CircuitPalettePanel.IsVisible = false;
        }
        RenderCircuitConnections();
        return component;
    }

    private void CircuitCanvasPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (sender is not Control surface)
        {
            return;
        }
        var point = args.GetCurrentPoint(surface);
        if (point.Properties.IsMiddleButtonPressed)
        {
            surface.Focus();
            panningCircuit = true;
            circuitPointerStart = args.GetPosition(surface);
            circuitPanStartX = circuitPanX;
            circuitPanStartY = circuitPanY;
            args.Pointer.Capture(surface);
            args.Handled = true;
            return;
        }
        if (point.Properties.IsLeftButtonPressed)
        {
            surface.Focus();
            circuitSelectionAdditive = (args.KeyModifiers & KeyModifiers.Control) != 0;
            if (!circuitSelectionAdditive)
            {
                ClearCircuitSelection();
            }
            ClearPendingCircuitPin();
            selectingCircuitBox = true;
            circuitSelectionStart = args.GetPosition(surface);
            UpdateCircuitSelectionRectangle(circuitSelectionStart, circuitSelectionStart);
            args.Pointer.Capture(surface);
            args.Handled = true;
        }
    }

    private void CircuitComponentPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (sender is not Border { Tag: AnalogComponentDraft component } card ||
            !args.GetCurrentPoint(card).Properties.IsLeftButtonPressed)
        {
            return;
        }
        ClearPendingCircuitPin();
        selectedCircuitNode = null;
        var additive = (args.KeyModifiers & KeyModifiers.Control) != 0;
        if (!additive && !selectedCircuitComponents.Contains(component))
        {
            ClearCircuitSelection();
        }
        if (additive && selectedCircuitComponents.Contains(component))
        {
            selectedCircuitComponents.Remove(component);
            selectedCircuitCards.Remove(component);
            card.BorderThickness = new Thickness(1);
            selectedCircuitComponent = selectedCircuitComponents.LastOrDefault();
            selectedCircuitCard = selectedCircuitComponent is not null &&
                                  selectedCircuitCards.TryGetValue(selectedCircuitComponent, out var selectedCard)
                ? selectedCard
                : null;
            UpdateCircuitSelectionStatus();
            args.Handled = true;
            return;
        }
        selectedCircuitComponents.Add(component);
        selectedCircuitCards[component] = card;
        selectedCircuitCard = card;
        selectedCircuitComponent = component;
        card.BorderThickness = new Thickness(2);
        UpdateCircuitSelectionStatus();
        if (AnalogComponentList.ContainerFromIndex(0) is Control inspectorItem)
        {
            inspectorItem.BringIntoView();
        }
        draggingCircuitComponent = component;
        draggingCircuitStarts.Clear();
        foreach (var selected in selectedCircuitComponents)
        {
            draggingCircuitStarts[selected] = new Point(selected.CanvasX, selected.CanvasY);
        }
        draggingCircuitPointerStart = CircuitViewportToWorld(args.GetPosition(CircuitCanvasSurface));
        Document.BeginEditTransaction();
        args.Pointer.Capture(CircuitCanvasSurface);
        CircuitCanvasSurface.Focus();
        args.Handled = true;
    }

    private void CircuitComponentKeyDown(object? sender, KeyEventArgs args)
    {
        if (sender is not Border { Tag: AnalogComponentDraft component } card ||
            args.Key is not (Key.Enter or Key.Space))
        {
            return;
        }
        if ((args.KeyModifiers & KeyModifiers.Control) != 0)
        {
            if (selectedCircuitComponents.Contains(component))
            {
                selectedCircuitComponents.Remove(component);
                selectedCircuitCards.Remove(component);
                card.BorderThickness = new Thickness(1);
            }
            else
            {
                selectedCircuitComponents.Add(component);
                selectedCircuitCards[component] = card;
                card.BorderThickness = new Thickness(2);
            }
            selectedCircuitComponent = selectedCircuitComponents.LastOrDefault();
            selectedCircuitCard = selectedCircuitComponent is not null &&
                                  selectedCircuitCards.TryGetValue(selectedCircuitComponent, out var selectedCard)
                ? selectedCard
                : null;
            UpdateCircuitSelectionStatus();
        }
        else
        {
            SelectCircuitComponents([component]);
        }
        CircuitCanvasSurface.Focus();
        args.Handled = true;
    }

    private void CircuitPinPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (sender is not Ellipse { Tag: AnalogPinDraft pin } visual)
        {
            return;
        }
        var properties = args.GetCurrentPoint(visual).Properties;
        if (properties.IsRightButtonPressed)
        {
            Document.DisconnectAnalogPin(pin);
            ClearPendingCircuitPin();
            RenderCircuitConnections();
            args.Handled = true;
            return;
        }
        if (!properties.IsLeftButtonPressed)
        {
            return;
        }
        if (pendingCircuitPin is null)
        {
            pendingCircuitPin = pin;
            pendingCircuitPinVisual = visual;
            visual.StrokeThickness = 3.0;
            visual.Stroke = circuitSelectedBrush;
            circuitWirePointerStart = args.GetPosition(CircuitCanvasSurface);
            draggingCircuitWire = false;
            args.Pointer.Capture(CircuitCanvasSurface);
            circuitPreviewPointer = args.GetPosition(CircuitCanvasSurface);
            UpdateCircuitWirePreview(circuitPreviewPointer);
        }
        else if (ReferenceEquals(pendingCircuitPin, pin))
        {
            ClearPendingCircuitPin();
        }
        else
        {
            if (IsCircuitPinConnectionValid(pendingCircuitPin, pin))
            {
                Document.ConnectAnalogPins(pendingCircuitPin, pin);
                ClearPendingCircuitPin();
                RenderCircuitConnections();
            }
            else
            {
                SetCircuitConnectionFeedback(CircuitPinConnectionDecision(pendingCircuitPin, pin));
                UpdateCircuitWirePreview(args.GetPosition(CircuitCanvasSurface));
            }
        }
        args.Handled = true;
    }

    private void CircuitPinKeyDown(object? sender, KeyEventArgs args)
    {
        if (sender is not Ellipse { Tag: AnalogPinDraft pin } visual ||
            args.Key is not (Key.Enter or Key.Space))
        {
            return;
        }
        if (pendingCircuitPin is null)
        {
            pendingCircuitPin = pin;
            pendingCircuitPinVisual = visual;
            visual.StrokeThickness = 3.0;
            visual.Stroke = circuitSelectedBrush;
            draggingCircuitWire = false;
            var endpoint = Document.AnalogCircuit.Components
                .SelectMany(component => component.Pins.Select((candidate, index) =>
                    (Pin: candidate, Point: CircuitPinPoint(component, index))))
                .FirstOrDefault(candidate => ReferenceEquals(candidate.Pin, pin));
            circuitPreviewPointer = CircuitWorldToViewport(endpoint.Point);
            UpdateCircuitWirePreview(circuitPreviewPointer);
        }
        else if (ReferenceEquals(pendingCircuitPin, pin))
        {
            ClearPendingCircuitPin();
        }
        else if (IsCircuitPinConnectionValid(pendingCircuitPin, pin))
        {
            Document.ConnectAnalogPins(pendingCircuitPin, pin);
            ClearPendingCircuitPin();
            RenderCircuitConnections();
        }
        else
        {
            SetCircuitConnectionFeedback(CircuitPinConnectionDecision(pendingCircuitPin, pin));
            UpdateCircuitWirePreview(circuitPreviewPointer);
        }
        args.Handled = true;
    }

    private void CircuitPinPointerEntered(object? sender, PointerEventArgs args)
    {
        if (sender is not Ellipse { Tag: AnalogPinDraft pin } visual)
        {
            return;
        }
        hoveredCircuitPin = pin;
        hoveredCircuitPinVisual = visual;
        if (!ReferenceEquals(pin, pendingCircuitPin))
        {
            visual.Stroke = pendingCircuitPin is not null
                ? IsCircuitPinConnectionValid(pendingCircuitPin, pin) ? circuitHoverBrush : circuitErrorBrush
                : circuitHoverBrush;
            visual.StrokeThickness = pendingCircuitPin is not null ? 3.0 : 2.5;
        }
    }

    private void CircuitPinPointerExited(object? sender, PointerEventArgs args)
    {
        if (sender is not Ellipse { Tag: AnalogPinDraft pin } visual || !ReferenceEquals(pin, hoveredCircuitPin))
        {
            return;
        }
        hoveredCircuitPin = null;
        hoveredCircuitPinVisual = null;
        RestoreCircuitPinVisual(visual);
    }

    private void RestoreCircuitPinVisual(Ellipse visual)
    {
        if (visual.Tag is not AnalogPinDraft pin)
        {
            return;
        }
        visual.Fill = circuitWireHaloBrush;
        var pending = ReferenceEquals(pin, pendingCircuitPin);
        var selectedNetwork = CircuitPinIsOnSelectedNetwork(pin);
        var hoveredNetwork = CircuitPinIsOnHoveredNetwork(pin);
        var inverseScale = 1.0 / Math.Max(circuitZoom, MinimumCircuitZoom);
        visual.RenderTransform = new ScaleTransform(inverseScale, inverseScale);
        visual.RenderTransformOrigin = RelativePoint.Center;
        visual.Stroke = pending || selectedNetwork
            ? circuitSelectedBrush
            : hoveredNetwork ? circuitHoverBrush
            : CircuitPinDiagnosticBrush(pin);
        visual.StrokeThickness = pending
            ? 3.0 : selectedNetwork ? 2.75 : hoveredNetwork ? 2.5 : circuitDiagnosticPins.Contains(pin) ? 2.5 : 2.0;
        var helpText = circuitDiagnosticMessages.TryGetValue(pin, out var message)
            ? $"{pin.DisplayName}：{message}" :
            $"{pin.DisplayName} · {CircuitPinRoleText(pin.ElectricalRole)} · 网络 {pin.Node}" +
            (pendingCircuitPin is null ? string.Empty :
                $" · {CircuitPinConnectionDecision(pendingCircuitPin, pin).Reason}");
        ToolTip.SetTip(visual, helpText);
        AutomationProperties.SetName(visual, $"{pin.DisplayName} 引脚");
        AutomationProperties.SetHelpText(visual, helpText);
    }

    private void CircuitNodeContainerPrepared(object? sender, ContainerPreparedEventArgs args)
    {
        var container = args.Container;
        void ContainerLayoutUpdated(object? layoutSender, EventArgs layoutArgs)
        {
            container.LayoutUpdated -= ContainerLayoutUpdated;
            UpdateCircuitPinVisualStates(container);
        }
        container.LayoutUpdated += ContainerLayoutUpdated;
    }

    private void UpdateCircuitPinVisualStates(Control? visualRoot = null)
    {
        visualRoot ??= CircuitNodeLayer;
        if (visualRoot is null)
        {
            return;
        }
        foreach (var visual in visualRoot.GetVisualDescendants().OfType<Ellipse>())
        {
            RestoreCircuitPinVisual(visual);
        }
        if (hoveredCircuitPinVisual is not null && hoveredCircuitPin is { } hoveredPin &&
            !ReferenceEquals(hoveredPin, pendingCircuitPin))
        {
            hoveredCircuitPinVisual.Stroke = pendingCircuitPin is not null
                ? CircuitPinConnectionDecision(pendingCircuitPin, hoveredPin).IsAllowed
                    ? circuitHoverBrush : circuitErrorBrush
                : circuitHoverBrush;
            hoveredCircuitPinVisual.StrokeThickness = pendingCircuitPin is not null ? 3.0 : 2.5;
        }
        if (pendingCircuitTargetVisual is not null)
        {
            pendingCircuitTargetVisual.StrokeThickness = 3.0;
        }
    }

    private IBrush CircuitPinBaseBrush(AnalogPinDraft pin) => pin.ElectricalRole switch
    {
        "positive" => CircuitPositivePinBrush,
        "negative" => CircuitNegativePinBrush,
        "control" => CircuitControlPinBrush,
        _ => circuitAccentBrush,
    };

    private bool CircuitPinIsOnSelectedNetwork(AnalogPinDraft pin) =>
        !string.IsNullOrWhiteSpace(selectedCircuitNode) &&
        string.Equals(pin.Node?.Trim(), selectedCircuitNode, StringComparison.Ordinal);

    private bool CircuitPinIsOnHoveredNetwork(AnalogPinDraft pin) =>
        !string.IsNullOrWhiteSpace(hoveredCircuitNode) &&
        string.Equals(pin.Node?.Trim(), hoveredCircuitNode, StringComparison.Ordinal);

    private IBrush CircuitPinDiagnosticBrush(AnalogPinDraft pin) =>
        circuitDiagnosticErrorPins.Contains(pin) ? circuitErrorBrush :
        circuitDiagnosticWarningPins.Contains(pin) ? circuitWarningBrush : CircuitPinBaseBrush(pin);

    private IBrush CircuitRouteDiagnosticBrush(AnalogWireRouteDraft route) =>
        circuitDiagnosticErrorRoutes.Contains(route) ? circuitErrorBrush :
        circuitDiagnosticWarningRoutes.Contains(route) ? circuitWarningBrush : circuitAccentBrush;

    private IBrush? CircuitNodeDiagnosticBrush(string node) =>
        circuitDiagnosticRoutes.FirstOrDefault(route => string.Equals(route.Node, node, StringComparison.Ordinal)) is { } route
            ? CircuitRouteDiagnosticBrush(route)
            : null;

    private static string CircuitPinRoleText(string role) => role switch
    {
        "positive" => "正向端",
        "negative" => "负向端",
        "control" => "控制端",
        _ => "无源端",
    };

    private static ConnectionFeedback CircuitPinConnectionDecision(
        AnalogPinDraft first, AnalogPinDraft second)
    {
        if (ReferenceEquals(first, second))
        {
            return ConnectionFeedback.Blocked("不能连接同一个引脚");
        }
        if (!string.IsNullOrWhiteSpace(first.Node) && !string.IsNullOrWhiteSpace(second.Node) &&
            string.Equals(first.Node.Trim(), second.Node.Trim(), StringComparison.Ordinal))
        {
            return ConnectionFeedback.Blocked("两个引脚已经属于同一网络，无法连接");
        }

        // Analog pins are net endpoints: role labels describe polarity/control
        // semantics, but do not impose a directional restriction on merging nets.
        var firstRole = NormalizeCircuitElectricalRole(first.ElectricalRole);
        var secondRole = NormalizeCircuitElectricalRole(second.ElectricalRole);
        if (!CircuitRoleCompatibility.Contains((firstRole, secondRole)))
        {
            return ConnectionFeedback.Blocked(
                $"不兼容的模拟角色：{CircuitPinRoleText(firstRole)} ↔ {CircuitPinRoleText(secondRole)}");
        }
        return ConnectionFeedback.Allowed(
            $"可连接：{CircuitPinRoleText(firstRole)} ↔ {CircuitPinRoleText(secondRole)}（模拟网络允许角色合并）");
    }

    private static string NormalizeCircuitElectricalRole(string? role) => role switch
    {
        "positive" => "positive",
        "negative" => "negative",
        "control" => "control",
        _ => "passive",
    };

    private static bool IsCircuitPinConnectionValid(AnalogPinDraft first, AnalogPinDraft second) =>
        CircuitPinConnectionDecision(first, second).IsAllowed;

    private void ClearPendingCircuitPin()
    {
        if (pendingCircuitPinVisual is not null)
        {
            RestoreCircuitPinVisual(pendingCircuitPinVisual);
        }
        if (pendingCircuitTargetVisual is not null)
        {
            RestoreCircuitPinVisual(pendingCircuitTargetVisual);
        }
        pendingCircuitPin = null;
        pendingCircuitPinVisual = null;
        pendingCircuitTargetVisual = null;
        draggingCircuitWire = false;
        if (CircuitWirePreview is not null)
        {
            CircuitWirePreview.Data = new StreamGeometry();
        }
        if (CircuitWirePreviewHalo is not null)
        {
            CircuitWirePreviewHalo.Data = new StreamGeometry();
        }
        if (CircuitWirePreview is { } preview)
        {
            preview.IsVisible = false;
        }
        if (CircuitWirePreviewHalo is { } previewHalo)
        {
            previewHalo.IsVisible = false;
        }
        if (CircuitWirePreviewTarget is { } previewTarget)
        {
            previewTarget.IsVisible = false;
        }
    }

    private void ClearCircuitSelection()
    {
        foreach (var card in selectedCircuitCards.Values)
        {
            card.BorderThickness = new Thickness(1);
        }
        selectedCircuitCards.Clear();
        selectedCircuitComponents.Clear();
        selectedCircuitCard = null;
        selectedCircuitComponent = null;
        selectedCircuitNode = null;
        selectedCircuitRoutePoint = null;
        hoveredCircuitNode = null;
        UpdateCircuitSelectionStatus();
        UpdateCircuitWireVisualState();
    }

    private void UpdateCircuitSelectionStatus()
    {
        CircuitSelectionText.Text = selectedCircuitNode is not null
            ? selectedCircuitRoutePoint is not null
                ? $"{CircuitNetworkStatusText(selectedCircuitNode)} · 折点 {selectedCircuitRoutePoint.X:0},{selectedCircuitRoutePoint.Y:0}"
                : CircuitNetworkStatusText(selectedCircuitNode)
            : selectedCircuitComponents.Count switch
            {
                0 => "未选择",
                1 => $"元件 {selectedCircuitComponents.First().Id}",
                _ => $"已选择 {selectedCircuitComponents.Count} 个元件",
            };
        UpdateCircuitInspectorState();
    }

    private void UpdateCircuitInspectorState()
    {
        var hasNetwork = !string.IsNullOrWhiteSpace(selectedCircuitNode);
        var selectedCount = selectedCircuitComponents.Count;
        CircuitInspectorEmptyState.IsVisible = !hasNetwork && selectedCount == 0;
        CircuitInspectorNetworkState.IsVisible = hasNetwork;
        CircuitInspectorMultiState.IsVisible = !hasNetwork && selectedCount > 1;
        AnalogComponentList.IsVisible = !hasNetwork && selectedCount == 1;
        AnalogComponentList.ItemsSource = selectedCount == 1
            ? new[] { selectedCircuitComponents.First() }
            : Array.Empty<AnalogComponentDraft>();
        CircuitInspectorNetworkText.Text = hasNetwork
            ? CircuitNetworkStatusText(selectedCircuitNode!)
            : string.Empty;
        CircuitInspectorMultiSelectionText.Text = selectedCount > 1
            ? $"{selectedCount} 个元件"
            : string.Empty;
        CircuitSelectionText.Foreground = hasNetwork
            ? circuitSelectedBrush
            : Resources["WorkshopMutedBrush"] as IBrush ?? circuitAccentBrush;
        UpdateCircuitWireVisualState();
    }

    private string CircuitNetworkStatusText(string node)
    {
        var pinCount = Document.AnalogCircuit.Components.SelectMany(component => component.Pins)
            .Count(pin => string.Equals(pin.Node.Trim(), node, StringComparison.Ordinal));
        var hasDiagnostic = circuitDiagnosticPins.Any(pin =>
            string.Equals(pin.Node.Trim(), node, StringComparison.Ordinal));
        return $"网络 {node} · {pinCount} 个引脚" + (hasDiagnostic ? " · 有诊断" : string.Empty);
    }

    private void UpdateCircuitWirePreview(Point pointer)
    {
        circuitPreviewPointer = pointer;
        if (pendingCircuitPin is null)
        {
            CircuitWirePreview.Data = new StreamGeometry();
            CircuitWirePreviewHalo.Data = new StreamGeometry();
            CircuitWirePreview.IsVisible = false;
            CircuitWirePreviewHalo.IsVisible = false;
            CircuitWirePreviewTarget.IsVisible = false;
            return;
        }
        var source = Document.AnalogCircuit.Components
            .SelectMany(component => component.Pins.Select((pin, index) =>
                new CircuitEndpoint(pin, CircuitPinPoint(component, index))))
            .FirstOrDefault(endpoint => ReferenceEquals(endpoint.Pin, pendingCircuitPin));
        if (source is null)
        {
            CircuitWirePreview.Data = new StreamGeometry();
            CircuitWirePreviewHalo.Data = new StreamGeometry();
            CircuitWirePreview.IsVisible = false;
            CircuitWirePreviewHalo.IsVisible = false;
            CircuitWirePreviewTarget.IsVisible = false;
            return;
        }
        var start = CircuitWorldToViewport(source.Point);
        var nearest = FindCircuitPinNearViewport(pointer, CircuitPinPreviewSnapTolerance);
        var target = nearest?.Endpoint;
        var targetPoint = target is null ? pointer : CircuitWorldToViewport(target.Point);
        var decision = target is null
            ? ConnectionFeedback.Neutral("请将连线移动到有效引脚")
            : CircuitPinConnectionDecision(pendingCircuitPin, target.Pin);
        var valid = target is not null && decision.IsAllowed;
        CircuitSelectionText.Text = target is null
            ? decision.Reason
            : $"{decision.Reason} · 已吸附到 {target.Pin.DisplayName}";
        CircuitSelectionText.Foreground = CircuitConnectionFeedbackBrush(decision.Kind);
        UpdateCircuitPinCandidateVisual(target?.Pin, valid);
        var points = BuildOrthogonalPreviewPoints(start, source.Pin.Side, targetPoint,
            target is null ? null : target.Pin.Side);
        var geometry = BuildPolylineGeometry(points);
        CircuitWirePreviewHalo.Data = geometry;
        CircuitWirePreview.Data = BuildPolylineGeometry(points);
        CircuitWirePreviewHalo.IsVisible = true;
        CircuitWirePreview.IsVisible = true;
        CircuitWirePreviewTarget.IsVisible = target is not null;
        if (target is not null)
        {
            Canvas.SetLeft(CircuitWirePreviewTarget, targetPoint.X - 9.0);
            Canvas.SetTop(CircuitWirePreviewTarget, targetPoint.Y - 9.0);
            CircuitWirePreviewTarget.Stroke = CircuitConnectionFeedbackBrush(decision.Kind);
            CircuitWirePreview.Stroke = CircuitConnectionFeedbackBrush(decision.Kind);
        }
        else
        {
            CircuitWirePreview.Stroke = CircuitConnectionFeedbackBrush(decision.Kind);
        }
    }

    private IBrush CircuitConnectionFeedbackBrush(ConnectionFeedbackKind kind) => kind switch
    {
        ConnectionFeedbackKind.Allowed => circuitHoverBrush,
        ConnectionFeedbackKind.Blocked => circuitErrorBrush,
        ConnectionFeedbackKind.Diagnostic => circuitWarningBrush,
        _ => circuitAccentBrush,
    };

    private void SetCircuitConnectionFeedback(ConnectionFeedback feedback)
    {
        CircuitSelectionText.Text = feedback.Reason;
        CircuitSelectionText.Foreground = CircuitConnectionFeedbackBrush(feedback.Kind);
    }

    private sealed record CircuitPinHit(CircuitEndpoint Endpoint, Ellipse? Visual);

    private CircuitPinHit? FindCircuitPinNearViewport(Point pointer, double threshold)
    {
        var thresholdSquared = threshold * threshold;
        return Document.AnalogCircuit.Components
            .SelectMany(component => component.Pins.Select((pin, index) =>
                new CircuitPinHit(new CircuitEndpoint(pin, CircuitPinPoint(component, index)), FindCircuitPinVisual(pin))))
            .Where(hit => !ReferenceEquals(hit.Endpoint.Pin, pendingCircuitPin) && hit.Visual is not null)
            .Select(hit => (Hit: hit, Distance: DistanceSquared(CircuitWorldToViewport(hit.Endpoint.Point), pointer)))
            .Where(candidate => candidate.Distance <= thresholdSquared)
            .OrderBy(candidate => candidate.Distance)
            .Select(candidate => candidate.Hit)
            .FirstOrDefault();
    }

    private Ellipse? FindCircuitPinVisual(AnalogPinDraft pin) =>
        CircuitNodeLayer.GetVisualDescendants().OfType<Ellipse>()
            .FirstOrDefault(visual => ReferenceEquals(visual.Tag, pin));

    private void UpdateCircuitPinCandidateVisual(AnalogPinDraft? target, bool valid)
    {
        if (pendingCircuitTargetVisual is not null &&
            (target is null || !ReferenceEquals(pendingCircuitTargetVisual.Tag, target)))
        {
            RestoreCircuitPinVisual(pendingCircuitTargetVisual);
            pendingCircuitTargetVisual = null;
        }
        if (target is null)
        {
            return;
        }
        pendingCircuitTargetVisual = FindCircuitPinVisual(target);
        if (pendingCircuitTargetVisual is not null)
        {
            pendingCircuitTargetVisual.Stroke = valid ? circuitHoverBrush : circuitErrorBrush;
            pendingCircuitTargetVisual.StrokeThickness = 3.0;
        }
    }

    private static IReadOnlyList<Point> BuildOrthogonalPreviewPoints(
        Point start, string startSide, Point end, string? endSide)
    {
        var leadDistance = 20.0;
        var startLead = OffsetCircuitPoint(start, startSide, leadDistance);
        if (endSide is null)
        {
            return DistinctCircuitPoints([start, startLead, new Point(startLead.X, end.Y), end]);
        }
        var endLead = OffsetCircuitPoint(end, endSide, leadDistance);
        if (startSide is "left" or "right" && endSide is "left" or "right")
        {
            var middleX = (startLead.X + endLead.X) * 0.5;
            return DistinctCircuitPoints([start, startLead, new Point(middleX, startLead.Y),
                new Point(middleX, endLead.Y), endLead, end]);
        }
        if (startSide is "top" or "bottom" && endSide is "top" or "bottom")
        {
            var middleY = (startLead.Y + endLead.Y) * 0.5;
            return DistinctCircuitPoints([start, startLead, new Point(startLead.X, middleY),
                new Point(endLead.X, middleY), endLead, end]);
        }
        return DistinctCircuitPoints([start, startLead, new Point(startLead.X, endLead.Y), endLead, end]);
    }

    private static Point OffsetCircuitPoint(Point point, string side, double distance) => side switch
    {
        "right" => new Point(point.X + distance, point.Y),
        "bottom" => new Point(point.X, point.Y + distance),
        "top" => new Point(point.X, point.Y - distance),
        _ => new Point(point.X - distance, point.Y),
    };

    private static IReadOnlyList<Point> DistinctCircuitPoints(IEnumerable<Point> points)
    {
        var result = new List<Point>();
        foreach (var point in points)
        {
            if (result.Count == 0 || DistanceSquared(result[^1], point) > 0.01)
            {
                result.Add(point);
            }
        }
        return result;
    }

    private static StreamGeometry BuildPolylineGeometry(IReadOnlyList<Point> points)
    {
        var geometry = new StreamGeometry();
        if (points.Count == 0)
        {
            return geometry;
        }
        using var context = geometry.Open();
        context.BeginFigure(points[0], false);
        foreach (var point in points.Skip(1))
        {
            context.LineTo(point);
        }
        context.EndFigure(false);
        return geometry;
    }

    private void CircuitCanvasPointerMoved(object? sender, PointerEventArgs args)
    {
        var viewportPoint = args.GetPosition(CircuitCanvasSurface);
        if (pendingCircuitPin is not null)
        {
            UpdateCircuitWirePreview(viewportPoint);
            if (args.Pointer.Captured == CircuitCanvasSurface)
            {
                draggingCircuitWire = draggingCircuitWire ||
                    DistanceSquared(circuitWirePointerStart, viewportPoint) > 16.0;
                if (draggingCircuitWire)
                {
                    return;
                }
            }
        }
        if (draggingCircuitRoutePoint is not null && args.Pointer.Captured == CircuitCanvasSurface)
        {
            var world = CircuitViewportToWorld(viewportPoint);
            draggingCircuitRoutePoint.X = Math.Clamp(SnapCircuitEdit(world.X), 0.0, CircuitCanvasWidth);
            draggingCircuitRoutePoint.Y = Math.Clamp(SnapCircuitEdit(world.Y), 0.0, CircuitCanvasHeight);
            RenderCircuitConnections();
            return;
        }
        if (panningCircuit && args.Pointer.Captured == CircuitCanvasSurface)
        {
            circuitPanX = circuitPanStartX + viewportPoint.X - circuitPointerStart.X;
            circuitPanY = circuitPanStartY + viewportPoint.Y - circuitPointerStart.Y;
            ApplyCircuitCanvasTransform();
            return;
        }
        if (selectingCircuitBox && args.Pointer.Captured == CircuitCanvasSurface)
        {
            UpdateCircuitSelectionRectangle(circuitSelectionStart, viewportPoint);
            return;
        }
        if (draggingCircuitComponent is null || args.Pointer.Captured != CircuitCanvasSurface)
        {
            return;
        }
        var pointWorld = CircuitViewportToWorld(viewportPoint);
        var deltaX = pointWorld.X - draggingCircuitPointerStart.X;
        var deltaY = pointWorld.Y - draggingCircuitPointerStart.Y;
        foreach (var (component, start) in draggingCircuitStarts)
        {
            component.CanvasX = Math.Clamp(
                SnapCircuitEdit(start.X + deltaX),
                0.0, CircuitCanvasWidth - component.Descriptor.SymbolWidth);
            component.CanvasY = Math.Clamp(
                SnapCircuitEdit(start.Y + deltaY),
                0.0, CircuitCanvasHeight - component.Descriptor.SymbolHeight);
        }
    }

    private void CircuitCanvasPointerReleased(object? sender, PointerReleasedEventArgs args)
    {
        if (pendingCircuitPin is not null && args.Pointer.Captured == CircuitCanvasSurface)
        {
            var viewportPoint = args.GetPosition(CircuitCanvasSurface);
            var wasDraggingWire = draggingCircuitWire;
            var candidate = FindCircuitPinNearViewport(viewportPoint, CircuitPinCommitSnapTolerance)?.Endpoint.Pin;
            if (wasDraggingWire)
            {
                if (candidate is not null && IsCircuitPinConnectionValid(pendingCircuitPin, candidate))
                {
                    Document.ConnectAnalogPins(pendingCircuitPin, candidate);
                    ClearPendingCircuitPin();
                    RenderCircuitConnections();
                }
                else
                {
                    CircuitSelectionText.Text = candidate is null
                        ? "连接目标无效，已取消"
                        : CircuitPinConnectionDecision(pendingCircuitPin, candidate).Reason;
                    ClearPendingCircuitPin();
                }
            }
            else
            {
                draggingCircuitWire = false;
                args.Pointer.Capture(null);
            }
            if (!wasDraggingWire)
            {
                return;
            }
        }
        if (draggingCircuitRoutePoint is not null)
        {
            draggingCircuitRoutePoint = null;
            draggingCircuitRoute = null;
            Document.EndEditTransaction();
            RenderCircuitConnections();
        }
        if (draggingCircuitComponent is not null)
        {
            draggingCircuitComponent = null;
            draggingCircuitStarts.Clear();
            Document.EndEditTransaction();
            RenderCircuitConnections();
        }
        if (selectingCircuitBox)
        {
            selectingCircuitBox = false;
            CompleteCircuitBoxSelection(circuitSelectionStart, args.GetPosition(CircuitCanvasSurface));
            CircuitSelectionRectangle.IsVisible = false;
        }
        panningCircuit = false;
        if (args.Pointer.Captured == CircuitCanvasSurface)
        {
            args.Pointer.Capture(null);
        }
    }

    private void UpdateCircuitSelectionRectangle(Point start, Point end)
    {
        var left = Math.Min(start.X, end.X);
        var top = Math.Min(start.Y, end.Y);
        Canvas.SetLeft(CircuitSelectionRectangle, left);
        Canvas.SetTop(CircuitSelectionRectangle, top);
        CircuitSelectionRectangle.Width = Math.Abs(end.X - start.X);
        CircuitSelectionRectangle.Height = Math.Abs(end.Y - start.Y);
        CircuitSelectionRectangle.IsVisible = true;
    }

    private void CompleteCircuitBoxSelection(Point start, Point end)
    {
        var first = CircuitViewportToWorld(start);
        var second = CircuitViewportToWorld(end);
        var left = Math.Min(first.X, second.X);
        var top = Math.Min(first.Y, second.Y);
        var right = Math.Max(first.X, second.X);
        var bottom = Math.Max(first.Y, second.Y);
        var selected = Document.AnalogCircuit.Components.Where(component =>
            component.CanvasX < right && component.CanvasX + component.Descriptor.SymbolWidth > left &&
            component.CanvasY < bottom && component.CanvasY + component.Descriptor.SymbolHeight > top).ToArray();
        if (!circuitSelectionAdditive)
        {
            SelectCircuitComponents(selected);
            return;
        }
        foreach (var component in selected)
        {
            selectedCircuitComponents.Add(component);
            var index = Document.AnalogCircuit.Components.IndexOf(component);
            var foundCard = index >= 0 && CircuitNodeLayer.ContainerFromIndex(index) is Control container
                ? container.GetVisualDescendants().OfType<Border>()
                    .FirstOrDefault(candidate => ReferenceEquals(candidate.Tag, component))
                : null;
            if (foundCard is not null)
            {
                foundCard.BorderThickness = new Thickness(2);
                selectedCircuitCards[component] = foundCard;
            }
        }
        selectedCircuitComponent = selectedCircuitComponents.LastOrDefault();
        selectedCircuitCard = selectedCircuitComponent is not null &&
                              selectedCircuitCards.TryGetValue(selectedCircuitComponent, out var primaryCard)
            ? primaryCard
            : null;
        UpdateCircuitSelectionStatus();
    }

    private void SelectCircuitComponents(IEnumerable<AnalogComponentDraft> components)
    {
        ClearCircuitSelection();
        foreach (var component in components)
        {
            selectedCircuitComponents.Add(component);
            var index = Document.AnalogCircuit.Components.IndexOf(component);
            var foundCard = index >= 0 && CircuitNodeLayer.ContainerFromIndex(index) is Control container
                ? container.GetVisualDescendants().OfType<Border>()
                    .FirstOrDefault(candidate => ReferenceEquals(candidate.Tag, component))
                : null;
            if (foundCard is not null)
            {
                foundCard.BorderThickness = new Thickness(2);
                selectedCircuitCards[component] = foundCard;
            }
        }
        selectedCircuitComponent = selectedCircuitComponents.LastOrDefault();
        selectedCircuitCard = selectedCircuitComponent is not null &&
                              selectedCircuitCards.TryGetValue(selectedCircuitComponent, out var primaryCard)
            ? primaryCard
            : null;
        UpdateCircuitSelectionStatus();
    }

    private static string? CircuitWireNode(object? sender) =>
        sender is Line { Tag: CircuitWireVisualTag tag } ? tag.Node : null;

    private void CircuitWirePointerEntered(object? sender, PointerEventArgs args)
    {
        var node = CircuitWireNode(sender);
        if (node is null)
        {
            return;
        }
        hoveredCircuitNode = node;
        UpdateCircuitWireVisualState();
        CircuitSelectionText.Text = $"悬停网络 {node}";
    }

    private void CircuitWirePointerExited(object? sender, PointerEventArgs args)
    {
        var node = CircuitWireNode(sender);
        if (node is null || !string.Equals(hoveredCircuitNode, node, StringComparison.Ordinal))
        {
            return;
        }
        hoveredCircuitNode = null;
        UpdateCircuitWireVisualState();
        UpdateCircuitSelectionStatus();
    }

    private void CircuitWirePointerPressed(object? sender, PointerPressedEventArgs args)
    {
        var node = CircuitWireNode(sender);
        if (node is null ||
            !args.GetCurrentPoint(CircuitCanvasSurface).Properties.IsLeftButtonPressed)
        {
            return;
        }
        ClearCircuitSelection();
        ClearPendingCircuitPin();
        selectedCircuitNode = node;
        UpdateCircuitSelectionStatus();
        RenderCircuitConnections();
        CircuitCanvasSurface.Focus();
        args.Handled = true;
    }

    private void CircuitRoutePointPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (sender is not Ellipse { Tag: AnalogWirePointDraft point } ||
            !args.GetCurrentPoint(CircuitCanvasSurface).Properties.IsLeftButtonPressed)
        {
            return;
        }
        draggingCircuitRoutePoint = point;
        draggingCircuitRoute = Document.AnalogCircuit.WireRoutes.FirstOrDefault(route => route.Points.Contains(point));
        selectedCircuitRoutePoint = point;
        if (draggingCircuitRoute is not null)
        {
            selectedCircuitNode = draggingCircuitRoute.Node;
            UpdateCircuitSelectionStatus();
        }
        CircuitCanvasSurface.Focus();
        Document.BeginEditTransaction();
        args.Pointer.Capture(CircuitCanvasSurface);
        args.Handled = true;
    }

    private void CircuitAddRoutePointClick(object? sender, RoutedEventArgs args)
    {
        if (string.IsNullOrWhiteSpace(selectedCircuitNode))
        {
            return;
        }
        var endpoints = Document.AnalogCircuit.Components
            .SelectMany(component => component.Pins.Select((pin, index) => new { pin.Node, Point = CircuitPinPoint(component, index) }))
            .Where(endpoint => string.Equals(endpoint.Node.Trim(), selectedCircuitNode, StringComparison.Ordinal))
            .Select(endpoint => endpoint.Point)
            .ToArray();
        if (endpoints.Length == 0)
        {
            return;
        }
        var route = Document.GetOrCreateAnalogWireRoute(selectedCircuitNode);
        var point = endpoints.Length == 1
            ? endpoints[0]
            : new Point((endpoints.Min(endpoint => endpoint.X) + endpoints.Max(endpoint => endpoint.X)) * 0.5,
                (endpoints.Min(endpoint => endpoint.Y) + endpoints.Max(endpoint => endpoint.Y)) * 0.5);
        Document.BeginEditTransaction();
        try
        {
            route.Points.Add(new AnalogWirePointDraft(SnapCircuitEdit(point.X), SnapCircuitEdit(point.Y)));
        }
        finally
        {
            Document.EndEditTransaction();
        }
        RenderCircuitConnections();
    }

    private void CircuitClearRouteClick(object? sender, RoutedEventArgs args)
    {
        if (string.IsNullOrWhiteSpace(selectedCircuitNode))
        {
            return;
        }
        Document.BeginEditTransaction();
        try
        {
            Document.RemoveAnalogWireRoute(selectedCircuitNode);
        }
        finally
        {
            Document.EndEditTransaction();
        }
        RenderCircuitConnections();
    }

    private void CircuitCanvasPointerWheelChanged(object? sender, PointerWheelEventArgs args)
    {
        var requested = circuitZoom + Math.Sign(args.Delta.Y) * 0.1;
        ZoomCircuitAt(args.GetPosition(CircuitCanvasSurface), requested);
        args.Handled = true;
    }

    private void CircuitCanvasKeyDown(object? sender, KeyEventArgs args)
    {
        if (args.Key == Key.Escape)
        {
            ClearPendingCircuitPin();
            ClearCircuitSelection();
            args.Handled = true;
            return;
        }
        if ((args.KeyModifiers & KeyModifiers.Control) != 0 && args.Key == Key.V && circuitClipboard.Count > 0)
        {
            circuitClipboardPasteOffset += CircuitGridSize * 2.0;
            Document.BeginEditTransaction();
            AnalogComponentDraft[] pasted;
            try
            {
                pasted = circuitClipboard.Select(component =>
                    Document.DuplicateAnalogComponent(component, circuitClipboardPasteOffset)).ToArray();
            }
            finally
            {
                Document.EndEditTransaction();
            }
            ClearCircuitSelection();
            foreach (var component in pasted)
            {
                selectedCircuitComponents.Add(component);
            }
            selectedCircuitComponent = pasted.LastOrDefault();
            args.Handled = true;
            return;
        }
        if ((args.Key is Key.Delete or Key.Back) && !string.IsNullOrWhiteSpace(selectedCircuitNode))
        {
            if (selectedCircuitRoutePoint is not null)
            {
                args.Handled = RemoveSelectedCircuitRoutePoint();
                return;
            }
            args.Handled = RemoveSelectedCircuitRoute();
            return;
        }
        if (selectedCircuitComponents.Count == 0)
        {
            return;
        }
        if ((args.KeyModifiers & KeyModifiers.Control) != 0 && args.Key == Key.C)
        {
            circuitClipboard.Clear();
            circuitClipboard.AddRange(selectedCircuitComponents.Select(CloneCircuitComponent));
            circuitClipboardPasteOffset = 0.0;
            args.Handled = true;
            return;
        }
        if (args.Key is Key.Delete or Key.Back)
        {
            Document.BeginEditTransaction();
            try
            {
                foreach (var component in selectedCircuitComponents.ToArray())
                {
                    Document.RemoveAnalogComponent(component);
                }
            }
            finally
            {
                Document.EndEditTransaction();
            }
            ClearCircuitSelection();
            args.Handled = true;
            return;
        }
        if ((args.KeyModifiers & KeyModifiers.Control) != 0 && args.Key == Key.D)
        {
            Document.BeginEditTransaction();
            AnalogComponentDraft[] duplicates;
            try
            {
                duplicates = selectedCircuitComponents.Select(component =>
                    Document.DuplicateAnalogComponent(component)).ToArray();
            }
            finally
            {
                Document.EndEditTransaction();
            }
            ClearCircuitSelection();
            foreach (var component in duplicates)
            {
                selectedCircuitComponents.Add(component);
            }
            selectedCircuitComponent = duplicates.LastOrDefault();
            args.Handled = true;
            return;
        }
        var step = (args.KeyModifiers & KeyModifiers.Shift) != 0 ? 1.0 : CircuitGridSize;
        var (deltaX, deltaY) = args.Key switch
        {
            Key.Left => (-step, 0.0),
            Key.Right => (step, 0.0),
            Key.Up => (0.0, -step),
            Key.Down => (0.0, step),
            _ => (0.0, 0.0),
        };
        if (deltaX == 0.0 && deltaY == 0.0)
        {
            return;
        }
        Document.BeginEditTransaction();
        foreach (var component in selectedCircuitComponents)
        {
            component.CanvasX = Math.Clamp(
                component.CanvasX + deltaX, 0.0, CircuitCanvasWidth - component.Descriptor.SymbolWidth);
            component.CanvasY = Math.Clamp(
                component.CanvasY + deltaY, 0.0, CircuitCanvasHeight - component.Descriptor.SymbolHeight);
        }
        Document.EndEditTransaction();
        args.Handled = true;
    }

    private bool RemoveSelectedCircuitRoute()
    {
        if (string.IsNullOrWhiteSpace(selectedCircuitNode))
        {
            return false;
        }
        Document.BeginEditTransaction();
        try
        {
            Document.RemoveAnalogWireRoute(selectedCircuitNode);
        }
        finally
        {
            Document.EndEditTransaction();
        }
        selectedCircuitNode = null;
        RenderCircuitConnections();
        return true;
    }

    private bool RemoveSelectedCircuitRoutePoint()
    {
        if (selectedCircuitRoutePoint is null || string.IsNullOrWhiteSpace(selectedCircuitNode))
        {
            return false;
        }
        var route = Document.AnalogCircuit.WireRoutes.FirstOrDefault(candidate =>
            string.Equals(candidate.Node, selectedCircuitNode, StringComparison.Ordinal) &&
            candidate.Points.Contains(selectedCircuitRoutePoint));
        if (route is null)
        {
            selectedCircuitRoutePoint = null;
            return false;
        }
        Document.BeginEditTransaction();
        try
        {
            route.Points.Remove(selectedCircuitRoutePoint);
            if (route.Points.Count == 0)
            {
                Document.AnalogCircuit.WireRoutes.Remove(route);
            }
        }
        finally
        {
            Document.EndEditTransaction();
        }
        selectedCircuitRoutePoint = null;
        RenderCircuitConnections();
        return true;
    }

    private static AnalogComponentDraft CloneCircuitComponent(AnalogComponentDraft source) =>
        new(source.Id, source.Type,
            source.Pins.Select(pin => new AnalogPinDraft(pin.Id, pin.DisplayName, pin.Node)),
            source.Properties.Select(property => new AnalogComponentPropertyDraft(property.Id, property.JsonValue)),
            source.CanvasX, source.CanvasY);

    private void ZoomCircuitAt(Point viewportPoint, double requestedZoom)
    {
        var next = Math.Clamp(requestedZoom, MinimumCircuitZoom, MaximumCircuitZoom);
        var world = CircuitViewportToWorld(viewportPoint);
        circuitZoom = next;
        circuitPanX = viewportPoint.X - world.X * circuitZoom;
        circuitPanY = viewportPoint.Y - world.Y * circuitZoom;
        ApplyCircuitCanvasTransform();
    }

    private void FitCircuitCanvas()
    {
        if (Document.AnalogCircuit.Components.Count == 0 || CircuitCanvasSurface.Bounds.Width <= 0 ||
            CircuitCanvasSurface.Bounds.Height <= 0)
        {
            circuitZoom = 1.0;
            circuitPanX = 0.0;
            circuitPanY = 0.0;
            ApplyCircuitCanvasTransform();
            return;
        }
        var minimumX = Document.AnalogCircuit.Components.Min(component => component.CanvasX);
        var minimumY = Document.AnalogCircuit.Components.Min(component => component.CanvasY);
        var maximumX = Document.AnalogCircuit.Components.Max(component =>
            component.CanvasX + component.Descriptor.SymbolWidth);
        var maximumY = Document.AnalogCircuit.Components.Max(component =>
            component.CanvasY + component.Descriptor.SymbolHeight);
        var width = Math.Max(maximumX - minimumX, 1.0);
        var height = Math.Max(maximumY - minimumY, 1.0);
        circuitZoom = Math.Clamp(Math.Min(
            (CircuitCanvasSurface.Bounds.Width - 40.0) / width,
            (CircuitCanvasSurface.Bounds.Height - 40.0) / height), MinimumCircuitZoom, MaximumCircuitZoom);
        circuitPanX = CircuitCanvasSurface.Bounds.Width * 0.5 - (minimumX + maximumX) * 0.5 * circuitZoom;
        circuitPanY = CircuitCanvasSurface.Bounds.Height * 0.5 - (minimumY + maximumY) * 0.5 * circuitZoom;
        ApplyCircuitCanvasTransform();
    }

    private void CircuitZoomOutClick(object? sender, RoutedEventArgs args) =>
        ZoomCircuitAt(new Point(CircuitCanvasSurface.Bounds.Width * 0.5,
            CircuitCanvasSurface.Bounds.Height * 0.5), circuitZoom - 0.1);
    private void CircuitZoomInClick(object? sender, RoutedEventArgs args) =>
        ZoomCircuitAt(new Point(CircuitCanvasSurface.Bounds.Width * 0.5,
            CircuitCanvasSurface.Bounds.Height * 0.5), circuitZoom + 0.1);
    private void CircuitFitClick(object? sender, RoutedEventArgs args) => FitCircuitCanvas();

    private void CenterCircuitComponent(AnalogComponentDraft component)
    {
        if (CircuitCanvasSurface.Bounds.Width <= 0 || CircuitCanvasSurface.Bounds.Height <= 0)
        {
            return;
        }
        circuitPanX = CircuitCanvasSurface.Bounds.Width * 0.5 -
                      (component.CanvasX + component.Descriptor.SymbolWidth * 0.5) * circuitZoom;
        circuitPanY = CircuitCanvasSurface.Bounds.Height * 0.5 -
                      (component.CanvasY + component.Descriptor.SymbolHeight * 0.5) * circuitZoom;
        ApplyCircuitCanvasTransform();
    }

    private void ValidationIssueSelected(object? sender, SelectionChangedEventArgs args)
    {
        if (ValidationList.SelectedItem is not ModuleAuthoringIssue issue)
        {
            return;
        }
        NavigateToValidationIssue(issue);
    }

    private void NavigateToValidationIssue(ModuleAuthoringIssue issue)
    {
        switch (issue.Target)
        {
            case ModuleAuthoringIssueTarget.Project:
                WorkshopTabs.SelectedIndex = 0;
                if (issue.FieldId == "moduleId")
                {
                    ModuleIdEditor.Focus();
                }
                break;
            case ModuleAuthoringIssueTarget.ModulePort:
                WorkshopTabs.SelectedIndex = 1;
                BringItemIntoView(PortList, issue.TargetObject);
                break;
            case ModuleAuthoringIssueTarget.Parameter:
                WorkshopTabs.SelectedIndex = 1;
                BringItemIntoView(ParameterList, issue.TargetObject);
                break;
            case ModuleAuthoringIssueTarget.Logic:
                WorkshopTabs.SelectedIndex = 2;
                BringItemIntoView(DigitalOperationList, issue.TargetObject);
                break;
            case ModuleAuthoringIssueTarget.AnalogPort:
                WorkshopTabs.SelectedIndex = 2;
                AnalogConfigurationTabs.SelectedIndex = 0;
                BringItemIntoView(AnalogPortList, issue.TargetObject);
                break;
            case ModuleAuthoringIssueTarget.AnalogSupply:
                WorkshopTabs.SelectedIndex = 2;
                AnalogConfigurationTabs.SelectedIndex = 1;
                BringItemIntoView(AnalogSupplyList, issue.TargetObject);
                break;
            case ModuleAuthoringIssueTarget.AnalogBinding:
                WorkshopTabs.SelectedIndex = 2;
                AnalogConfigurationTabs.SelectedIndex = 2;
                BringItemIntoView(AnalogBindingList, issue.TargetObject);
                break;
            case ModuleAuthoringIssueTarget.AnalogComponent:
            case ModuleAuthoringIssueTarget.AnalogPin:
            case ModuleAuthoringIssueTarget.AnalogProperty:
                WorkshopTabs.SelectedIndex = 2;
                if (issue.TargetObject is AnalogComponentDraft component)
                {
                    SelectCircuitComponents([component]);
                    CenterCircuitComponent(component);
                    CircuitCanvasSurface.Focus();
                    Dispatcher.UIThread.Post(() =>
                    {
                        BringItemIntoView(AnalogComponentList, component);
                    });
                }
                break;
            case ModuleAuthoringIssueTarget.AnalogCircuit:
                WorkshopTabs.SelectedIndex = 2;
                if (issue.FieldId is "solver" or "convergence" or "oversampling")
                {
                    AnalogConfigurationTabs.SelectedIndex = 3;
                }
                break;
            case ModuleAuthoringIssueTarget.Skin:
                WorkshopTabs.SelectedIndex = 3;
                break;
            case ModuleAuthoringIssueTarget.NativeRuntime:
                WorkshopTabs.SelectedIndex = Document.IsAnalogLogic ? 2 : 4;
                break;
        }
    }

    private static void BringItemIntoView(ItemsControl list, object? item)
    {
        if (item is null)
        {
            return;
        }
        var index = list.Items.IndexOf(item);
        if (index >= 0 && list.ContainerFromIndex(index) is Control container)
        {
            container.BringIntoView();
            container.Focus();
        }
    }

    private void NewProjectClick(object? sender, RoutedEventArgs args) => _ = NewProjectAsync();
    private void OpenProjectClick(object? sender, RoutedEventArgs args) => _ = OpenProjectAsync();
    private void SaveProjectClick(object? sender, RoutedEventArgs args) => _ = SaveAsync();
    private void SaveProjectAsClick(object? sender, RoutedEventArgs args) => _ = SaveAsAsync();
    private void CopyProjectClick(object? sender, RoutedEventArgs args) => _ = CopyProjectAsync();
    private void ExportProjectClick(object? sender, RoutedEventArgs args) => _ = ExportProjectAsync();
    private void InstallProjectClick(object? sender, RoutedEventArgs args) => _ = InstallProjectAsync();
    private void UndoClick(object? sender, RoutedEventArgs args)
    {
        ClearSkinSelection();
        Document.Undo();
    }
    private void RedoClick(object? sender, RoutedEventArgs args)
    {
        ClearSkinSelection();
        Document.Redo();
    }

    private async Task InstallProjectAsync()
    {
        var ready = Document.IsDirty || !Document.HasProjectDirectory
            ? await SaveAsync()
            : ValidateNativePackage();
        if (!ready || Document.ProjectDirectory is null)
        {
            return;
        }
        try
        {
            string installedDirectory;
            try
            {
                installedDirectory = ModulePackageWorkflow.Install(Document.ProjectDirectory);
            }
            catch (ModuleInstallConflictException conflict)
            {
                if (!await ConfirmInstallOverwriteAsync(conflict)) return;
                installedDirectory = ModulePackageWorkflow.Install(Document.ProjectDirectory, overwrite: true);
            }
            Document.ReportStatus($"已安装 {Document.ModuleId} {Document.PackageVersion}");
            InstallRequested?.Invoke(this, installedDirectory);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                          InvalidDataException or JsonException)
        {
            Document.ReportStatus($"安装失败：{exception.Message}");
        }
    }

    private async Task<bool> ConfirmInstallOverwriteAsync(ModuleInstallConflictException conflict)
    {
        var owner = TopLevel.GetTopLevel(this) as Window;
        if (owner is null) return false;
        var dialog = new Window
        {
            Title = "模块版本冲突",
            Width = 460,
            SizeToContent = SizeToContent.Height,
            CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
        };
        var cancel = new Button { Content = "取消", MinWidth = 88 };
        var overwrite = new Button { Content = "覆盖安装", MinWidth = 104 };
        cancel.Click += (_, _) => dialog.Close(false);
        overwrite.Click += (_, _) => dialog.Close(true);
        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Spacing = 8,
            Children = { cancel, overwrite },
        };
        dialog.Content = new StackPanel
        {
            Margin = new Thickness(24),
            Spacing = 16,
            Children =
            {
                new TextBlock
                {
                    Text = $"模块库中已有版本 {conflict.InstalledVersion}，当前项目版本为 " +
                           $"{conflict.RequestedVersion}。覆盖会先创建可回滚备份。",
                    TextWrapping = TextWrapping.Wrap,
                },
                buttons,
            },
        };
        return await dialog.ShowDialog<bool>(owner);
    }

    private async Task CopyProjectAsync()
    {
        var ready = Document.IsDirty || !Document.HasProjectDirectory ? await SaveAsync() : true;
        if (!ready || Document.ProjectDirectory is null) return;
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null) return;
        var folders = await storage.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "选择项目副本目录",
            AllowMultiple = false,
        });
        var destination = folders.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(destination))
        {
            Document.ReportStatus("已取消复制项目");
            return;
        }
        try
        {
            ModulePackageWorkflow.CopyProject(Document.ProjectDirectory, destination);
            RecentProjects.Touch(destination);
            RefreshRecentProjects();
            Document.ReportStatus($"项目副本已创建：{destination}");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            Document.ReportStatus($"复制失败：{exception.Message}");
        }
    }

    private async Task ExportProjectAsync()
    {
        var ready = Document.IsDirty || !Document.HasProjectDirectory ? await SaveAsync() : ValidateNativePackage();
        if (!ready || Document.ProjectDirectory is null) return;
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null) return;
        var file = await storage.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "导出 SoloRig 模块包",
            SuggestedFileName = $"{Document.ModuleId}-{Document.PackageVersion}.srmodule",
            DefaultExtension = "srmodule",
            FileTypeChoices = [ModulePackageFileType],
        });
        var destination = file?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(destination))
        {
            Document.ReportStatus("已取消导出模块包");
            return;
        }
        try
        {
            ModulePackageWorkflow.ExportPackage(Document.ProjectDirectory, destination);
            Document.ReportStatus($"模块包已导出：{destination}");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                          InvalidDataException or JsonException)
        {
            Document.ReportStatus($"导出失败：{exception.Message}");
        }
    }

    private async Task NewProjectAsync()
    {
        if (!await ConfirmPendingChangesAsync())
        {
            return;
        }
        var selectedKind = (ProjectKindSelector.SelectedItem as ModuleAuthoringKindOption)?.Kind ??
                           ModuleAuthoringKind.DigitalAlgorithm;
        var selected = TemplateSelector.SelectedItem as ModuleAuthoringTemplateOption ??
                       ModuleAuthoringDocument.TemplatesForKind(selectedKind).First();
        ClearSkinSelection();
        Document.NewProjectFromTemplate(selected.Id);
        WorkshopTabs.SelectedIndex = 0;
        ModuleIdEditor.Focus();
        ModuleIdEditor.SelectAll();
    }

    private async Task OpenProjectAsync()
    {
        if (!await ConfirmPendingChangesAsync())
        {
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            Document.ReportStatus("无法访问目录选择器");
            return;
        }
        var folders = await storage.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "打开 SoloRig 效果器项目",
            AllowMultiple = false,
        });
        var path = folders.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            Document.ReportStatus("已取消打开项目");
            return;
        }
        if (Document.OpenFromDirectory(path))
        {
            ClearSkinSelection();
            RecentProjects.Touch(path);
            RefreshRecentProjects();
            SelectProjectCreationKind(Document.Kind);
            WorkshopTabs.SelectedIndex = 0;
        }
    }

    private async Task<bool> SaveAsync()
    {
        if (Document.HasProjectDirectory)
        {
            var saved = Document.Save() && ValidateNativePackage();
            if (saved && Document.ProjectDirectory is not null)
            {
                RecentProjects.Touch(Document.ProjectDirectory);
                RefreshRecentProjects();
            }
            return saved;
        }
        return await SaveAsAsync();
    }

    private async Task<bool> SaveAsAsync()
    {
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            Document.ReportStatus("无法访问目录选择器");
            return false;
        }
        var folders = await storage.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "选择效果器项目目录",
            AllowMultiple = false,
        });
        var path = folders.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            Document.ReportStatus("已取消保存项目");
            return false;
        }
        var saved = Document.SaveToDirectory(path) && ValidateNativePackage();
        if (saved)
        {
            RecentProjects.Touch(path);
            RefreshRecentProjects();
        }
        return saved;
    }

    private void OpenRecentProjectClick(object? sender, RoutedEventArgs args) => _ = OpenRecentProjectAsync();

    private void RecentProjectSearchChanged(object? sender, TextChangedEventArgs args) => RefreshRecentProjects();

    private void RefreshRecentProjects()
    {
        var query = RecentProjectSearchBox?.Text?.Trim() ?? string.Empty;
        var entries = string.IsNullOrWhiteSpace(query)
            ? RecentProjects.Entries.ToArray()
            : RecentProjects.Entries.Where(entry =>
                entry.SearchText.Contains(query, StringComparison.OrdinalIgnoreCase)).ToArray();
        RecentProjectSelector.ItemsSource = entries;
        RecentProjectSelector.SelectedIndex = entries.Length > 0 ? 0 : -1;
    }

    private async Task OpenRecentProjectAsync()
    {
        if (RecentProjectSelector.SelectedItem is not RecentModuleProject project)
        {
            Document.ReportStatus("请选择一个最近项目");
            return;
        }
        if (!await ConfirmPendingChangesAsync())
        {
            return;
        }
        if (!Document.OpenFromDirectory(project.Directory))
        {
            return;
        }
        RecentProjects.Touch(project.Directory);
        RefreshRecentProjects();
        SelectProjectCreationKind(Document.Kind);
        WorkshopTabs.SelectedIndex = 0;
    }

    private void ProjectKindSelectionChanged(object? sender, SelectionChangedEventArgs args)
    {
        if (ProjectKindSelector.SelectedItem is ModuleAuthoringKindOption option)
        {
            RefreshTemplateOptions(option.Kind);
        }
    }

    private void SelectProjectCreationKind(ModuleAuthoringKind projectKind)
    {
        ProjectKindSelector.SelectedItem = ModuleAuthoringDocument.KindOptions.First(option => option.Kind == projectKind);
        RefreshTemplateOptions(projectKind);
    }

    private void RefreshTemplateOptions(ModuleAuthoringKind projectKind)
    {
        var templates = ModuleAuthoringDocument.TemplatesForKind(projectKind);
        TemplateSelector.ItemsSource = templates;
        TemplateSelector.SelectedIndex = templates.Count > 0 ? 0 : -1;
    }

    private void AddInputPortClick(object? sender, RoutedEventArgs args) => Document.AddPort("input");
    private void AddOutputPortClick(object? sender, RoutedEventArgs args) => Document.AddPort("output");
    private void AddParameterClick(object? sender, RoutedEventArgs args) => Document.AddParameter();
    private void AddDigitalOperationClick(object? sender, RoutedEventArgs args) => Document.AddDigitalOperation();
    private void AddAnalogComponentClick(object? sender, RoutedEventArgs args) =>
        Document.AddAnalogComponent((AnalogAddTypeSelector.SelectedItem as AnalogComponentDescriptor)?.Type ?? "resistor");
    private void AddAnalogInputPortClick(object? sender, RoutedEventArgs args) =>
        Document.AddAnalogCircuitPort("input");
    private void AddAnalogOutputPortClick(object? sender, RoutedEventArgs args) =>
        Document.AddAnalogCircuitPort("output");
    private void AddAnalogSupplyClick(object? sender, RoutedEventArgs args) => Document.AddAnalogSupply();
    private void AddAnalogBindingClick(object? sender, RoutedEventArgs args) => Document.AddAnalogParameterBinding();
    private void AddSkinParameterPlacementClick(object? sender, RoutedEventArgs args) =>
        SelectSkinPlacement(Document.AddSkinParameterPlacement());

    private void SkinCanvasPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (!args.GetCurrentPoint(SkinDesignCanvas).Properties.IsLeftButtonPressed) return;
        ClearSkinSelection();
        args.Handled = true;
    }

    private void SkinPlacementPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (sender is not Border { Tag: PedalSkinParameterPlacementDraft placement } ||
            !args.GetCurrentPoint(SkinDesignCanvas).Properties.IsLeftButtonPressed)
        {
            return;
        }
        SelectSkinPlacement(placement);
        draggingSkinElement = true;
        skinDragPointerStart = args.GetPosition(SkinDesignCanvas);
        skinDragElementStart = new Point(placement.X, placement.Y);
        Document.BeginEditTransaction();
        args.Pointer.Capture(SkinDesignCanvas);
        args.Handled = true;
    }

    private void SkinElementPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (sender is not Border { Tag: PedalSkinElementDraft element } ||
            !args.GetCurrentPoint(SkinDesignCanvas).Properties.IsLeftButtonPressed)
        {
            return;
        }
        SelectSkinElement(element);
        SkinSelectionText.Text = $"{element.DisplayName} · {element.Id} · {element.X:0}, {element.Y:0}";
        draggingSkinElement = true;
        skinDragPointerStart = args.GetPosition(SkinDesignCanvas);
        skinDragElementStart = new Point(element.X, element.Y);
        Document.BeginEditTransaction();
        args.Pointer.Capture(SkinDesignCanvas);
        args.Handled = true;
    }

    private void SkinFootswitchPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (!args.GetCurrentPoint(SkinDesignCanvas).Properties.IsLeftButtonPressed) return;
        ClearSkinSelection();
        skinFootswitchSelected = true;
        SkinFootswitchVisual.BorderBrush = AuditionInputBrush;
        SkinFootswitchVisual.BorderThickness = new Thickness(1);
        SkinFootswitchVisual.CornerRadius = new CornerRadius(4);
        SkinSelectionText.Text = "踩钉 · 拖动以设置自定义位置";
        draggingSkinElement = true;
        skinDragPointerStart = args.GetPosition(SkinDesignCanvas);
        skinDragElementStart = new Point(
            Document.Skin.UseCustomFootswitchPosition ? Document.Skin.FootswitchX : Document.Skin.Width * 0.5,
            Document.Skin.UseCustomFootswitchPosition ? Document.Skin.FootswitchY : Document.Skin.Height - 28.0);
        Document.BeginEditTransaction();
        Document.Skin.UseCustomFootswitchPosition = true;
        Document.Skin.FootswitchX = skinDragElementStart.X;
        Document.Skin.FootswitchY = skinDragElementStart.Y;
        args.Pointer.Capture(SkinDesignCanvas);
        args.Handled = true;
    }

    private void SkinCanvasPointerMoved(object? sender, PointerEventArgs args)
    {
        if (!draggingSkinElement || args.Pointer.Captured != SkinDesignCanvas) return;
        var pointer = args.GetPosition(SkinDesignCanvas);
        var x = SnapSkinCoordinate(skinDragElementStart.X + pointer.X - skinDragPointerStart.X);
        var y = SnapSkinCoordinate(skinDragElementStart.Y + pointer.Y - skinDragPointerStart.Y);
        if (selectedSkinElement is not null)
        {
            selectedSkinElement.X = Math.Clamp(x, 0.0, Math.Max(0.0, Document.Skin.Width - selectedSkinElement.Width));
            selectedSkinElement.Y = Math.Clamp(y, 0.0, Math.Max(0.0, Document.Skin.Height - selectedSkinElement.Height));
            SkinSelectionText.Text = $"{selectedSkinElement.DisplayName} · {selectedSkinElement.Id} · " +
                                     $"{selectedSkinElement.X:0}, {selectedSkinElement.Y:0}";
        }
        else if (selectedSkinPlacement is not null)
        {
            selectedSkinPlacement.X = Math.Clamp(x, 26.0, Math.Max(26.0, Document.Skin.Width - 26.0));
            selectedSkinPlacement.Y = Math.Clamp(y, 26.0, Math.Max(26.0, Document.Skin.Height - 26.0));
            SkinSelectionText.Text = $"{selectedSkinPlacement.ParameterId} · " +
                                     $"{selectedSkinPlacement.ControlKindDisplay} · " +
                                     $"{selectedSkinPlacement.X:0}, {selectedSkinPlacement.Y:0}";
        }
        else if (skinFootswitchSelected)
        {
            Document.Skin.FootswitchX = Math.Clamp(x, 17.0, Math.Max(17.0, Document.Skin.Width - 17.0));
            Document.Skin.FootswitchY = Math.Clamp(y, 17.0, Math.Max(17.0, Document.Skin.Height - 17.0));
            SkinSelectionText.Text = $"踩钉 · {Document.Skin.FootswitchX:0}, {Document.Skin.FootswitchY:0}";
        }
        args.Handled = true;
    }

    private void SkinCanvasPointerReleased(object? sender, PointerReleasedEventArgs args)
    {
        if (!draggingSkinElement) return;
        draggingSkinElement = false;
        if (args.Pointer.Captured == SkinDesignCanvas) args.Pointer.Capture(null);
        Document.EndEditTransaction();
        args.Handled = true;
    }

    private double SnapSkinCoordinate(double value) =>
        SkinSnapToggle.IsChecked == false ? value : Math.Round(value / 8.0) * 8.0;

    private void SelectSkinPlacement(PedalSkinParameterPlacementDraft placement)
    {
        ClearSkinSelection();
        selectedSkinPlacement = placement;
        placement.IsSelected = true;
        SkinSelectionText.Text = $"{placement.ParameterId} · {placement.ControlKindDisplay} · {placement.X:0}, {placement.Y:0}";
    }

    private void SelectSkinElement(PedalSkinElementDraft element)
    {
        ClearSkinSelection();
        selectedSkinElement = element;
        SkinSelectionText.Text = $"{element.DisplayName} · {element.Id} · {element.X:0}, {element.Y:0}";
        RefreshSkinElementInspector();
    }

    private void ClearSkinSelection()
    {
        if (selectedSkinPlacement is not null) selectedSkinPlacement.IsSelected = false;
        selectedSkinPlacement = null;
        selectedSkinElement = null;
        skinFootswitchSelected = false;
        SkinFootswitchVisual.BorderBrush = null;
        SkinFootswitchVisual.BorderThickness = default;
        SkinSelectionText.Text = "未选择控件";
        RefreshSkinElementInspector();
    }

    private void RefreshSkinElementInspector()
    {
        if (SkinElementIdEditor is null)
        {
            return;
        }
        updatingSkinInspector = true;
        try
        {
            var element = selectedSkinElement;
            SkinElementInspector.IsVisible = element is not null;
            SkinElementEmptyText.IsVisible = element is null;
            SkinElementIdEditor.Text = element?.Id ?? string.Empty;
            SkinElementBindingEditor.Text = element?.Binding ?? string.Empty;
            SkinElementTypeSelector.SelectedItem = element is null
                ? null : PedalControlCatalog.Get(element.Type);
            SkinElementX.Value = element is null ? null : (decimal?)element.X;
            SkinElementY.Value = element is null ? null : (decimal?)element.Y;
            SkinElementWidth.Value = element is null ? null : (decimal?)element.Width;
            SkinElementHeight.Value = element is null ? null : (decimal?)element.Height;
            SkinElementRotation.Value = element is null ? null : (decimal?)element.Rotation;
            SkinElementDetents.Value = element is null ? null : (decimal?)element.Detents;
            SkinElementZIndex.Value = element is null ? null : (decimal?)element.ZIndex;
            SkinElementLabelEditor.Text = element?.Label ?? string.Empty;
        }
        finally
        {
            updatingSkinInspector = false;
        }
    }

    private void AddSkinElementClick(object? sender, RoutedEventArgs args)
    {
        if (sender is not Button { Tag: PedalControlDescriptor descriptor })
        {
            return;
        }
        var existingIds = Document.Skin.Elements.Select(element => element.Id);
        var id = "element-1";
        for (var index = 1; existingIds.Contains(id, StringComparer.Ordinal); index++)
        {
            id = $"element-{index + 1}";
        }
        var x = Math.Clamp((Document.Skin.Width - descriptor.DefaultWidth) * 0.5, 0.0,
            Math.Max(0.0, Document.Skin.Width - descriptor.DefaultWidth));
        var y = Math.Clamp((Document.Skin.Height - descriptor.DefaultHeight) * 0.5, 0.0,
            Math.Max(0.0, Document.Skin.Height - descriptor.DefaultHeight));
        Document.BeginEditTransaction();
        PedalSkinElementDraft element;
        try
        {
            element = new PedalSkinElementDraft(id, descriptor.Type, string.Empty, x, y);
            Document.Skin.Elements.Add(element);
        }
        finally
        {
            Document.EndEditTransaction();
        }
        SelectSkinElement(element);
    }

    private void SkinElementIdChanged(object? sender, TextChangedEventArgs args)
    {
        if (!updatingSkinInspector && selectedSkinElement is not null)
        {
            selectedSkinElement.Id = SkinElementIdEditor.Text ?? string.Empty;
        }
    }

    private void SkinElementBindingChanged(object? sender, TextChangedEventArgs args)
    {
        if (!updatingSkinInspector && selectedSkinElement is not null)
        {
            selectedSkinElement.Binding = SkinElementBindingEditor.Text ?? string.Empty;
        }
    }

    private void SkinElementLabelChanged(object? sender, TextChangedEventArgs args)
    {
        if (!updatingSkinInspector && selectedSkinElement is not null)
        {
            selectedSkinElement.Label = SkinElementLabelEditor.Text ?? string.Empty;
        }
    }

    private void SkinElementTypeChanged(object? sender, SelectionChangedEventArgs args)
    {
        if (!updatingSkinInspector && selectedSkinElement is not null &&
            SkinElementTypeSelector.SelectedItem is PedalControlDescriptor descriptor)
        {
            selectedSkinElement.Type = descriptor.Type;
            RefreshSkinElementInspector();
        }
    }

    private void SkinElementNumberChanged(object? sender, NumericUpDownValueChangedEventArgs args)
    {
        if (updatingSkinInspector || selectedSkinElement is null || sender is not NumericUpDown field ||
            !field.Value.HasValue)
        {
            return;
        }
        var value = (double)field.Value.Value;
        switch (field.Tag as string)
        {
            case "x": selectedSkinElement.X = value; break;
            case "y": selectedSkinElement.Y = value; break;
            case "width": selectedSkinElement.Width = value; break;
            case "height": selectedSkinElement.Height = value; break;
            case "rotation": selectedSkinElement.Rotation = value; break;
            case "detents": selectedSkinElement.Detents = (int)Math.Round(value); break;
            case "z-index": selectedSkinElement.ZIndex = (int)Math.Round(value); break;
        }
    }

    private void DuplicateSkinPlacementClick(object? sender, RoutedEventArgs args)
    {
        if (selectedSkinPlacement is null)
        {
            Document.ReportStatus("请先选择一个参数控件");
            return;
        }
        var nextParameter = Document.Parameters.FirstOrDefault(parameter =>
            Document.Skin.ParameterPlacements.All(placement =>
                !string.Equals(placement.ParameterId, parameter.Id, StringComparison.Ordinal)));
        if (nextParameter is null)
        {
            Document.ReportStatus("没有尚未布局的参数可用于复制");
            return;
        }
        var previous = selectedSkinPlacement;
        var duplicate = Document.AddSkinParameterPlacement();
        duplicate.X = Math.Clamp(SnapSkinCoordinate(previous.X + 32.0), 26.0,
            Math.Max(26.0, Document.Skin.Width - 26.0));
        duplicate.Y = Math.Clamp(SnapSkinCoordinate(previous.Y + 32.0), 26.0,
            Math.Max(26.0, Document.Skin.Height - 26.0));
        SelectSkinPlacement(duplicate);
    }

    private void DeleteSelectedSkinElementClick(object? sender, RoutedEventArgs args) =>
        DeleteSelectedSkinElement();

    private void DeleteSelectedSkinElement()
    {
        if (selectedSkinElement is not null)
        {
            var selected = selectedSkinElement;
            ClearSkinSelection();
            Document.Skin.Elements.Remove(selected);
        }
        else if (selectedSkinPlacement is not null)
        {
            var selected = selectedSkinPlacement;
            ClearSkinSelection();
            Document.RemoveSkinParameterPlacement(selected);
        }
        else if (skinFootswitchSelected)
        {
            ClearSkinSelection();
            Document.Skin.UseCustomFootswitchPosition = false;
        }
    }

    private void AlignSkinLeftClick(object? sender, RoutedEventArgs args) =>
        MoveSelectedSkinElement(26.0, null);

    private void AlignSkinCenterClick(object? sender, RoutedEventArgs args) =>
        MoveSelectedSkinElement(Document.Skin.Width * 0.5, null);

    private void AlignSkinTopClick(object? sender, RoutedEventArgs args) =>
        MoveSelectedSkinElement(null, 42.0);

    private void MoveSelectedSkinElement(double? x, double? y)
    {
        if (selectedSkinElement is not null)
        {
            if (x.HasValue) selectedSkinElement.X = Math.Clamp(SnapSkinCoordinate(x.Value), 0.0,
                Math.Max(0.0, Document.Skin.Width - selectedSkinElement.Width));
            if (y.HasValue) selectedSkinElement.Y = Math.Clamp(SnapSkinCoordinate(y.Value), 0.0,
                Math.Max(0.0, Document.Skin.Height - selectedSkinElement.Height));
            SkinSelectionText.Text = $"{selectedSkinElement.DisplayName} · {selectedSkinElement.Id} · " +
                                     $"{selectedSkinElement.X:0}, {selectedSkinElement.Y:0}";
        }
        else if (selectedSkinPlacement is not null)
        {
            if (x.HasValue) selectedSkinPlacement.X = SnapSkinCoordinate(x.Value);
            if (y.HasValue) selectedSkinPlacement.Y = SnapSkinCoordinate(y.Value);
            SelectSkinPlacement(selectedSkinPlacement);
        }
        else if (skinFootswitchSelected)
        {
            Document.Skin.UseCustomFootswitchPosition = true;
            if (x.HasValue) Document.Skin.FootswitchX = SnapSkinCoordinate(x.Value);
            if (y.HasValue) Document.Skin.FootswitchY = SnapSkinCoordinate(y.Value);
        }
    }

    private void BringSkinPlacementFrontClick(object? sender, RoutedEventArgs args)
    {
        if (selectedSkinElement is not null)
        {
            selectedSkinElement.ZIndex = Document.Skin.Elements.Count == 0
                ? 0 : Document.Skin.Elements.Max(element => element.ZIndex) + 1;
            SkinSelectionText.Text = $"{selectedSkinElement.DisplayName} · {selectedSkinElement.Id} · 置顶";
            return;
        }
        if (selectedSkinPlacement is null) return;
        var selected = selectedSkinPlacement;
        var index = Document.Skin.ParameterPlacements.IndexOf(selected);
        if (index < 0 || index == Document.Skin.ParameterPlacements.Count - 1) return;
        Document.BeginEditTransaction();
        Document.Skin.ParameterPlacements.RemoveAt(index);
        Document.Skin.ParameterPlacements.Add(selected);
        Document.EndEditTransaction();
        SelectSkinPlacement(selected);
    }

    private void RemovePortClick(object? sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: ModulePortDraft port })
        {
            Document.RemovePort(port);
        }
    }

    private void RemoveParameterClick(object? sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: ModuleParameterDraft parameter })
        {
            Document.RemoveParameter(parameter);
        }
    }

    private void RemoveDigitalOperationClick(object? sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: DigitalOperationDraft operation })
        {
            Document.RemoveDigitalOperation(operation);
        }
    }

    private void RemoveAnalogComponentClick(object? sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: AnalogComponentDraft component })
        {
            if (selectedCircuitComponents.Contains(component))
            {
                selectedCircuitComponents.Remove(component);
                if (selectedCircuitCards.Remove(component, out var card))
                {
                    card.BorderThickness = new Thickness(1);
                }
                selectedCircuitComponent = selectedCircuitComponents.LastOrDefault();
                selectedCircuitCard = selectedCircuitComponent is not null &&
                                      selectedCircuitCards.TryGetValue(selectedCircuitComponent, out var selectedCard)
                    ? selectedCard
                    : null;
            }
            Document.RemoveAnalogComponent(component);
            UpdateCircuitSelectionStatus();
        }
    }

    private void RemoveAnalogPortClick(object? sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: AnalogCircuitPortDraft port })
        {
            Document.RemoveAnalogCircuitPort(port);
        }
    }

    private void RemoveAnalogSupplyClick(object? sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: AnalogSupplyDraft supply })
        {
            Document.RemoveAnalogSupply(supply);
        }
    }

    private void RemoveAnalogBindingClick(object? sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: AnalogParameterBindingDraft binding })
        {
            Document.RemoveAnalogParameterBinding(binding);
        }
    }

    private void RemoveSkinParameterPlacementClick(object? sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: PedalSkinParameterPlacementDraft placement })
        {
            Document.RemoveSkinParameterPlacement(placement);
        }
    }

    private void ImportSkinImageClick(object? sender, RoutedEventArgs args)
    {
        if (sender is Button { Tag: string kind }) _ = ImportSkinImageAsync(kind);
    }

    private async Task ImportSkinImageAsync(string kind)
    {
        if (Document.IsDirty || !Document.HasProjectDirectory)
        {
            if (!await SaveAsync()) return;
        }
        if (Document.ProjectDirectory is null) return;
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null) return;
        var files = await storage.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "导入踏板控件图像",
            AllowMultiple = false,
            FileTypeFilter = [SkinImageFileType],
        });
        var source = files.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(source))
        {
            Document.ReportStatus("已取消导入控件图像");
            return;
        }
        try
        {
            var relative = ModulePackageWorkflow.ImportProjectResource(
                Document.ProjectDirectory, source, "ui/images");
            var skinRelative = relative.StartsWith("ui/", StringComparison.OrdinalIgnoreCase)
                ? relative[3..] : relative;
            switch (kind)
            {
                case "toggle": Document.Skin.ToggleImage = skinRelative; break;
                case "button": Document.Skin.ButtonImage = skinRelative; break;
                case "footswitch": Document.Skin.FootswitchImage = skinRelative; break;
                default: Document.Skin.KnobImage = skinRelative; break;
            }
            Document.ReportStatus($"已导入控件图像 {skinRelative}");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            Document.ReportStatus($"资源导入失败：{exception.Message}");
        }
    }

    private void ToggleAnalogEditorModeClick(object? sender, RoutedEventArgs args) =>
        Document.SetAnalogStructuredMode(!Document.AnalogStructuredMode);

    private void ValidateProjectClick(object? sender, RoutedEventArgs args)
    {
        Document.RefreshValidation();
        if (Document.IsValid && Document.HasProjectDirectory)
        {
            ValidateNativePackage();
        }
        else
        {
            Document.ReportStatus(Document.ValidationSummary);
        }
        WorkshopTabs.SelectedIndex = 4;
    }

    private bool ValidateNativePackage()
    {
        if (Document.ProjectDirectory is null)
        {
            return Document.IsValid;
        }
        try
        {
            using var backend = new BackendSession();
            Document.SetWhiteboxRuntimeStatus(backend.ReadWhiteboxStatus(), backend.ReadRealtimeContract());
            backend.ValidateModulePackage(Document.ProjectDirectory);
            Document.SetNativeValidationResult(true, string.Empty);
            return true;
        }
        catch (Exception exception) when (exception is BackendException or DllNotFoundException or EntryPointNotFoundException)
        {
            Document.SetNativeValidationResult(false, exception.Message);
            WorkshopTabs.SelectedIndex = 4;
            return false;
        }
    }

    private void AuditionProjectClick(object? sender, RoutedEventArgs args) => _ = AuditionProjectAsync();

    private void SelectAuditionFileClick(object? sender, RoutedEventArgs args) => _ = SelectAuditionFileAsync();

    private async Task SelectAuditionFileAsync()
    {
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            Document.SetAuditionFailure("无法访问文件选择器");
            return;
        }
        var files = await storage.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "选择离线试听 WAV",
            AllowMultiple = false,
            FileTypeFilter = [WaveFileType],
        });
        var path = files.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            Document.ReportStatus("已取消选择试听文件");
            return;
        }
        try
        {
            Document.SetAuditionWaveFile(AuditionWaveFile.Load(path));
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                          InvalidDataException or NotSupportedException)
        {
            Document.SetAuditionFailure(exception.Message);
        }
        WorkshopTabs.SelectedIndex = 4;
    }

    private void AuditionWaveformSizeChanged(object? sender, SizeChangedEventArgs args) =>
        RenderAuditionWaveforms();

    private void FrequencyResponseSizeChanged(object? sender, SizeChangedEventArgs args) =>
        RenderFrequencyResponse();

    private void ParameterAnalysisSizeChanged(object? sender, SizeChangedEventArgs args) =>
        RenderParameterAnalysis();

    private void RenderAuditionWaveforms()
    {
        var analysis = Document.AuditionAnalysis;
        var scale = analysis is null ? 1.0 : Math.Max(analysis.InputPeak, analysis.OutputPeak);
        if (!double.IsFinite(scale) || scale < 0.000001)
        {
            scale = 1.0;
        }
        RenderWaveform(AuditionInputWaveform, analysis?.InputWaveform ?? [], scale, AuditionInputBrush);
        RenderWaveform(AuditionOutputWaveform, analysis?.OutputWaveform ?? [], scale, AuditionOutputBrush);
    }

    private static void RenderWaveform(Canvas canvas, IReadOnlyList<float> samples, double scale, IBrush stroke)
    {
        canvas.Children.Clear();
        var width = Math.Max(canvas.Bounds.Width, canvas.MinWidth);
        var height = Math.Max(canvas.Bounds.Height, canvas.Height);
        var middle = height * 0.5;
        canvas.Children.Add(new Line
        {
            StartPoint = new Point(0, middle),
            EndPoint = new Point(width, middle),
            Stroke = AuditionBaselineBrush,
            StrokeThickness = 1,
        });
        if (samples.Count < 2)
        {
            return;
        }
        AppendWaveform(canvas, samples, scale, stroke, width, height);
    }

    private static void AppendWaveform(Canvas canvas, IReadOnlyList<float> samples,
        double scale, IBrush stroke, double width, double height)
    {
        if (samples.Count < 2) return;
        var maxSegments = Math.Max(2, (int)Math.Min(512.0, width));
        var step = Math.Max(1, (samples.Count - 1 + maxSegments - 1) / maxSegments);
        var previousIndex = 0;
        var previous = WaveformPoint(samples, previousIndex, width, height, scale);
        for (var index = step; index < samples.Count; index += step)
        {
            var currentIndex = Math.Min(index, samples.Count - 1);
            var current = WaveformPoint(samples, currentIndex, width, height, scale);
            canvas.Children.Add(new Line
            {
                StartPoint = previous,
                EndPoint = current,
                Stroke = stroke,
                StrokeThickness = 1.4,
            });
            previousIndex = currentIndex;
            previous = current;
        }
        if (previousIndex != samples.Count - 1)
        {
            canvas.Children.Add(new Line
            {
                StartPoint = previous,
                EndPoint = WaveformPoint(samples, samples.Count - 1, width, height, scale),
                Stroke = stroke,
                StrokeThickness = 1.4,
            });
        }
    }

    private static Point WaveformPoint(IReadOnlyList<float> samples, int index,
        double width, double height, double scale)
    {
        var x = width * index / Math.Max(1, samples.Count - 1);
        var normalized = Math.Clamp(samples[index] / scale, -1.0, 1.0);
        return new Point(x, height * 0.5 - normalized * height * 0.44);
    }

    private void RenderFrequencyResponse()
    {
        var points = Document.FrequencyResponse?.Points ?? [];
        var minimumMagnitude = points.Count == 0 ? -24.0 : points.Min(point => point.MagnitudeDb);
        var maximumMagnitude = points.Count == 0 ? 6.0 : points.Max(point => point.MagnitudeDb);
        var magnitudePadding = Math.Max(3.0, (maximumMagnitude - minimumMagnitude) * 0.12);
        RenderResponsePlot(FrequencyMagnitudePlot, points, point => point.MagnitudeDb,
            Math.Floor((minimumMagnitude - magnitudePadding) / 6.0) * 6.0,
            Math.Ceiling((maximumMagnitude + magnitudePadding) / 6.0) * 6.0,
            FrequencyMagnitudeBrush);
        RenderResponsePlot(FrequencyPhasePlot, points, point => point.PhaseDegrees,
            -180.0, 180.0, FrequencyPhaseBrush);
    }

    private static void RenderResponsePlot(Canvas canvas, IReadOnlyList<ModuleFrequencyResponsePoint> points,
        Func<ModuleFrequencyResponsePoint, double> value, double minimum, double maximum, IBrush stroke)
    {
        canvas.Children.Clear();
        var width = Math.Max(canvas.Bounds.Width, canvas.MinWidth);
        var height = Math.Max(canvas.Bounds.Height, canvas.Height);
        for (var index = 1; index < 5; index++)
        {
            var y = height * index / 5.0;
            canvas.Children.Add(new Line
            {
                StartPoint = new Point(0, y),
                EndPoint = new Point(width, y),
                Stroke = AuditionBaselineBrush,
                StrokeThickness = 1,
            });
        }
        if (points.Count < 2 || maximum <= minimum)
        {
            return;
        }
        var startLog = Math.Log10(points[0].FrequencyHz);
        var endLog = Math.Log10(points[^1].FrequencyHz);
        Point Map(ModuleFrequencyResponsePoint point)
        {
            var x = width * (Math.Log10(point.FrequencyHz) - startLog) / Math.Max(1.0e-12, endLog - startLog);
            var normalized = Math.Clamp((value(point) - minimum) / (maximum - minimum), 0.0, 1.0);
            return new Point(x, height * (1.0 - normalized));
        }
        var previous = Map(points[0]);
        for (var index = 1; index < points.Count; index++)
        {
            var current = Map(points[index]);
            canvas.Children.Add(new Line
            {
                StartPoint = previous,
                EndPoint = current,
                Stroke = stroke,
                StrokeThickness = 1.8,
            });
            previous = current;
        }
    }

    private void RenderParameterAnalysis()
    {
        RenderParameterSweepPlot();
        var comparison = Document.AuditionComparison;
        AuditionComparisonPlot.Children.Clear();
        var width = Math.Max(AuditionComparisonPlot.Bounds.Width, AuditionComparisonPlot.MinWidth);
        var height = Math.Max(AuditionComparisonPlot.Bounds.Height, AuditionComparisonPlot.Height);
        var middle = height * 0.5;
        AuditionComparisonPlot.Children.Add(new Line
        {
            StartPoint = new Point(0, middle), EndPoint = new Point(width, middle),
            Stroke = AuditionBaselineBrush, StrokeThickness = 1,
        });
        if (comparison is null) return;
        var scale = Math.Max(comparison.ResultA.OutputPeak, comparison.ResultB.OutputPeak);
        if (!double.IsFinite(scale) || scale < 0.000001) scale = 1.0;
        AppendWaveform(AuditionComparisonPlot, comparison.ResultA.OutputWaveform,
            scale, ComparisonABrush, width, height);
        AppendWaveform(AuditionComparisonPlot, comparison.ResultB.OutputWaveform,
            scale, ComparisonBBrush, width, height);
    }

    private void RenderParameterSweepPlot()
    {
        ParameterSweepPlot.Children.Clear();
        var width = Math.Max(ParameterSweepPlot.Bounds.Width, ParameterSweepPlot.MinWidth);
        var height = Math.Max(ParameterSweepPlot.Bounds.Height, ParameterSweepPlot.Height);
        for (var index = 1; index < 5; index++)
        {
            var y = height * index / 5.0;
            ParameterSweepPlot.Children.Add(new Line
            {
                StartPoint = new Point(0, y), EndPoint = new Point(width, y),
                Stroke = AuditionBaselineBrush, StrokeThickness = 1,
            });
        }
        var sweep = Document.ParameterSweep;
        if (sweep is null || sweep.Points.Count < 2) return;
        var minimumGain = sweep.Points.Min(point => point.GainDb);
        var maximumGain = sweep.Points.Max(point => point.GainDb);
        if (maximumGain - minimumGain < 1.0)
        {
            minimumGain -= 0.5;
            maximumGain += 0.5;
        }
        Point Map(ModuleParameterSweepPoint point)
        {
            var x = width * (point.ParameterValue - sweep.Minimum) /
                    Math.Max(1.0e-12, sweep.Maximum - sweep.Minimum);
            var y = height * (1.0 - (point.GainDb - minimumGain) /
                Math.Max(1.0e-12, maximumGain - minimumGain));
            return new Point(x, y);
        }
        var previous = Map(sweep.Points[0]);
        for (var index = 1; index < sweep.Points.Count; index++)
        {
            var current = Map(sweep.Points[index]);
            ParameterSweepPlot.Children.Add(new Line
            {
                StartPoint = previous, EndPoint = current,
                Stroke = FrequencyMagnitudeBrush, StrokeThickness = 1.8,
            });
            previous = current;
        }
    }

    private void MeasureParameterSweepClick(object? sender, RoutedEventArgs args) =>
        _ = MeasureParameterSweepAsync();

    private void CompareAuditionParameterClick(object? sender, RoutedEventArgs args) =>
        _ = CompareAuditionParameterAsync();

    private async Task MeasureParameterSweepAsync()
    {
        var parameter = Document.SelectedAuditionParameter;
        if (parameter is null)
        {
            Document.SetParameterSweepFailure("当前项目没有可扫动参数");
            return;
        }
        var ready = Document.IsDirty || !Document.HasProjectDirectory
            ? await SaveAsync() : ValidateNativePackage();
        if (!ready || Document.ProjectDirectory is null) return;
        try
        {
            using var backend = new BackendSession();
            Document.SetParameterSweepResult(backend.MeasureModuleParameterSweep(
                Document.ProjectDirectory, Document.CreateAuditionRequest(), parameter.Id,
                parameter.Minimum, parameter.Maximum));
        }
        catch (Exception exception) when (exception is BackendException or DllNotFoundException or
                                          EntryPointNotFoundException or ArgumentException or InvalidOperationException)
        {
            Document.SetParameterSweepFailure(exception.Message);
        }
        WorkshopTabs.SelectedIndex = 4;
    }

    private async Task CompareAuditionParameterAsync()
    {
        var parameter = Document.SelectedAuditionParameter;
        if (parameter is null)
        {
            Document.SetAuditionComparisonFailure("当前项目没有可对比参数");
            return;
        }
        var ready = Document.IsDirty || !Document.HasProjectDirectory
            ? await SaveAsync() : ValidateNativePackage();
        if (!ready || Document.ProjectDirectory is null) return;
        try
        {
            using var backend = new BackendSession();
            Document.SetAuditionComparisonResult(backend.CompareModuleParameter(
                Document.ProjectDirectory, Document.CreateAuditionRequest(), parameter.Id,
                Document.AuditionParameterValueA, Document.AuditionParameterValueB));
        }
        catch (Exception exception) when (exception is BackendException or DllNotFoundException or
                                          EntryPointNotFoundException or ArgumentException or InvalidOperationException)
        {
            Document.SetAuditionComparisonFailure(exception.Message);
        }
        WorkshopTabs.SelectedIndex = 4;
    }

    private void MeasureFrequencyResponseClick(object? sender, RoutedEventArgs args) =>
        _ = MeasureFrequencyResponseAsync();

    private async Task MeasureFrequencyResponseAsync()
    {
        var ready = Document.IsDirty || !Document.HasProjectDirectory
            ? await SaveAsync()
            : ValidateNativePackage();
        if (!ready || Document.ProjectDirectory is null)
        {
            WorkshopTabs.SelectedIndex = 4;
            return;
        }
        try
        {
            using var backend = new BackendSession();
            Document.SetFrequencyResponseResult(backend.MeasureModuleFrequencyResponse(
                Document.ProjectDirectory, Document.CreateAuditionRequest()));
        }
        catch (Exception exception) when (exception is BackendException or DllNotFoundException or
                                          EntryPointNotFoundException or ArgumentException or InvalidOperationException)
        {
            Document.SetFrequencyResponseFailure(exception.Message);
        }
        WorkshopTabs.SelectedIndex = 4;
    }

    private async Task AuditionProjectAsync()
    {
        var ready = Document.IsDirty || !Document.HasProjectDirectory
            ? await SaveAsync()
            : ValidateNativePackage();
        if (!ready || Document.ProjectDirectory is null)
        {
            WorkshopTabs.SelectedIndex = 4;
            return;
        }
        try
        {
            using var backend = new BackendSession();
            Document.SetAuditionResult(backend.AuditionModulePackage(
                Document.ProjectDirectory, Document.CreateAuditionRequest()));
        }
        catch (Exception exception) when (exception is BackendException or DllNotFoundException or
                                          EntryPointNotFoundException or ArgumentException or InvalidOperationException)
        {
            Document.SetAuditionFailure(exception.Message);
        }
        WorkshopTabs.SelectedIndex = 4;
    }

    private void SelectSourceAssetClick(object? sender, RoutedEventArgs args) => _ = SelectSourceAssetAsync();

    private async Task SelectSourceAssetAsync()
    {
        if (!Document.UsesExternalAsset)
        {
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            Document.ReportStatus("无法访问文件选择器");
            return;
        }
        var files = await storage.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = Document.Kind == ModuleAuthoringKind.NeuralAmpModel ? "选择 NAM / NAM A2 模型" : "选择 WAV 脉冲响应",
            AllowMultiple = false,
            FileTypeFilter = [Document.Kind == ModuleAuthoringKind.NeuralAmpModel ? NamFileType : WaveFileType],
        });
        var path = files.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            Document.ReportStatus("已取消选择资源");
            return;
        }
        Document.SetSourceAsset(path);
    }

    private void SelectSkinFileClick(object? sender, RoutedEventArgs args) => _ = SelectSkinFileAsync();

    private async Task SelectSkinFileAsync()
    {
        if (Document.ProjectDirectory is null)
        {
            Document.ReportStatus("请先保存项目，再选择项目目录内的外观文件");
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            Document.ReportStatus("无法访问文件选择器");
            return;
        }
        var files = await storage.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "选择项目内的 pedal.json",
            AllowMultiple = false,
            FileTypeFilter = [PedalSkinFileType],
        });
        var path = files.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            Document.ReportStatus("已取消选择外观文件");
            return;
        }
        var relative = Path.GetRelativePath(Document.ProjectDirectory, path).Replace('\\', '/');
        if (relative.StartsWith("../", StringComparison.Ordinal) || Path.IsPathRooted(relative))
        {
            Document.ReportStatus("外观文件必须位于当前项目目录内");
            return;
        }
        Document.SkinFile = relative;
        Document.ReportStatus($"已选择外观文件 {relative}");
    }
}
