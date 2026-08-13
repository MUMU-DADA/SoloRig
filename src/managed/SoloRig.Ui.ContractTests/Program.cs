using SoloRig.Ui;

using var viewModel = new RigEditorViewModel();
Require(viewModel.Nodes.Count >= 2, "default snapshot exposes Drive and Delay nodes");
Require(viewModel.MidiStatus.Contains("closed", StringComparison.OrdinalIgnoreCase), "startup MIDI remains closed");
Require(viewModel.SetParameter("drive-1.drive", 6.0F), "C ABI parameter edit succeeds");
Require(viewModel.Nodes.SelectMany(node => node.Parameters).Any(parameter => parameter.Id == "drive" && parameter.Value == 6.0F),
    "edited parameter is visible in the next snapshot");
Console.WriteLine("[PASS] Avalonia shared ViewModel performs C ABI snapshot and parameter edit");

static void Require(bool condition, string description)
{
    if (!condition)
    {
        throw new InvalidOperationException(description);
    }
}
