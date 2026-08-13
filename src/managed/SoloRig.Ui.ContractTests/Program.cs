using SoloRig.Ui;

using var viewModel = new RigEditorViewModel();
Require(viewModel.Nodes.Count >= 2, "default snapshot exposes Drive and Delay nodes");
Require(viewModel.Snapshot.Connections.Count == 1, "default snapshot exposes the Drive-to-Delay connection");
Require(viewModel.MidiStatus.Contains("closed", StringComparison.OrdinalIgnoreCase), "startup MIDI remains closed");
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
try
{
    File.Delete(path);
    File.Delete(resourcePath);
    File.WriteAllText(corruptPath, "{not-json");
    File.WriteAllText(resourcePath, "model");
    Require(viewModel.ImportResource(resourcePath), "explicit resource import is recognized");
    Require(viewModel.Snapshot.ResourceStatus.Contains("NAM", StringComparison.Ordinal), "resource status identifies NAM");
    Require(viewModel.SaveToPath(path), "explicit Save writes a validated preset");
    Require(!viewModel.IsDirty, "successful Save clears dirty state");
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
}

Console.WriteLine("[PASS] Avalonia shared ViewModel performs M5 catalog, graph editing, performance, resource and preset workflow");

static void Require(bool condition, string description)
{
    if (!condition)
    {
        throw new InvalidOperationException(description);
    }
}
