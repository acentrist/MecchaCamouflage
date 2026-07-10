using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using MecchaCamouflage.Core;

namespace MecchaCamouflage.Controller;

public sealed record PackagedAssetEntry(
    string Role,
    string LogicalPath,
    string ResourceName,
    long Size,
    string Sha256,
    bool Required);

public sealed record PackagedAssetManifest(
    int Schema,
    string AssetSetId,
    string CreatedUtc,
    IReadOnlyList<PackagedAssetEntry> Assets);

public sealed record PackagedAssetValidation(bool Valid, string Code, string Message);

public static class PackagedAssets
{
    private const string ResourcePrefix = "packaged/";
    private static readonly object ExtractLock = new();
    private static readonly object AssetLogLock = new();
    private static readonly object ManifestLock = new();
    private static readonly HashSet<string> LoggedAssetSetIds = new(StringComparer.OrdinalIgnoreCase);
    private static PackagedAssetManifest? cachedManifest;
    private static string[]? cachedManifestResources;
    private static int WorkerCount => Math.Max(1, Environment.ProcessorCount);

    public static string ResolveAssetRoot(AppPaths paths, string directoryName, RuntimeLog? log = null)
    {
        var extracted = EnsureExtracted(paths, directoryName, log);
        if (!string.IsNullOrEmpty(extracted) && Directory.Exists(Path.Combine(extracted, directoryName)))
            return extracted;
        return AppContext.BaseDirectory;
    }

    public static string ResolveRequiredAssetRoot(AppPaths paths, string directoryName, RuntimeLog? log = null)
    {
        var root = ResolveAssetRoot(paths, directoryName, log);
        if (!Directory.Exists(Path.Combine(root, directoryName)))
        {
            DiagnosticsState.SetLastCode("MC-RT-001", $"missing packaged asset directory: {directoryName}");
            throw new DirectoryNotFoundException($"MC-RT-001 Packaged asset directory is missing: {directoryName}");
        }
        return root;
    }

    public static PackagedAssetValidation ValidateExtractedAssetSet(string root, PackagedAssetManifest manifest)
        => ValidateExtractedAssetSet(root, manifest, directoryName: null);

    private static PackagedAssetValidation ValidateExtractedAssetSet(string root, PackagedAssetManifest manifest, string? directoryName)
    {
        if (!Directory.Exists(root))
            return new PackagedAssetValidation(false, "MC-RT-010", "asset cache directory is missing");

        var readyPath = Path.Combine(root, "ready.json");
        if (!File.Exists(readyPath))
            return new PackagedAssetValidation(false, "MC-RT-010", "asset cache ready.json is missing");

        try
        {
            using var ready = JsonDocument.Parse(File.ReadAllText(readyPath));
            if (!ready.RootElement.TryGetProperty("assetSetId", out var id) ||
                !string.Equals(id.GetString(), manifest.AssetSetId, StringComparison.OrdinalIgnoreCase))
            {
                return new PackagedAssetValidation(false, "MC-RT-010", "asset cache ready.json belongs to a different asset set");
            }
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            return new PackagedAssetValidation(false, "MC-RT-010", "asset cache ready.json could not be read");
        }

        var requiredAssets = manifest.Assets.Where(asset => asset.Required);
        if (!string.IsNullOrWhiteSpace(directoryName))
        {
            var prefix = directoryName.Replace('\\', '/').Trim('/') + "/";
            requiredAssets = requiredAssets.Where(asset => asset.LogicalPath.StartsWith(prefix, StringComparison.OrdinalIgnoreCase));
        }

        var failure = requiredAssets
            .OrderBy(asset => asset.LogicalPath, StringComparer.Ordinal)
            .AsParallel()
            .AsOrdered()
            .WithDegreeOfParallelism(WorkerCount)
            .Select(asset => ValidateOneAsset(root, asset))
            .FirstOrDefault(result => !result.Valid);
        if (failure is not null)
            return failure;

        return new PackagedAssetValidation(true, "", "asset cache is valid");
    }

    public static bool CopyIfInvalid(string source, string target)
    {
        if (!File.Exists(source))
            throw new FileNotFoundException("Source asset is missing.", source);

        var sourceInfo = new FileInfo(source);
        var needsCopy = !File.Exists(target);
        if (!needsCopy)
        {
            var targetInfo = new FileInfo(target);
            needsCopy = targetInfo.Length != sourceInfo.Length ||
                        !string.Equals(Sha256File(source), Sha256File(target), StringComparison.OrdinalIgnoreCase);
        }
        if (!needsCopy)
            return false;

        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        File.Copy(source, target, true);
        return true;
    }

    public static string Sha256File(string path)
    {
        using var stream = File.OpenRead(path);
        return Sha256Stream(stream);
    }

    private static string? EnsureExtracted(AppPaths paths, string directoryName, RuntimeLog? log)
    {
        var assembly = Assembly.GetEntryAssembly() ?? typeof(PackagedAssets).Assembly;
        var resources = assembly.GetManifestResourceNames()
            .Where(name => name.StartsWith(ResourcePrefix, StringComparison.Ordinal))
            .Order(StringComparer.Ordinal)
            .ToArray();
        if (resources.Length == 0)
            return null;

        var manifest = GetManifest(assembly, resources);
        var parent = Path.Combine(paths.RuntimeDirectory, "package-assets");
        var root = Path.Combine(parent, manifest.AssetSetId);
        var validation = ValidateExtractedAssetSet(root, manifest, directoryName);
        if (validation.Valid)
        {
            DiagnosticsState.SetAssetValidation($"valid {manifest.AssetSetId}");
            LogValidAssetSetOnce(manifest, log);
            return root;
        }

        lock (ExtractLock)
        {
            validation = ValidateExtractedAssetSet(root, manifest, directoryName);
            if (validation.Valid)
            {
                DiagnosticsState.SetAssetValidation($"valid {manifest.AssetSetId}");
                LogValidAssetSetOnce(manifest, log);
                return root;
            }

            DiagnosticsState.SetAssetValidation($"repair {manifest.AssetSetId}: {validation.Message}");
            if (validation.Message.Contains("directory is missing", StringComparison.OrdinalIgnoreCase) ||
                validation.Message.Contains("ready.json is missing", StringComparison.OrdinalIgnoreCase))
            {
                log?.Info("Runtime assets: preparing local cache.");
            }
            else
            {
                log?.Warn("Runtime assets: cache invalid; repairing.");
            }
            Directory.CreateDirectory(parent);
            var staging = Path.Combine(parent, manifest.AssetSetId + "." + Guid.NewGuid().ToString("N") + ".staging");
            Directory.CreateDirectory(staging);
            try
            {
                ExtractResources(assembly, manifest, staging);
                WriteReadyFile(staging, manifest);
                validation = ValidateExtractedAssetSet(staging, manifest, directoryName);
                if (!validation.Valid)
                    throw new IOException($"{validation.Code} extracted runtime assets failed validation: {validation.Message}");

                if (Directory.Exists(root))
                    Directory.Delete(root, recursive: true);
                Directory.Move(staging, root);

                validation = ValidateExtractedAssetSet(root, manifest, directoryName);
                if (!validation.Valid)
                {
                    DiagnosticsState.SetLastCode("MC-RT-012", validation.Message);
                    throw new IOException($"MC-RT-012 runtime asset vanished or changed after extraction: {validation.Message}");
                }

                DiagnosticsState.SetAssetValidation($"repaired {manifest.AssetSetId}");
                MarkAssetSetLogged(manifest);
                log?.Info("Runtime assets: prepared.");
                CleanupOldAssetDirectories(parent, root);
            }
            catch
            {
                TryDeleteDirectory(staging);
                throw;
            }
        }
        return root;
    }

    private static void LogValidAssetSetOnce(PackagedAssetManifest manifest, RuntimeLog? log)
    {
        lock (AssetLogLock)
        {
            if (!LoggedAssetSetIds.Add(manifest.AssetSetId))
                return;
        }
        log?.Info("Runtime assets: prepared.");
    }

    private static void MarkAssetSetLogged(PackagedAssetManifest manifest)
    {
        lock (AssetLogLock)
            LoggedAssetSetIds.Add(manifest.AssetSetId);
    }

    private static PackagedAssetManifest BuildManifest(Assembly assembly, string[] resources)
    {
        var assets = resources
            .AsParallel()
            .WithDegreeOfParallelism(WorkerCount)
            .Select(resource => BuildManifestEntry(assembly, resource))
            .OrderBy(asset => asset.LogicalPath, StringComparer.Ordinal)
            .ToList();

        using var manifestSha = SHA256.Create();
        foreach (var asset in assets.OrderBy(asset => asset.LogicalPath, StringComparer.Ordinal))
        {
            var bytes = Encoding.UTF8.GetBytes($"{asset.Role}\n{asset.LogicalPath}\n{asset.Size}\n{asset.Sha256}\n");
            manifestSha.TransformBlock(bytes, 0, bytes.Length, null, 0);
        }
        manifestSha.TransformFinalBlock(Array.Empty<byte>(), 0, 0);
        var id = Convert.ToHexString(manifestSha.Hash ?? Array.Empty<byte>()).ToLowerInvariant()[..16];
        return new PackagedAssetManifest(1, id, DateTimeOffset.UtcNow.ToString("O"), assets);
    }

    private static PackagedAssetManifest GetManifest(Assembly assembly, string[] resources)
    {
        lock (ManifestLock)
        {
            if (cachedManifest is not null &&
                cachedManifestResources is not null &&
                cachedManifestResources.SequenceEqual(resources, StringComparer.Ordinal))
            {
                return cachedManifest;
            }
            cachedManifest = BuildManifest(assembly, resources);
            cachedManifestResources = resources;
            return cachedManifest;
        }
    }

    private static void ExtractResources(Assembly assembly, PackagedAssetManifest manifest, string root)
    {
        var fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        Parallel.ForEach(
            manifest.Assets,
            new ParallelOptions { MaxDegreeOfParallelism = WorkerCount },
            asset =>
        {
            var destination = Path.GetFullPath(Path.Combine(root, ToNativeRelativePath(asset.LogicalPath)));
            if (!destination.StartsWith(fullRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Invalid packaged resource path: " + asset.ResourceName);

            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            using var input = assembly.GetManifestResourceStream(asset.ResourceName)
                ?? throw new FileNotFoundException("Packaged resource is missing: " + asset.ResourceName);
            using var output = File.Create(destination);
            input.CopyTo(output);
        });
    }

    private static PackagedAssetEntry BuildManifestEntry(Assembly assembly, string resource)
    {
        using var stream = assembly.GetManifestResourceStream(resource)
            ?? throw new FileNotFoundException("Packaged resource is missing: " + resource);
        var sha = Sha256Stream(stream);
        return new PackagedAssetEntry(
            RoleFor(resource[ResourcePrefix.Length..]),
            resource[ResourcePrefix.Length..],
            resource,
            stream.Length,
            sha,
            Required: true);
    }

    private static PackagedAssetValidation ValidateOneAsset(string root, PackagedAssetEntry asset)
    {
        var path = Path.Combine(root, ToNativeRelativePath(asset.LogicalPath));
        if (!File.Exists(path))
            return new PackagedAssetValidation(false, "MC-RT-011", $"required asset is missing: {asset.LogicalPath}");
        var info = new FileInfo(path);
        if (info.Length != asset.Size)
            return new PackagedAssetValidation(false, "MC-RT-011", $"required asset has wrong size: {asset.LogicalPath}");
        var hash = Sha256File(path);
        if (!string.Equals(hash, asset.Sha256, StringComparison.OrdinalIgnoreCase))
            return new PackagedAssetValidation(false, "MC-RT-011", $"required asset has wrong hash: {asset.LogicalPath}");
        return new PackagedAssetValidation(true, "", "");
    }

    private static void WriteReadyFile(string root, PackagedAssetManifest manifest)
    {
        var payload = new
        {
            schema = manifest.Schema,
            assetSetId = manifest.AssetSetId,
            createdUtc = DateTimeOffset.UtcNow.ToString("O"),
            fileCount = manifest.Assets.Count,
            roles = manifest.Assets.Select(asset => asset.Role).Distinct().Order(StringComparer.Ordinal).ToArray()
        };
        File.WriteAllText(Path.Combine(root, "ready.json"), JsonSerializer.Serialize(payload, new JsonSerializerOptions { WriteIndented = true }));
    }

    private static string Sha256Stream(Stream stream)
    {
        using var sha = SHA256.Create();
        var hash = sha.ComputeHash(stream);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }

    private static string RoleFor(string logicalPath)
    {
        var normalized = logicalPath.Replace('\\', '/');
        if (string.Equals(normalized, "native/bridge-loader.dll", StringComparison.OrdinalIgnoreCase))
            return "native.bridge-loader";
        if (string.Equals(normalized, "native/runtime-bridge.dll", StringComparison.OrdinalIgnoreCase))
            return "native.bridge";
        if (string.Equals(normalized, "native/runtime-injector.exe", StringComparison.OrdinalIgnoreCase))
            return "native.injector";
        if (normalized.StartsWith("webview2/", StringComparison.OrdinalIgnoreCase))
            return "webview2.fixed";
        if (normalized.StartsWith("web/", StringComparison.OrdinalIgnoreCase))
            return "web.assets";
        if (normalized.StartsWith("mesh-profiles/", StringComparison.OrdinalIgnoreCase))
            return "mesh.profile";
        return "asset";
    }

    private static string ToNativeRelativePath(string logicalPath)
    {
        var normalized = logicalPath.Replace('\\', '/');
        if (Path.IsPathRooted(normalized) || normalized.Split('/').Any(part => part == ".."))
            throw new InvalidOperationException("Invalid packaged resource path: " + logicalPath);
        return Path.Combine(normalized.Split('/'));
    }

    private static void CleanupOldAssetDirectories(string parent, string keepDirectory)
    {
        var keep = Path.GetFullPath(keepDirectory).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        foreach (var directory in Directory.GetDirectories(parent)
                     .Select(path => new DirectoryInfo(path))
                     .Where(info => !string.Equals(
                         Path.GetFullPath(info.FullName).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                         keep,
                         StringComparison.OrdinalIgnoreCase))
                     .Where(info => !info.Name.Contains(".staging", StringComparison.OrdinalIgnoreCase))
                     .OrderByDescending(info => info.LastWriteTimeUtc)
                     .Skip(2))
        {
            TryDeleteDirectory(directory.FullName);
        }
    }

    private static void TryDeleteDirectory(string path)
    {
        try
        {
            if (Directory.Exists(path))
                Directory.Delete(path, recursive: true);
        }
        catch
        {
            // Extracted package assets can be cleaned on a later launch.
        }
    }
}
