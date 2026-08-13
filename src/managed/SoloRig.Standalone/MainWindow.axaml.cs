using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.Styling;
using SoloRig.Ui;

namespace SoloRig.Standalone;

public sealed partial class MainWindow : Window
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

    public MainWindow()
    {
        InitializeComponent();
    }

    public MainWindow(RigEditorViewModel editor)
        : this()
    {
        viewModel = editor;
        DataContext = editor;
        editor.PropertyChanged += ViewModelPropertyChanged;
        ApplyTheme(editor.DarkTheme);
    }

    public bool HasEditorVisualContent =>
        EditorCanvas is not null && viewModel is not null &&
        viewModel.CanvasNodes.Count >= 2 && viewModel.CanvasConnections.Count >= 1 &&
        EditorCanvas.Bounds.Width > 0 && EditorCanvas.Bounds.Height > 0;

    public bool HasM5VisualContent => HasEditorVisualContent &&
        ModuleCatalogList is not null && ModuleCatalogList.Bounds.Width > 0 &&
        ModuleCatalogList.ItemCount >= 2 && PerformanceText is not null &&
        PresetSelector is not null && PresetSelector.Bounds.Width > 0;

    public bool HasM17VisualContent => HasM5VisualContent &&
        RealtimeStatusText is not null && WhiteboxStatusText is not null &&
        !string.IsNullOrWhiteSpace(RealtimeStatusText.Text) &&
        !string.IsNullOrWhiteSpace(WhiteboxStatusText.Text);

    private void ViewModelPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (args.PropertyName == nameof(RigEditorViewModel.DarkTheme) && viewModel is not null)
        {
            ApplyTheme(viewModel.DarkTheme);
        }
    }

    private void ApplyTheme(bool dark)
    {
        RequestedThemeVariant = dark ? ThemeVariant.Dark : ThemeVariant.Light;
    }

    private void CanvasSizeChanged(object? sender, SizeChangedEventArgs args)
    {
        if (viewModel is not null && args.NewSize.Width > 0 && args.NewSize.Height > 0)
        {
            viewModel.SetViewport(args.NewSize.Width, args.NewSize.Height, RenderScaling);
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

    private void WindowKeyDown(object? sender, KeyEventArgs args)
    {
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
    }

    private void OpenClick(object? sender, RoutedEventArgs args) => _ = OpenAsync();
    private void SaveClick(object? sender, RoutedEventArgs args) => _ = SaveAsync();
    private void SaveAsClick(object? sender, RoutedEventArgs args) => _ = SaveAsAsync();
    private void ImportResourceClick(object? sender, RoutedEventArgs args) => _ = ImportResourceAsync();

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
}
