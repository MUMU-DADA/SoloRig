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
                    var externalTheme = visualContent && RunVisualProbe("external theme", mainWindow.ProbeExternalTheme);
                    var themeSwitch = externalTheme && RunVisualProbe("theme switch", mainWindow.ProbeThemeSwitch);
                    var parameterBinding = themeSwitch && RunVisualProbe("parameter binding", mainWindow.ProbeParameterBinding);
                    var dpiScaling = parameterBinding && RunVisualProbe("DPI scaling", mainWindow.ProbeDpiScaling);
                    var renderedSurface = dpiScaling && RunVisualProbe("rendered surface", mainWindow.ProbeRenderedSurface);
                    var keyboardShortcuts = renderedSurface && RunVisualProbe("keyboard shortcuts", mainWindow.ProbeKeyboardShortcuts);
                    var selectionState = keyboardShortcuts && RunVisualProbe("selection state", mainWindow.ProbeSelectionState);
                    var documentStatus = selectionState && RunVisualProbe("document status", mainWindow.ProbeDocumentStatus);
                    var actionGates = documentStatus && RunVisualProbe("action gates", mainWindow.ProbeActionGates);
                    var moduleInsertion = actionGates && RunVisualProbe("module insertion", mainWindow.ProbeModuleInsertion);
                    var presetLibrary = moduleInsertion && RunVisualProbe("preset library", mainWindow.ProbePresetLibrary);
                    var undoRedo = presetLibrary && RunVisualProbe("undo/redo", mainWindow.ProbeUndoRedo);
                    if (visualContent && externalTheme && themeSwitch && parameterBinding && dpiScaling && renderedSurface &&
                        keyboardShortcuts && selectionState && documentStatus && actionGates && moduleInsertion && presetLibrary && undoRedo)
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
