using System.Runtime.InteropServices;

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
    public uint NodeCount;
    public uint BindingCount;
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
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeParameterView
{
    public IntPtr ParameterId;
    public float Value;
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
    internal static extern NativeStatus solorig_backend_snapshot_create(IntPtr backend, out IntPtr snapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern void solorig_snapshot_destroy(IntPtr snapshot);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_snapshot_info(IntPtr snapshot, out NativeSnapshotInfo info);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_snapshot_node(IntPtr snapshot, uint index, out NativeNodeView node);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_snapshot_parameter(
        IntPtr snapshot, uint nodeIndex, uint parameterIndex, out NativeParameterView parameter);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_snapshot_binding(
        IntPtr snapshot, uint index, out NativeBindingView binding);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_set_parameter_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string instanceId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string parameterId, float value);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_set_theme_dark(IntPtr backend, byte dark);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_save_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern NativeStatus solorig_backend_open_utf8(
        IntPtr backend, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

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

public sealed record ParameterSnapshot(string Id, float Value)
{
    public string DisplayText => $"{Id}: {Value:0.###}";
}
public sealed record NodeSnapshot(
    string InstanceId,
    string ModuleId,
    float LayoutX,
    float LayoutY,
    bool Selected,
    bool Unresolved,
    string Diagnostic,
    IReadOnlyList<ParameterSnapshot> Parameters)
{
    public string LayoutText => $"Layout: ({LayoutX:0}, {LayoutY:0})";
}
public sealed record BindingSnapshot(
    uint MessageType,
    uint Channel,
    uint Number,
    uint TargetKind,
    string TargetId,
    float Minimum,
    float Maximum);
public sealed record BackendSnapshot(
    uint SchemaVersion,
    bool DarkTheme,
    string PresetId,
    string PresetName,
    string SourcePath,
    string AudioStatus,
    string FileStatus,
    string MidiStatus,
    IReadOnlyList<NodeSnapshot> Nodes,
    IReadOnlyList<BindingSnapshot> Bindings);

public sealed class BackendSession : IDisposable
{
    private IntPtr handle;

    public BackendSession()
    {
        var status = NativeMethods.solorig_backend_create(out handle);
        Ensure(status, "backend create");
    }

    public BackendSnapshot ReadSnapshot()
    {
        var status = NativeMethods.solorig_backend_snapshot_create(handle, out var snapshot);
        Ensure(status, "snapshot create");
        try
        {
            Ensure(NativeMethods.solorig_snapshot_info(snapshot, out var info), "snapshot info");
            var nodes = new List<NodeSnapshot>((int)info.NodeCount);
            for (uint nodeIndex = 0; nodeIndex < info.NodeCount; nodeIndex++)
            {
                Ensure(NativeMethods.solorig_snapshot_node(snapshot, nodeIndex, out var node), "snapshot node");
                var parameters = new List<ParameterSnapshot>((int)node.ParameterCount);
                for (uint parameterIndex = 0; parameterIndex < node.ParameterCount; parameterIndex++)
                {
                    Ensure(NativeMethods.solorig_snapshot_parameter(snapshot, nodeIndex, parameterIndex, out var parameter), "snapshot parameter");
                    parameters.Add(new ParameterSnapshot(NativeMethods.Utf8(parameter.ParameterId), parameter.Value));
                }
                nodes.Add(new NodeSnapshot(
                    NativeMethods.Utf8(node.InstanceId), NativeMethods.Utf8(node.ModuleId), node.LayoutX, node.LayoutY,
                    node.Selected != 0, node.Unresolved != 0, NativeMethods.Utf8(node.Diagnostic), parameters));
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
                NativeMethods.Utf8(info.FileStatus), NativeMethods.Utf8(info.MidiStatus), nodes, bindings);
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

    public void SetTheme(bool dark) => Ensure(NativeMethods.solorig_backend_set_theme_dark(handle, dark ? (byte)1 : (byte)0), "set theme");

    public void Save(string path) => Ensure(NativeMethods.solorig_backend_save_utf8(handle, path), "save preset");

    public void Open(string path) => Ensure(NativeMethods.solorig_backend_open_utf8(handle, path), "open preset");

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
}
