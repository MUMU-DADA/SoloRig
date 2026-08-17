using Avalonia.Controls;
using Avalonia.Input;
using SoloRig.Ui;
using SoloRig.Ui.Avalonia;

namespace SoloRig.Standalone;

public sealed partial class MainWindow : Window
{
    private bool allowClose;
    private bool closePromptOpen;

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

    public bool ProbeKeyboardShortcuts() => EditorView.ProbeKeyboardShortcuts();

    public bool ProbeSelectionState() => EditorView.ProbeSelectionState();

    public bool ProbeConnectionSelection() => EditorView.ProbeConnectionSelection();

    public bool ProbeDocumentStatus() => EditorView.ProbeDocumentStatus();

    public bool ProbeActionGates() => EditorView.ProbeActionGates();

    public bool ProbeModuleInsertion() => EditorView.ProbeModuleInsertion();

    public bool ProbeModuleDragPreview() => EditorView.ProbeModuleDragPreview();

    public bool ProbeDeviceAvailabilityState() => EditorView.ProbeDeviceAvailabilityState();

    public bool ProbePresetLibrary() => EditorView.ProbePresetLibrary();

    public bool ProbeUndoRedo() => EditorView.ProbeUndoRedo();

    private void WindowKeyDown(object? sender, KeyEventArgs args) => EditorView.HandleWindowKeyDown(args);

    private async void WindowClosing(object? sender, WindowClosingEventArgs args)
    {
        if (allowClose || DataContext is not RigEditorViewModel editor || !editor.IsDirty)
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
            if (!await EditorView.ConfirmPendingChangesAsync())
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
