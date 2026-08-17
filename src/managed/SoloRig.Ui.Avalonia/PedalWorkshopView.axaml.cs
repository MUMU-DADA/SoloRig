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
using System.Text.Json;
using Ellipse = Avalonia.Controls.Shapes.Ellipse;
using Line = Avalonia.Controls.Shapes.Line;

namespace SoloRig.Ui.Avalonia;

public sealed partial class PedalWorkshopView : UserControl
{
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
        string Error,
        string GridMinor,
        string GridMajor);

    private static readonly WorkshopThemePalette DarkPalette = new(
        "#11151B", "#161B22", "#191F27", "#10151A", "#222A34", "#2A3440", "#394655",
        "#EEF3F8", "#9AA8B8", "#4DD6A4", "#10261F", "#FF7B72", "#1B2530", "#263542");

    private static readonly WorkshopThemePalette LightPalette = new(
        "#F4F7FB", "#FFFFFF", "#EEF3F7", "#F8FAFC", "#FFFFFF", "#E8F0F6", "#C7D2DE",
        "#172033", "#5F6B78", "#0F766E", "#FFFFFF", "#B42318", "#DDE7F0", "#C7D2DE");

    private const double CircuitCanvasWidth = 2400.0;
    private const double CircuitCanvasHeight = 1600.0;
    private const double CircuitCardWidth = 190.0;
    private const double CircuitCardHeight = 140.0;
    private const double CircuitGridSize = 16.0;
    private const double MinimumCircuitZoom = 0.35;
    private const double MaximumCircuitZoom = 2.5;
    private static readonly IBrush AuditionBaselineBrush = new SolidColorBrush(Color.Parse("#394655"));
    private static readonly IBrush AuditionInputBrush = new SolidColorBrush(Color.Parse("#4DD6A4"));
    private static readonly IBrush AuditionOutputBrush = new SolidColorBrush(Color.Parse("#FFB454"));
    private static readonly IBrush FrequencyMagnitudeBrush = new SolidColorBrush(Color.Parse("#FFD166"));
    private static readonly IBrush FrequencyPhaseBrush = new SolidColorBrush(Color.Parse("#61AFEF"));
    private static readonly IBrush ComparisonABrush = new SolidColorBrush(Color.Parse("#4DD6A4"));
    private static readonly IBrush ComparisonBBrush = new SolidColorBrush(Color.Parse("#61AFEF"));
    private bool compactLayout;
    private ThemeSnapshot workshopTheme = ThemeSnapshot.Empty;
    private bool workshopDark = true;
    private IBrush circuitMinorGridBrush = new SolidColorBrush(Color.Parse("#1B2530"));
    private IBrush circuitMajorGridBrush = new SolidColorBrush(Color.Parse("#263542"));
    private double circuitZoom = 1.0;
    private double circuitPanX;
    private double circuitPanY;
    private bool panningCircuit;
    private bool selectingCircuitBox;
    private bool draggingCircuitPalette;
    private Point circuitPointerStart;
    private Point circuitSelectionStart;
    private Point circuitPreviewPointer;
    private double circuitPanStartX;
    private double circuitPanStartY;
    private AnalogComponentDraft? draggingCircuitComponent;
    private AnalogComponentDraft? selectedCircuitComponent;
    private readonly HashSet<AnalogComponentDraft> selectedCircuitComponents = [];
    private readonly Dictionary<AnalogComponentDraft, Border> selectedCircuitCards = [];
    private readonly Dictionary<AnalogComponentDraft, Point> draggingCircuitStarts = [];
    private readonly List<AnalogComponentDraft> circuitClipboard = [];
    private double circuitClipboardPasteOffset;
    private AnalogPinDraft? pendingCircuitPin;
    private AnalogComponentDescriptor? circuitPaletteDescriptor;
    private string? selectedCircuitNode;
    private Point draggingCircuitPointerStart;
    private Point circuitPalettePointerStart;
    private Border? selectedCircuitCard;
    private Ellipse? pendingCircuitPinVisual;
    private PedalSkinParameterPlacementDraft? selectedSkinPlacement;
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
        TemplateSelector.ItemsSource = ModuleAuthoringDocument.TemplateOptions;
        TemplateSelector.SelectedIndex = 0;
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
        SetBrush("WorkshopErrorBrush", palette.Error);
        circuitMinorGridBrush = new SolidColorBrush(Color.Parse(palette.GridMinor));
        circuitMajorGridBrush = new SolidColorBrush(Color.Parse(palette.GridMajor));
        if (CircuitGridLayer is not null)
        {
            CircuitGridLayer.Children.Clear();
            BuildCircuitGrid();
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

    public bool ProbeResponsiveLayout()
    {
        var originalWidth = Bounds.Width > 0 ? Bounds.Width : 1180.0;
        var originalCompact = compactLayout;
        try
        {
            UpdateResponsiveLayout(860.0);
            UpdateLayout();
            return compactLayout && Grid.GetRow(WorkshopActions) == 1 && Grid.GetColumn(WorkshopActions) == 0 &&
                   WorkshopActions.HorizontalAlignment == HorizontalAlignment.Left &&
                   WorkshopStatusGrid.ColumnDefinitions.Count == 4 &&
                   WorkshopStatusGrid.RowDefinitions.Count == 2 &&
                   Grid.GetRow(RecentProjectSearchBox) == 1 && Grid.GetColumn(RecentProjectSearchBox) == 0 &&
                   Grid.GetRow(RecentProjectSelector) == 1 && Grid.GetColumn(RecentProjectSelector) == 1 &&
                   Grid.GetRow(OpenRecentProjectButton) == 1 && Grid.GetColumn(OpenRecentProjectButton) == 2 &&
                   Grid.GetRow(ValidateProjectButton) == 1 && Grid.GetColumn(ValidateProjectButton) == 3 &&
                   SkinWorkspaceGrid.ColumnDefinitions.Count == 1 &&
                   SkinWorkspaceGrid.RowDefinitions.Count == 2 &&
                   Grid.GetRow(SkinPreviewPanel) == 1 && Grid.GetColumn(SkinPreviewPanel) == 0;
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
        UpdateResponsiveLayout(Bounds.Width);
        UpdateLayout();
        var circuitCanvasName = CircuitCanvasSurface.GetValue(AutomationProperties.NameProperty) ?? string.Empty;
        var skinCanvasName = SkinDesignCanvas.GetValue(AutomationProperties.NameProperty) ?? string.Empty;
        return Bounds.Width > 0 && Bounds.Width < 1000.0 && compactLayout &&
               WorkshopTopbarGrid.Bounds.Width > 0 && WorkshopActions.Bounds.Width > 0 &&
               WorkshopActions.Bounds.Right <= WorkshopTopbarGrid.Bounds.Width + 0.5 &&
               WorkshopStatusGrid.Bounds.Width > 0 && RecentProjectSearchBox.Bounds.Width > 0 &&
               RecentProjectSelector.Bounds.Width > 0 &&
               RecentProjectSearchBox.Bounds.Right <= WorkshopStatusGrid.Bounds.Width + 0.5 &&
               RecentProjectSelector.Bounds.Right <= WorkshopStatusGrid.Bounds.Width + 0.5 &&
               OpenRecentProjectButton.Bounds.Right <= WorkshopStatusGrid.Bounds.Width + 0.5 &&
               ValidateProjectButton.Bounds.Right <= WorkshopStatusGrid.Bounds.Width + 0.5 &&
               RecentProjectSearchBox.Bounds.Top >= WorkshopStatusText.Bounds.Bottom - 0.5 &&
               !string.IsNullOrWhiteSpace(circuitCanvasName) && !string.IsNullOrWhiteSpace(skinCanvasName);
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
        WorkshopTabs.SelectedIndex = 3;
        UpdateLayout();
        var result = Document.Skin.ParameterPlacements.Count == 2 &&
                     Document.Skin.ParameterPlacements[^1] == first &&
                     second.IsToggle && Math.Abs(second.X - 96.0) < 0.001 &&
                     Math.Abs(second.Y - 40.0) < 0.001 &&
                     Document.Skin.UseCustomFootswitchPosition &&
                     SkinDesignCanvas.Bounds.Width > 0 && SkinDesignCanvas.Bounds.Height > 0 &&
                     SkinPlacementLayer.ItemCount == 2;
        ClearSkinSelection();
        return result;
    }

    public bool ProbeCircuitCanvas()
    {
        Document.NewProject(ModuleAuthoringKind.AnalogCircuit);
        WorkshopTabs.SelectedIndex = 2;
        UpdateLayout();
        RenderCircuitConnections();
        var component = Document.AnalogCircuit.Components[0];
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
        pendingCircuitPin = Document.AnalogCircuit.Components[0].Pins[1];
        UpdateCircuitWirePreview(new Point(240.0, 160.0));
        var previewVisible = CircuitWirePreview.IsVisible;
        ClearPendingCircuitPin();
        selectedCircuitNode = "out";
        RenderCircuitConnections();
        var wireSelected = CircuitSelectionText.Text?.Contains("out", StringComparison.Ordinal) == true &&
                           CircuitWireLayer.Children.OfType<Line>().Any(line => line.StrokeThickness >= 4.0);
        ClearCircuitSelection();
        var diagnosticComponent = Document.AnalogCircuit.Components[0];
        var diagnosticNode = diagnosticComponent.Pins[0].Node;
        diagnosticComponent.Pins[0].Node = string.Empty;
        var diagnosticIssue = Document.ValidationIssues.FirstOrDefault(issue =>
            issue.Target == ModuleAuthoringIssueTarget.AnalogComponent &&
            ReferenceEquals(issue.TargetObject, diagnosticComponent));
        if (diagnosticIssue is not null)
        {
            NavigateToValidationIssue(diagnosticIssue);
            UpdateLayout();
        }
        var diagnosticNavigation = diagnosticIssue is not null && WorkshopTabs.SelectedIndex == 2 &&
                                   selectedCircuitComponents.Contains(diagnosticComponent);
        diagnosticComponent.Pins[0].Node = diagnosticNode;
        Document.RefreshValidation();
        return CircuitCanvasSurface.Bounds.Width > 0 && CircuitCanvasSurface.Bounds.Height > 0 &&
               CircuitNodeLayer.ItemCount == Document.AnalogCircuit.Components.Count &&
               CircuitWireLayer.Children.Count > 0 && moved && undone && paletteDrop && boxSelected &&
               previewVisible && wireSelected && diagnosticNavigation;
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
        var compact = width > 0 && width < 1000.0;
        if (compact == compactLayout)
        {
            return;
        }
        compactLayout = compact;
        WorkshopTopbarGrid.ColumnDefinitions.Clear();
        WorkshopTopbarGrid.RowDefinitions.Clear();
        WorkshopStatusGrid.ColumnDefinitions.Clear();
        WorkshopStatusGrid.RowDefinitions.Clear();
        SkinWorkspaceGrid.ColumnDefinitions.Clear();
        SkinWorkspaceGrid.RowDefinitions.Clear();
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
        }
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
        if (CircuitWireLayer is null)
        {
            return;
        }
        CircuitWireLayer.Children.Clear();
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
            var points = node.Select(endpoint => endpoint.Point).ToArray();
            if (points.Length < 2)
            {
                continue;
            }
            var stroke = new SolidColorBrush(string.Equals(node.Key, selectedCircuitNode, StringComparison.Ordinal)
                ? Color.Parse("#FFD166")
                : node.Key == Document.AnalogCircuit.ReferenceGround
                    ? Color.Parse("#82909F")
                    : Color.Parse("#4DD6A4"));
            for (var index = 1; index < points.Length; ++index)
            {
                AddCircuitWire(node.Key, points[0], points[index], stroke);
            }
        }
        if (selectedCircuitNode is not null)
        {
            var pinCount = Document.AnalogCircuit.Components.SelectMany(component => component.Pins)
                .Count(pin => string.Equals(pin.Node.Trim(), selectedCircuitNode, StringComparison.Ordinal));
            CircuitSelectionText.Text = $"网络 {selectedCircuitNode} · {pinCount} 个引脚";
        }
        ApplyCircuitCanvasTransform();
    }

    private static Point CircuitPinPoint(AnalogComponentDraft component, int pinIndex)
    {
        var right = pinIndex % 2 == 1;
        return new Point(
            component.CanvasX + (right ? CircuitCardWidth : 0.0),
            component.CanvasY + 58.0 + pinIndex / 2 * 22.0);
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
        var line = new Line
        {
            StartPoint = start,
            EndPoint = end,
            Stroke = stroke,
            StrokeThickness = string.Equals(node, selectedCircuitNode, StringComparison.Ordinal) ? 4.0 : 3.0,
            Tag = node,
            Cursor = new Cursor(StandardCursorType.Hand),
        };
        ToolTip.SetTip(line, $"网络 {node}");
        line.PointerPressed += CircuitWirePointerPressed;
        CircuitWireLayer.Children.Add(line);
    }

    private void ApplyCircuitCanvasTransform()
    {
        if (CircuitGridLayer is null || CircuitWireLayer is null || CircuitNodeLayer is null)
        {
            return;
        }
        var matrix = new Matrix(circuitZoom, 0.0, 0.0, circuitZoom, circuitPanX, circuitPanY);
        CircuitGridLayer.RenderTransform = new MatrixTransform(matrix);
        CircuitWireLayer.RenderTransform = new MatrixTransform(matrix);
        CircuitNodeLayer.RenderTransform = new MatrixTransform(matrix);
        CircuitZoomText.Content = $"{circuitZoom * 100:0}%";
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
        AnalogAddTypeSelector.SelectedItem = descriptor.Type;
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
        var x = Math.Clamp(Math.Round((world.X - CircuitCardWidth * 0.5) / CircuitGridSize) * CircuitGridSize,
            0.0, CircuitCanvasWidth - CircuitCardWidth);
        var y = Math.Clamp(Math.Round((world.Y - CircuitCardHeight * 0.5) / CircuitGridSize) * CircuitGridSize,
            0.0, CircuitCanvasHeight - CircuitCardHeight);
        var component = Document.AddAnalogComponent(descriptor.Type, x, y);
        UpdateLayout();
        SelectCircuitComponents([component]);
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
            ClearCircuitSelection();
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
        var componentIndex = Document.AnalogCircuit.Components.IndexOf(component);
        if (componentIndex >= 0 && AnalogComponentList.ContainerFromIndex(componentIndex) is Control inspectorItem)
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
            circuitPreviewPointer = args.GetPosition(CircuitCanvasSurface);
            UpdateCircuitWirePreview(circuitPreviewPointer);
        }
        else if (ReferenceEquals(pendingCircuitPin, pin))
        {
            ClearPendingCircuitPin();
        }
        else
        {
            Document.ConnectAnalogPins(pendingCircuitPin, pin);
            ClearPendingCircuitPin();
            RenderCircuitConnections();
        }
        args.Handled = true;
    }

    private void ClearPendingCircuitPin()
    {
        if (pendingCircuitPinVisual is not null)
        {
            pendingCircuitPinVisual.StrokeThickness = 1.0;
        }
        pendingCircuitPin = null;
        pendingCircuitPinVisual = null;
        CircuitWirePreview.IsVisible = false;
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
        UpdateCircuitSelectionStatus();
    }

    private void UpdateCircuitSelectionStatus()
    {
        CircuitSelectionText.Text = selectedCircuitNode is not null
            ? $"网络 {selectedCircuitNode}"
            : selectedCircuitComponents.Count switch
            {
                0 => "未选择",
                1 => $"元件 {selectedCircuitComponents.First().Id}",
                _ => $"已选择 {selectedCircuitComponents.Count} 个元件",
            };
    }

    private void UpdateCircuitWirePreview(Point pointer)
    {
        circuitPreviewPointer = pointer;
        if (pendingCircuitPin is null)
        {
            CircuitWirePreview.IsVisible = false;
            return;
        }
        foreach (var component in Document.AnalogCircuit.Components)
        {
            var pinIndex = component.Pins.IndexOf(pendingCircuitPin);
            if (pinIndex < 0)
            {
                continue;
            }
            CircuitWirePreview.StartPoint = CircuitWorldToViewport(CircuitPinPoint(component, pinIndex));
            CircuitWirePreview.EndPoint = pointer;
            CircuitWirePreview.IsVisible = true;
            return;
        }
        CircuitWirePreview.IsVisible = false;
    }

    private void CircuitCanvasPointerMoved(object? sender, PointerEventArgs args)
    {
        var viewportPoint = args.GetPosition(CircuitCanvasSurface);
        if (pendingCircuitPin is not null)
        {
            UpdateCircuitWirePreview(viewportPoint);
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
                Math.Round((start.X + deltaX) / CircuitGridSize) * CircuitGridSize,
                0.0, CircuitCanvasWidth - CircuitCardWidth);
            component.CanvasY = Math.Clamp(
                Math.Round((start.Y + deltaY) / CircuitGridSize) * CircuitGridSize,
                0.0, CircuitCanvasHeight - CircuitCardHeight);
        }
    }

    private void CircuitCanvasPointerReleased(object? sender, PointerReleasedEventArgs args)
    {
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
            component.CanvasX < right && component.CanvasX + CircuitCardWidth > left &&
            component.CanvasY < bottom && component.CanvasY + CircuitCardHeight > top).ToArray();
        SelectCircuitComponents(selected);
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

    private void CircuitWirePointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (sender is not Line { Tag: string node } ||
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
        if (args.Key == Key.Delete)
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
                component.CanvasX + deltaX, 0.0, CircuitCanvasWidth - CircuitCardWidth);
            component.CanvasY = Math.Clamp(
                component.CanvasY + deltaY, 0.0, CircuitCanvasHeight - CircuitCardHeight);
        }
        Document.EndEditTransaction();
        args.Handled = true;
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
        var maximumX = Document.AnalogCircuit.Components.Max(component => component.CanvasX + CircuitCardWidth);
        var maximumY = Document.AnalogCircuit.Components.Max(component => component.CanvasY + CircuitCardHeight);
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
                      (component.CanvasX + CircuitCardWidth * 0.5) * circuitZoom;
        circuitPanY = CircuitCanvasSurface.Bounds.Height * 0.5 -
                      (component.CanvasY + CircuitCardHeight * 0.5) * circuitZoom;
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
        var selected = TemplateSelector.SelectedItem as ModuleAuthoringTemplateOption ??
                       ModuleAuthoringDocument.TemplateOptions[0];
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
            TemplateSelector.SelectedItem = ModuleAuthoringDocument.TemplateOptions.First(option => option.Kind == Document.Kind);
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
        TemplateSelector.SelectedItem = ModuleAuthoringDocument.TemplateOptions.First(option => option.Kind == Document.Kind);
        WorkshopTabs.SelectedIndex = 0;
    }

    private void AddInputPortClick(object? sender, RoutedEventArgs args) => Document.AddPort("input");
    private void AddOutputPortClick(object? sender, RoutedEventArgs args) => Document.AddPort("output");
    private void AddParameterClick(object? sender, RoutedEventArgs args) => Document.AddParameter();
    private void AddDigitalOperationClick(object? sender, RoutedEventArgs args) => Document.AddDigitalOperation();
    private void AddAnalogComponentClick(object? sender, RoutedEventArgs args) =>
        Document.AddAnalogComponent(AnalogAddTypeSelector.SelectedItem as string ?? "resistor");
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
        if (selectedSkinPlacement is not null)
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

    private void ClearSkinSelection()
    {
        if (selectedSkinPlacement is not null) selectedSkinPlacement.IsSelected = false;
        selectedSkinPlacement = null;
        skinFootswitchSelected = false;
        SkinFootswitchVisual.BorderBrush = null;
        SkinFootswitchVisual.BorderThickness = default;
        SkinSelectionText.Text = "未选择控件";
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
        if (selectedSkinPlacement is not null)
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
        if (selectedSkinPlacement is not null)
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
