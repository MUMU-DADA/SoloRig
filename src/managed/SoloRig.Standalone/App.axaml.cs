using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
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
            desktop.MainWindow = new MainWindow(viewModel);
            desktop.Exit += (_, _) => viewModel.Dispose();
        }
        base.OnFrameworkInitializationCompleted();
    }
}
