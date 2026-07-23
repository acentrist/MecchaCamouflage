using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace MecchaCamouflage.Core;

/// <summary>
/// Stores an editable Image Paint preset as one deterministic, uncompressed
/// container. PNG/JPEG sources are already compressed; keeping entries raw
/// makes Save predictable and avoids spending CPU recompressing a 64 MiB
/// draft. The same format is used for the private active state.
/// </summary>
public sealed class ImagePresetStore
{
    public const string PresetExtension = ".mcpreset";
    private const int ContainerVersion = 2;
    private const int FirstSupportedContainerVersion = 1;
    private static readonly byte[] Magic = "MCIPRST1"u8.ToArray();
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = false,
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower
    };

    private readonly AppPaths paths;

    public ImagePresetStore(AppPaths paths) => this.paths = paths;

    public ImagePresetResult SavePreset(string path, ImagePaintSettings design) =>
        Save(path, design, requirePresetExtension: true);

    public ImagePresetResult SaveActive(ImagePaintSettings design) =>
        Save(paths.ActiveImageStatePath, design, requirePresetExtension: false);

    public bool TryLoadPreset(string path, out ImagePaintSettings design, out string message) =>
        TryLoad(path, requirePresetExtension: true, out design, out message);

    public bool TryLoadActive(out ImagePaintSettings design, out string message) =>
        TryLoad(paths.ActiveImageStatePath, requirePresetExtension: false, out design, out message);

    private ImagePresetResult Save(string path, ImagePaintSettings source, bool requirePresetExtension)
    {
        if (!TryNormalizePath(path, requirePresetExtension, out var target, out var pathMessage))
            return new ImagePresetResult(false, pathMessage);
        if (source is null)
            return new ImagePresetResult(false, "Preset data is missing.");

        var design = Clone(source);
        design.MigrateLegacyLayerTransforms();
        design.Revision = design.Enabled ? Math.Max(1, design.Revision) : 0;
        if (design.Enabled && !design.TryValidate(out var validationMessage))
            return new ImagePresetResult(false, validationMessage);

        try
        {
            var entries = new List<PresetEntry>();
            if (design.Enabled)
            {
                entries.Add(new PresetEntry("canvas.rgba", Convert.FromBase64String(design.CanvasRgbaBase64)));
                design.CanvasRgbaBase64 = "";
                for (var index = 0; index < design.Layers.Count; ++index)
                {
                    var layer = design.Layers[index];
                    layer.AssetId = Guid.TryParse(layer.AssetId, out var assetId) ? assetId.ToString("N") : Guid.NewGuid().ToString("N");
                    var extension = layer.MimeType == "image/jpeg" ? "jpg" : "png";
                    entries.Add(new PresetEntry($"layers/{index}.{extension}", Convert.FromBase64String(layer.DataBase64)));
                    layer.DataBase64 = "";
                }
            }
            var manifest = new PresetManifest { SchemaVersion = ContainerVersion, Image = design };
            var manifestBytes = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(manifest, JsonOptions));
            if (manifestBytes.Length is < 2 or > 1_048_576)
                return new ImagePresetResult(false, "Preset manifest is invalid.");

            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            var temporary = target + ".tmp-" + Guid.NewGuid().ToString("N");
            try
            {
                using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                using (var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: false))
                {
                    writer.Write(Magic);
                    writer.Write(ContainerVersion);
                    writer.Write(manifestBytes.Length);
                    writer.Write(entries.Count);
                    writer.Write(manifestBytes);
                    foreach (var entry in entries)
                    {
                        writer.Write(entry.Name);
                        writer.Write((long)entry.Data.Length);
                        writer.Write(SHA256.HashData(entry.Data));
                    }
                    foreach (var entry in entries)
                        writer.Write(entry.Data);
                }
                File.Move(temporary, target, overwrite: true);
                return new ImagePresetResult(true, "", target);
            }
            finally
            {
                if (File.Exists(temporary))
                    File.Delete(temporary);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or FormatException or JsonException)
        {
            return new ImagePresetResult(false, ex.Message);
        }
    }

    private bool TryLoad(string path, bool requirePresetExtension, out ImagePaintSettings design, out string message)
    {
        design = new ImagePaintSettings();
        if (!TryNormalizePath(path, requirePresetExtension, out var target, out message))
            return false;
        if (!File.Exists(target))
        {
            message = "Preset file does not exist.";
            return false;
        }
        try
        {
            using var stream = new FileStream(target, FileMode.Open, FileAccess.Read, FileShare.Read);
            using var reader = new BinaryReader(stream, Encoding.UTF8, leaveOpen: false);
            if (!reader.ReadBytes(Magic.Length).SequenceEqual(Magic))
            {
                message = "Preset format is unsupported.";
                return false;
            }
            var containerVersion = reader.ReadInt32();
            if (containerVersion is < FirstSupportedContainerVersion or > ContainerVersion)
            {
                message = "Preset format is unsupported.";
                return false;
            }
            var manifestLength = reader.ReadInt32();
            var entryCount = reader.ReadInt32();
            if (manifestLength is < 2 or > 1_048_576 || entryCount is < 0 or > 1024)
            {
                message = "Preset header is invalid.";
                return false;
            }
            var manifest = JsonSerializer.Deserialize<PresetManifest>(reader.ReadBytes(manifestLength), JsonOptions);
            if (manifest is null || manifest.SchemaVersion != containerVersion || manifest.Image is null)
            {
                message = "Preset manifest is invalid.";
                return false;
            }
            var descriptors = new List<PresetEntryDescriptor>(entryCount);
            long totalBytes = 0;
            for (var index = 0; index < entryCount; ++index)
            {
                var name = reader.ReadString();
                var length = reader.ReadInt64();
                var hash = reader.ReadBytes(32);
                if (!IsEntryNameValid(name) || length is < 0 or > ImagePaintSettings.MaximumTotalSourceBytes || hash.Length != 32)
                {
                    message = "Preset entry table is invalid.";
                    return false;
                }
                totalBytes += length;
                if (totalBytes > ImagePaintSettings.MaximumTotalSourceBytes + ImagePaintSettings.CanvasByteLength)
                {
                    message = "Preset entries are too large.";
                    return false;
                }
                descriptors.Add(new PresetEntryDescriptor(name, length, hash));
            }
            var data = new Dictionary<string, byte[]>(StringComparer.Ordinal);
            foreach (var descriptor in descriptors)
            {
                var bytes = reader.ReadBytes(checked((int)descriptor.Length));
                if (bytes.LongLength != descriptor.Length || !SHA256.HashData(bytes).SequenceEqual(descriptor.Hash) || !data.TryAdd(descriptor.Name, bytes))
                {
                    message = "Preset entry hash does not match.";
                    return false;
                }
            }
            if (stream.Position != stream.Length)
            {
                message = "Preset has trailing data.";
                return false;
            }

            design = Clone(manifest.Image);
            design.MigrateLegacyLayerTransforms();
            if (!design.Enabled)
            {
                if (data.Count != 0)
                {
                    message = "Disabled image state must not contain assets.";
                    return false;
                }
                return true;
            }
            if (!data.Remove("canvas.rgba", out var canvas) || canvas.Length != ImagePaintSettings.CanvasByteLength)
            {
                message = "Preset canvas is missing or invalid.";
                return false;
            }
            design.CanvasRgbaBase64 = Convert.ToBase64String(canvas);
            for (var index = 0; index < design.Layers.Count; ++index)
            {
                var layer = design.Layers[index];
                var extension = layer.MimeType == "image/jpeg" ? "jpg" : "png";
                if (!data.Remove($"layers/{index}.{extension}", out var source))
                {
                    message = "Preset layer source is missing.";
                    return false;
                }
                layer.DataBase64 = Convert.ToBase64String(source);
            }
            if (data.Count != 0 || !design.TryValidate(out message))
            {
                if (data.Count != 0)
                    message = "Preset contains unexpected assets.";
                return false;
            }
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or EndOfStreamException or JsonException or FormatException or OverflowException)
        {
            message = ex.Message;
            design = new ImagePaintSettings();
            return false;
        }
    }

    private static bool TryNormalizePath(string path, bool requirePresetExtension, out string fullPath, out string message)
    {
        fullPath = "";
        message = "";
        try
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                message = "Preset path is missing.";
                return false;
            }
            fullPath = Path.GetFullPath(path);
            if (requirePresetExtension && !string.Equals(Path.GetExtension(fullPath), PresetExtension, StringComparison.OrdinalIgnoreCase))
            {
                message = "Preset files must use the .mcpreset extension.";
                return false;
            }
            if (Path.GetDirectoryName(fullPath) is not { Length: > 0 })
            {
                message = "Preset directory is invalid.";
                return false;
            }
            return true;
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            message = ex.Message;
            return false;
        }
    }

    private static bool IsEntryNameValid(string name) =>
        name.Length is > 0 and <= 260 &&
        !name.Contains("..", StringComparison.Ordinal) &&
        !Path.IsPathRooted(name) &&
        name.All(ch => char.IsLetterOrDigit(ch) || ch is '.' or '_' or '/' or '-');

    private static ImagePaintSettings Clone(ImagePaintSettings design) =>
        JsonSerializer.Deserialize<ImagePaintSettings>(JsonSerializer.Serialize(design, JsonOptions), JsonOptions) ?? new ImagePaintSettings();

    private sealed class PresetManifest
    {
        public int SchemaVersion { get; set; }
        public ImagePaintSettings Image { get; set; } = new();
    }

    private sealed record PresetEntry(string Name, byte[] Data);
    private sealed record PresetEntryDescriptor(string Name, long Length, byte[] Hash);
}

public sealed record ImagePresetResult(bool Success, string Message, string Path = "");
