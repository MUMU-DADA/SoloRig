using Avalonia.Controls;
using SoloRig.Ui;

namespace SoloRig.Standalone;

public sealed partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
    }

    public MainWindow(RigEditorViewModel viewModel)
        : this()
    {
        DataContext = viewModel;
    }
}
