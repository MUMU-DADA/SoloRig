using SoloRig.Ui;

using var viewModel = new RigEditorViewModel();
Require(viewModel.Nodes.Count >= 2, "default snapshot exposes Drive and Delay nodes");
Require(viewModel.Nodes.All(node => node.EstimatedCpuPercent > 0), "per-node estimated performance is visible");
Require(viewModel.Nodes.All(node => !node.MeasurementAvailable), "startup keeps measured performance explicitly unavailable");
Require(viewModel.PerformanceStatus.Contains("实测未连接", StringComparison.Ordinal), "performance status distinguishes estimate from measurement");
Require(viewModel.Snapshot.Connections.Count == 1, "default snapshot exposes the Drive-to-Delay connection");
Require(viewModel.MidiStatus.Contains("closed", StringComparison.OrdinalIgnoreCase), "startup MIDI remains closed");
Require(viewModel.AsioDrivers.Count == 0 && viewModel.MidiDevices.Count == 0,
    "startup keeps ASIO and MIDI enumeration explicit and side-effect free");
Require(!viewModel.TunerEnabled, "startup keeps the tuner disabled");
Require(viewModel.MidiLearnTargets.Count >= 2, "MIDI learn exposes current parameter targets without opening hardware");
Require(!viewModel.ArmSelectedMidiLearn(), "MIDI learn rejects an unopened MIDI input");
Require(!viewModel.StartAsio(), "ASIO start is rejected before an explicit device open");
Require(viewModel.ModuleCatalog.Count >= 2, "module catalog exposes built-in modules");
Require(viewModel.ModuleCatalog.Any(module => module.ModuleId == "org.mumu-dada.solorig.drive"), "catalog exposes Drive metadata");
Require(viewModel.RealtimeContract.IsRealtimeSafe, "C ABI reports a C++-only, preallocated realtime boundary");
Require(viewModel.RealtimeContract.MaximumOversampling is >= 1 and <= 16, "realtime contract bounds oversampling");
Require(viewModel.RealtimeStatus.Contains("C++ safe", StringComparison.Ordinal), "realtime status is visible to the shared ViewModel");
Require(viewModel.WhiteboxStatus.Diagnostic.Contains("PedalKernel", StringComparison.Ordinal), "whitebox status keeps the clean-room boundary visible");

using (var lifecycleProbe = new BackendSession())
{
    Require(lifecycleProbe.ReadRealtimeContract().IsRealtimeSafe, "direct C ABI contract read succeeds");
    lifecycleProbe.Dispose();
    var disposedRejected = false;
    try
    {
        _ = lifecycleProbe.ReadWhiteboxStatus();
    }
    catch (ObjectDisposedException)
    {
        disposedRejected = true;
    }
    Require(disposedRejected, "disposed backend rejects further status reads");
}
using (var hostProbe = new BackendSession())
{
    var invalidAsioRejected = false;
    try
    {
        hostProbe.OpenAsio(0, IntPtr.Zero, 0, 1, 0, 1);
    }
    catch (BackendException exception) when (exception.Status == NativeStatus.NotFound)
    {
        invalidAsioRejected = true;
    }
    Require(invalidAsioRejected, "ASIO open rejects an invalid driver index");

    var invalidMidiRejected = false;
    try
    {
        hostProbe.OpenMidi(0);
    }
    catch (BackendException)
    {
        invalidMidiRejected = true;
    }
    Require(invalidMidiRejected, "MIDI open rejects a device before enumeration");
}
Require(viewModel.AddModule("org.mumu-dada.solorig.drive", "drive-extra", 640, 320), "module add uses the C ABI transaction");
Require(viewModel.Snapshot.Nodes.Any(node => node.InstanceId == "drive-extra" && node.Parameters.Any(parameter => parameter.Id == "drive" && parameter.Value == 4.0F)),
    "added module receives validated defaults");
Require(viewModel.DuplicateNode("drive-extra", "drive-copy"), "node duplication uses the C ABI transaction");
Require(viewModel.ConnectNodes("drive-extra", "drive-copy"), "serial connection edit succeeds");
Require(viewModel.Snapshot.Connections.Any(connection => connection.SourceInstanceId == "drive-extra"), "connection feedback is visible");
Require(viewModel.DeleteNode("drive-copy"), "node deletion removes the copied module");
Require(viewModel.Snapshot.TotalLatencySamples > 0 && viewModel.Snapshot.EstimatedCpuPercent > 0, "performance snapshot is visible");
Require(viewModel.PresetNames.Contains("SoloRig Default"), "preset list exposes the in-memory default");
viewModel.NewPresetCommand.Execute(null);
Require(string.IsNullOrEmpty(viewModel.Snapshot.SourcePath) && !viewModel.IsDirty, "quick default preset switching stays in memory and clean");
Require(viewModel.SetParameter("drive-1.drive", 6.0F), "C ABI parameter edit succeeds");
Require(viewModel.Nodes.SelectMany(node => node.Parameters).Any(parameter => parameter.Id == "drive" && parameter.Value == 6.0F),
    "edited parameter is visible in the next snapshot");
Require(viewModel.SelectPreset("SoloRig Default"), "preset selector activates the quick default action");
Require(viewModel.SelectedPresetName == "SoloRig Default" && !viewModel.IsDirty,
    "preset selector restores the clean default state");
Require(!viewModel.SelectPreset("missing-preset"), "preset selector rejects an unavailable preset");
Require(viewModel.SetParameter("drive-1.drive", 6.0F), "parameter edit after preset selection succeeds");
Require(viewModel.SelectAt(74.0, 158.0), "C ABI selects a node using editor coordinates");
Require(viewModel.BeginDrag(74.0, 158.0), "C ABI begins a node drag");
Require(viewModel.DragTo(210.0, 232.0, true), "C ABI drags and snaps a node");
viewModel.EndDrag();
Require(viewModel.Nodes.Single(node => node.InstanceId == "drive-1").Selected, "dragged node remains selected");
Require(viewModel.ToggleTheme(), "C ABI theme command succeeds");
Require(!viewModel.Snapshot.DarkTheme, "theme change is visible in the snapshot");

var path = Path.Combine(Path.GetTempPath(), "solorig-m2-contract.srig");
var corruptPath = Path.Combine(Path.GetTempPath(), "solorig-m2-corrupt.srig");
var resourcePath = Path.Combine(Path.GetTempPath(), "solorig-m5-resource.nam");
var midiBankDirectory = Path.Combine(Path.GetTempPath(), "solorig-m2-midi-bank");
try
{
    File.Delete(path);
    File.Delete(resourcePath);
    if (Directory.Exists(midiBankDirectory))
    {
        Directory.Delete(midiBankDirectory, true);
    }
    File.WriteAllText(corruptPath, "{not-json");
    var modelCandidates = new[]
    {
        Path.GetFullPath(Path.Combine(Directory.GetCurrentDirectory(), "third_party", "NeuralAmpModelerCore", "example_models", "wavenet.nam")),
        Path.GetFullPath(Path.Combine(Directory.GetCurrentDirectory(), "..", "third_party", "NeuralAmpModelerCore", "example_models", "wavenet.nam")),
        Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "third_party", "NeuralAmpModelerCore", "example_models", "wavenet.nam")),
        Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "third_party", "NeuralAmpModelerCore", "example_models", "wavenet.nam"))
    };
    var sourceModel = modelCandidates.FirstOrDefault(File.Exists);
    Require(!string.IsNullOrWhiteSpace(sourceModel), "NAM contract fixture is available");
    File.Copy(sourceModel!, resourcePath, true);
    Require(viewModel.ImportResource(resourcePath), "explicit resource import is recognized");
    Require(viewModel.Snapshot.ResourceStatus.Contains("NAM", StringComparison.Ordinal), "resource status identifies NAM");
    Require(viewModel.SaveToPath(path), "explicit Save writes a validated preset");
    Require(!viewModel.IsDirty, "successful Save clears dirty state");
    Directory.CreateDirectory(midiBankDirectory);
    File.Copy(path, Path.Combine(midiBankDirectory, "01-default.srig"), true);
    Require(viewModel.ImportMidiBank(midiBankDirectory), "MIDI Bank directory import uses the C ABI");
    Require(viewModel.Snapshot.Bindings.Any(binding => binding.MessageType == 1U && binding.TargetKind == 1U),
        "MIDI Bank import creates a Program Change preset binding");
    Require(viewModel.DeleteSelectedMidiBinding(), "selected MIDI binding can be deleted from C# UI state");
    Require(viewModel.ClearMidiBindings(), "MIDI bindings can be cleared from C# UI state");
    Require(viewModel.Snapshot.Bindings.Count == 0, "clearing MIDI bindings updates the shared snapshot");
    var savedLayout = viewModel.Nodes.Single(node => node.InstanceId == "drive-1").LayoutX;
    Require(viewModel.SetParameter("drive-1.drive", 5.0F), "post-save edit succeeds");
    var beforeFailure = viewModel.Snapshot;
    Require(!viewModel.OpenFromPath(corruptPath), "corrupt Open is rejected");
    Require(viewModel.Snapshot.SourcePath == beforeFailure.SourcePath, "failed Open keeps source path");
    Require(viewModel.IsDirty == beforeFailure.Dirty, "failed Open keeps dirty state");
    Require(viewModel.Snapshot.DarkTheme == beforeFailure.DarkTheme, "failed Open keeps theme state");
    Require(viewModel.Nodes.Single(node => node.InstanceId == "drive-1").LayoutX == savedLayout,
        "failed Open keeps the edited layout");
    Require(viewModel.OpenFromPath(path), "valid Open restores the saved preset");
    Require(viewModel.Nodes.Single(node => node.InstanceId == "drive-1").LayoutX == savedLayout,
        "Open restores the saved layout");
    Require(viewModel.Nodes.SelectMany(node => node.Parameters).Any(parameter => parameter.Id == "drive" && parameter.Value == 6.0F),
        "Open restores the saved parameter");
    var beforeCancel = viewModel.Snapshot;
    viewModel.ReportFileDialogCancelled("Open");
    Require(viewModel.Snapshot.SourcePath == beforeCancel.SourcePath && viewModel.IsDirty == beforeCancel.Dirty,
        "cancelled dialog keeps source and dirty state");
}
finally
{
    File.Delete(path);
    File.Delete(corruptPath);
    File.Delete(resourcePath);
    if (Directory.Exists(midiBankDirectory))
    {
        Directory.Delete(midiBankDirectory, true);
    }
}

Console.WriteLine("[PASS] Shared C# ViewModel performs catalog, graph editing, performance, resource and preset workflow");

static void Require(bool condition, string description)
{
    if (!condition)
    {
        throw new InvalidOperationException(description);
    }
}
