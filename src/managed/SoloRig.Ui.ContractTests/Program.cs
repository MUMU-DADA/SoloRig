using SoloRig.Ui;

using var viewModel = new RigEditorViewModel();
Require(viewModel.Nodes.Count >= 2, "default snapshot exposes Drive and Delay nodes");
Require(viewModel.Snapshot.Connections.Count == 1, "default snapshot exposes the Drive-to-Delay connection");
Require(viewModel.MidiStatus.Contains("closed", StringComparison.OrdinalIgnoreCase), "startup MIDI remains closed");
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
try
{
    File.Delete(path);
    File.WriteAllText(corruptPath, "{not-json");
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
}

Console.WriteLine("[PASS] Avalonia shared ViewModel performs M2 layout, parameter, theme and file workflow");

static void Require(bool condition, string description)
{
    if (!condition)
    {
        throw new InvalidOperationException(description);
    }
}
