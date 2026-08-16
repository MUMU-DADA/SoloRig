using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.Styling;
using SoloRig.Ui;

namespace SoloRig.Ui.Avalonia;

public sealed partial class RigEditorView : UserControl
{
    private static readonly FilePickerFileType SrigFileType = new("SoloRig preset")
    {
        Patterns = ["*.srig"],
        MimeTypes = ["application/x-solorig-preset"]
    };
    private static readonly FilePickerFileType ResourceFileType = new("SoloRig resources")
    {
        Patterns = ["*.nam", "*.nam-a2", "*.ir", "*.wav", "*.json"]
    };

    private RigEditorViewModel? viewModel;
    private bool dragging;
    private string? pendingOutputNode;

    public RigEditorView()
    {
        InitializeComponent();
    }

    public RigEditorView(RigEditorViewModel editor)
        : this()
    {
        AttachEditor(editor);
    }

    public RigEditorViewModel? ViewModel => viewModel;

    public bool HasGraphVisualContent =>
        EditorCanvas is not null && viewModel is not null &&
        viewModel.CanvasNodes.Count >= 2 && viewModel.CanvasConnections.Count >= 1 &&
        EditorCanvas.Bounds.Width > 0 && EditorCanvas.Bounds.Height > 0;

    public bool HasEditorControlsVisualContent => HasGraphVisualContent &&
        ModuleCatalogList is not null && ModuleCatalogList.Bounds.Width > 0 &&
        ModuleCatalogList.ItemCount >= 2 && PerformanceText is not null &&
        PresetSelector is not null && PresetSelector.Bounds.Width > 0;

    public bool HasStandaloneVisualContent => HasEditorControlsVisualContent &&
        RealtimeStatusText is not null && WhiteboxStatusText is not null &&
        !string.IsNullOrWhiteSpace(RealtimeStatusText.Text) &&
        !string.IsNullOrWhiteSpace(WhiteboxStatusText.Text);

    public void AttachEditor(RigEditorViewModel editor)
    {
        ArgumentNullException.ThrowIfNull(editor);
        if (ReferenceEquals(viewModel, editor))
        {
            return;
        }

        if (viewModel is not null)
        {
            viewModel.PropertyChanged -= ViewModelPropertyChanged;
        }

        viewModel = editor;
        DataContext = editor;
        editor.PropertyChanged += ViewModelPropertyChanged;
        ApplyTheme(editor.DarkTheme);
    }

    public void HandleWindowKeyDown(KeyEventArgs args)
    {
        if (viewModel is null)
        {
            return;
        }

        if ((args.KeyModifiers & KeyModifiers.Control) != 0 && args.Key == Key.S)
        {
            if ((args.KeyModifiers & KeyModifiers.Shift) != 0)
            {
                _ = SaveAsAsync();
            }
            else
            {
                _ = SaveAsync();
            }
            args.Handled = true;
        }
        else if ((args.KeyModifiers & KeyModifiers.Control) != 0 && args.Key == Key.O)
        {
            _ = OpenAsync();
            args.Handled = true;
        }
        else if ((args.KeyModifiers & KeyModifiers.Control) != 0 && args.Key == Key.D1)
        {
            viewModel.SelectPreset("SoloRig Default");
            args.Handled = true;
        }
    }

    private void ViewModelPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (args.PropertyName == nameof(RigEditorViewModel.DarkTheme) && viewModel is not null)
        {
            ApplyTheme(viewModel.DarkTheme);
        }
    }

    private void ApplyTheme(bool dark)
    {
        if (TopLevel.GetTopLevel(this) is TopLevel topLevel)
        {
            topLevel.RequestedThemeVariant = dark ? ThemeVariant.Dark : ThemeVariant.Light;
        }
    }

    private void CanvasSizeChanged(object? sender, SizeChangedEventArgs args)
    {
        if (viewModel is not null && args.NewSize.Width > 0 && args.NewSize.Height > 0)
        {
            var dpiScale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
            viewModel.SetViewport(args.NewSize.Width, args.NewSize.Height, dpiScale);
        }
    }

    private void CanvasPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        if (viewModel is null || sender is not Control source || !IsEditorCanvas(source))
        {
            return;
        }
        var point = args.GetPosition(EditorCanvas);
        if (viewModel.BeginDrag(point.X, point.Y))
        {
            dragging = true;
            args.Pointer.Capture(EditorCanvas);
            args.Handled = true;
        }
    }

    private void CanvasPointerMoved(object? sender, PointerEventArgs args)
    {
        if (!dragging || viewModel is null || args.Pointer.Captured != EditorCanvas)
        {
            return;
        }
        var point = args.GetPosition(EditorCanvas);
        viewModel.DragTo(point.X, point.Y, true);
        args.Handled = true;
    }

    private void CanvasPointerReleased(object? sender, PointerReleasedEventArgs args)
    {
        if (!dragging || viewModel is null)
        {
            return;
        }
        dragging = false;
        viewModel.EndDrag();
        args.Pointer.Capture(null);
        args.Handled = true;
    }

    private bool IsEditorCanvas(Control source) => source == EditorCanvas || source is Border || source is ItemsControl;

    private void ParameterLostFocus(object? sender, RoutedEventArgs args)
    {
        if (viewModel is null || sender is not TextBox textBox || textBox.Tag is not string target)
        {
            return;
        }
        if (!viewModel.SetParameterText(target, textBox.Text ?? string.Empty))
        {
            textBox.Text = viewModel.SelectedParameters.FirstOrDefault(item => item.Target == target)?.ValueText ?? textBox.Text;
        }
    }

    private void OpenClick(object? sender, RoutedEventArgs args) => _ = OpenAsync();
    private void SaveClick(object? sender, RoutedEventArgs args) => _ = SaveAsync();
    private void SaveAsClick(object? sender, RoutedEventArgs args) => _ = SaveAsAsync();
    private void ImportResourceClick(object? sender, RoutedEventArgs args) => _ = ImportResourceAsync();

    private void RefreshAsioClick(object? sender, RoutedEventArgs args) => viewModel?.RefreshAsioDevices();

    private void OpenAsioClick(object? sender, RoutedEventArgs args)
    {
        var nativeWindow = TopLevel.GetTopLevel(this)?.TryGetPlatformHandle()?.Handle ?? IntPtr.Zero;
        viewModel?.OpenSelectedAsio(nativeWindow);
    }

    private void StartAsioClick(object? sender, RoutedEventArgs args) => viewModel?.StartAsio();
    private void StopAsioClick(object? sender, RoutedEventArgs args) => viewModel?.StopAsio();
    private void CloseAsioClick(object? sender, RoutedEventArgs args) => viewModel?.CloseAsio();
    private void RecoverAsioClick(object? sender, RoutedEventArgs args) => viewModel?.RecoverAsio();

    private void ToggleTunerClick(object? sender, RoutedEventArgs args) =>
        viewModel?.SetTunerEnabled(!viewModel.TunerEnabled);

    private void RefreshMidiClick(object? sender, RoutedEventArgs args) => viewModel?.RefreshMidiDevices();
    private void OpenMidiClick(object? sender, RoutedEventArgs args) => viewModel?.OpenSelectedMidi();
    private void CloseMidiClick(object? sender, RoutedEventArgs args) => viewModel?.CloseMidi();
    private void DrainMidiClick(object? sender, RoutedEventArgs args) => viewModel?.DrainMidi();
    private void ArmMidiLearnClick(object? sender, RoutedEventArgs args) => viewModel?.ArmSelectedMidiLearn();
    private void CancelMidiLearnClick(object? sender, RoutedEventArgs args) => viewModel?.CancelMidiLearn();
    private void DeleteMidiBindingClick(object? sender, RoutedEventArgs args) => viewModel?.DeleteSelectedMidiBinding();
    private void ClearMidiBindingsClick(object? sender, RoutedEventArgs args) => viewModel?.ClearMidiBindings();
    private void ImportMidiBankClick(object? sender, RoutedEventArgs args) => _ = ImportMidiBankAsync();

    private void NodeOutputEndpointPressed(object? sender, PointerPressedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not string nodeId)
        {
            return;
        }
        pendingOutputNode = nodeId;
        viewModel.ReportInteractionStatus($"已选择 {nodeId} 的输出端点，请点击目标效果器的输入端点");
        args.Handled = true;
    }

    private void NodeInputEndpointPressed(object? sender, PointerPressedEventArgs args)
    {
        if (viewModel is null || sender is not Control control || control.Tag is not string nodeId)
        {
            return;
        }
        if (pendingOutputNode is null)
        {
            viewModel.ReportInteractionStatus($"已点击 {nodeId} 的输入端点；请先点击来源效果器的输出端点");
        }
        else
        {
            var sourceNode = pendingOutputNode;
            pendingOutputNode = null;
            if (sourceNode == nodeId)
            {
                viewModel.ReportInteractionStatus("不能连接效果器自身；请选择另一个输入端点");
            }
            else
            {
                viewModel.ConnectNodes(sourceNode, nodeId);
            }
        }
        args.Handled = true;
    }

    private void FixedEndpointPressed(object? sender, PointerPressedEventArgs args)
    {
        viewModel?.ReportInteractionStatus("INPUT / OUTPUT 是当前 ASIO 设备的固定物理端口，不能作为效果器节点移动");
        args.Handled = true;
    }

    private async Task ImportResourceAsync()
    {
        if (viewModel is null)
        {
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            viewModel.ReportFileDialogCancelled("Resource import");
            return;
        }
        var files = await storage.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "导入 NAM / IR / circuit 资源",
            AllowMultiple = false,
            FileTypeFilter = [ResourceFileType]
        });
        var path = files.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            viewModel.ReportFileDialogCancelled("Resource import");
            return;
        }
        viewModel.ImportResource(path);
    }

    private async Task ImportMidiBankAsync()
    {
        if (viewModel is null)
        {
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            viewModel.ReportFileDialogCancelled("MIDI Bank import");
            return;
        }
        var folders = await storage.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "选择 MIDI Bank 目录",
            AllowMultiple = false
        });
        var path = folders.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            viewModel.ReportFileDialogCancelled("MIDI Bank import");
            return;
        }
        viewModel.ImportMidiBank(path);
    }

    private async Task OpenAsync()
    {
        if (viewModel is null)
        {
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            viewModel.ReportFileDialogCancelled("Open");
            return;
        }
        var files = await storage.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "打开 SoloRig 预设",
            AllowMultiple = false,
            FileTypeFilter = [SrigFileType]
        });
        var path = files.FirstOrDefault()?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            viewModel.ReportFileDialogCancelled("Open");
            return;
        }
        viewModel.OpenFromPath(path);
    }

    private async Task SaveAsync()
    {
        if (viewModel is null)
        {
            return;
        }
        if (!string.IsNullOrWhiteSpace(viewModel.Snapshot.SourcePath))
        {
            viewModel.SaveToPath(viewModel.Snapshot.SourcePath);
            return;
        }
        await SaveAsAsync();
    }

    private async Task SaveAsAsync()
    {
        if (viewModel is null)
        {
            return;
        }
        var storage = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storage is null)
        {
            viewModel.ReportFileDialogCancelled("Save As");
            return;
        }
        var file = await storage.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "保存 SoloRig 预设",
            SuggestedFileName = "SoloRig.srig",
            DefaultExtension = "srig",
            FileTypeChoices = [SrigFileType]
        });
        var path = file?.Path.LocalPath;
        if (string.IsNullOrWhiteSpace(path))
        {
            viewModel.ReportFileDialogCancelled("Save As");
            return;
        }
        viewModel.SaveToPath(path);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        if (viewModel is not null)
        {
            viewModel.PropertyChanged -= ViewModelPropertyChanged;
        }
        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        if (viewModel is not null)
        {
            ApplyTheme(viewModel.DarkTheme);
        }
    }
}
