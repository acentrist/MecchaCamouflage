using System.Text.Json;
using System.Security.Cryptography;

namespace MecchaCamouflage.Core;

/// <summary>
/// Version-scoped, file-backed Image Paint designs.  Manifests stay readable
/// while image sources and the canonical raster remain binary assets.
/// </summary>
public sealed class ImageDesignLibrary
{
    private const int SchemaVersion = 1;
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower
    };

    private readonly AppPaths paths;

    public ImageDesignLibrary(AppPaths paths)
    {
        this.paths = paths;
    }

    public IReadOnlyList<ImageDesignSummary> List()
    {
        var index = ReadIndex();
        var valid = new List<ImageDesignSummary>();
        var changed = false;
        foreach (var entry in index.Designs)
        {
            if (!TryReadManifest(entry.Id, out var manifest, out _))
            {
                changed = true;
                continue;
            }
            valid.Add(ToSummary(manifest));
        }
        if (changed)
            WriteIndex(new ImageDesignIndex { Designs = valid.Select(summary => new ImageDesignIndexEntry(summary.Id, summary.Name)).ToList() });
        return valid;
    }

    public ImageDesignSaveResult Save(string? requestedId, string? requestedName, ImagePaintSettings design)
    {
        if (design is null)
            return new ImageDesignSaveResult(false, "The image design is missing.");
        if (!design.TryValidate(out var validationMessage))
            return new ImageDesignSaveResult(false, validationMessage);

        var id = NormalizeId(requestedId) ?? Guid.NewGuid().ToString("N");
        var name = NormalizeName(requestedName);
        if (name.Length == 0)
            return new ImageDesignSaveResult(false, "Image design names are required.");

        var hadPrevious = TryReadManifest(id, out var previous, out _);
        var revision = Math.Max(1, hadPrevious ? previous.Design.Revision + 1 : design.Revision);
        var writable = Clone(design);
        writable.Revision = revision;
        foreach (var layer in writable.Layers)
            layer.AssetId = NormalizeId(layer.AssetId) ?? Guid.NewGuid().ToString("N");

        var staging = Path.Combine(paths.ImageDesignsDirectory, ".staging-" + Guid.NewGuid().ToString("N"));
        var target = Path.Combine(paths.ImageDesignsDirectory, id);
        var backup = target + ".backup-" + Guid.NewGuid().ToString("N");
        var targetInstalled = false;
        try
        {
            Directory.CreateDirectory(Path.Combine(staging, "assets"));
            var canvasBytes = Convert.FromBase64String(writable.CanvasRgbaBase64);
            File.WriteAllBytes(Path.Combine(staging, "canvas.rgba"), canvasBytes);
            foreach (var layer in writable.Layers)
            {
                var extension = layer.MimeType == "image/jpeg" ? ".jpg" : ".png";
                File.WriteAllBytes(Path.Combine(staging, "assets", layer.AssetId + extension), Convert.FromBase64String(layer.DataBase64));
                layer.DataBase64 = "";
            }
            writable.CanvasRgbaBase64 = "";
            var manifest = new ImageDesignManifest
            {
                SchemaVersion = SchemaVersion,
                Id = id,
                Name = name,
                Design = writable,
                Canvas = new ImageDesignCanvasMetadata
                {
                    Width = ImagePaintSettings.CanvasWidth,
                    Height = ImagePaintSettings.CanvasHeight,
                    ByteLength = canvasBytes.Length,
                    Sha256 = Convert.ToHexString(SHA256.HashData(canvasBytes)).ToLowerInvariant()
                }
            };
            File.WriteAllText(Path.Combine(staging, "design.json"), JsonSerializer.Serialize(manifest, JsonOptions) + Environment.NewLine);

            Directory.CreateDirectory(paths.ImageDesignsDirectory);
            if (Directory.Exists(target))
                Directory.Move(target, backup);
            Directory.Move(staging, target);
            targetInstalled = true;

            var index = ReadIndex();
            index.Designs.RemoveAll(entry => entry.Id == id);
            index.Designs.Add(new ImageDesignIndexEntry(id, name));
            WriteIndex(index);
            if (Directory.Exists(backup))
            {
                try
                {
                    Directory.Delete(backup, recursive: true);
                }
                catch (IOException)
                {
                    // The new target and index are already published. A stale
                    // backup is harmless and must not roll back that commit.
                }
                catch (UnauthorizedAccessException)
                {
                    // See the IOException case above.
                }
            }
            design.Revision = revision;
            foreach (var (source, destination) in design.Layers.Zip(writable.Layers))
                source.AssetId = destination.AssetId;
            return new ImageDesignSaveResult(true, "", ToSummary(manifest));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or FormatException or JsonException)
        {
            try
            {
                // The library index is published only after the staged design has
                // replaced its target.  If that final publication fails, restore
                // the prior directory (or remove a newly-created one) so an
                // index entry never points at a partial replacement.
                if (targetInstalled && Directory.Exists(target))
                    Directory.Delete(target, recursive: true);
                if (!Directory.Exists(target) && Directory.Exists(backup))
                    Directory.Move(backup, target);
            }
            catch
            {
                // The original error is more useful than a best-effort restore failure.
            }
            return new ImageDesignSaveResult(false, ex.Message);
        }
        finally
        {
            if (Directory.Exists(staging))
                Directory.Delete(staging, recursive: true);
            if (Directory.Exists(backup) && !targetInstalled)
                Directory.Move(backup, target);
        }
    }

    public bool TryLoad(string? requestedId, out ImagePaintSettings design, out string message)
    {
        design = new ImagePaintSettings();
        message = "";
        var id = NormalizeId(requestedId);
        if (id is null || !TryReadManifest(id, out var manifest, out message))
            return false;

        try
        {
            design = Clone(manifest.Design);
            design.CanvasRgbaBase64 = Convert.ToBase64String(File.ReadAllBytes(Path.Combine(paths.ImageDesignsDirectory, id, "canvas.rgba")));
            foreach (var layer in design.Layers)
            {
                var assetPath = AssetPath(id, layer);
                layer.DataBase64 = Convert.ToBase64String(File.ReadAllBytes(assetPath));
            }
            if (!design.TryValidate(out message))
                return false;
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or FormatException)
        {
            message = ex.Message;
            design = new ImagePaintSettings();
            return false;
        }
    }

    public bool TryLoadMetadata(string? requestedId, out ImageDesignSummary summary, out ImagePaintSettings design, out string message)
    {
        summary = default!;
        design = new ImagePaintSettings();
        message = "";
        var id = NormalizeId(requestedId);
        if (id is null || !TryReadManifest(id, out var manifest, out message))
            return false;
        summary = ToSummary(manifest);
        design = Clone(manifest.Design);
        return true;
    }

    public bool TryGetAssetBase64(string? requestedId, string? asset, out string data, out string message)
    {
        data = "";
        message = "";
        var id = NormalizeId(requestedId);
        if (id is null || !TryReadManifest(id, out var manifest, out message))
            return false;
        try
        {
            string path;
            if (string.Equals(asset, "canvas", StringComparison.Ordinal))
            {
                path = Path.Combine(paths.ImageDesignsDirectory, id, "canvas.rgba");
            }
            else if (asset is not null && asset.StartsWith("layer", StringComparison.Ordinal) &&
                     int.TryParse(asset["layer".Length..], out var index) && index >= 0 && index < manifest.Design.Layers.Count)
            {
                path = AssetPath(id, manifest.Design.Layers[index]);
            }
            else
            {
                message = "The requested image asset is invalid.";
                return false;
            }
            data = Convert.ToBase64String(File.ReadAllBytes(path));
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            message = ex.Message;
            return false;
        }
    }

    public bool Delete(string? requestedId, out string message)
    {
        message = "";
        var id = NormalizeId(requestedId);
        if (id is null || !TryReadManifest(id, out _, out message))
            return false;
        try
        {
            Directory.Delete(Path.Combine(paths.ImageDesignsDirectory, id), recursive: true);
            var index = ReadIndex();
            index.Designs.RemoveAll(entry => entry.Id == id);
            WriteIndex(index);
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            message = ex.Message;
            return false;
        }
    }

    private bool TryReadManifest(string id, out ImageDesignManifest manifest, out string message)
    {
        manifest = new ImageDesignManifest();
        message = "";
        try
        {
            var path = Path.Combine(paths.ImageDesignsDirectory, id, "design.json");
            if (!File.Exists(path))
            {
                message = "The image design does not exist.";
                return false;
            }
            manifest = JsonSerializer.Deserialize<ImageDesignManifest>(File.ReadAllText(path), JsonOptions) ?? new ImageDesignManifest();
            if (manifest.SchemaVersion != SchemaVersion || manifest.Id != id || string.IsNullOrWhiteSpace(manifest.Name) || manifest.Design is null || manifest.Canvas is null)
            {
                message = "The image design manifest is invalid.";
                return false;
            }
            if (manifest.Design.Layers.Count < 1 || manifest.Design.Layers.Any(layer => NormalizeId(layer.AssetId) is null))
            {
                message = "The image design manifest has invalid layers.";
                return false;
            }
            if (manifest.Design.BodyType is not ("round" or "cube") ||
                manifest.Design.AlphaMode is not ("skip" or "background") ||
                manifest.Design.CanvasEncodingVersion is not (0 or ImagePaintSettings.BackgroundPbrCanvasEncodingVersion) ||
                manifest.Design.Revision < 1)
            {
                message = "The image design manifest has invalid settings.";
                return false;
            }

            var directory = Path.Combine(paths.ImageDesignsDirectory, id);
            var canvas = Path.Combine(directory, "canvas.rgba");
            if (!File.Exists(canvas) ||
                manifest.Canvas.Width != ImagePaintSettings.CanvasWidth ||
                manifest.Canvas.Height != ImagePaintSettings.CanvasHeight ||
                manifest.Canvas.ByteLength != ImagePaintSettings.CanvasByteLength ||
                new FileInfo(canvas).Length != manifest.Canvas.ByteLength)
            {
                message = "The image design canvas is missing or invalid.";
                return false;
            }
            var canvasHash = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(canvas))).ToLowerInvariant();
            if (!string.Equals(canvasHash, manifest.Canvas.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                message = "The image design canvas hash does not match its manifest.";
                return false;
            }
            long sourceBytes = 0;
            foreach (var layer in manifest.Design.Layers)
            {
                if (layer.MimeType is not ("image/png" or "image/jpeg") ||
                    string.IsNullOrWhiteSpace(layer.FileName) ||
                    layer.FileName.Length > 260 ||
                    !double.IsFinite(layer.CenterX) || !double.IsFinite(layer.CenterY) ||
                    !double.IsFinite(layer.Width) || !double.IsFinite(layer.Height) ||
                    !double.IsFinite(layer.CropX) || !double.IsFinite(layer.CropY) ||
                    !double.IsFinite(layer.CropWidth) || !double.IsFinite(layer.CropHeight) ||
                    layer.Width <= 0.0 || layer.Height <= 0.0 || layer.CropWidth <= 0.0 || layer.CropHeight <= 0.0)
                {
                    message = "The image design manifest has invalid layer metadata.";
                    return false;
                }
                var asset = AssetPath(id, layer);
                if (!File.Exists(asset))
                {
                    message = "The image design source asset is missing.";
                    return false;
                }
                var length = new FileInfo(asset).Length;
                if (length is < 1 or > ImagePaintLayer.MaximumSourceBytes)
                {
                    message = "The image design source asset is too large.";
                    return false;
                }
                sourceBytes += length;
                if (sourceBytes > ImagePaintSettings.MaximumTotalSourceBytes)
                {
                    message = "The image design source assets exceed 64 MiB.";
                    return false;
                }
            }
            return true;
        }
        catch (Exception ex) when (ex is IOException or JsonException)
        {
            message = ex.Message;
            return false;
        }
    }

    private ImageDesignIndex ReadIndex()
    {
        try
        {
            var path = Path.Combine(paths.ImageDesignsDirectory, "index.json");
            return File.Exists(path)
                ? JsonSerializer.Deserialize<ImageDesignIndex>(File.ReadAllText(path), JsonOptions) ?? new ImageDesignIndex()
                : new ImageDesignIndex();
        }
        catch (Exception)
        {
            return new ImageDesignIndex();
        }
    }

    private void WriteIndex(ImageDesignIndex index)
    {
        Directory.CreateDirectory(paths.ImageDesignsDirectory);
        var path = Path.Combine(paths.ImageDesignsDirectory, "index.json");
        var temporary = path + ".tmp-" + Guid.NewGuid().ToString("N");
        File.WriteAllText(temporary, JsonSerializer.Serialize(index, JsonOptions) + Environment.NewLine);
        File.Move(temporary, path, overwrite: true);
    }

    private string AssetPath(string id, ImagePaintLayer layer)
    {
        var extension = layer.MimeType == "image/jpeg" ? ".jpg" : ".png";
        return Path.Combine(paths.ImageDesignsDirectory, id, "assets", layer.AssetId + extension);
    }

    private static ImageDesignSummary ToSummary(ImageDesignManifest manifest) =>
        new(manifest.Id, manifest.Name, manifest.Design.Revision, manifest.Design.BodyType, manifest.Design.Layers.Count, manifest.Design.Enabled);

    private static ImagePaintSettings Clone(ImagePaintSettings design) =>
        JsonSerializer.Deserialize<ImagePaintSettings>(JsonSerializer.Serialize(design, JsonOptions), JsonOptions) ?? new ImagePaintSettings();

    private static string? NormalizeId(string? value) =>
        Guid.TryParse(value, out var id) ? id.ToString("N") : null;

    private static string NormalizeName(string? value)
    {
        var name = (value ?? "").Trim();
        return name.Length > 80 ? name[..80] : name;
    }

    private sealed class ImageDesignManifest
    {
        public int SchemaVersion { get; set; }
        public string Id { get; set; } = "";
        public string Name { get; set; } = "";
        public ImagePaintSettings Design { get; set; } = new();
        public ImageDesignCanvasMetadata Canvas { get; set; } = new();
    }

    private sealed class ImageDesignCanvasMetadata
    {
        public int Width { get; set; }
        public int Height { get; set; }
        public int ByteLength { get; set; }
        public string Sha256 { get; set; } = "";
    }

    private sealed class ImageDesignIndex
    {
        public List<ImageDesignIndexEntry> Designs { get; set; } = [];
    }

    private sealed record ImageDesignIndexEntry(string Id, string Name);
}

public sealed record ImageDesignSummary(string Id, string Name, int Revision, string BodyType, int LayerCount, bool Enabled);

public sealed record ImageDesignSaveResult(bool Success, string Message, ImageDesignSummary? Design = null);
