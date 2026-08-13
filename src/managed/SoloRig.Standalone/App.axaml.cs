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
            if ((desktop.Args ?? Array.Empty<string>()).Contains("--m2-visual-probe", StringComparer.Ordinal))
            {
                var attempts = 0;
                var probe = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
                probe.Tick += (_, _) =>
                {
                    attempts++;
                    if (mainWindow.HasEditorVisualContent)
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
