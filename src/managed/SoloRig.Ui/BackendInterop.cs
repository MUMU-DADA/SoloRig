using System.Runtime.InteropServices;

[assembly: DefaultDllImportSearchPaths(DllImportSearchPath.AssemblyDirectory | DllImportSearchPath.System32)]

namespace SoloRig.Ui;

public enum NativeStatus : uint
{
    Ok = 0,
    InvalidArgument = 1,
    OutOfMemory = 2,
    NotFound = 3,
    FileError = 4,
    InvalidPreset = 5,
    InternalError = 6,
    Unsupported = 7,
    InvalidTheme = 8,
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSnapshotInfo
{
    public uint SchemaVersion;
    public byte DarkTheme;
    public IntPtr PresetId;
    public IntPtr PresetName;
    public IntPtr SourcePath;
    public IntPtr AudioStatus;
    public IntPtr FileStatus;
    public IntPtr MidiStatus;
    public double ViewportWidth;
    public double ViewportHeight;
    public double DpiScale;
    public byte Dirty;
    public uint NodeCount;
    public uint ConnectionCount;
    public uint BindingCount;
    public uint TotalLatencySamples;
    public double EstimatedCpuPercent;
    public IntPtr ResourceStatus;
    public double MeasuredCpuPercent;
    public double PeakCpuPercent;
    public uint MeasuredNodeCount;
    public byte MeasurementAvailable;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAudioStateView
{
    public byte IsOpen;
    public byte IsRunning;
    public byte ResetRequested;
    public byte DropoutReported;
    public double SampleRateChangedTo;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeNodeView
{
    public IntPtr InstanceId;
    public IntPtr ModuleId;
    public float LayoutX;
    public float LayoutY;
    public byte Selected;
    public byte Unresolved;
    public IntPtr Diagnostic;
    public uint ParameterCount;
    public uint DeclaredLatencySamples;
    public double EstimatedCpuPercent;
    public double MeasuredCpuPercent;
    public double PeakCpuPercent;
    public byte MeasurementAvailable;
    public byte Bypassed;
    public IntPtr SkinId;
    public IntPtr KnobSkinId;
    public IntPtr ToggleSkinId;
    public IntPtr ButtonSkinId;
    public IntPtr FootswitchSkinId;
    public IntPtr KnobImagePath;
    public IntPtr ToggleImagePath;
    public IntPtr ButtonImagePath;
    public IntPtr FootswitchImagePath;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeParameterView
{
    public IntPtr ParameterId;
    public float Value;
    public float Minimum;
    public float Maximum;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeConnectionView
{
    public IntPtr SourceInstanceId;
    public IntPtr DestinationInstanceId;
    public double SourceX;
    public double SourceY;
    public double DestinationX;
    public double DestinationY;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeBindingView
{
    public uint MessageType;
    public uint Channel;
    public uint Number;
    public uint TargetKind;
    public IntPtr TargetId;
    public float Minimum;
    public float Maximum;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeModuleCatalogView
{
    public IntPtr ModuleId;
    public IntPtr DisplayName;
    public IntPtr Category;
    public IntPtr Description;
    public uint ParameterCount;
    public uint DeclaredLatencySamples;
    public double EstimatedCpuPercent;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeRealtimeContractView
{
    public uint ContractVersion;
    public uint ProcessNoexcept;
    public uint ProcessAllocates;
    public uint ProcessReadsFiles;
    public uint ProcessParsesJson;
    public uint ProcessLocks;
    public uint PrepareOwnsCompilation;
    public uint MaximumOversampling;
    public uint SampleRateHz;
    public uint MaxBlockSize;
    public uint ChannelCount;
    public uint DeclaredLatencySamples;
    public double EstimatedCpuPercent;
    public IntPtr ThreadRule;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeWhiteboxStatusView
{
    public uint SchemaVersion;
    public IntPtr GraphStatus;
    public IntPtr RuntimeStatus;
    public IntPtr EvidenceStatus;
    public IntPtr Diagnostic;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeThemeInfoView
{
    public IntPtr Id;
    public IntPtr DisplayName;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAsioDriverView
{
    public IntPtr DriverId;
    public IntPtr DisplayName;
    public uint InputChannelCount;
    public uint OutputChannelCount;
    public double NativeSampleRateHz;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeMidiDeviceView
{
    public uint DeviceIndex;
    public IntPtr DisplayName;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeMidiLearnTargetView
{
    public IntPtr TargetId;
    public IntPtr DisplayName;
    public float Minimum;
    public float Maximum;
}

internal static class NativeMethods
{
    private const string Library = "SoloRigBackend";

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_create(out IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern void solorig_backend_destroy(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern IntPtr solorig_backend_last_error_utf8(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_realtime_contract(
        IntPtr backend, out NativeRealtimeContractView contractView);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_whitebox_status(
        IntPtr backend, out NativeWhiteboxStatusView statusView);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_snapshot_create(IntPtr backend, out IntPtr snapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern void solorig_snapshot_destroy(IntPtr snapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_snapshot_info(IntPtr snapshot, out NativeSnapshotInfo info);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_snapshot_audio_state(IntPtr snapshot, out NativeAudioStateView state);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_snapshot_node(IntPtr snapshot, uint index, out NativeNodeView node);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_snapshot_parameter(
        IntPtr snapshot, uint nodeIndex, uint parameterIndex, out NativeParameterView parameter);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_snapshot_connection(
        IntPtr snapshot, uint index, out NativeConnectionView connection);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_snapshot_binding(
        IntPtr snapshot, uint index, out NativeBindingView binding);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_set_parameter_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string instanceId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string parameterId, float value);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_set_node_bypassed_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string instanceId, byte bypassed);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_set_node_performance_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string instanceId,
        uint declaredLatencySamples, double measuredCpuPercent, double peakCpuPercent);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_clear_node_performance(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_set_theme_dark(IntPtr backend, byte dark);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_load_theme_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_theme_info(
        IntPtr backend, out NativeThemeInfoView theme);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern IntPtr solorig_backend_theme_color_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string colorKey);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_set_viewport(
        IntPtr backend, double width, double height, double dpiScale);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_select_at(IntPtr backend, double x, double y);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_begin_drag(IntPtr backend, double x, double y);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_drag_to(IntPtr backend, double x, double y, byte snapToGrid);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern void solorig_backend_end_drag(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_save_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_open_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_new_preset(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_catalog_count(IntPtr backend, out uint count);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_catalog_entry(IntPtr backend, uint index, out NativeModuleCatalogView entry);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_add_node_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string moduleId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string instanceId, double layoutX, double layoutY);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_delete_node_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string instanceId);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_duplicate_node_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string sourceInstanceId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string newInstanceId);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_connect_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string sourceInstanceId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string destinationInstanceId);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_disconnect_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string sourceInstanceId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string destinationInstanceId);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_import_resource_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_asio_refresh(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_asio_driver_count(IntPtr backend, out uint count);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_asio_driver_entry(
        IntPtr backend, uint index, out NativeAsioDriverView entry);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_asio_open(
        IntPtr backend, uint driverIndex, IntPtr nativeWindow,
        uint inputChannel0, uint inputChannel1, uint outputChannel0, uint outputChannel1);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_asio_start(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_asio_stop(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_asio_close(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_asio_recover(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_tuner_set_enabled(IntPtr backend, byte enabled);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_refresh(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_device_count(IntPtr backend, out uint count);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_device_entry(
        IntPtr backend, uint index, out NativeMidiDeviceView entry);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_open(IntPtr backend, uint deviceIndex);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_close(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_drain(IntPtr backend, out uint processedMessages);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_target_count(IntPtr backend, out uint count);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_target_entry(
        IntPtr backend, uint index, out NativeMidiLearnTargetView target);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_learn_arm(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string targetId);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_learn_cancel(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_delete_binding(IntPtr backend, uint index);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_clear_bindings(IntPtr backend);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_midi_import_bank_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string directoryPath);

    internal static string Utf8(IntPtr pointer) => pointer == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUTF8(pointer) ?? string.Empty;
}

public sealed class BackendException : Exception
{
    public BackendException(string operation, NativeStatus status, string detail)
        : base($"{operation} failed ({status}): {detail}")
    {
        Status = status;
    }

    public NativeStatus Status { get; }
}

public sealed record ParameterSnapshot(string Id, float Value, float Minimum = 0.0F, float Maximum = 1.0F)
{
    public string DisplayText => $"{Id}: {Value:0.###}";
    public string RangeText => $"{Minimum:0.###} .. {Maximum:0.###}";
    public bool IsValidRange => float.IsFinite(Minimum) && float.IsFinite(Maximum) && Maximum > Minimum;
}

public sealed record NodeSkinSnapshot(
    string Id,
    string KnobId,
    string ToggleId,
    string ButtonId,
    string FootswitchId,
    string KnobImagePath,
    string ToggleImagePath,
    string ButtonImagePath,
    string FootswitchImagePath)
{
    public bool HasKnobImage => !string.IsNullOrWhiteSpace(KnobImagePath);
    public bool HasToggleImage => !string.IsNullOrWhiteSpace(ToggleImagePath);
    public bool HasButtonImage => !string.IsNullOrWhiteSpace(ButtonImagePath);
    public bool HasFootswitchImage => !string.IsNullOrWhiteSpace(FootswitchImagePath);
}

public sealed record ThemeSnapshot(string Id, string DisplayName, IReadOnlyDictionary<string, string> Colors)
{
    public static ThemeSnapshot Empty { get; } = new("", "", new Dictionary<string, string>());

    public string? GetColor(string key) => Colors.TryGetValue(key, out var color) ? color : null;
}
public sealed record NodeSnapshot(
    string InstanceId,
    string ModuleId,
    float LayoutX,
    float LayoutY,
    bool Selected,
    bool Unresolved,
    bool Bypassed,
    string Diagnostic,
    IReadOnlyList<ParameterSnapshot> Parameters,
    NodeSkinSnapshot Skin,
    uint DeclaredLatencySamples,
    double EstimatedCpuPercent,
    double MeasuredCpuPercent,
    double PeakCpuPercent,
    bool MeasurementAvailable)
{
    public string LayoutText => $"Layout: ({LayoutX:0}, {LayoutY:0})";
    public string PerformanceText => MeasurementAvailable
        ? $"实测 {MeasuredCpuPercent:0.0}% / 峰值 {PeakCpuPercent:0.0}% CPU · {DeclaredLatencySamples} samples"
        : $"预计 {EstimatedCpuPercent:0.0}% CPU · {DeclaredLatencySamples} samples · 实测未连接";
}
public sealed record ConnectionSnapshot(
    string SourceInstanceId,
    string DestinationInstanceId,
    double SourceX,
    double SourceY,
    double DestinationX,
    double DestinationY);
public sealed record BindingSnapshot(
    uint MessageType,
    uint Channel,
    uint Number,
    uint TargetKind,
    string TargetId,
    float Minimum,
    float Maximum)
{
    public string MessageTypeText => MessageType == 0U ? "CC" : "PC";
    public string TargetKindText => TargetKind == 0U ? "参数" : "预设";
    public string DisplayText =>
        $"{MessageTypeText} ch{Channel + 1} #{Number} → {TargetId} [{Minimum:0.###}..{Maximum:0.###}]";
}
public sealed record ModuleCatalogSnapshot(
    string ModuleId,
    string DisplayName,
    string Category,
    string Description,
    uint ParameterCount,
    uint DeclaredLatencySamples,
    double EstimatedCpuPercent)
{
    public string DisplayText => $"{DisplayName} · {Category}";
}

public sealed record AsioDriverSnapshot(
    string DriverId,
    string DisplayName,
    uint InputChannelCount,
    uint OutputChannelCount,
    double NativeSampleRateHz)
{
    public string DisplayText => $"{DisplayName} · {InputChannelCount} in / {OutputChannelCount} out";
    public string SampleRateText => NativeSampleRateHz > 0.0 ? $"{NativeSampleRateHz:0} Hz" : "probe unavailable";
}

public sealed record MidiDeviceSnapshot(uint DeviceIndex, string DisplayName)
{
    public string DisplayText => $"{DisplayName} · device {DeviceIndex}";
}

public sealed record MidiLearnTargetSnapshot(string TargetId, string DisplayName, float Minimum, float Maximum)
{
    public string DisplayText => $"{DisplayName} [{Minimum:0.###}..{Maximum:0.###}]";
}

public sealed record RealtimeContractSnapshot(
    uint ContractVersion,
    bool ProcessNoexcept,
    bool ProcessAllocates,
    bool ProcessReadsFiles,
    bool ProcessParsesJson,
    bool ProcessLocks,
    bool PrepareOwnsCompilation,
    uint MaximumOversampling,
    uint SampleRateHz,
    uint MaxBlockSize,
    uint ChannelCount,
    uint DeclaredLatencySamples,
    double EstimatedCpuPercent,
    string ThreadRule)
{
    public bool IsRealtimeSafe => ProcessNoexcept && !ProcessAllocates && !ProcessReadsFiles &&
                                  !ProcessParsesJson && !ProcessLocks && PrepareOwnsCompilation &&
                                  MaximumOversampling is >= 1 and <= 16;
    public string SampleFormatText => SampleRateHz == 0 ? "host-configured" : $"{SampleRateHz} Hz";
}

public sealed record WhiteboxStatusSnapshot(
    uint SchemaVersion,
    string GraphStatus,
    string RuntimeStatus,
    string EvidenceStatus,
    string Diagnostic);
public sealed record BackendSnapshot(
    uint SchemaVersion,
    bool DarkTheme,
    string PresetId,
    string PresetName,
    string SourcePath,
    string AudioStatus,
    string FileStatus,
    string MidiStatus,
    double ViewportWidth,
    double ViewportHeight,
    double DpiScale,
    bool Dirty,
    IReadOnlyList<NodeSnapshot> Nodes,
    IReadOnlyList<ConnectionSnapshot> Connections,
    IReadOnlyList<BindingSnapshot> Bindings,
    uint TotalLatencySamples,
    double EstimatedCpuPercent,
    string ResourceStatus,
    double MeasuredCpuPercent,
    double PeakCpuPercent,
    uint MeasuredNodeCount,
    bool MeasurementAvailable,
    bool AudioOpen,
    bool AudioRunning,
    bool AudioResetRequested,
    bool AudioDropoutReported,
    double AudioSampleRateChangedTo);

public sealed class BackendSession : IDisposable
{
    private static readonly string[] ThemeColorKeys =
    [
        "background", "surface", "text", "accent", "warning",
        "window", "topbar", "panel", "canvas", "overlay", "control", "controlHover",
        "card", "inputPanel", "outputPanel", "border", "strongBorder", "cardBorder",
        "muted", "accentHover", "accentText", "input", "inputOutline", "output",
        "outputOutline", "track", "knob", "grid", "canvasStart", "canvasEnd",
        "accentOutline", "toolBorder"
    ];
    private IntPtr handle;

    public bool IsAvailable => handle != IntPtr.Zero;

    public BackendSession()
    {
        var status = NativeMethods.solorig_backend_create(out handle);
        Ensure(status, "backend create");
    }

    public RealtimeContractSnapshot ReadRealtimeContract()
    {
        ThrowIfDisposed();
        Ensure(NativeMethods.solorig_backend_realtime_contract(handle, out var value), "realtime contract");
        return new RealtimeContractSnapshot(
            value.ContractVersion, value.ProcessNoexcept != 0, value.ProcessAllocates != 0,
            value.ProcessReadsFiles != 0, value.ProcessParsesJson != 0, value.ProcessLocks != 0,
            value.PrepareOwnsCompilation != 0, value.MaximumOversampling, value.SampleRateHz,
            value.MaxBlockSize, value.ChannelCount, value.DeclaredLatencySamples,
            value.EstimatedCpuPercent, NativeMethods.Utf8(value.ThreadRule));
    }

    public WhiteboxStatusSnapshot ReadWhiteboxStatus()
    {
        ThrowIfDisposed();
        Ensure(NativeMethods.solorig_backend_whitebox_status(handle, out var value), "whitebox status");
        return new WhiteboxStatusSnapshot(
            value.SchemaVersion, NativeMethods.Utf8(value.GraphStatus), NativeMethods.Utf8(value.RuntimeStatus),
            NativeMethods.Utf8(value.EvidenceStatus), NativeMethods.Utf8(value.Diagnostic));
    }

    public ThemeSnapshot ReadTheme()
    {
        ThrowIfDisposed();
        Ensure(NativeMethods.solorig_backend_theme_info(handle, out var info), "theme info");
        var colors = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var key in ThemeColorKeys)
        {
            var color = NativeMethods.Utf8(NativeMethods.solorig_backend_theme_color_utf8(handle, key));
            if (!string.IsNullOrWhiteSpace(color))
            {
                colors[key] = color;
            }
        }
        return new ThemeSnapshot(NativeMethods.Utf8(info.Id), NativeMethods.Utf8(info.DisplayName), colors);
    }

    public BackendSnapshot ReadSnapshot()
    {
        var status = NativeMethods.solorig_backend_snapshot_create(handle, out var snapshot);
        Ensure(status, "snapshot create");
        try
        {
            Ensure(NativeMethods.solorig_snapshot_info(snapshot, out var info), "snapshot info");
            Ensure(NativeMethods.solorig_snapshot_audio_state(snapshot, out var audioState), "snapshot audio state");
            var nodes = new List<NodeSnapshot>((int)info.NodeCount);
            for (uint nodeIndex = 0; nodeIndex < info.NodeCount; nodeIndex++)
            {
                Ensure(NativeMethods.solorig_snapshot_node(snapshot, nodeIndex, out var node), "snapshot node");
                var parameters = new List<ParameterSnapshot>((int)node.ParameterCount);
                for (uint parameterIndex = 0; parameterIndex < node.ParameterCount; parameterIndex++)
                {
                    Ensure(NativeMethods.solorig_snapshot_parameter(snapshot, nodeIndex, parameterIndex, out var parameter), "snapshot parameter");
                    parameters.Add(new ParameterSnapshot(NativeMethods.Utf8(parameter.ParameterId), parameter.Value,
                        parameter.Minimum, parameter.Maximum));
                }
                nodes.Add(new NodeSnapshot(
                    NativeMethods.Utf8(node.InstanceId), NativeMethods.Utf8(node.ModuleId), node.LayoutX, node.LayoutY,
                    node.Selected != 0, node.Unresolved != 0, node.Bypassed != 0, NativeMethods.Utf8(node.Diagnostic), parameters,
                    new NodeSkinSnapshot(
                        NativeMethods.Utf8(node.SkinId), NativeMethods.Utf8(node.KnobSkinId),
                        NativeMethods.Utf8(node.ToggleSkinId), NativeMethods.Utf8(node.ButtonSkinId),
                        NativeMethods.Utf8(node.FootswitchSkinId), NativeMethods.Utf8(node.KnobImagePath),
                        NativeMethods.Utf8(node.ToggleImagePath), NativeMethods.Utf8(node.ButtonImagePath),
                        NativeMethods.Utf8(node.FootswitchImagePath)),
                    node.DeclaredLatencySamples, node.EstimatedCpuPercent, node.MeasuredCpuPercent,
                    node.PeakCpuPercent, node.MeasurementAvailable != 0));
            }
            var connections = new List<ConnectionSnapshot>((int)info.ConnectionCount);
            for (uint connectionIndex = 0; connectionIndex < info.ConnectionCount; connectionIndex++)
            {
                Ensure(NativeMethods.solorig_snapshot_connection(snapshot, connectionIndex, out var connection), "snapshot connection");
                connections.Add(new ConnectionSnapshot(
                    NativeMethods.Utf8(connection.SourceInstanceId), NativeMethods.Utf8(connection.DestinationInstanceId),
                    connection.SourceX, connection.SourceY, connection.DestinationX, connection.DestinationY));
            }
            var bindings = new List<BindingSnapshot>((int)info.BindingCount);
            for (uint bindingIndex = 0; bindingIndex < info.BindingCount; bindingIndex++)
            {
                Ensure(NativeMethods.solorig_snapshot_binding(snapshot, bindingIndex, out var binding), "snapshot binding");
                bindings.Add(new BindingSnapshot(binding.MessageType, binding.Channel, binding.Number, binding.TargetKind,
                    NativeMethods.Utf8(binding.TargetId), binding.Minimum, binding.Maximum));
            }
            return new BackendSnapshot(info.SchemaVersion, info.DarkTheme != 0, NativeMethods.Utf8(info.PresetId),
                NativeMethods.Utf8(info.PresetName), NativeMethods.Utf8(info.SourcePath), NativeMethods.Utf8(info.AudioStatus),
                NativeMethods.Utf8(info.FileStatus), NativeMethods.Utf8(info.MidiStatus), info.ViewportWidth,
                info.ViewportHeight, info.DpiScale, info.Dirty != 0, nodes, connections, bindings,
                info.TotalLatencySamples, info.EstimatedCpuPercent, NativeMethods.Utf8(info.ResourceStatus),
                info.MeasuredCpuPercent, info.PeakCpuPercent, info.MeasuredNodeCount,
                info.MeasurementAvailable != 0, audioState.IsOpen != 0, audioState.IsRunning != 0,
                audioState.ResetRequested != 0, audioState.DropoutReported != 0,
                audioState.SampleRateChangedTo);
        }
        finally
        {
            NativeMethods.solorig_snapshot_destroy(snapshot);
        }
    }

    public void SetParameter(string target, float value)
    {
        var separator = target.LastIndexOf('.');
        if (separator <= 0 || separator == target.Length - 1)
        {
            throw new ArgumentException("Target must be instanceId.parameterId", nameof(target));
        }
        Ensure(NativeMethods.solorig_backend_set_parameter_utf8(handle, target[..separator], target[(separator + 1)..], value), "set parameter");
    }

    public void SetNodeBypassed(string instanceId, bool bypassed) =>
        Ensure(NativeMethods.solorig_backend_set_node_bypassed_utf8(handle, instanceId, bypassed ? (byte)1 : (byte)0),
            "set node bypass");

    public void SetNodePerformance(string instanceId, uint declaredLatencySamples,
        double measuredCpuPercent, double peakCpuPercent) =>
        Ensure(NativeMethods.solorig_backend_set_node_performance_utf8(handle, instanceId,
            declaredLatencySamples, measuredCpuPercent, peakCpuPercent), "set node performance");

    public void ClearNodePerformance() =>
        Ensure(NativeMethods.solorig_backend_clear_node_performance(handle), "clear node performance");

    public void SetTheme(bool dark) => Ensure(NativeMethods.solorig_backend_set_theme_dark(handle, dark ? (byte)1 : (byte)0), "set theme");

    public void LoadTheme(string path) =>
        Ensure(NativeMethods.solorig_backend_load_theme_utf8(handle, path), "load theme");

    public void SetViewport(double width, double height, double dpiScale) =>
        Ensure(NativeMethods.solorig_backend_set_viewport(handle, width, height, dpiScale), "set viewport");

    public void SelectAt(double x, double y) => Ensure(NativeMethods.solorig_backend_select_at(handle, x, y), "select node");

    public void BeginDrag(double x, double y) => Ensure(NativeMethods.solorig_backend_begin_drag(handle, x, y), "begin drag");

    public void DragTo(double x, double y, bool snapToGrid) =>
        Ensure(NativeMethods.solorig_backend_drag_to(handle, x, y, snapToGrid ? (byte)1 : (byte)0), "drag node");

    public void EndDrag() => NativeMethods.solorig_backend_end_drag(handle);

    public void Save(string path) => Ensure(NativeMethods.solorig_backend_save_utf8(handle, path), "save preset");

    public void Open(string path) => Ensure(NativeMethods.solorig_backend_open_utf8(handle, path), "open preset");

    public void NewPreset() => Ensure(NativeMethods.solorig_backend_new_preset(handle), "new preset");

    public IReadOnlyList<ModuleCatalogSnapshot> ReadCatalog()
    {
        Ensure(NativeMethods.solorig_backend_catalog_count(handle, out var count), "catalog count");
        var entries = new List<ModuleCatalogSnapshot>((int)count);
        for (uint index = 0; index < count; index++)
        {
            Ensure(NativeMethods.solorig_backend_catalog_entry(handle, index, out var entry), "catalog entry");
            entries.Add(new ModuleCatalogSnapshot(NativeMethods.Utf8(entry.ModuleId), NativeMethods.Utf8(entry.DisplayName),
                NativeMethods.Utf8(entry.Category), NativeMethods.Utf8(entry.Description), entry.ParameterCount,
                entry.DeclaredLatencySamples, entry.EstimatedCpuPercent));
        }
        return entries;
    }

    public void AddNode(string moduleId, string instanceId, double layoutX, double layoutY) =>
        Ensure(NativeMethods.solorig_backend_add_node_utf8(handle, moduleId, instanceId, layoutX, layoutY), "add node");

    public void DeleteNode(string instanceId) =>
        Ensure(NativeMethods.solorig_backend_delete_node_utf8(handle, instanceId), "delete node");

    public void DuplicateNode(string sourceInstanceId, string newInstanceId) =>
        Ensure(NativeMethods.solorig_backend_duplicate_node_utf8(handle, sourceInstanceId, newInstanceId), "duplicate node");

    public void Connect(string sourceInstanceId, string destinationInstanceId) =>
        Ensure(NativeMethods.solorig_backend_connect_utf8(handle, sourceInstanceId, destinationInstanceId), "connect nodes");

    public void Disconnect(string sourceInstanceId, string destinationInstanceId) =>
        Ensure(NativeMethods.solorig_backend_disconnect_utf8(handle, sourceInstanceId, destinationInstanceId), "disconnect nodes");

    public void ImportResource(string path) =>
        Ensure(NativeMethods.solorig_backend_import_resource_utf8(handle, path), "import resource");

    public void RefreshAsioDrivers()
    {
        ThrowIfDisposed();
        Ensure(NativeMethods.solorig_backend_asio_refresh(handle), "refresh ASIO drivers");
    }

    public IReadOnlyList<AsioDriverSnapshot> ReadAsioDrivers()
    {
        ThrowIfDisposed();
        Ensure(NativeMethods.solorig_backend_asio_driver_count(handle, out var count), "ASIO driver count");
        var entries = new List<AsioDriverSnapshot>((int)count);
        for (uint index = 0; index < count; index++)
        {
            Ensure(NativeMethods.solorig_backend_asio_driver_entry(handle, index, out var entry), "ASIO driver entry");
            entries.Add(new AsioDriverSnapshot(NativeMethods.Utf8(entry.DriverId), NativeMethods.Utf8(entry.DisplayName),
                entry.InputChannelCount, entry.OutputChannelCount, entry.NativeSampleRateHz));
        }
        return entries;
    }

    public void OpenAsio(uint driverIndex, IntPtr nativeWindow,
        uint inputChannel0, uint inputChannel1, uint outputChannel0, uint outputChannel1) =>
        Ensure(NativeMethods.solorig_backend_asio_open(handle, driverIndex, nativeWindow,
            inputChannel0, inputChannel1, outputChannel0, outputChannel1), "open ASIO");

    public void StartAsio() => Ensure(NativeMethods.solorig_backend_asio_start(handle), "start ASIO");
    public void StopAsio() => Ensure(NativeMethods.solorig_backend_asio_stop(handle), "stop ASIO");
    public void CloseAsio() => Ensure(NativeMethods.solorig_backend_asio_close(handle), "close ASIO");
    public void RecoverAsio() => Ensure(NativeMethods.solorig_backend_asio_recover(handle), "recover ASIO");
    public void SetTunerEnabled(bool enabled) =>
        Ensure(NativeMethods.solorig_backend_tuner_set_enabled(handle, enabled ? (byte)1 : (byte)0), "set tuner");

    public void RefreshMidiDevices()
    {
        ThrowIfDisposed();
        Ensure(NativeMethods.solorig_backend_midi_refresh(handle), "refresh MIDI devices");
    }

    public IReadOnlyList<MidiDeviceSnapshot> ReadMidiDevices()
    {
        ThrowIfDisposed();
        Ensure(NativeMethods.solorig_backend_midi_device_count(handle, out var count), "MIDI device count");
        var entries = new List<MidiDeviceSnapshot>((int)count);
        for (uint index = 0; index < count; index++)
        {
            Ensure(NativeMethods.solorig_backend_midi_device_entry(handle, index, out var entry), "MIDI device entry");
            entries.Add(new MidiDeviceSnapshot(entry.DeviceIndex, NativeMethods.Utf8(entry.DisplayName)));
        }
        return entries;
    }

    public void OpenMidi(uint deviceIndex) =>
        Ensure(NativeMethods.solorig_backend_midi_open(handle, deviceIndex), "open MIDI");

    public void CloseMidi() => Ensure(NativeMethods.solorig_backend_midi_close(handle), "close MIDI");

    public uint DrainMidi()
    {
        Ensure(NativeMethods.solorig_backend_midi_drain(handle, out var processedMessages), "drain MIDI");
        return processedMessages;
    }

    public IReadOnlyList<MidiLearnTargetSnapshot> ReadMidiLearnTargets()
    {
        ThrowIfDisposed();
        Ensure(NativeMethods.solorig_backend_midi_target_count(handle, out var count), "MIDI target count");
        var entries = new List<MidiLearnTargetSnapshot>((int)count);
        for (uint index = 0; index < count; index++)
        {
            Ensure(NativeMethods.solorig_backend_midi_target_entry(handle, index, out var target),
                "MIDI target entry");
            entries.Add(new MidiLearnTargetSnapshot(NativeMethods.Utf8(target.TargetId),
                NativeMethods.Utf8(target.DisplayName), target.Minimum, target.Maximum));
        }
        return entries;
    }

    public void ArmMidiLearn(string targetId) =>
        Ensure(NativeMethods.solorig_backend_midi_learn_arm(handle, targetId), "arm MIDI learn");

    public void CancelMidiLearn() =>
        Ensure(NativeMethods.solorig_backend_midi_learn_cancel(handle), "cancel MIDI learn");

    public void DeleteMidiBinding(uint index) =>
        Ensure(NativeMethods.solorig_backend_midi_delete_binding(handle, index), "delete MIDI binding");

    public void ClearMidiBindings() =>
        Ensure(NativeMethods.solorig_backend_midi_clear_bindings(handle), "clear MIDI bindings");

    public void ImportMidiBank(string directoryPath) =>
        Ensure(NativeMethods.solorig_backend_midi_import_bank_utf8(handle, directoryPath),
            "import MIDI preset bank");

    public void Dispose()
    {
        if (handle != IntPtr.Zero)
        {
            NativeMethods.solorig_backend_destroy(handle);
            handle = IntPtr.Zero;
        }
        GC.SuppressFinalize(this);
    }

    private void Ensure(NativeStatus status, string operation)
    {
        if (status == NativeStatus.Ok)
        {
            return;
        }
        var detail = NativeMethods.Utf8(NativeMethods.solorig_backend_last_error_utf8(handle));
        throw new BackendException(operation, status, detail);
    }

    private void ThrowIfDisposed()
    {
        if (handle == IntPtr.Zero)
        {
            throw new ObjectDisposedException(nameof(BackendSession));
        }
    }
}
