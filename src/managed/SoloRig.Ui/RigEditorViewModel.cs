using System.ComponentModel;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Windows.Input;

namespace SoloRig.Ui;

public sealed record CanvasNodeSnapshot(NodeSnapshot Source, double CanvasX, double CanvasY)
{
    public string InstanceId => Source.InstanceId;
    public string ModuleId => Source.ModuleId;
    public bool Selected => Source.Selected;
    public bool Unresolved => Source.Unresolved;
    public string Diagnostic => Source.Diagnostic;
    public IReadOnlyList<ParameterSnapshot> Parameters => Source.Parameters;
    public string LayoutText => Source.LayoutText;
    public double Width => 196.0;
    public double Height => 126.0;
    public string CardBackground => Selected ? "#3D6682" : "#30343B";
    public string CardBorder => Selected ? "#79C7FF" : "#777F8A";
}

public sealed record CanvasConnectionSnapshot(
    string SourceInstanceId,
    string DestinationInstanceId,
    double SourceX,
    double SourceY,
    double DestinationX,
    double DestinationY)
{
    public string SourcePoint => $"{SourceX:0.###},{SourceY:0.###}";
    public string DestinationPoint => $"{DestinationX:0.###},{DestinationY:0.###}";
}

public sealed record ParameterEditorSnapshot(string Target, string Id, float Value)
{
    public string ValueText => Value.ToString("0.###", CultureInfo.InvariantCulture);
}

public sealed class RigEditorViewModel : INotifyPropertyChanged, IDisposable
{
    private readonly BackendSession? backend;
    private BackendSnapshot snapshot = EmptySnapshot;
    private IReadOnlyList<CanvasNodeSnapshot> canvasNodes = [];
    private IReadOnlyList<CanvasConnectionSnapshot> canvasConnections = [];
    private string status = "Starting backend probe...";

    private static BackendSnapshot EmptySnapshot => new(
        0, true, string.Empty, string.Empty, string.Empty, string.Empty, string.Empty, string.Empty,
        1280, 720, 1, false, [], [], []);

    public RigEditorViewModel()
    {
        try
        {
            backend = new BackendSession();
            Refresh();
        }
        catch (Exception exception)
        {
            status = exception.Message;
        }
        RefreshCommand = new RelayCommand(Refresh);
        IncreaseDriveCommand = new RelayCommand(IncreaseDrive);
        ToggleThemeCommand = new RelayCommand(ToggleThemeAction);
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    public BackendSnapshot Snapshot => snapshot;
    public IReadOnlyList<NodeSnapshot> Nodes => snapshot.Nodes;
    public IReadOnlyList<CanvasNodeSnapshot> CanvasNodes => canvasNodes;
    public IReadOnlyList<CanvasConnectionSnapshot> CanvasConnections => canvasConnections;
    public NodeSnapshot? SelectedNode => snapshot.Nodes.FirstOrDefault(node => node.Selected);
    public IReadOnlyList<ParameterEditorSnapshot> SelectedParameters =>
        SelectedNode is null
            ? []
            : SelectedNode.Parameters.Select(parameter =>
                new ParameterEditorSnapshot($"{SelectedNode.InstanceId}.{parameter.Id}", parameter.Id, parameter.Value)).ToArray();
    public bool IsDirty => snapshot.Dirty;
    public bool DarkTheme => snapshot.DarkTheme;
    public string WindowTitle => string.IsNullOrWhiteSpace(snapshot.PresetName)
        ? "SoloRig Avalonia M2"
        : $"SoloRig Avalonia M2 - {snapshot.PresetName}{(snapshot.Dirty ? " *" : string.Empty)}";
    public string AudioStatus => $"Audio: {snapshot.AudioStatus}";
    public string MidiStatus => $"MIDI: {snapshot.MidiStatus} | mappings: {snapshot.Bindings.Count}";
    public string FileStatus => string.IsNullOrWhiteSpace(snapshot.SourcePath)
        ? $"Preset: unsaved default{(snapshot.Dirty ? " *" : string.Empty)}"
        : $"Preset: {snapshot.SourcePath}{(snapshot.Dirty ? " *" : string.Empty)}";
    public string Status { get => status; private set => Set(ref status, value); }
    public ICommand RefreshCommand { get; }
    public ICommand IncreaseDriveCommand { get; }
    public ICommand ToggleThemeCommand { get; }

    public void Refresh()
    {
        if (backend is null)
        {
            OnPropertiesChanged(nameof(Status));
            return;
        }
        try
        {
            snapshot = backend.ReadSnapshot();
            RebuildCanvasProjections();
            Status = $"Backend probe OK | schema {snapshot.SchemaVersion} | theme {(snapshot.DarkTheme ? "dark" : "light")}";
            OnPropertiesChanged(nameof(Snapshot), nameof(Nodes), nameof(CanvasNodes), nameof(CanvasConnections),
                nameof(SelectedNode), nameof(SelectedParameters), nameof(IsDirty), nameof(DarkTheme),
                nameof(WindowTitle), nameof(AudioStatus), nameof(MidiStatus), nameof(FileStatus));
        }
        catch (Exception exception)
        {
            Status = exception.Message;
        }
    }

    public void SetViewport(double width, double height, double dpiScale)
    {
        if (backend is null || width <= 0 || height <= 0 || dpiScale <= 0)
        {
            return;
        }
        try
        {
            backend.SetViewport(width, height, dpiScale);
            snapshot = backend.ReadSnapshot();
            RebuildCanvasProjections();
            OnPropertiesChanged(nameof(Snapshot), nameof(Nodes), nameof(CanvasNodes), nameof(CanvasConnections),
                nameof(SelectedNode), nameof(SelectedParameters), nameof(IsDirty), nameof(DarkTheme), nameof(WindowTitle),
                nameof(FileStatus));
        }
        catch (Exception exception)
        {
            Status = exception.Message;
        }
    }

    public bool SetParameter(string target, float value)
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.SetParameter(target, value);
            Refresh();
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public bool SetParameterText(string target, string text)
    {
        return float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var value) &&
               SetParameter(target, value);
    }

    public bool ToggleTheme()
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.SetTheme(!snapshot.DarkTheme);
            Refresh();
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public bool SelectAt(double x, double y)
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.SelectAt(ToBackendX(x), ToBackendY(y));
            snapshot = backend.ReadSnapshot();
            RebuildCanvasProjections();
            OnPropertiesChanged(nameof(Snapshot), nameof(Nodes), nameof(CanvasNodes), nameof(SelectedNode), nameof(SelectedParameters));
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public bool BeginDrag(double x, double y)
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.BeginDrag(ToBackendX(x), ToBackendY(y));
            snapshot = backend.ReadSnapshot();
            RebuildCanvasProjections();
            OnPropertiesChanged(nameof(Snapshot), nameof(Nodes), nameof(CanvasNodes), nameof(SelectedNode), nameof(SelectedParameters));
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public bool DragTo(double x, double y, bool snapToGrid)
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.DragTo(ToBackendX(x), ToBackendY(y), snapToGrid);
            snapshot = backend.ReadSnapshot();
            RebuildCanvasProjections();
            OnPropertiesChanged(nameof(Snapshot), nameof(Nodes), nameof(CanvasNodes), nameof(SelectedNode), nameof(SelectedParameters),
                nameof(IsDirty), nameof(WindowTitle), nameof(FileStatus));
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public void EndDrag() => backend?.EndDrag();

    public bool SaveToPath(string path)
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.Save(path);
            Refresh();
            Status = $"Saved: {snapshot.SourcePath}";
            return true;
        }
        catch (Exception exception)
        {
            Refresh();
            Status = exception.Message;
            return false;
        }
    }

    public bool OpenFromPath(string path)
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.Open(path);
            Refresh();
            Status = $"Loaded: {snapshot.SourcePath}";
            return true;
        }
        catch (Exception exception)
        {
            Refresh();
            Status = exception.Message;
            return false;
        }
    }

    public void ReportFileDialogCancelled(string operation) => Status = $"{operation} cancelled; current preset unchanged.";

    public void Dispose()
    {
        backend?.EndDrag();
        backend?.Dispose();
    }

    private void IncreaseDrive()
    {
        var drive = snapshot.Nodes.SelectMany(node => node.Parameters.Select(parameter => (node.InstanceId, parameter)))
            .FirstOrDefault(item => item.parameter.Id == "drive");
        if (drive.parameter is not null)
        {
            SetParameter($"{drive.InstanceId}.drive", drive.parameter.Value + 1.0F);
        }
    }

    private void ToggleThemeAction() => ToggleTheme();

    private double ToBackendX(double x) => x * Math.Max(snapshot.DpiScale, 1.0);
    private double ToBackendY(double y) => y * Math.Max(snapshot.DpiScale, 1.0);

    private void RebuildCanvasProjections()
    {
        var scale = Math.Max(snapshot.DpiScale, 1.0);
        canvasNodes = snapshot.Nodes.Select(node => new CanvasNodeSnapshot(node, node.LayoutX / scale, node.LayoutY / scale)).ToArray();
        canvasConnections = snapshot.Connections.Select(connection => new CanvasConnectionSnapshot(
            connection.SourceInstanceId, connection.DestinationInstanceId,
            connection.SourceX / scale, connection.SourceY / scale,
            connection.DestinationX / scale, connection.DestinationY / scale)).ToArray();
    }

    private void Set<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return;
        }
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    private void OnPropertiesChanged(params string[] names)
    {
        foreach (var name in names)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }
    }
}

internal sealed class RelayCommand(Action action) : ICommand
{
    public event EventHandler? CanExecuteChanged
    {
        add { }
        remove { }
    }
    public bool CanExecute(object? parameter) => true;
    public void Execute(object? parameter) => action();
}
