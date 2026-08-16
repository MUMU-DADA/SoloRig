using System.ComponentModel;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Windows.Input;

namespace SoloRig.Ui;

public sealed record CanvasNodeSnapshot(NodeSnapshot Source, double CanvasX, double CanvasY)
{
    public string InstanceId => Source.InstanceId;
    public string ModuleId => Source.ModuleId;
    public string DisplayName => FormatModuleName(Source.ModuleId, Source.InstanceId);
    public string CategoryText => Source.ModuleId.Contains("delay", StringComparison.OrdinalIgnoreCase)
        ? "Digital · Feedback"
        : Source.ModuleId.Contains("drive", StringComparison.OrdinalIgnoreCase)
            ? "Analog · Whitebox MNA"
            : "Whitebox · Audio module";
    public bool Selected => Source.Selected;
    public bool Unresolved => Source.Unresolved;
    public string Diagnostic => Source.Diagnostic;
    public IReadOnlyList<ParameterSnapshot> Parameters => Source.Parameters;
    public string LayoutText => Source.LayoutText;
    public string PerformanceText => Source.PerformanceText;
    public string LatencyText => $"{Source.DeclaredLatencySamples} samples · {Source.EstimatedCpuPercent:0.0}% CPU";
    public string NodeStateText => Unresolved ? "资源缺失" : "已启用";
    public double Width => 196.0;
    public double Height => 154.0;
    public string CardBackground => Selected ? "#29463F" : "#232D37";
    public string CardBorder => Selected ? "#4DD6A4" : "#4B5B6A";

    private static string FormatModuleName(string moduleId, string fallback)
    {
        var name = moduleId.Split('.', StringSplitOptions.RemoveEmptyEntries).LastOrDefault();
        return string.IsNullOrWhiteSpace(name) ? fallback : name.Replace('-', ' ');
    }
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
    private IReadOnlyList<AsioDriverSnapshot> asioDrivers = [];
    private IReadOnlyList<MidiDeviceSnapshot> midiDevices = [];
    private IReadOnlyList<MidiLearnTargetSnapshot> midiLearnTargets = [];
    private IReadOnlyList<uint> asioInputPorts = [];
    private IReadOnlyList<uint> asioOutputPorts = [];
    private int selectedAsioDriverIndex = -1;
    private int selectedMidiDeviceIndex = -1;
    private int selectedMidiTargetIndex = -1;
    private int selectedMidiBindingIndex = -1;
    private uint selectedInputChannel0;
    private uint selectedInputChannel1 = 1;
    private uint selectedOutputChannel0;
    private uint selectedOutputChannel1 = 1;
    private bool tunerEnabled;

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
    public bool IsBackendAvailable => backend is not null && backend.IsAvailable;
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
    public IReadOnlyList<AsioDriverSnapshot> AsioDrivers => asioDrivers;
    public IReadOnlyList<MidiDeviceSnapshot> MidiDevices => midiDevices;
    public IReadOnlyList<MidiLearnTargetSnapshot> MidiLearnTargets => midiLearnTargets;
    public IReadOnlyList<BindingSnapshot> MidiBindings => snapshot.Bindings;
    public IReadOnlyList<uint> AsioInputPorts => asioInputPorts;
    public IReadOnlyList<uint> AsioOutputPorts => asioOutputPorts;
    public int SelectedAsioDriverIndex
    {
        get => selectedAsioDriverIndex;
        set
        {
            if (Set(ref selectedAsioDriverIndex, value))
            {
                UpdateAsioPortLists();
                OnPropertiesChanged(nameof(SelectedAsioDriver), nameof(AsioRouteText),
                    nameof(FixedInputPortText), nameof(FixedOutputPortText));
            }
        }
    }
    public int SelectedMidiDeviceIndex
    {
        get => selectedMidiDeviceIndex;
        set => Set(ref selectedMidiDeviceIndex, value);
    }
    public int SelectedMidiTargetIndex
    {
        get => selectedMidiTargetIndex;
        set => Set(ref selectedMidiTargetIndex, value);
    }
    public int SelectedMidiBindingIndex
    {
        get => selectedMidiBindingIndex;
        set => Set(ref selectedMidiBindingIndex, value);
    }
    public MidiLearnTargetSnapshot? SelectedMidiLearnTarget =>
        selectedMidiTargetIndex >= 0 && selectedMidiTargetIndex < midiLearnTargets.Count
            ? midiLearnTargets[selectedMidiTargetIndex]
            : null;
    public AsioDriverSnapshot? SelectedAsioDriver =>
        selectedAsioDriverIndex >= 0 && selectedAsioDriverIndex < asioDrivers.Count
            ? asioDrivers[selectedAsioDriverIndex]
            : null;
    public MidiDeviceSnapshot? SelectedMidiDevice =>
        selectedMidiDeviceIndex >= 0 && selectedMidiDeviceIndex < midiDevices.Count
            ? midiDevices[selectedMidiDeviceIndex]
            : null;
    public uint SelectedInputChannel0
    {
        get => selectedInputChannel0;
        set
        {
            if (Set(ref selectedInputChannel0, value))
            {
                OnPropertiesChanged(nameof(AsioRouteText), nameof(FixedInputPortText));
            }
        }
    }
    public uint SelectedInputChannel1
    {
        get => selectedInputChannel1;
        set
        {
            if (Set(ref selectedInputChannel1, value))
            {
                OnPropertiesChanged(nameof(AsioRouteText), nameof(FixedInputPortText));
            }
        }
    }
    public uint SelectedOutputChannel0
    {
        get => selectedOutputChannel0;
        set
        {
            if (Set(ref selectedOutputChannel0, value))
            {
                OnPropertiesChanged(nameof(AsioRouteText), nameof(FixedOutputPortText));
            }
        }
    }
    public uint SelectedOutputChannel1
    {
        get => selectedOutputChannel1;
        set
        {
            if (Set(ref selectedOutputChannel1, value))
            {
                OnPropertiesChanged(nameof(AsioRouteText), nameof(FixedOutputPortText));
            }
        }
    }
    public bool TunerEnabled
    {
        get => tunerEnabled;
        private set => Set(ref tunerEnabled, value);
    }
    public string AsioRouteText => SelectedAsioDriver is null
        ? "ASIO: 未选择设备；输入/输出端口固定为选定设备的物理通道"
        : $"ASIO: {SelectedAsioDriver.DisplayName} · 输入 {SelectedInputChannel0}/{SelectedInputChannel1} → 输出 {SelectedOutputChannel0}/{SelectedOutputChannel1}";
    public string FixedInputPortText => SelectedAsioDriver is null
        ? "固定输入 · 未选 ASIO 设备"
        : $"固定输入 · {SelectedAsioDriver.DisplayName} · 物理通道 {SelectedInputChannel0}/{SelectedInputChannel1}";
    public string FixedOutputPortText => SelectedAsioDriver is null
        ? "固定输出 · 未选 ASIO 设备"
        : $"固定输出 · {SelectedAsioDriver.DisplayName} · 物理通道 {SelectedOutputChannel0}/{SelectedOutputChannel1}";
    public string TunerStatus => $"调音器: {(TunerEnabled ? "开启" : "关闭")}";
    public string MidiLearnStatus => snapshot.MidiStatus.Contains("learn armed", StringComparison.OrdinalIgnoreCase)
        ? "学习中：等待 CC"
        : "未进入学习";
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
            var nextMidiLearnTargets = backend.ReadMidiLearnTargets();
            snapshot = nextSnapshot;
            selectedPresetName = string.IsNullOrWhiteSpace(snapshot.SourcePath) ||
                                 string.IsNullOrWhiteSpace(snapshot.PresetName)
                ? "SoloRig Default"
                : snapshot.PresetName;
            realtimeContract = nextRealtimeContract;
            whiteboxStatus = nextWhiteboxStatus;
            midiLearnTargets = nextMidiLearnTargets;
            selectedMidiTargetIndex = midiLearnTargets.Count == 0
                ? -1
                : selectedMidiTargetIndex < 0 || selectedMidiTargetIndex >= midiLearnTargets.Count
                    ? 0
                    : selectedMidiTargetIndex;
            selectedMidiBindingIndex = snapshot.Bindings.Count == 0
                ? -1
                : selectedMidiBindingIndex < 0 || selectedMidiBindingIndex >= snapshot.Bindings.Count
                    ? 0
                    : selectedMidiBindingIndex;
            RebuildCanvasProjections();
            Status = $"Backend probe OK | schema {snapshot.SchemaVersion} | theme {(snapshot.DarkTheme ? "dark" : "light")}";
            OnPropertiesChanged(nameof(Snapshot), nameof(Nodes), nameof(CanvasNodes), nameof(CanvasConnections),
                nameof(SelectedNode), nameof(SelectedParameters), nameof(IsDirty), nameof(DarkTheme),
                nameof(WindowTitle), nameof(AudioStatus), nameof(MidiStatus), nameof(FileStatus),
                nameof(PerformanceStatus), nameof(ResourceStatus), nameof(PresetNames), nameof(SelectedPresetName),
                nameof(MidiLearnTargets), nameof(MidiBindings), nameof(SelectedMidiLearnTarget),
                nameof(SelectedMidiTargetIndex), nameof(SelectedMidiBindingIndex), nameof(MidiLearnStatus),
                nameof(RealtimeContract),
                nameof(RealtimeStatus), nameof(WhiteboxStatus), nameof(WhiteboxStatusText),
                nameof(AsioRouteText), nameof(TunerStatus));
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

    public bool RefreshAsioDevices()
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.RefreshAsioDrivers();
            asioDrivers = backend.ReadAsioDrivers();
            OnPropertiesChanged(nameof(AsioDrivers));
            SelectedAsioDriverIndex = asioDrivers.Count == 0 ? -1 : 0;
            Refresh();
            Status = asioDrivers.Count == 0
                ? "未发现 ASIO 驱动；请确认驱动已安装"
                : $"已发现 {asioDrivers.Count} 个 ASIO 驱动";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public bool OpenSelectedAsio(IntPtr nativeWindow)
    {
        if (backend is null || SelectedAsioDriver is null)
        {
            Status = "请先刷新并选择 ASIO 设备";
            return false;
        }
        if (SelectedAsioDriver.InputChannelCount < 2 || SelectedAsioDriver.OutputChannelCount < 2)
        {
            Status = "所选 ASIO 设备至少需要 2 个输入和 2 个输出通道";
            return false;
        }
        try
        {
            backend.OpenAsio((uint)selectedAsioDriverIndex, nativeWindow,
                SelectedInputChannel0, SelectedInputChannel1, SelectedOutputChannel0, SelectedOutputChannel1);
            Refresh();
            Status = $"ASIO 已打开；固定输入 {SelectedInputChannel0}/{SelectedInputChannel1}，固定输出 {SelectedOutputChannel0}/{SelectedOutputChannel1}";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            Refresh();
            return false;
        }
    }

    public bool StartAsio()
    {
        return RunAudioAction(() => backend?.StartAsio(), "ASIO 已启动");
    }

    public bool StopAsio()
    {
        return RunAudioAction(() => backend?.StopAsio(), "ASIO 已停止");
    }

    public bool CloseAsio()
    {
        return RunAudioAction(() => backend?.CloseAsio(), "ASIO 已关闭");
    }

    public bool RecoverAsio()
    {
        return RunAudioAction(() => backend?.RecoverAsio(), "ASIO 已执行恢复");
    }

    public bool SetTunerEnabled(bool enabled)
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.SetTunerEnabled(enabled);
            TunerEnabled = enabled;
            Refresh();
            Status = TunerStatus;
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public bool RefreshMidiDevices()
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.RefreshMidiDevices();
            midiDevices = backend.ReadMidiDevices();
            OnPropertiesChanged(nameof(MidiDevices));
            SelectedMidiDeviceIndex = midiDevices.Count == 0 ? -1 : 0;
            Refresh();
            Status = midiDevices.Count == 0
                ? "未发现 MIDI 输入设备"
                : $"已发现 {midiDevices.Count} 个 MIDI 输入设备";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public bool OpenSelectedMidi()
    {
        if (backend is null || SelectedMidiDevice is null)
        {
            Status = "请先刷新并选择 MIDI 输入设备";
            return false;
        }
        try
        {
            backend.OpenMidi(SelectedMidiDevice.DeviceIndex);
            Refresh();
            Status = $"MIDI 已打开: {SelectedMidiDevice.DisplayName}";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            Refresh();
            return false;
        }
    }

    public bool CloseMidi()
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.CloseMidi();
            Refresh();
            Status = "MIDI 已关闭";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public bool DrainMidi()
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            var processed = backend.DrainMidi();
            Refresh();
            Status = $"MIDI 已处理 {processed} 条消息";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public bool ArmSelectedMidiLearn()
    {
        if (backend is null || SelectedMidiLearnTarget is null)
        {
            Status = "请先选择 MIDI Learn 目标";
            return false;
        }
        try
        {
            backend.ArmMidiLearn(SelectedMidiLearnTarget.TargetId);
            Refresh();
            Status = $"MIDI Learn 已开始: {SelectedMidiLearnTarget.DisplayName}";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            Refresh();
            return false;
        }
    }

    public bool CancelMidiLearn()
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.CancelMidiLearn();
            Refresh();
            Status = "MIDI Learn 已取消";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
    }

    public bool DeleteSelectedMidiBinding()
    {
        if (backend is null || selectedMidiBindingIndex < 0 || selectedMidiBindingIndex >= snapshot.Bindings.Count)
        {
            Status = "请先选择 MIDI 绑定";
            return false;
        }
        try
        {
            backend.DeleteMidiBinding((uint)selectedMidiBindingIndex);
            Refresh();
            Status = "MIDI 绑定已删除";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            Refresh();
            return false;
        }
    }

    public bool ClearMidiBindings()
    {
        if (backend is null)
        {
            return false;
        }
        try
        {
            backend.ClearMidiBindings();
            Refresh();
            Status = "MIDI 绑定已清空";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            Refresh();
            return false;
        }
    }

    public bool ImportMidiBank(string directoryPath)
    {
        if (backend is null || string.IsNullOrWhiteSpace(directoryPath))
        {
            return false;
        }
        try
        {
            backend.ImportMidiBank(directoryPath);
            Refresh();
            Status = "MIDI Bank 已导入，Program Change 已按文件名建立映射";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            Refresh();
            return false;
        }
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

    public void ReportInteractionStatus(string message) => Status = message;

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
        var baseId = module.ModuleId.Split('.').LastOrDefault() ?? "module";
        var id = CreateUniqueNodeId(baseId);
        AddModule(module.ModuleId, id, 64 + snapshot.Nodes.Count * 32, 300);
    }

    private void DeleteSelectedNode()
    {
        if (SelectedNode is not null) DeleteNode(SelectedNode.InstanceId);
    }

    private void DuplicateSelectedNode()
    {
        if (SelectedNode is not null)
        {
            DuplicateNode(SelectedNode.InstanceId, CreateUniqueNodeId($"{SelectedNode.InstanceId}-copy"));
        }
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

    private string CreateUniqueNodeId(string proposed)
    {
        var sanitized = new string(proposed
            .Select(character => char.IsLetterOrDigit(character) || character is '-' or '_' ? character : '-')
            .ToArray())
            .Trim('-');
        if (string.IsNullOrWhiteSpace(sanitized))
        {
            sanitized = "module";
        }
        var candidate = sanitized;
        var suffix = 2;
        while (snapshot.Nodes.Any(node => string.Equals(node.InstanceId, candidate, StringComparison.Ordinal)))
        {
            candidate = $"{sanitized}-{suffix++}";
        }
        return candidate;
    }

    private bool RunAudioAction(Action? action, string successStatus)
    {
        if (action is null)
        {
            Status = "音频后端不可用";
            return false;
        }
        try
        {
            action();
            Refresh();
            Status = successStatus;
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            Refresh();
            return false;
        }
    }

    private void UpdateAsioPortLists()
    {
        var driver = SelectedAsioDriver;
        asioInputPorts = driver is null ? [] : Enumerable.Range(0, checked((int)driver.InputChannelCount)).Select(static value => (uint)value).ToArray();
        asioOutputPorts = driver is null ? [] : Enumerable.Range(0, checked((int)driver.OutputChannelCount)).Select(static value => (uint)value).ToArray();
        if (asioInputPorts.Count > 0)
        {
            SelectedInputChannel0 = asioInputPorts.Contains(SelectedInputChannel0) ? SelectedInputChannel0 : asioInputPorts[0];
            SelectedInputChannel1 = asioInputPorts.Contains(SelectedInputChannel1) ? SelectedInputChannel1 :
                asioInputPorts[Math.Min(1, asioInputPorts.Count - 1)];
        }
        else
        {
            SelectedInputChannel0 = 0;
            SelectedInputChannel1 = 1;
        }
        if (asioOutputPorts.Count > 0)
        {
            SelectedOutputChannel0 = asioOutputPorts.Contains(SelectedOutputChannel0) ? SelectedOutputChannel0 : asioOutputPorts[0];
            SelectedOutputChannel1 = asioOutputPorts.Contains(SelectedOutputChannel1) ? SelectedOutputChannel1 :
                asioOutputPorts[Math.Min(1, asioOutputPorts.Count - 1)];
        }
        else
        {
            SelectedOutputChannel0 = 0;
            SelectedOutputChannel1 = 1;
        }
        OnPropertiesChanged(nameof(AsioInputPorts), nameof(AsioOutputPorts), nameof(AsioRouteText));
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
