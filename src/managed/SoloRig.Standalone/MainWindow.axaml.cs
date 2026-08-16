using Avalonia.Controls;
using Avalonia.Input;
using SoloRig.Ui;
using SoloRig.Ui.Avalonia;

namespace SoloRig.Standalone;

public sealed partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
    }

    public MainWindow(RigEditorViewModel editor)
        : this()
    {
        DataContext = editor;
        EditorView.AttachEditor(editor);
    }

    public bool HasStandaloneVisualContent => EditorView.HasStandaloneVisualContent;

    private void WindowKeyDown(object? sender, KeyEventArgs args) => EditorView.HandleWindowKeyDown(args);
}
