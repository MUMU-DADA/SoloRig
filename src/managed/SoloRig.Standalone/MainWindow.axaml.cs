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

    public bool ProbeThemeSwitch() => EditorView.ProbeThemeSwitch();

    public bool ProbeExternalTheme() => EditorView.ProbeExternalTheme();

    public bool ProbeParameterBinding() => EditorView.ProbeParameterBinding();

    public bool ProbeDpiScaling() => EditorView.ProbeDpiScaling();

    public bool ProbeRenderedSurface() => EditorView.ProbeRenderedSurface();

    private void WindowKeyDown(object? sender, KeyEventArgs args) => EditorView.HandleWindowKeyDown(args);
}
