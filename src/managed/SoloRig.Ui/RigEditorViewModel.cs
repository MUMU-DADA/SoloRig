using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows.Input;

namespace SoloRig.Ui;

public sealed class RigEditorViewModel : INotifyPropertyChanged, IDisposable
{
    private readonly BackendSession backend;
    private BackendSnapshot snapshot = new(0, true, string.Empty, string.Empty, string.Empty, string.Empty, string.Empty, string.Empty, [], []);
    private string status = "Starting backend probe...";

    public RigEditorViewModel()
    {
        try
        {
            backend = new BackendSession();
            Refresh();
        }
        catch (Exception exception)
        {
            status = exception.Message;
            backend = null!;
        }
        RefreshCommand = new RelayCommand(Refresh);
        IncreaseDriveCommand = new RelayCommand(IncreaseDrive);
        ToggleThemeCommand = new RelayCommand(ToggleTheme);
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    public IReadOnlyList<NodeSnapshot> Nodes => snapshot.Nodes;
    public string WindowTitle => string.IsNullOrWhiteSpace(snapshot.PresetName) ? "SoloRig Avalonia M1" : $"SoloRig Avalonia M1 - {snapshot.PresetName}";
    public string AudioStatus => $"Audio: {snapshot.AudioStatus}";
    public string MidiStatus => $"MIDI: {snapshot.MidiStatus} | mappings: {snapshot.Bindings.Count}";
    public string FileStatus => string.IsNullOrWhiteSpace(snapshot.SourcePath) ? "Preset: unsaved default" : $"Preset: {snapshot.SourcePath}";
    public string Status { get => status; private set => Set(ref status, value); }
    public ICommand RefreshCommand { get; }
    public ICommand IncreaseDriveCommand { get; }
    public ICommand ToggleThemeCommand { get; }

    public void Refresh()
    {
        if (backend is null)
        {
            OnPropertiesChanged(nameof(Status));
            return;
        }
        try
        {
            snapshot = backend.ReadSnapshot();
            Status = $"Backend probe OK | schema {snapshot.SchemaVersion} | theme {(snapshot.DarkTheme ? "dark" : "light")}";
            OnPropertiesChanged(nameof(Nodes), nameof(WindowTitle), nameof(AudioStatus), nameof(MidiStatus), nameof(FileStatus));
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

    public void Dispose() => backend?.Dispose();

    private void IncreaseDrive()
    {
        var drive = snapshot.Nodes.SelectMany(node => node.Parameters.Select(parameter => (node.InstanceId, parameter)))
            .FirstOrDefault(item => item.parameter.Id == "drive");
        if (drive.parameter is not null)
        {
            SetParameter($"{drive.InstanceId}.drive", drive.parameter.Value + 1.0F);
        }
    }

    private void ToggleTheme()
    {
        if (backend is null)
        {
            return;
        }
        try
        {
            backend.SetTheme(!snapshot.DarkTheme);
            Refresh();
        }
        catch (Exception exception)
        {
            Status = exception.Message;
        }
    }

    private void Set<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return;
        }
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
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
