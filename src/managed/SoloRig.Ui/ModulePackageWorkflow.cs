using System.IO.Compression;
using System.Text.Json;

namespace SoloRig.Ui;

public sealed record ModulePackageManifestInfo(
    string ModuleId,
    string DisplayName,
    string PackageVersion,
    string Category);

public sealed class ModuleInstallConflictException(
    string destination, string installedVersion, string requestedVersion)
    : IOException($"模块已安装：{installedVersion}，待安装版本：{requestedVersion}")
{
    public string Destination { get; } = destination;
    public string InstalledVersion { get; } = installedVersion;
    public string RequestedVersion { get; } = requestedVersion;
}

public static class ModulePackageWorkflow
{
    public static ModulePackageManifestInfo ReadManifest(string projectDirectory)
    {
        var manifestPath = Path.Combine(Path.GetFullPath(projectDirectory), "manifest.json");
        using var document = JsonDocument.Parse(File.ReadAllText(manifestPath));
        var root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object ||
            !root.TryGetProperty("moduleId", out var moduleId) ||
            !root.TryGetProperty("displayName", out var displayName) ||
            !root.TryGetProperty("packageVersion", out var version) ||
            !root.TryGetProperty("category", out var category))
        {
            throw new InvalidDataException("模块清单缺少安装所需字段");
        }
        return new ModulePackageManifestInfo(
            moduleId.GetString() ?? throw new InvalidDataException("模块 ID 无效"),
            displayName.GetString() ?? throw new InvalidDataException("模块显示名称无效"),
            version.GetString() ?? throw new InvalidDataException("模块版本无效"),
            category.GetString() ?? throw new InvalidDataException("模块类别无效"));
    }

    public static string CreateStagingDirectory(string destination, bool preserveExisting)
    {
        var fullDestination = Path.GetFullPath(destination);
        var parent = Path.GetDirectoryName(fullDestination) ??
                     throw new InvalidDataException("目标目录缺少父目录");
        Directory.CreateDirectory(parent);
        var name = Path.GetFileName(fullDestination.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
        if (string.IsNullOrWhiteSpace(name)) throw new InvalidDataException("目标目录无效");
        var staging = Path.Combine(parent, $".{name}.{Guid.NewGuid():N}.staging");
        Directory.CreateDirectory(staging);
        if (preserveExisting && Directory.Exists(fullDestination))
        {
            CopyDirectoryContents(fullDestination, staging);
        }
        return staging;
    }

    public static void CommitStagedDirectory(string stagingDirectory, string destination)
    {
        var staging = Path.GetFullPath(stagingDirectory);
        var fullDestination = Path.GetFullPath(destination);
        var parent = Path.GetDirectoryName(fullDestination) ??
                     throw new InvalidDataException("目标目录缺少父目录");
        if (!string.Equals(Path.GetDirectoryName(staging), parent, StringComparison.OrdinalIgnoreCase) ||
            !Directory.Exists(staging))
        {
            throw new InvalidDataException("暂存目录不属于目标目录的安全同级位置");
        }
        if (!Directory.Exists(fullDestination))
        {
            Directory.Move(staging, fullDestination);
            return;
        }

        var backup = Path.Combine(parent, $".{Path.GetFileName(fullDestination)}.{Guid.NewGuid():N}.backup");
        Directory.Move(fullDestination, backup);
        try
        {
            Directory.Move(staging, fullDestination);
        }
        catch
        {
            if (!Directory.Exists(fullDestination) && Directory.Exists(backup))
            {
                Directory.Move(backup, fullDestination);
            }
            throw;
        }
        try
        {
            Directory.Delete(backup, true);
        }
        catch (IOException)
        {
            // The committed project is valid. A locked backup can be removed manually later.
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    public static void CopyProject(string sourceDirectory, string destinationDirectory)
    {
        var source = Path.GetFullPath(sourceDirectory);
        var destination = Path.GetFullPath(destinationDirectory);
        if (string.Equals(source, destination, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("项目副本目录不能与源目录相同");
        }
        if (Directory.Exists(destination) && Directory.EnumerateFileSystemEntries(destination).Any())
        {
            throw new IOException("项目副本目标目录不是空目录");
        }
        var staging = CreateStagingDirectory(destination, false);
        try
        {
            CopyDirectoryContents(source, staging);
            CommitStagedDirectory(staging, destination);
        }
        finally
        {
            TryDeleteDirectory(staging);
        }
    }

    public static void ExportPackage(string projectDirectory, string destinationZip)
    {
        var source = Path.GetFullPath(projectDirectory);
        _ = ReadManifest(source);
        var destination = Path.GetFullPath(destinationZip);
        var parent = Path.GetDirectoryName(destination) ??
                     throw new InvalidDataException("导出文件缺少父目录");
        Directory.CreateDirectory(parent);
        var temporary = Path.Combine(parent, $".{Path.GetFileName(destination)}.{Guid.NewGuid():N}.tmp");
        try
        {
            using (var archive = ZipFile.Open(temporary, ZipArchiveMode.Create))
            {
                foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
                {
                    var relative = Path.GetRelativePath(source, file).Replace('\\', '/');
                    if (relative.StartsWith(".solorig/", StringComparison.OrdinalIgnoreCase)) continue;
                    archive.CreateEntryFromFile(file, relative, CompressionLevel.Optimal);
                }
            }
            File.Move(temporary, destination, true);
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }

    public static string Install(string projectDirectory, string? libraryRoot = null, bool overwrite = false)
    {
        var source = Path.GetFullPath(projectDirectory);
        var manifest = ReadManifest(source);
        var root = Path.GetFullPath(libraryRoot ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SoloRig", "modules"));
        Directory.CreateDirectory(root);
        var safeName = string.Concat(manifest.ModuleId.Select(character =>
            char.IsLetterOrDigit(character) || character is '.' or '-' or '_' ? character : '_'));
        var destination = Path.Combine(root, safeName);
        if (Directory.Exists(destination) && !overwrite)
        {
            var installed = ReadManifest(destination);
            throw new ModuleInstallConflictException(destination, installed.PackageVersion, manifest.PackageVersion);
        }
        var staging = CreateStagingDirectory(destination, false);
        try
        {
            CopyDirectoryContents(source, staging, skipWorkshopMetadata: true);
            _ = ReadManifest(staging);
            CommitStagedDirectory(staging, destination);
            return destination;
        }
        finally
        {
            TryDeleteDirectory(staging);
        }
    }

    public static string ImportProjectResource(string projectDirectory, string sourcePath, string relativeFolder)
    {
        var project = Path.GetFullPath(projectDirectory);
        var source = Path.GetFullPath(sourcePath);
        if (!File.Exists(source)) throw new FileNotFoundException("资源文件不存在", source);
        var normalizedFolder = relativeFolder.Replace('\\', '/').Trim('/');
        if (normalizedFolder.Split('/').Any(part => part is "" or "." or ".."))
        {
            throw new InvalidDataException("资源目录不安全");
        }
        var destinationFolder = Path.Combine(project,
            normalizedFolder.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(destinationFolder);
        var stem = Path.GetFileNameWithoutExtension(source);
        var extension = Path.GetExtension(source).ToLowerInvariant();
        var destination = Path.Combine(destinationFolder, stem + extension);
        for (var index = 2; File.Exists(destination); ++index)
        {
            destination = Path.Combine(destinationFolder, $"{stem}-{index}{extension}");
        }
        var temporary = Path.Combine(destinationFolder, $".{Path.GetFileName(destination)}.{Guid.NewGuid():N}.tmp");
        try
        {
            File.Copy(source, temporary, false);
            File.Move(temporary, destination);
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
        return Path.GetRelativePath(project, destination).Replace('\\', '/');
    }

    private static void CopyDirectoryContents(string source, string destination, bool skipWorkshopMetadata = false)
    {
        foreach (var directory in Directory.EnumerateDirectories(source, "*", SearchOption.AllDirectories))
        {
            var relative = Path.GetRelativePath(source, directory).Replace('\\', '/');
            if (skipWorkshopMetadata &&
                (relative.Equals(".solorig", StringComparison.OrdinalIgnoreCase) ||
                 relative.StartsWith(".solorig/", StringComparison.OrdinalIgnoreCase))) continue;
            Directory.CreateDirectory(Path.Combine(destination,
                relative.Replace('/', Path.DirectorySeparatorChar)));
        }
        foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            var relative = Path.GetRelativePath(source, file).Replace('\\', '/');
            if (skipWorkshopMetadata && relative.StartsWith(".solorig/", StringComparison.OrdinalIgnoreCase)) continue;
            var target = Path.Combine(destination, relative.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(file, target, true);
        }
    }

    private static void TryDeleteDirectory(string directory)
    {
        try
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, true);
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }
}
