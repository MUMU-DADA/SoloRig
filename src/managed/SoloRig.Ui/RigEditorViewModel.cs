using System.ComponentModel;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Windows.Input;

namespace SoloRig.Ui;

public sealed record NodeControlSnapshot(string Id, string Label, float Value, float Minimum, float Maximum, bool IsToggle,
                                         string ImagePath = "")
{
    public bool IsKnob => !IsToggle;
    public bool HasImage => !string.IsNullOrWhiteSpace(ImagePath);
    public string ValueText => Value.ToString("0.##", CultureInfo.InvariantCulture);
    public string RangeText => $"{Minimum.ToString("0.##", CultureInfo.InvariantCulture)}..{Maximum.ToString("0.##", CultureInfo.InvariantCulture)}";
    public double KnobAngle
    {
        get
        {
            if (!float.IsFinite(Minimum) || !float.IsFinite(Maximum) || Maximum <= Minimum)
            {
                return 0.0;
            }
            var normalized = Math.Clamp((Value - Minimum) / (Maximum - Minimum), 0.0F, 1.0F);
            return -135.0 + normalized * 270.0;
        }
    }
}

public sealed record CanvasNodeSnapshot(NodeSnapshot Source, double CanvasX, double CanvasY, bool DarkTheme)
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
    public bool Bypassed => Source.Bypassed;
    public NodeSkinSnapshot Skin => Source.Skin;
    public string Diagnostic => Source.Diagnostic;
    public IReadOnlyList<ParameterSnapshot> Parameters => Source.Parameters;
    public string LayoutText => Source.LayoutText;
    public string PerformanceText => Source.PerformanceText;
    public string LatencyText => $"{Source.DeclaredLatencySamples} samples · {Source.EstimatedCpuPercent:0.0}% CPU";
    public string NodeStateText => Unresolved ? "资源缺失" : Bypassed ? "旁路" : "已启用";
    public string FootswitchText => Unresolved ? "缺失" : Bypassed ? "旁路" : "启用";
    public double FootswitchOpacity => Unresolved ? 0.45 : Bypassed ? 0.72 : 1.0;
    public IReadOnlyList<NodeControlSnapshot> Controls => Parameters
        .Take(3)
        .Select(parameter =>
        {
            var isToggle = IsToggleParameter(parameter.Id);
            var imagePath = isToggle ? Skin.ToggleImagePath : Skin.KnobImagePath;
            return new NodeControlSnapshot(parameter.Id, FormatParameterName(parameter.Id), parameter.Value,
                parameter.Minimum, parameter.Maximum, isToggle, imagePath);
        })
        .ToArray();
    public string FootswitchImagePath => Skin.FootswitchImagePath;
    public bool HasFootswitchImage => Skin.HasFootswitchImage;
    public double Width => 196.0;
    public double Height => 154.0;
    public string CardBackground => Selected
        ? (DarkTheme ? "#29463F" : "#D8F3E9")
        : (DarkTheme ? "#232D37" : "#FFFFFF");
    public string CardBorder => Selected
        ? (DarkTheme ? "#4DD6A4" : "#0F766E")
        : (DarkTheme ? "#4B5B6A" : "#B9C6D3");

    private static string FormatModuleName(string moduleId, string fallback)
    {
        var name = moduleId.Split('.', StringSplitOptions.RemoveEmptyEntries).LastOrDefault();
        return string.IsNullOrWhiteSpace(name) ? fallback : name.Replace('-', ' ');
    }

    private static string FormatParameterName(string id) => id switch
    {
        "drive" => "增益",
        "outputGain" => "输出",
        "diodeThreshold" => "阈值",
        "time" => "时间",
        "feedback" => "反馈",
        "mix" => "混合",
        _ => id
    };

    private static bool IsToggleParameter(string id) =>
        id.Contains("bypass", StringComparison.OrdinalIgnoreCase) ||
        id.Contains("toggle", StringComparison.OrdinalIgnoreCase) ||
        id.Contains("switch", StringComparison.OrdinalIgnoreCase) ||
        id.Contains("mode", StringComparison.OrdinalIgnoreCase);
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

public sealed record ParameterEditorSnapshot(string Target, string Id, float Value, float Minimum, float Maximum)
{
    public string ValueText => Value.ToString("0.###", CultureInfo.InvariantCulture);
    public string RangeText => $"{Minimum.ToString("0.###", CultureInfo.InvariantCulture)} .. {Maximum.ToString("0.###", CultureInfo.InvariantCulture)}";
    public bool IsValidRange => float.IsFinite(Minimum) && float.IsFinite(Maximum) && Maximum > Minimum;
}

public sealed class AsioRouteSelection : INotifyPropertyChanged
{
    private IReadOnlyList<AsioDriverSnapshot> driverOptions = [];
    private IReadOnlyList<uint> deviceChannels = [];
    private int selectedDriverIndex = -1;
    private uint selectedDeviceChannel;

    public AsioRouteSelection(string directionLabel, uint processingChannel)
    {
        DirectionLabel = directionLabel;
        ProcessingChannel = processingChannel;
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    public string DirectionLabel { get; }
    public uint ProcessingChannel { get; }
    public IReadOnlyList<AsioDriverSnapshot> DriverOptions => driverOptions;
    public IReadOnlyList<uint> DeviceChannels => deviceChannels;
    public int SelectedDriverIndex
    {
        get => selectedDriverIndex;
        set
        {
            if (selectedDriverIndex == value)
            {
                return;
            }
            selectedDriverIndex = value;
            UpdateDeviceChannels();
            OnPropertiesChanged(nameof(SelectedDriver), nameof(HasSelectedDriver), nameof(DeviceChannels), nameof(SelectedDeviceChannel),
                nameof(IsConfigured), nameof(RouteText));
        }
    }
    public uint SelectedDeviceChannel
    {
        get => selectedDeviceChannel;
        set
        {
            if (selectedDeviceChannel == value)
            {
                return;
            }
            selectedDeviceChannel = value;
            OnPropertiesChanged(nameof(IsConfigured), nameof(RouteText));
        }
    }
    public AsioDriverSnapshot? SelectedDriver => selectedDriverIndex >= 0 && selectedDriverIndex < driverOptions.Count
        ? driverOptions[selectedDriverIndex]
        : null;
    public bool HasSelectedDriver => SelectedDriver is not null;
    public bool IsConfigured => SelectedDriver is not null && deviceChannels.Contains(selectedDeviceChannel);
    public string RouteText => SelectedDriver is null
        ? $"{DirectionLabel} → 未选择设备"
        : $"{DirectionLabel} · {SelectedDriver.DisplayName} 通道 {SelectedDeviceChannel} → 内部 {ProcessingChannel + 1}";

    public void SetDrivers(IReadOnlyList<AsioDriverSnapshot> drivers)
    {
        var previousDriverId = SelectedDriver?.DriverId;
        driverOptions = drivers;
        selectedDriverIndex = previousDriverId is null
            ? (drivers.Count > 0 ? 0 : -1)
            : drivers.Select((driver, index) => (driver, index))
                .FirstOrDefault(item => string.Equals(item.driver.DriverId, previousDriverId, StringComparison.Ordinal)).index;
        if (previousDriverId is not null &&
            (selectedDriverIndex < 0 || selectedDriverIndex >= drivers.Count ||
             !string.Equals(drivers[selectedDriverIndex].DriverId, previousDriverId, StringComparison.Ordinal)))
        {
            selectedDriverIndex = drivers.Count > 0 ? 0 : -1;
        }
        UpdateDeviceChannels();
        OnPropertiesChanged(nameof(DriverOptions), nameof(SelectedDriverIndex), nameof(SelectedDriver), nameof(HasSelectedDriver),
            nameof(DeviceChannels), nameof(SelectedDeviceChannel), nameof(IsConfigured), nameof(RouteText));
    }

    public AsioChannelRouteSnapshot ToSnapshot() => new(
        (uint)selectedDriverIndex, selectedDeviceChannel, ProcessingChannel);

    private void UpdateDeviceChannels()
    {
        var driver = SelectedDriver;
        var count = driver is null ? 0U : (DirectionLabel.StartsWith("输入", StringComparison.Ordinal)
            ? driver.InputChannelCount
            : driver.OutputChannelCount);
        deviceChannels = count == 0U ? [] : Enumerable.Range(0, checked((int)count)).Select(static value => (uint)value).ToArray();
        if (deviceChannels.Count == 0)
        {
            selectedDeviceChannel = 0U;
        }
        else if (!deviceChannels.Contains(selectedDeviceChannel))
        {
            selectedDeviceChannel = deviceChannels.Contains(ProcessingChannel) ? ProcessingChannel : deviceChannels[0];
        }
    }

    private void OnPropertiesChanged(params string[] names)
    {
        foreach (var name in names)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }
    }
}

public sealed class RigEditorViewModel : INotifyPropertyChanged, IDisposable
{
    private readonly BackendSession? backend;
    private BackendSnapshot snapshot = EmptySnapshot;
    private ThemeSnapshot theme = ThemeSnapshot.Empty;
    private IReadOnlyList<CanvasNodeSnapshot> canvasNodes = [];
    private IReadOnlyList<CanvasConnectionSnapshot> canvasConnections = [];
    private IReadOnlyList<ModuleCatalogSnapshot> moduleCatalog = [];
    private string moduleSearchText = string.Empty;
    private string moduleCatalogFilter = "全部";
    private ModuleCatalogSnapshot? selectedCatalogModule;
    private string inspectorTab = "参数";
    private string selectedPresetName = "SoloRig Default";
    private string status = "Starting backend probe...";
    private RealtimeContractSnapshot realtimeContract = EmptyRealtimeContract;
    private WhiteboxStatusSnapshot whiteboxStatus = EmptyWhiteboxStatus;
    private IReadOnlyList<AsioDriverSnapshot> asioDrivers = [];
    private readonly IReadOnlyList<AsioRouteSelection> asioInputRoutes =
    [
        new AsioRouteSelection("输入 1", 0U),
        new AsioRouteSelection("输入 2", 1U),
    ];
    private readonly IReadOnlyList<AsioRouteSelection> asioOutputRoutes =
    [
        new AsioRouteSelection("输出 1", 0U),
        new AsioRouteSelection("输出 2", 1U),
    ];
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
        1280, 720, 1, false, [], [], [], 0, 0, "No external resource imported", 0, 0, 0, false,
        false, false, false, false, 0.0, false, false, 0, 0U, 0UL);

    public RigEditorViewModel()
    {
        foreach (var route in asioInputRoutes.Concat(asioOutputRoutes))
        {
            route.PropertyChanged += AsioRouteChanged;
        }
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
    public ThemeSnapshot Theme => theme;
    public string ThemeDisplayName => string.IsNullOrWhiteSpace(theme.DisplayName) ? "主题不可用" : theme.DisplayName;
    public IReadOnlyList<NodeSnapshot> Nodes => snapshot.Nodes;
    public IReadOnlyList<CanvasNodeSnapshot> CanvasNodes => canvasNodes;
    public IReadOnlyList<CanvasConnectionSnapshot> CanvasConnections => canvasConnections;
    public IReadOnlyList<ModuleCatalogSnapshot> ModuleCatalog => moduleCatalog;
    public IReadOnlyList<ModuleCatalogSnapshot> FilteredModuleCatalog => moduleCatalog
        .Where(module => string.IsNullOrWhiteSpace(ModuleSearchText) ||
                         module.DisplayText.Contains(ModuleSearchText, StringComparison.OrdinalIgnoreCase) ||
                         module.ModuleId.Contains(ModuleSearchText, StringComparison.OrdinalIgnoreCase))
        .Where(MatchesModuleCatalogFilter)
        .ToArray();
    public string ModuleCatalogFilter => moduleCatalogFilter;
    public string ModuleCatalogSummaryText => $"{FilteredModuleCatalog.Count} 个可用 · {ModuleCatalogFilter}";
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
    public bool SetModuleCatalogFilter(string filter)
    {
        if (filter is not ("全部" or "模拟" or "数字" or "外部"))
        {
            return false;
        }
        if (string.Equals(moduleCatalogFilter, filter, StringComparison.Ordinal))
        {
            return true;
        }
        moduleCatalogFilter = filter;
        if (selectedCatalogModule is not null &&
            !FilteredModuleCatalog.Any(module => module.ModuleId == selectedCatalogModule.ModuleId))
        {
            selectedCatalogModule = null;
        }
        OnPropertiesChanged(nameof(ModuleCatalogFilter), nameof(ModuleCatalogSummaryText),
            nameof(FilteredModuleCatalog), nameof(SelectedCatalogModule));
        Status = $"模块库筛选: {filter}";
        return true;
    }
    public NodeSnapshot? SelectedNode => snapshot.Nodes.FirstOrDefault(node => node.Selected);
    public string SelectedNodeDisplayName => SelectedNode is null
        ? "未选择效果器"
        : CanvasNodes.FirstOrDefault(node => node.InstanceId == SelectedNode.InstanceId)?.DisplayName ?? SelectedNode.InstanceId;
    public IReadOnlyList<ParameterEditorSnapshot> SelectedParameters =>
        SelectedNode is null
            ? []
            : SelectedNode.Parameters.Select(parameter =>
                new ParameterEditorSnapshot($"{SelectedNode.InstanceId}.{parameter.Id}", parameter.Id, parameter.Value,
                    parameter.Minimum, parameter.Maximum)).ToArray();
    public bool IsDirty => snapshot.Dirty;
    public bool DarkTheme => snapshot.DarkTheme;
    public string WindowTitle => string.IsNullOrWhiteSpace(snapshot.PresetName)
        ? "SoloRig"
        : $"SoloRig - {snapshot.PresetName}{(snapshot.Dirty ? " *" : string.Empty)}";
    public string AudioStatus => $"Audio: {snapshot.AudioStatus}";
    public bool AudioOpen => snapshot.AudioOpen;
    public bool AudioRunning => snapshot.AudioRunning;
    public bool AudioResetRequested => snapshot.AudioResetRequested;
    public bool AudioDropoutReported => snapshot.AudioDropoutReported;
    public double AudioSampleRateChangedTo => snapshot.AudioSampleRateChangedTo;
    public string MidiStatus => $"MIDI: {snapshot.MidiStatus} | mappings: {snapshot.Bindings.Count}";
    public bool MidiOpen => snapshot.MidiOpen;
    public bool MidiLearnArmed => snapshot.MidiLearnArmed;
    public byte MidiState => snapshot.MidiState;
    public uint MidiSelectedDeviceIndex => snapshot.MidiSelectedDeviceIndex;
    public ulong MidiDroppedMessageCount => snapshot.MidiDroppedMessageCount;
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
    public IReadOnlyList<AsioRouteSelection> AsioInputRoutes => asioInputRoutes;
    public IReadOnlyList<AsioRouteSelection> AsioOutputRoutes => asioOutputRoutes;
    public bool HasAsioDrivers => asioDrivers.Count > 0;
    public bool HasSelectedAsio => asioInputRoutes.Any(route => route.SelectedDriver is not null) ||
        asioOutputRoutes.Any(route => route.SelectedDriver is not null);
    public bool AsioRouteMapValid
    {
        get
        {
            if (!asioInputRoutes.All(route => route.IsConfigured) || !asioOutputRoutes.All(route => route.IsConfigured))
            {
                return false;
            }
            var inputs = asioInputRoutes.Select(route => route.ToSnapshot()).ToArray();
            var outputs = asioOutputRoutes.Select(route => route.ToSnapshot()).ToArray();
            return inputs.GroupBy(route => (route.DriverIndex, route.DeviceChannel)).All(group => group.Count() == 1) &&
                inputs.GroupBy(route => route.ProcessingChannel).All(group => group.Count() == 1) &&
                outputs.GroupBy(route => (route.DriverIndex, route.DeviceChannel)).All(group => group.Count() == 1);
        }
    }
    public string AsioRouteValidationText => !asioInputRoutes.All(route => route.IsConfigured) ||
        !asioOutputRoutes.All(route => route.IsConfigured)
        ? "路由待配置"
        : AsioRouteMapValid ? "路由有效 · 可使用多个 ASIO 设备" : "路由无效 · 输入处理通道或物理端口重复";
    public bool CanOpenAsio => HasAsioDrivers && !AudioOpen && AsioRouteMapValid;
    public bool CanStartAsio => AudioOpen && !AudioRunning;
    public bool CanStopAsio => AudioRunning;
    public bool CanCloseAsio => AudioOpen;
    public bool CanRecoverAsio => AudioOpen &&
        (AudioResetRequested || AudioDropoutReported || (double.IsFinite(AudioSampleRateChangedTo) && AudioSampleRateChangedTo > 0.0));
    public IReadOnlyList<MidiDeviceSnapshot> MidiDevices => midiDevices;
    public bool HasMidiDevices => midiDevices.Count > 0;
    public IReadOnlyList<MidiLearnTargetSnapshot> MidiLearnTargets => midiLearnTargets;
    public IReadOnlyList<BindingSnapshot> MidiBindings => snapshot.Bindings;
    public bool CanOpenMidi => SelectedMidiDevice is not null && !MidiOpen;
    public bool CanCloseMidi => MidiOpen;
    public bool CanDrainMidi => MidiOpen;
    public bool CanArmMidiLearn => MidiOpen && SelectedMidiLearnTarget is not null && !MidiLearnArmed;
    public bool CanCancelMidiLearn => MidiLearnArmed;
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
                    nameof(FixedInputPortText), nameof(FixedOutputPortText), nameof(HasSelectedAsio),
                    nameof(CanOpenAsio));
            }
        }
    }
    public int SelectedMidiDeviceIndex
    {
        get => selectedMidiDeviceIndex;
        set
        {
            if (Set(ref selectedMidiDeviceIndex, value))
            {
                OnPropertiesChanged(nameof(SelectedMidiDevice), nameof(CanOpenMidi));
            }
        }
    }
    public int SelectedMidiTargetIndex
    {
        get => selectedMidiTargetIndex;
        set
        {
            if (Set(ref selectedMidiTargetIndex, value))
            {
                OnPropertiesChanged(nameof(SelectedMidiLearnTarget), nameof(CanArmMidiLearn));
            }
        }
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
    public string InspectorTab => inspectorTab;
    public bool IsParameterInspectorVisible => inspectorTab == "参数";
    public bool IsPortInspectorVisible => inspectorTab == "端口";
    public bool IsResourceInspectorVisible => inspectorTab == "资源";
    public bool SelectInspectorTab(string tab)
    {
        if (tab is not ("参数" or "端口" or "资源"))
        {
            return false;
        }
        if (string.Equals(inspectorTab, tab, StringComparison.Ordinal))
        {
            return true;
        }
        inspectorTab = tab;
        OnPropertiesChanged(nameof(InspectorTab), nameof(IsParameterInspectorVisible),
            nameof(IsPortInspectorVisible), nameof(IsResourceInspectorVisible));
        Status = $"检查器页签: {tab}";
        return true;
    }
    public string AsioRouteText => "ASIO: " + string.Join(" | ", asioInputRoutes.Concat(asioOutputRoutes).Select(route => route.RouteText));
    public string FixedInputPortText => "输入路由 · " + string.Join(" | ", asioInputRoutes.Select(route => route.RouteText));
    public string FixedOutputPortText => "输出路由 · " + string.Join(" | ", asioOutputRoutes.Select(route => route.RouteText));
    public string TunerStatus => $"调音器: {(TunerEnabled ? "开启" : "关闭")}";
    public string MidiLifecycleText => $"MIDI 生命周期: {snapshot.MidiState switch
    {
        0 => "closed",
        1 => "no device",
        2 => "open",
        3 => "learn armed",
        4 => "learned",
        5 => "error",
        _ => "unknown"
    }} · 丢弃 {snapshot.MidiDroppedMessageCount} 条";
    public string MidiLearnStatus => MidiLearnArmed
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
            var nextModuleCatalog = backend.ReadCatalog();
            var nextSnapshot = backend.ReadSnapshot();
            var nextTheme = backend.ReadTheme();
            var nextRealtimeContract = backend.ReadRealtimeContract();
            var nextWhiteboxStatus = backend.ReadWhiteboxStatus();
            var nextMidiLearnTargets = backend.ReadMidiLearnTargets();
            var selectedCatalogModuleId = selectedCatalogModule?.ModuleId;
            moduleCatalog = nextModuleCatalog;
            selectedCatalogModule = selectedCatalogModuleId is null
                ? null
                : moduleCatalog.FirstOrDefault(module => module.ModuleId == selectedCatalogModuleId);
            snapshot = nextSnapshot;
            theme = nextTheme;
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
                nameof(SelectedNode), nameof(SelectedNodeDisplayName), nameof(SelectedParameters), nameof(IsDirty), nameof(DarkTheme),
                nameof(Theme), nameof(ThemeDisplayName),
                nameof(WindowTitle), nameof(AudioStatus), nameof(AudioOpen), nameof(AudioRunning),
                nameof(AudioResetRequested), nameof(AudioDropoutReported), nameof(AudioSampleRateChangedTo),
                nameof(CanOpenAsio), nameof(CanStartAsio), nameof(CanStopAsio), nameof(CanCloseAsio),
                nameof(CanRecoverAsio), nameof(MidiStatus), nameof(MidiOpen), nameof(MidiLearnArmed),
                nameof(MidiState), nameof(MidiSelectedDeviceIndex), nameof(MidiDroppedMessageCount),
                nameof(MidiLifecycleText), nameof(CanOpenMidi), nameof(CanCloseMidi), nameof(CanDrainMidi),
                nameof(CanArmMidiLearn), nameof(CanCancelMidiLearn), nameof(FileStatus),
                nameof(PerformanceStatus), nameof(ResourceStatus), nameof(PresetNames), nameof(SelectedPresetName),
                nameof(ModuleCatalog), nameof(ModuleCatalogSummaryText), nameof(FilteredModuleCatalog),
                nameof(SelectedCatalogModule),
                nameof(MidiDevices), nameof(HasMidiDevices), nameof(MidiLearnTargets), nameof(MidiBindings), nameof(SelectedMidiDevice), nameof(SelectedMidiLearnTarget),
                nameof(SelectedMidiTargetIndex), nameof(SelectedMidiBindingIndex), nameof(MidiLearnStatus),
                nameof(RealtimeContract),
                nameof(RealtimeStatus), nameof(WhiteboxStatus), nameof(WhiteboxStatusText),
                nameof(AsioInputRoutes), nameof(AsioOutputRoutes), nameof(HasAsioDrivers), nameof(HasSelectedAsio),
                nameof(AsioRouteMapValid), nameof(AsioRouteValidationText), nameof(AsioRouteText), nameof(TunerStatus));
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
                nameof(SelectedNode), nameof(SelectedNodeDisplayName), nameof(SelectedParameters), nameof(IsDirty), nameof(DarkTheme), nameof(WindowTitle),
                nameof(FileStatus));
        }
        catch (Exception exception)
        {
            Status = exception.Message;
        }
    }

    public bool ProbeDpiScaling()
    {
        if (backend is null || snapshot.ViewportWidth <= 0 || snapshot.ViewportHeight <= 0 ||
            CanvasNodes.Count < 2 || CanvasConnections.Count == 0)
        {
            return false;
        }
        var originalWidth = snapshot.ViewportWidth;
        var originalHeight = snapshot.ViewportHeight;
        var originalDpi = snapshot.DpiScale;
        var originalNode = CanvasNodes[0];
        var originalConnection = CanvasConnections[0];
        try
        {
            foreach (var dpiScale in new[] { 1.0, 1.25, 2.0 })
            {
                SetViewport(originalWidth, originalHeight, dpiScale);
                if (Math.Abs(snapshot.DpiScale - dpiScale) > 0.00001 ||
                    CanvasNodes.Count < 2 || CanvasConnections.Count == 0)
                {
                    return false;
                }
                var node = CanvasNodes.FirstOrDefault(candidate => candidate.InstanceId == originalNode.InstanceId);
                var connection = CanvasConnections.FirstOrDefault(candidate =>
                    candidate.SourceInstanceId == originalConnection.SourceInstanceId &&
                    candidate.DestinationInstanceId == originalConnection.DestinationInstanceId);
                if (node is null || connection is null ||
                    Math.Abs(node.CanvasX - originalNode.CanvasX) > 0.00001 ||
                    Math.Abs(node.CanvasY - originalNode.CanvasY) > 0.00001 ||
                    Math.Abs(connection.SourceX - originalConnection.SourceX) > 0.00001 ||
                    Math.Abs(connection.SourceY - originalConnection.SourceY) > 0.00001 ||
                    Math.Abs(connection.DestinationX - originalConnection.DestinationX) > 0.00001 ||
                    Math.Abs(connection.DestinationY - originalConnection.DestinationY) > 0.00001 ||
                    !SelectAt(node.CanvasX + 10.0, node.CanvasY + 10.0) ||
                    !CanvasNodes.First(candidate => candidate.InstanceId == node.InstanceId).Selected)
                {
                    return false;
                }
            }
            return true;
        }
        finally
        {
            SetViewport(originalWidth, originalHeight, originalDpi);
        }
    }

    public bool SetParameter(string target, float value)
    {
        if (backend is null)
        {
            return false;
        }
        var separator = target.LastIndexOf('.');
        if (separator > 0 && separator < target.Length - 1 && float.IsFinite(value))
        {
            var instanceId = target[..separator];
            var parameterId = target[(separator + 1)..];
            var descriptor = snapshot.Nodes.FirstOrDefault(node => node.InstanceId == instanceId)?.Parameters
                .FirstOrDefault(parameter => parameter.Id == parameterId);
            if (descriptor is not null && descriptor.IsValidRange &&
                (value < descriptor.Minimum || value > descriptor.Maximum))
            {
                Status = $"参数 {parameterId} 超出范围 {descriptor.RangeText}";
                return false;
            }
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

    public bool ToggleNodeBypass(string instanceId)
    {
        if (backend is null)
        {
            return false;
        }
        var node = snapshot.Nodes.FirstOrDefault(candidate => candidate.InstanceId == instanceId);
        if (node is null)
        {
            return false;
        }
        try
        {
            backend.SetNodeBypassed(instanceId, !node.Bypassed);
            Refresh();
            Status = snapshot.Nodes.First(candidate => candidate.InstanceId == instanceId).Bypassed
                ? $"{instanceId} 已旁路"
                : $"{instanceId} 已启用";
            return true;
        }
        catch (Exception exception)
        {
            Status = exception.Message;
            return false;
        }
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

    public bool LoadTheme(string path)
    {
        if (backend is null || string.IsNullOrWhiteSpace(path))
        {
            return false;
        }
        try
        {
            backend.LoadTheme(path);
            Refresh();
            Status = $"主题已加载: {ThemeDisplayName}";
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
            OnPropertiesChanged(nameof(Snapshot), nameof(Nodes), nameof(CanvasNodes), nameof(SelectedNode),
                nameof(SelectedNodeDisplayName), nameof(SelectedParameters));
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
            OnPropertiesChanged(nameof(Snapshot), nameof(Nodes), nameof(CanvasNodes), nameof(SelectedNode),
                nameof(SelectedNodeDisplayName), nameof(SelectedParameters));
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
            OnPropertiesChanged(nameof(Snapshot), nameof(Nodes), nameof(CanvasNodes), nameof(SelectedNode),
                nameof(SelectedNodeDisplayName), nameof(SelectedParameters),
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
            foreach (var route in asioInputRoutes.Concat(asioOutputRoutes))
            {
                route.SetDrivers(asioDrivers);
            }
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
        if (backend is null || !CanOpenAsio)
        {
            Status = "请先为全部输入/输出路由选择有效的 ASIO 设备和物理通道";
            return false;
        }
        try
        {
            backend.OpenAsioRouted(asioInputRoutes.Select(route => route.ToSnapshot()).ToArray(),
                asioOutputRoutes.Select(route => route.ToSnapshot()).ToArray(), nativeWindow);
            Refresh();
            Status = $"ASIO 已打开；{asioInputRoutes.Count} 路输入 / {asioOutputRoutes.Count} 路输出，支持跨设备路由";
            return true;
        }
        catch (Exception exception)
        {
            var error = exception.Message;
            Refresh();
            Status = error;
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
            OnPropertiesChanged(nameof(MidiDevices), nameof(HasMidiDevices));
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
        if (backend is null || !CanOpenMidi || SelectedMidiDevice is null)
        {
            Status = MidiOpen ? "MIDI 输入已经打开" : "请先刷新并选择未打开的 MIDI 输入设备";
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
            Refresh();
            Status = exception.Message;
            return false;
        }
    }

    public bool CloseMidi()
    {
        if (backend is null || !CanCloseMidi)
        {
            Status = "MIDI 输入当前未打开";
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
        if (backend is null || !CanDrainMidi)
        {
            Status = "MIDI 输入当前未打开";
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
        if (backend is null || !CanArmMidiLearn || SelectedMidiLearnTarget is null)
        {
            Status = "请先打开 MIDI 输入并选择未学习的 MIDI Learn 目标";
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
        if (backend is null || !CanCancelMidiLearn)
        {
            Status = "当前没有正在进行的 MIDI Learn";
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
        foreach (var route in asioInputRoutes.Concat(asioOutputRoutes))
        {
            route.PropertyChanged -= AsioRouteChanged;
        }
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

    private bool MatchesModuleCatalogFilter(ModuleCatalogSnapshot module) => moduleCatalogFilter switch
    {
        "模拟" => module.Category.Equals("Analog", StringComparison.OrdinalIgnoreCase),
        "数字" => module.Category.Equals("Digital", StringComparison.OrdinalIgnoreCase),
        "外部" => !module.Category.Equals("Analog", StringComparison.OrdinalIgnoreCase) &&
                   !module.Category.Equals("Digital", StringComparison.OrdinalIgnoreCase),
        _ => true
    };

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
            var error = exception.Message;
            Refresh();
            Status = error;
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

    private void AsioRouteChanged(object? sender, PropertyChangedEventArgs args)
    {
        OnPropertiesChanged(nameof(AsioRouteMapValid), nameof(AsioRouteValidationText), nameof(CanOpenAsio), nameof(HasSelectedAsio), nameof(AsioRouteText),
            nameof(FixedInputPortText), nameof(FixedOutputPortText));
    }

    private double ToBackendX(double x) => x * Math.Max(snapshot.DpiScale, 1.0);
    private double ToBackendY(double y) => y * Math.Max(snapshot.DpiScale, 1.0);

    private void RebuildCanvasProjections()
    {
        var scale = Math.Max(snapshot.DpiScale, 1.0);
        canvasNodes = snapshot.Nodes.Select(node =>
            new CanvasNodeSnapshot(node, node.LayoutX / scale, node.LayoutY / scale, snapshot.DarkTheme)).ToArray();
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
