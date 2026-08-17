using System.Collections.ObjectModel;
using System.Text.Json;

namespace SoloRig.Ui;

public sealed class RecentModuleProject
{
    public RecentModuleProject(string directory)
    {
        Directory = Path.GetFullPath(directory);
        try
        {
            var manifest = ModulePackageWorkflow.ReadManifest(Directory);
            ModuleId = manifest.ModuleId;
            ModuleDisplayName = manifest.DisplayName;
            PackageVersion = manifest.PackageVersion;
            Category = manifest.Category;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException)
        {
        }
    }

    public string Directory { get; }
    public string ModuleId { get; } = string.Empty;
    public string ModuleDisplayName { get; } = string.Empty;
    public string PackageVersion { get; } = string.Empty;
    public string Category { get; } = string.Empty;
    public string DisplayName
    {
        get
        {
            var folderName = Path.GetFileName(Directory.TrimEnd(
                Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            var name = string.IsNullOrWhiteSpace(ModuleDisplayName) ? folderName : ModuleDisplayName;
            var version = string.IsNullOrWhiteSpace(PackageVersion) ? string.Empty : $" · v{PackageVersion}";
            return $"{name}{version} · {Directory}";
        }
    }
    public string SearchText => $"{ModuleDisplayName} {ModuleId} {PackageVersion} {Category} {Directory}";
}

public sealed class ModuleAuthoringRecentProjects
{
    private const int MaximumEntries = 8;
    private readonly string storagePath;

    public ModuleAuthoringRecentProjects(string? storagePath = null)
    {
        this.storagePath = storagePath ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "SoloRig", "recent-module-projects.json");
        Load();
    }

    public ObservableCollection<RecentModuleProject> Entries { get; } = [];

    public void Touch(string directory)
    {
        var fullPath = Path.GetFullPath(directory);
        var existing = Entries.FirstOrDefault(entry =>
            string.Equals(Path.GetFullPath(entry.Directory), fullPath, StringComparison.OrdinalIgnoreCase));
        if (existing is not null)
        {
            Entries.Remove(existing);
        }
        Entries.Insert(0, new RecentModuleProject(fullPath));
        while (Entries.Count > MaximumEntries)
        {
            Entries.RemoveAt(Entries.Count - 1);
        }
        Save();
    }

    private void Load()
    {
        try
        {
            if (!File.Exists(storagePath))
            {
                return;
            }
            var paths = JsonSerializer.Deserialize<string[]>(File.ReadAllText(storagePath)) ?? [];
            foreach (var path in paths.Where(path => !string.IsNullOrWhiteSpace(path)).Take(MaximumEntries))
            {
                var fullPath = Path.GetFullPath(path);
                if (Directory.Exists(fullPath) && File.Exists(Path.Combine(fullPath, "manifest.json")) &&
                    !Entries.Any(entry => string.Equals(entry.Directory, fullPath, StringComparison.OrdinalIgnoreCase)))
                {
                    Entries.Add(new RecentModuleProject(fullPath));
                }
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or JsonException or ArgumentException)
        {
            Entries.Clear();
        }
    }

    private void Save()
    {
        try
        {
            var parent = Path.GetDirectoryName(storagePath);
            if (string.IsNullOrWhiteSpace(parent))
            {
                return;
            }
            Directory.CreateDirectory(parent);
            var temporary = Path.Combine(parent, $".{Path.GetFileName(storagePath)}.{Guid.NewGuid():N}.tmp");
            try
            {
                File.WriteAllText(temporary, JsonSerializer.Serialize(Entries.Select(entry => entry.Directory).ToArray()));
                File.Move(temporary, storagePath, true);
            }
            finally
            {
                if (File.Exists(temporary))
                {
                    File.Delete(temporary);
                }
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            // Recent projects are convenience state; project operations remain successful if persistence is unavailable.
        }
    }
}
