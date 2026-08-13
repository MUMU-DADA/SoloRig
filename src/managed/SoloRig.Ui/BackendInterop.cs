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
    internal static extern NativeStatus solorig_backend_set_theme_dark(IntPtr backend, byte dark);

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
    float Maximum);
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
    string ResourceStatus);

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
                info.TotalLatencySamples, info.EstimatedCpuPercent, NativeMethods.Utf8(info.ResourceStatus));
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
