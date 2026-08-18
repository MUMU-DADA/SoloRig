using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using SoloRig.Ui;

namespace SoloRig.Standalone;

public sealed class App : Application
{
    private RigEditorViewModel? viewModel;

    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        viewModel = new RigEditorViewModel();
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var mainWindow = new MainWindow(viewModel);
            desktop.MainWindow = mainWindow;
            desktop.Exit += (_, _) => viewModel.Dispose();
            var args = desktop.Args ?? Array.Empty<string>();
            if (args.Contains("--standalone-visual-probe", StringComparer.Ordinal))
            {
                var attempts = 0;
                var probe = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
                probe.Tick += (_, _) =>
                {
                    attempts++;
                    var visualContent = mainWindow.HasStandaloneVisualContent;
                    if (!visualContent)
                    {
                        ReportVisualProbeFailure("visual content");
                    }
                    var deviceAvailability = visualContent && RunVisualProbe("device availability", mainWindow.ProbeDeviceAvailabilityState);
                    var externalTheme = deviceAvailability && RunVisualProbe("external theme", mainWindow.ProbeExternalTheme);
                    var themeSwitch = externalTheme && RunVisualProbe("theme switch", mainWindow.ProbeThemeSwitch);
                    var parameterBinding = themeSwitch && RunVisualProbe("parameter binding", mainWindow.ProbeParameterBinding);
                    var parameterEditing = parameterBinding && RunVisualProbe("parameter editing", mainWindow.ProbeParameterEditingWorkflow);
                    var runtimeControls = parameterEditing && RunVisualProbe("runtime control interaction", mainWindow.ProbeRuntimeControlInteraction);
                    var dpiScaling = runtimeControls && RunVisualProbe("DPI scaling", mainWindow.ProbeDpiScaling);
                    var canvasNavigation = dpiScaling && RunVisualProbe("canvas navigation", mainWindow.ProbeCanvasNavigation);
                    var responsiveWorkspace = canvasNavigation && RunVisualProbe("responsive workspace", mainWindow.ProbeResponsiveWorkspace);
                    var renderedSurface = responsiveWorkspace && RunVisualProbe("rendered surface", mainWindow.ProbeRenderedSurface);
                    var keyboardShortcuts = renderedSurface && RunVisualProbe("keyboard shortcuts", mainWindow.ProbeKeyboardShortcuts);
                    var keyboardAccessibility = keyboardShortcuts && RunVisualProbe("keyboard accessibility", mainWindow.ProbeKeyboardAccessibility);
                    var selectionState = keyboardAccessibility && RunVisualProbe("selection state", mainWindow.ProbeSelectionState);
                    var inspectorDetails = selectionState && RunVisualProbe("inspector details", mainWindow.ProbeInspectorDetails);
                    var connectionSelection = inspectorDetails && RunVisualProbe("connection selection", mainWindow.ProbeConnectionSelection);
                    var topologyEditor = connectionSelection && RunVisualProbe("topology editor", mainWindow.ProbeTopologyEditor);
                    var documentStatus = topologyEditor && RunVisualProbe("document status", mainWindow.ProbeDocumentStatus);
                    var actionGates = documentStatus && RunVisualProbe("action gates", mainWindow.ProbeActionGates);
                    var nodeNavigator = actionGates && RunVisualProbe("node navigator batch workflow", mainWindow.ProbeNodeNavigatorBatchWorkflow);
                    var moduleDragPreview = nodeNavigator && RunVisualProbe("module drag preview", mainWindow.ProbeModuleDragPreview);
                    var moduleInsertion = moduleDragPreview && RunVisualProbe("module insertion", mainWindow.ProbeModuleInsertion);
                    var presetLibrary = moduleInsertion && RunVisualProbe("preset library", mainWindow.ProbePresetLibrary);
                    var undoRedo = presetLibrary && RunVisualProbe("undo/redo", mainWindow.ProbeUndoRedo);
                    var workshop = undoRedo && RunVisualProbe("DIY effect workshop", mainWindow.ProbeWorkshopWorkspace);
                    if (visualContent && deviceAvailability && externalTheme && themeSwitch && parameterBinding && parameterEditing && runtimeControls && dpiScaling && canvasNavigation && responsiveWorkspace && renderedSurface &&
                        keyboardShortcuts && keyboardAccessibility && selectionState && inspectorDetails && connectionSelection && topologyEditor && documentStatus && actionGates && nodeNavigator && moduleDragPreview &&
                        moduleInsertion && presetLibrary && undoRedo && workshop)
                    {
                        probe.Stop();
                        desktop.Shutdown(0);
                    }
                    else if (attempts >= 20)
                    {
                        probe.Stop();
                        desktop.Shutdown(1);
                    }
                };
                probe.Start();
            }
        }
        base.OnFrameworkInitializationCompleted();
    }

    private static bool RunVisualProbe(string name, Func<bool> probe)
    {
        var passed = probe();
        if (!passed)
        {
            ReportVisualProbeFailure(name);
        }
        return passed;
    }

    private static void ReportVisualProbeFailure(string name) =>
        Console.Error.WriteLine($"[standalone-visual-probe] failed: {name}");
}
