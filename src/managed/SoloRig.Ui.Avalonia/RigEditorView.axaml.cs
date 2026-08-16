using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using Avalonia.Layout;
using Avalonia.Styling;
using Avalonia.Threading;
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
        "#9AA8B8", "#4DD6A4", "#69E2B5", "#10261F", "#66A7FF", "#E8F2FF", "#B889F5",
        "#FFF4FF", "#10151A", "#303C47", "#31414F", "#26313C", "#131820", "#D8FAED", "#637384");

    private static readonly ThemePalette LightPalette = new(
        "#F4F7FB", "#FFFFFF", "#EEF3F7", "#E6EDF4", "#DCE6EF", "#FFFFFF", "#E8F0F6",
        "#F8FAFC", "#EAF2F9", "#F1ECFB", "#C7D2DE", "#B5C2CF", "#C5D1DC", "#172033",
        "#5F6B78", "#0F766E", "#0B5F59", "#FFFFFF", "#2F80ED", "#EAF2FF", "#7C3AED",
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
    private string? pendingOutputNode;
    private ModuleCatalogSnapshot? draggedModule;
    private Point moduleDragStart;
    private bool draggingModule;
    private bool moduleDragOverCanvas;

    public RigEditorView()
    {
        InitializeComponent();
        ApplyTheme(ThemeSnapshot.Empty, true);
    }

    public RigEditorView(RigEditorViewModel editor)
        : this()
    {
        AttachEditor(editor);
    }

    public RigEditorViewModel? ViewModel => viewModel;

    public bool HasGraphVisualContent =>
        EditorCanvas is not null && viewModel is not null &&
        viewModel.CanvasNodes.Count >= 2 && viewModel.CanvasConnections.Count >= 1 &&
        EditorCanvas.Bounds.Width > 0 && EditorCanvas.Bounds.Height > 0;

    public bool HasEditorControlsVisualContent => HasGraphVisualContent &&
        ModuleCatalogList is not null && ModuleCatalogList.Bounds.Width > 0 &&
        ModuleCatalogList.ItemCount >= 2 && PerformanceText is not null &&
        PresetSelector is not null && PresetSelector.Bounds.Width > 0;

    public bool HasStandaloneVisualContent => HasEditorControlsVisualContent &&
        DocumentStatusText is not null && RealtimeStatusText is not null && WhiteboxStatusText is not null &&
        EffectChainStripPanel is not null && EffectChainStripPanel.Children.Count >= 2 &&
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

    public bool ProbeDpiScaling() => viewModel is not null && viewModel.ProbeDpiScaling();

    public bool ProbeRenderedSurface()
    {
        if (viewModel is null || !HasStandaloneVisualContent || Bounds.Width <= 0 || Bounds.Height <= 0)
        {
            return false;
        }

        var original = CaptureRenderedSurface();
        var repeated = CaptureRenderedSurface();
        if (original is null || repeated is null || original != repeated)
        {
            return false;
        }

        var originalDark = viewModel.DarkTheme;
        if (!viewModel.ToggleTheme(markDirty: false))
        {
            return false;
        }

        try
        {
            var switched = CaptureRenderedSurface();
            if (switched is null || switched == original || !viewModel.ToggleTheme(markDirty: false) ||
                viewModel.DarkTheme != originalDark)
            {
                return false;
            }
            return CaptureRenderedSurface() == original;
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
        if (viewModel.DarkTheme != originalDark)
        {
            viewModel.ToggleTheme(markDirty: false);
        }
        return toggled && modifierIgnored && viewModel.DarkTheme == originalDark;
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

    public bool ProbeEffectChainSelection()
    {
        if (viewModel is null || EffectChainStripPanel is null || EffectChainStripPanel.Children.Count == 0)
        {
            return false;
        }

        var originalSelected = viewModel.SelectedNode?.InstanceId;
        var target = viewModel.EffectChainNodes.LastOrDefault();
        var originalFingerprint = BuildVisualRegressionFingerprint();
        if (target is null)
        {
            return false;
        }

        var button = EffectChainStripPanel.Children
            .OfType<Button>()
            .FirstOrDefault(candidate => candidate.DataContext is CanvasNodeSnapshot node &&
                                         node.InstanceId == target.InstanceId);
        if (button is null)
        {
            return false;
        }

        button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        var selected = viewModel.SelectedNode?.InstanceId == target.InstanceId;
        var inspectorUpdated = viewModel.SelectedNodeDisplayName == target.DisplayName;

        var restored = originalSelected is null
            ? viewModel.ClearSelection()
            : viewModel.SelectNode(originalSelected);
        return selected && inspectorUpdated && restored &&
               (viewModel.SelectedNode?.InstanceId ?? string.Empty) == (originalSelected ?? string.Empty) &&
               BuildVisualRegressionFingerprint() == originalFingerprint;
    }

    public bool ProbeConnectionSelection()
    {
        if (viewModel is null || viewModel.CanvasConnections.Count == 0)
        {
            return false;
        }

        var original = viewModel.SelectedConnection;
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
        var restored = original is null
            ? viewModel.ClearConnectionSelection()
            : viewModel.SelectConnection(original.SourceInstanceId, original.DestinationInstanceId);
        return selected && restored &&
               BuildVisualRegressionFingerprint() == originalFingerprint &&
               (viewModel.SelectedConnection?.SourceInstanceId ?? string.Empty) == (original?.SourceInstanceId ?? string.Empty);
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

    public bool ProbeModuleInsertion()
    {
        if (viewModel is null || !HasGraphVisualContent || CaptureRenderedSurface() is not { } originalSurface)
        {
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
            var projectedChainButton = EffectChainStripPanel?.Children
                .OfType<Button>()
                .Any(button => button.DataContext is CanvasNodeSnapshot node &&
                              node.InstanceId == addedNode.InstanceId) == true;
            var afterSurface = CaptureRenderedSurface();
            return projectedInsideCanvas && linkedToChain && projectedIntoChain &&
                   projectedChainButton &&
                   afterSurface is not null && afterSurface != originalSurface;
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

    private RenderedSurfaceFingerprint? CaptureRenderedSurface()
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
            $"{node.InstanceId}:{node.Selected}:{node.Unresolved}:{node.Bypassed}:{node.FootswitchText}:skin={node.Skin.Id}:" +
            $"images={node.Skin.HasKnobImage},{node.Skin.HasToggleImage},{node.Skin.HasFootswitchImage}:" +
            string.Join(",", node.Controls.Select(control =>
                $"{control.Id}:{control.Value:0.######}:{control.KnobAngle:0.###}:{control.IsToggle}"))));
        var library = string.Join("|", viewModel.PresetLibraryEntries.Select(entry =>
            $"{entry.DisplayName}:{entry.IsValid}:{entry.SourcePath}"));
        var chain = string.Join("|", viewModel.EffectChainNodes.Select(node =>
            $"{node.InstanceId}:{node.Selected}:{node.Bypassed}"));
        var connections = string.Join("|", viewModel.CanvasConnections.Select(connection =>
            $"{connection.SourceInstanceId}>{connection.DestinationInstanceId}:{connection.Selected}"));
        return $"theme={viewModel.Theme.Id};dark={viewModel.DarkTheme};file={viewModel.FileStatus};filter={viewModel.ModuleCatalogFilter};inspector={viewModel.InspectorTab};library={viewModel.PresetLibraryDirectory}:{viewModel.SelectedPresetLibraryIndex}:{library};resources={resources};nodes={nodes};chain={chain};connections={connections}";
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
        RenderEffectChainStrip();
        UpdateSelectionStyles();
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
        if (viewModel is not null &&
            (args.PropertyName == nameof(RigEditorViewModel.CanvasNodes) ||
             args.PropertyName == nameof(RigEditorViewModel.EffectChainNodes) ||
             args.PropertyName == nameof(RigEditorViewModel.DarkTheme) ||
             args.PropertyName == nameof(RigEditorViewModel.Theme)))
        {
            RenderEffectChainStrip();
        }
    }

    private void RenderEffectChainStrip()
    {
        if (EffectChainStripPanel is null)
        {
            return;
        }

        EffectChainStripPanel.Children.Clear();
        if (viewModel is null)
        {
            return;
        }

        var accentBrush = ResourceBrush("SrgAccentBrush", "#4DD6A4");
        var textBrush = ResourceBrush("SrgTextBrush", "#E7EEF4");
        foreach (var node in viewModel.EffectChainNodes)
        {
            var button = new Button
            {
                DataContext = node,
                Background = new SolidColorBrush(Color.Parse(node.CardBackground)),
                BorderBrush = new SolidColorBrush(Color.Parse(node.CardBorder)),
                Content = new StackPanel
                {
                    Orientation = Orientation.Horizontal,
                    Spacing = 4,
                    Children =
                    {
                        new TextBlock { Text = "FX", Foreground = accentBrush, FontSize = 8, FontWeight = FontWeight.Bold },
                        new TextBlock { Text = node.DisplayName, Foreground = textBrush, FontSize = 9 }
                    }
                }
            };
            button.Classes.Add("srg-chain-item");
            button.Click += EffectChainNodeClick;
            EffectChainStripPanel.Children.Add(button);
        }
    }

    private IBrush ResourceBrush(string key, string fallback) =>
        Resources[key] as IBrush ?? new SolidColorBrush(Color.Parse(fallback));

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

    private void CanvasPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (viewModel is null || sender is not Control source || !IsEditorCanvas(source))
        {
            return;
        }
        var point = args.GetPosition(EditorCanvas);
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
        if (!dragging || viewModel is null || args.Pointer.Captured != EditorCanvas)
        {
            return;
        }
        var point = args.GetPosition(EditorCanvas);
        viewModel.DragTo(point.X, point.Y, true);
        args.Handled = true;
    }

    private void CanvasPointerReleased(object? sender, PointerReleasedEventArgs args)
    {
        if (!dragging || viewModel is null)
        {
            return;
        }
        dragging = false;
        viewModel.EndDrag();
        args.Pointer.Capture(null);
        args.Handled = true;
    }

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
            viewModel.AddModuleAtCanvasPoint(module.ModuleId, canvasPoint.X, canvasPoint.Y);
        }
        args.Handled = true;
    }

    private bool IsEditorCanvas(Control source) => source == EditorCanvas || source is Border || source is ItemsControl;

    private void ParameterLostFocus(object? sender, RoutedEventArgs args)
    {
        if (viewModel is null || sender is not TextBox textBox || textBox.Tag is not string target)
        {
            return;
        }
        if (!viewModel.SetParameterText(target, textBox.Text ?? string.Empty))
        {
            textBox.Text = viewModel.SelectedParameters.FirstOrDefault(item => item.Target == target)?.ValueText ?? textBox.Text;
        }
    }

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

    private void ModuleCatalogFilterClick(object? sender, RoutedEventArgs args)
    {
        if (viewModel is not null && sender is Control control && control.Tag is string filter)
        {
            viewModel.SetModuleCatalogFilter(filter);
        }
    }

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

    private void NodeOutputEndpointPressed(object? sender, PointerPressedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not string nodeId)
        {
            return;
        }
        pendingOutputNode = nodeId;
        viewModel.ReportInteractionStatus($"已选择 {nodeId} 的输出端点，请点击目标效果器的输入端点");
        args.Handled = true;
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

    private void EffectChainNodeClick(object? sender, RoutedEventArgs args)
    {
        if (viewModel is null || sender is not Control control ||
            control.DataContext is not CanvasNodeSnapshot node)
        {
            return;
        }
        if (viewModel.SelectNode(node.InstanceId))
        {
            viewModel.ReportInteractionStatus($"已选择效果器: {node.DisplayName}");
        }
        args.Handled = true;
    }

    private void NodeInputEndpointPressed(object? sender, PointerPressedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not string nodeId)
        {
            return;
        }
        if (pendingOutputNode is null)
        {
            viewModel.ReportInteractionStatus($"已点击 {nodeId} 的输入端点；请先点击来源效果器的输出端点");
        }
        else
        {
            var sourceNode = pendingOutputNode;
            pendingOutputNode = null;
            if (sourceNode == nodeId)
            {
                viewModel.ReportInteractionStatus("不能连接效果器自身；请选择另一个输入端点");
            }
            else
            {
                viewModel.ConnectNodes(sourceNode, nodeId);
            }
        }
        args.Handled = true;
    }

    private void FixedEndpointPressed(object? sender, PointerPressedEventArgs args)
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
