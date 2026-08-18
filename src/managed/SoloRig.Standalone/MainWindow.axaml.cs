using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using SoloRig.Ui;
using SoloRig.Ui.Avalonia;

namespace SoloRig.Standalone;

public sealed partial class MainWindow : Window
{
    private bool allowClose;
    private bool closePromptOpen;
    private bool workshopActive;

    public MainWindow()
    {
        InitializeComponent();
    }

    public MainWindow(RigEditorViewModel editor)
        : this()
    {
        DataContext = editor;
        EditorView.AttachEditor(editor);
        editor.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName is nameof(RigEditorViewModel.Theme) or nameof(RigEditorViewModel.DarkTheme) or null or "")
            {
                WorkshopView.ApplyTheme(editor.Theme, editor.DarkTheme);
            }
            if (!workshopActive && args.PropertyName is nameof(RigEditorViewModel.WindowTitle) or null or "")
            {
                Title = editor.WindowTitle;
            }
        };
        WorkshopView.ApplyTheme(editor.Theme, editor.DarkTheme);
        WorkshopView.Document.PropertyChanged += (_, args) =>
        {
            if (workshopActive && args.PropertyName is nameof(ModuleAuthoringDocument.WindowTitle) or null or "")
            {
                Title = WorkshopView.Document.WindowTitle;
            }
        };
        WorkshopView.InstallRequested += (_, directory) =>
        {
            if (!editor.ImportResource(directory))
            {
                WorkshopView.Document.ReportStatus($"安装失败：{editor.Status}");
                return;
            }
            editor.SetModuleCatalogFilter("外部");
            editor.SelectedCatalogModule = editor.ModuleCatalog.FirstOrDefault(module =>
                string.Equals(module.ModuleId, WorkshopView.Document.ModuleId, StringComparison.Ordinal));
            WorkshopView.Document.ReportStatus("模块已安装到当前模块库");
            SetWorkspace(false);
        };
        Title = editor.WindowTitle;
    }

    public bool HasStandaloneVisualContent => EditorView.HasStandaloneVisualContent;

    public bool ProbeThemeSwitch() => EditorView.ProbeThemeSwitch();

    public bool ProbeExternalTheme() => EditorView.ProbeExternalTheme();

    public bool ProbeParameterBinding() => EditorView.ProbeParameterBinding();

    public bool ProbeParameterEditingWorkflow() => EditorView.ProbeParameterEditingWorkflow();
    public bool ProbeRuntimeControlInteraction() => EditorView.ProbeRuntimeControlInteraction();

    public bool ProbeDpiScaling() => EditorView.ProbeDpiScaling();

    public bool ProbeCanvasNavigation() => EditorView.ProbeCanvasNavigation();

    public bool ProbeResponsiveWorkspace() => EditorView.ProbeResponsiveWorkspace();

    public bool ProbeRenderedSurface() => EditorView.ProbeRenderedSurface();

    public bool ProbeKeyboardShortcuts() => EditorView.ProbeKeyboardShortcuts();

    public bool ProbeKeyboardAccessibility() => EditorView.ProbeKeyboardAccessibility();

    public bool ProbeSelectionState() => EditorView.ProbeSelectionState();

    public bool ProbeInspectorDetails() => EditorView.ProbeInspectorDetails();

    public bool ProbeConnectionSelection() => EditorView.ProbeConnectionSelection();

    public bool ProbeTopologyEditor() => EditorView.ProbeTopologyEditor();

    public bool ProbeDocumentStatus() => EditorView.ProbeDocumentStatus();

    public bool ProbeActionGates() => EditorView.ProbeActionGates();

    public bool ProbeNodeNavigatorBatchWorkflow() => EditorView.ProbeNodeNavigatorBatchWorkflow();

    public bool ProbeModuleInsertion() => EditorView.ProbeModuleInsertion();

    public bool ProbeModuleDragPreview() => EditorView.ProbeModuleDragPreview();

    public bool ProbeDeviceAvailabilityState() => EditorView.ProbeDeviceAvailabilityState();

    public bool ProbePresetLibrary() => EditorView.ProbePresetLibrary();

    public bool ProbeUndoRedo() => EditorView.ProbeUndoRedo();

    public bool ProbeWorkshopWorkspace()
    {
        var originalWidth = Width;
        var originalHeight = Height;
        try
        {
            Width = 820;
            Height = 520;
            SetWorkspace(true);
            UpdateLayout();
            WorkshopView.UpdateLayout();
            var visualContent = WorkshopView.HasWorkshopVisualContent;
            var project = visualContent && WorkshopView.ProbeProjectWorkflow();
            var responsive = project && WorkshopView.ProbeResponsiveLayout();
            var dpiSnapshots = responsive && WorkshopView.ProbeResponsiveDpiSnapshots();
            // Restore the narrow probe fixture after the DPI samples; a real window
            // resize is applied on the next layout pass, so make the fixture explicit.
            Width = 820;
            Height = 520;
            SetWorkspace(true);
            UpdateLayout();
            WorkshopView.UpdateLayout();
            var narrow = dpiSnapshots && WorkshopView.ProbeNarrowLayoutBounds();
            var palette = narrow && WorkshopView.ProbeThemePalette();
            var skin = palette && WorkshopView.ProbeSkinDesigner();
            var circuit = skin && WorkshopView.ProbeCircuitCanvas();
            var routingStress = circuit && WorkshopView.ProbeCircuitRoutingStress();
            var validation = routingStress && WorkshopView.ProbeValidationStates();
            var audition = validation && WorkshopView.ProbeAuditionWorkspace();
            if (!audition)
            {
                Console.Error.WriteLine($"[standalone-visual-probe] workshop detail: visual={visualContent}, project={project}, " +
                                        $"responsive={responsive}, dpiSnapshots={dpiSnapshots}, narrow={narrow}, palette={palette}, skin={skin}, " +
                                        $"circuit={circuit}, routingStress={routingStress}, validation={validation}, audition={audition}");
            }
            return audition;
        }
        finally
        {
            Width = originalWidth;
            Height = originalHeight;
            SetWorkspace(false);
            UpdateLayout();
        }
    }

    private void WindowKeyDown(object? sender, KeyEventArgs args)
    {
        if (workshopActive)
        {
            WorkshopView.HandleWindowKeyDown(args);
        }
        else
        {
            EditorView.HandleWindowKeyDown(args);
        }
    }

    private void ShowRigWorkspaceClick(object? sender, RoutedEventArgs args) => SetWorkspace(false);

    private void ShowWorkshopWorkspaceClick(object? sender, RoutedEventArgs args) => SetWorkspace(true);

    private void SetWorkspace(bool showWorkshop)
    {
        workshopActive = showWorkshop;
        EditorView.IsVisible = !showWorkshop;
        WorkshopView.IsVisible = showWorkshop;
        RigWorkspaceButton.Classes.Set("active", !showWorkshop);
        WorkshopWorkspaceButton.Classes.Set("active", showWorkshop);
        Title = showWorkshop
            ? WorkshopView.Document.WindowTitle
            : (DataContext as RigEditorViewModel)?.WindowTitle ?? "SoloRig";
    }

    private async void WindowClosing(object? sender, WindowClosingEventArgs args)
    {
        if (allowClose || DataContext is not RigEditorViewModel editor ||
            (!editor.IsDirty && !WorkshopView.Document.IsDirty))
        {
            return;
        }

        args.Cancel = true;
        if (closePromptOpen)
        {
            return;
        }

        closePromptOpen = true;
        try
        {
            if (WorkshopView.Document.IsDirty && !await WorkshopView.ConfirmPendingChangesAsync())
            {
                return;
            }
            if (editor.IsDirty && !await EditorView.ConfirmPendingChangesAsync())
            {
                return;
            }
            allowClose = true;
            Close();
        }
        finally
        {
            closePromptOpen = false;
        }
    }
}
