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
    public string PerformanceText => Source.PerformanceText;
    public double Width => 196.0;
    public double Height => 142.0;
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
    private IReadOnlyList<ModuleCatalogSnapshot> moduleCatalog = [];
    private string moduleSearchText = string.Empty;
    private ModuleCatalogSnapshot? selectedCatalogModule;
    private string selectedPresetName = "SoloRig Default";
    private string status = "Starting backend probe...";
    private RealtimeContractSnapshot realtimeContract = EmptyRealtimeContract;
    private WhiteboxStatusSnapshot whiteboxStatus = EmptyWhiteboxStatus;

    private static RealtimeContractSnapshot EmptyRealtimeContract => new(
        0, false, true, true, true, true, false, 0, 0, 0, 0, 0, 0.0, "backend unavailable");
    private static WhiteboxStatusSnapshot EmptyWhiteboxStatus => new(
        0, "unavailable", "unavailable", "unavailable", "backend unavailable");

    private static BackendSnapshot EmptySnapshot => new(
        0, true, string.Empty, string.Empty, string.Empty, string.Empty, string.Empty, string.Empty,
        1280, 720, 1, false, [], [], [], 0, 0, "No external resource imported", 0, 0, 0, false);

    public RigEditorViewModel()
    {
        try
        {
            backend = new BackendSession();
            moduleCatalog = backend.ReadCatalog();
            Refresh();
        }
        catch (Exception exception)
        {
            status = exception.Message;
        }
        RefreshCommand = new RelayCommand(Refresh);
        IncreaseDriveCommand = new RelayCommand(IncreaseDrive);
        ToggleThemeCommand = new RelayCommand(ToggleThemeAction);
        NewPresetCommand = new RelayCommand(NewPreset);
        AddModuleCommand = new RelayCommand(AddSelectedModule);
        DeleteSelectedCommand = new RelayCommand(DeleteSelectedNode);
        DuplicateSelectedCommand = new RelayCommand(DuplicateSelectedNode);
        ConnectSelectedCommand = new RelayCommand(ConnectSelectedNodes);
        DisconnectSelectedCommand = new RelayCommand(DisconnectSelectedNodes);
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    public BackendSnapshot Snapshot => snapshot;
    public IReadOnlyList<NodeSnapshot> Nodes => snapshot.Nodes;
    public IReadOnlyList<CanvasNodeSnapshot> CanvasNodes => canvasNodes;
    public IReadOnlyList<CanvasConnectionSnapshot> CanvasConnections => canvasConnections;
    public IReadOnlyList<ModuleCatalogSnapshot> ModuleCatalog => moduleCatalog;
    public IReadOnlyList<ModuleCatalogSnapshot> FilteredModuleCatalog => moduleCatalog
        .Where(module => string.IsNullOrWhiteSpace(ModuleSearchText) ||
                         module.DisplayText.Contains(ModuleSearchText, StringComparison.OrdinalIgnoreCase) ||
                         module.ModuleId.Contains(ModuleSearchText, StringComparison.OrdinalIgnoreCase))
        .ToArray();
    public string ModuleSearchText
    {
        get => moduleSearchText;
        set
        {
            if (Set(ref moduleSearchText, value))
            {
                OnPropertiesChanged(nameof(FilteredModuleCatalog));
            }
        }
    }
    public ModuleCatalogSnapshot? SelectedCatalogModule
    {
        get => selectedCatalogModule;
        set => Set(ref selectedCatalogModule, value);
    }
    public NodeSnapshot? SelectedNode => snapshot.Nodes.FirstOrDefault(node => node.Selected);
    public IReadOnlyList<ParameterEditorSnapshot> SelectedParameters =>
        SelectedNode is null
            ? []
            : SelectedNode.Parameters.Select(parameter =>
                new ParameterEditorSnapshot($"{SelectedNode.InstanceId}.{parameter.Id}", parameter.Id, parameter.Value)).ToArray();
    public bool IsDirty => snapshot.Dirty;
    public bool DarkTheme => snapshot.DarkTheme;
    public string WindowTitle => string.IsNullOrWhiteSpace(snapshot.PresetName)
        ? "SoloRig"
        : $"SoloRig - {snapshot.PresetName}{(snapshot.Dirty ? " *" : string.Empty)}";
    public string AudioStatus => $"Audio: {snapshot.AudioStatus}";
    public string MidiStatus => $"MIDI: {snapshot.MidiStatus} | mappings: {snapshot.Bindings.Count}";
    public string FileStatus => string.IsNullOrWhiteSpace(snapshot.SourcePath)
        ? $"Preset: unsaved default{(snapshot.Dirty ? " *" : string.Empty)}"
        : $"Preset: {snapshot.SourcePath}{(snapshot.Dirty ? " *" : string.Empty)}";
    public string PerformanceStatus => snapshot.MeasurementAvailable
        ? $"性能/延迟: 实测 {snapshot.MeasuredCpuPercent:0.0}% / 峰值 {snapshot.PeakCpuPercent:0.0}% CPU · {snapshot.TotalLatencySamples} samples"
        : $"性能/延迟: 预计 {snapshot.EstimatedCpuPercent:0.0}% CPU · {snapshot.TotalLatencySamples} samples · 实测未连接";
    public string ResourceStatus => $"资源: {snapshot.ResourceStatus}";
    public RealtimeContractSnapshot RealtimeContract => realtimeContract;
    public WhiteboxStatusSnapshot WhiteboxStatus => whiteboxStatus;
    public string RealtimeStatus => realtimeContract.ContractVersion == 0
        ? "实时边界: unavailable"
        : $"实时边界: {(realtimeContract.IsRealtimeSafe ? "C++ safe" : "拒绝/未就绪")} · " +
          $"Prepare owns compile · {realtimeContract.SampleFormatText} · oversampling ≤{realtimeContract.MaximumOversampling}";
    public string WhiteboxStatusText => whiteboxStatus.SchemaVersion == 0
        ? "白盒状态: unavailable"
        : $"白盒: {whiteboxStatus.RuntimeStatus} · 证据: {whiteboxStatus.EvidenceStatus}";
    public IReadOnlyList<string> PresetNames =>
        string.IsNullOrWhiteSpace(snapshot.PresetName) ||
        string.Equals(snapshot.PresetName, "SoloRig Default", StringComparison.Ordinal)
            ? ["SoloRig Default"]
            : ["SoloRig Default", snapshot.PresetName];
    public string SelectedPresetName
    {
        get => selectedPresetName;
        set
        {
            if (string.IsNullOrWhiteSpace(value))
            {
                return;
            }
            var previous = selectedPresetName;
            if (Set(ref selectedPresetName, value) && !SelectPreset(value))
            {
                selectedPresetName = previous;
                OnPropertiesChanged(nameof(SelectedPresetName));
            }
        }
    }
    public string Status { get => status; private set => Set(ref status, value); }
    public ICommand RefreshCommand { get; }
    public ICommand IncreaseDriveCommand { get; }
    public ICommand ToggleThemeCommand { get; }
    public ICommand NewPresetCommand { get; }
    public ICommand AddModuleCommand { get; }
    public ICommand DeleteSelectedCommand { get; }
    public ICommand DuplicateSelectedCommand { get; }
    public ICommand ConnectSelectedCommand { get; }
    public ICommand DisconnectSelectedCommand { get; }

    public void Refresh()
    {
        if (backend is null)
        {
            OnPropertiesChanged(nameof(Status));
            return;
        }
        try
        {
            var nextSnapshot = backend.ReadSnapshot();
            var nextRealtimeContract = backend.ReadRealtimeContract();
            var nextWhiteboxStatus = backend.ReadWhiteboxStatus();
            snapshot = nextSnapshot;
            selectedPresetName = string.IsNullOrWhiteSpace(snapshot.SourcePath) ||
                                 string.IsNullOrWhiteSpace(snapshot.PresetName)
                ? "SoloRig Default"
                : snapshot.PresetName;
            realtimeContract = nextRealtimeContract;
            whiteboxStatus = nextWhiteboxStatus;
            RebuildCanvasProjections();
            Status = $"Backend probe OK | schema {snapshot.SchemaVersion} | theme {(snapshot.DarkTheme ? "dark" : "light")}";
            OnPropertiesChanged(nameof(Snapshot), nameof(Nodes), nameof(CanvasNodes), nameof(CanvasConnections),
                nameof(SelectedNode), nameof(SelectedParameters), nameof(IsDirty), nameof(DarkTheme),
                nameof(WindowTitle), nameof(AudioStatus), nameof(MidiStatus), nameof(FileStatus),
                nameof(PerformanceStatus), nameof(ResourceStatus), nameof(PresetNames), nameof(SelectedPresetName),
                nameof(RealtimeContract),
                nameof(RealtimeStatus), nameof(WhiteboxStatus), nameof(WhiteboxStatusText));
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

    public bool AddModule(string moduleId, string instanceId, double x, double y)
    {
        if (backend is null) return false;
        try { backend.AddNode(moduleId, instanceId, x, y); Refresh(); Status = $"Added module: {instanceId}"; return true; }
        catch (Exception exception) { Status = exception.Message; Refresh(); return false; }
    }

    public bool DeleteNode(string instanceId)
    {
        if (backend is null) return false;
        try { backend.DeleteNode(instanceId); Refresh(); Status = $"Deleted node: {instanceId}"; return true; }
        catch (Exception exception) { Status = exception.Message; Refresh(); return false; }
    }

    public bool DuplicateNode(string sourceInstanceId, string newInstanceId)
    {
        if (backend is null) return false;
        try { backend.DuplicateNode(sourceInstanceId, newInstanceId); Refresh(); Status = $"Duplicated node: {newInstanceId}"; return true; }
        catch (Exception exception) { Status = exception.Message; Refresh(); return false; }
    }

    public bool ConnectNodes(string sourceInstanceId, string destinationInstanceId)
    {
        if (backend is null) return false;
        try { backend.Connect(sourceInstanceId, destinationInstanceId); Refresh(); Status = "Connection updated"; return true; }
        catch (Exception exception) { Status = exception.Message; Refresh(); return false; }
    }

    public bool DisconnectNodes(string sourceInstanceId, string destinationInstanceId)
    {
        if (backend is null) return false;
        try { backend.Disconnect(sourceInstanceId, destinationInstanceId); Refresh(); Status = "Connection removed"; return true; }
        catch (Exception exception) { Status = exception.Message; Refresh(); return false; }
    }

    public bool ImportResource(string path)
    {
        if (backend is null) return false;
        try { backend.ImportResource(path); Refresh(); Status = snapshot.ResourceStatus; return true; }
        catch (Exception exception) { Status = exception.Message; return false; }
    }

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

    public bool SelectPreset(string? name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return false;
        }
        if (string.Equals(name, "SoloRig Default", StringComparison.Ordinal))
        {
            if (string.IsNullOrWhiteSpace(snapshot.SourcePath) && !snapshot.Dirty &&
                string.Equals(snapshot.PresetName, "SoloRig Default", StringComparison.Ordinal))
            {
                return true;
            }
            if (backend is null)
            {
                return false;
            }
            try
            {
                backend.NewPreset();
                Refresh();
                Status = "Quick default preset selected";
                return true;
            }
            catch (Exception exception)
            {
                Status = exception.Message;
                return false;
            }
        }
        if (string.Equals(name, snapshot.PresetName, StringComparison.Ordinal))
        {
            return true;
        }
        Status = $"Preset is not loaded: {name}";
        return false;
    }

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

    private void NewPreset()
    {
        SelectPreset("SoloRig Default");
    }

    private void AddSelectedModule()
    {
        var module = SelectedCatalogModule ?? FilteredModuleCatalog.FirstOrDefault();
        if (module is null) return;
        var id = $"{module.DisplayName.ToLowerInvariant()}-{snapshot.Nodes.Count + 1}";
        AddModule(module.ModuleId, id, 64 + snapshot.Nodes.Count * 32, 300);
    }

    private void DeleteSelectedNode()
    {
        if (SelectedNode is not null) DeleteNode(SelectedNode.InstanceId);
    }

    private void DuplicateSelectedNode()
    {
        if (SelectedNode is not null) DuplicateNode(SelectedNode.InstanceId, $"{SelectedNode.InstanceId}-copy");
    }

    private void ConnectSelectedNodes()
    {
        if (snapshot.Nodes.Count >= 2) ConnectNodes(snapshot.Nodes[0].InstanceId, snapshot.Nodes[^1].InstanceId);
    }

    private void DisconnectSelectedNodes()
    {
        var connection = snapshot.Connections.FirstOrDefault();
        if (connection is not null) DisconnectNodes(connection.SourceInstanceId, connection.DestinationInstanceId);
    }

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

    private bool Set<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        return true;
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
