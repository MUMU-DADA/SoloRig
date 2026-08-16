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
                    if (mainWindow.HasStandaloneVisualContent && mainWindow.ProbeExternalTheme() &&
                        mainWindow.ProbeThemeSwitch() && mainWindow.ProbeParameterBinding() &&
                        mainWindow.ProbeDpiScaling() && mainWindow.ProbeRenderedSurface() &&
                        mainWindow.ProbeKeyboardShortcuts())
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
}
