[CmdletBinding()]
param(
    [string]$PaksPath = "",
    [string]$MappingsPath = "",
    [string]$Cue4ParsePath = "",
    [string]$OutputPath = "",
    [string]$AssetPath = "Chameleon/Content/3Dmodel/cLeon/charactor/paintman/skeltal/paintman.uasset",
    [string]$ExportName = "paintman",
    [string]$GameVersion = "GAME_UE5_6",
    [string]$OodlePath = "",
    [string]$ZlibPath = "",
    [int]$TextureSize = 1024,
    [int]$ExpectedVertices = 1660,
    [int]$ExpectedIndices = 8352,
    [int]$ExpectedBones = 28
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return [System.IO.Path]::GetFullPath(
        (Resolve-Path -LiteralPath (Join-Path $scriptDir "..")).ProviderPath
    )
}

function Default-PaksPath {
    if ($IsWindows -or [string]::IsNullOrEmpty($IsWindows)) {
        return "C:\Program Files (x86)\Steam\steamapps\common\MECCHA CHAMELEON\Chameleon\Content\Paks"
    }
    return "/mnt/c/Program Files (x86)/Steam/steamapps/common/MECCHA CHAMELEON/Chameleon/Content/Paks"
}

function Require-Path([string]$Path, [string]$Description) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path)) {
        throw "$Description not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).ProviderPath
}

$RepoRoot = Resolve-RepoRoot

if ([string]::IsNullOrWhiteSpace($PaksPath)) {
    $PaksPath = $env:MECCHA_PAKS_PATH
}
if ([string]::IsNullOrWhiteSpace($PaksPath)) {
    $PaksPath = Default-PaksPath
}

if ([string]::IsNullOrWhiteSpace($MappingsPath)) {
    $MappingsPath = $env:MECCHA_MAPPINGS_PATH
}
if ([string]::IsNullOrWhiteSpace($MappingsPath)) {
    throw "MappingsPath is required: this repository intentionally has no generated .usmap. Create Mappings.usmap from the current game build with third_party/UnrealMappingsDumper, then run make mesh MAPPINGS=<path-to-Mappings.usmap> or set MECCHA_MAPPINGS_PATH."
}

if ([string]::IsNullOrWhiteSpace($Cue4ParsePath)) {
    $Cue4ParsePath = $env:MECCHA_CUE4PARSE_ROOT
}
if ([string]::IsNullOrWhiteSpace($Cue4ParsePath)) {
    $Cue4ParsePath = Join-Path $RepoRoot "third_party\CUE4Parse"
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $SafeExportName = if ([string]::IsNullOrWhiteSpace($ExportName)) { "mesh" } else { $ExportName -replace '[^A-Za-z0-9_.-]', '_' }
    $OutputPath = Join-Path $RepoRoot "resources\mesh-profiles\$SafeExportName.mesh-profile-v2.json"
}

$PaksPath = Require-Path $PaksPath "MECCHA CHAMELEON Paks directory"
$MappingsPath = Require-Path $MappingsPath "Unreal mappings file"
$Cue4ParsePath = Require-Path $Cue4ParsePath "CUE4Parse checkout"

$Cue4ParseProject = Join-Path $Cue4ParsePath "CUE4Parse\CUE4Parse.csproj"
$Cue4ParseConversionProject = Join-Path $Cue4ParsePath "CUE4Parse-Conversion\CUE4Parse-Conversion.csproj"
$Cue4ParseProject = Require-Path $Cue4ParseProject "CUE4Parse project"
$Cue4ParseConversionProject = Require-Path $Cue4ParseConversionProject "CUE4Parse conversion project"

$Dotnet = Get-Command dotnet -ErrorAction Stop
$ToolDir = Join-Path $RepoRoot ".build\tools\mesh-profile-tool"
New-Item -ItemType Directory -Force -Path $ToolDir | Out-Null
if ([string]::IsNullOrWhiteSpace($OodlePath)) {
    $OodlePath = Join-Path $ToolDir ($(if ($IsLinux) { "liboodle-data-shared.so" } else { "oodle-data-shared.dll" }))
}
if ([string]::IsNullOrWhiteSpace($ZlibPath)) {
    $ZlibPath = Join-Path $ToolDir ($(if ($IsLinux) { "libz-ng.so" } else { "zlib-ng2.dll" }))
}

function Escape-Xml([string]$Value) {
    return [System.Security.SecurityElement]::Escape($Value)
}

$Csproj = @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <TreatWarningsAsErrors>false</TreatWarningsAsErrors>
    <WarningsAsErrors></WarningsAsErrors>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="$(Escape-Xml $Cue4ParseProject)" />
    <ProjectReference Include="$(Escape-Xml $Cue4ParseConversionProject)" />
  </ItemGroup>
</Project>
"@

$Program = @'
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using CUE4Parse.Compression;
using CUE4Parse.FileProvider;
using CUE4Parse.MappingsProvider.Usmap;
using CUE4Parse.UE4.Assets.Exports.Texture;
using CUE4Parse.UE4.Assets.Exports.SkeletalMesh;
using CUE4Parse.UE4.Versions;
using CUE4Parse_Conversion.Meshes;
using CUE4Parse_Conversion.Meshes.PSK;

internal static class Program
{
    public static int Main(string[] args)
    {
        try
        {
            var options = CliOptions.Parse(args);
            Generate(options);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("mesh-profile generation failed:");
            Console.Error.WriteLine(ex.Message);
            Console.Error.WriteLine(ex);
            return 2;
        }
    }

    private static void Generate(CliOptions options)
    {
        ZlibHelper.Initialize(options.ZlibPath);
        OodleHelper.Initialize(options.OodlePath);

        if (!Enum.TryParse<EGame>(options.GameVersion, ignoreCase: true, out var game))
            throw new InvalidOperationException($"Unknown CUE4Parse game version: {options.GameVersion}");

        var version = new VersionContainer(game, ETexturePlatform.DesktopMobile);
        var provider = new DefaultFileProvider(options.PaksPath, SearchOption.TopDirectoryOnly, version, StringComparer.OrdinalIgnoreCase)
        {
            MappingsContainer = new FileUsmapTypeMappingsProvider(options.MappingsPath, StringComparer.OrdinalIgnoreCase)
        };

        Console.WriteLine($"paks={options.PaksPath}");
        Console.WriteLine($"mappings={options.MappingsPath}");
        Console.WriteLine($"game_version={game}");
        Console.WriteLine($"asset={options.AssetPath}");

        provider.Initialize();
        Console.WriteLine($"registered_archives={provider.UnloadedVfs.Count + provider.MountedVfs.Count}");
        var mounted = provider.Mount();
        Console.WriteLine($"mounted={mounted} files={provider.Files.Count} required_keys={provider.RequiredKeys.Count}");
        if (provider.RequiredKeys.Count > 0)
            throw new InvalidOperationException("Archive encryption keys are required; provide decrypted game content or extend scripts/mesh.ps1 with explicit AES key handling before regenerating profiles.");

        try
        {
            provider.PostMount();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"postmount_warning={ex.GetType().Name}: {ex.Message}");
        }

        var assetPath = NormalizeAssetPath(options.AssetPath);
        var gameFile = provider.Files.Values.FirstOrDefault(file =>
            string.Equals(NormalizeAssetPath(file.Path), assetPath, StringComparison.OrdinalIgnoreCase));
        if (gameFile is null)
            throw new InvalidOperationException($"Asset not found in mounted archives: {assetPath}");

        var package = provider.LoadPackage(gameFile);
        var mesh = package.GetExports()
            .OfType<USkeletalMesh>()
            .FirstOrDefault(export => string.Equals(export.Name, options.ExportName, StringComparison.OrdinalIgnoreCase))
            ?? package.GetExports().OfType<USkeletalMesh>().SingleOrDefault()
            ?? throw new InvalidOperationException($"Skeletal mesh export not found: {options.ExportName}");

        if (!mesh.TryConvert(out var converted))
            throw new InvalidOperationException("CUE4Parse could not convert the skeletal mesh.");
        if (converted.LODs.Count <= 0)
            throw new InvalidOperationException("Converted skeletal mesh has no LODs.");

        var lod = converted.LODs[0];
        if (lod.Verts is null)
            throw new InvalidOperationException("Converted LOD0 has no vertices.");
        if (lod.Indices is null)
            throw new InvalidOperationException("Converted LOD0 has no indices.");
        if (lod.Sections is null)
            throw new InvalidOperationException("Converted LOD0 has no sections.");

        var indices = lod.Indices.Value.Select(i => checked((int)i)).ToArray();
        if (indices.Length % 3 != 0)
            throw new InvalidOperationException($"Index count is not divisible by 3: {indices.Length}");

        ValidateCount("VertexCount", lod.Verts.Length, options.ExpectedVertices);
        ValidateCount("IndexCount", indices.Length, options.ExpectedIndices);
        ValidateCount("BoneCount", converted.RefSkeleton.Count, options.ExpectedBones);

        var bones = converted.RefSkeleton.Select((bone, index) => new BoneDto
        {
            Index = index,
            Name = bone.Name.Text,
            ParentIndex = bone.ParentIndex,
            X = bone.Position.X,
            Y = bone.Position.Y,
            Z = bone.Position.Z,
            RotationX = bone.Orientation.X,
            RotationY = bone.Orientation.Y,
            RotationZ = bone.Orientation.Z,
            RotationW = bone.Orientation.W
        }).ToList();

        var vertices = lod.Verts.Select(vertex => new VertexDto
        {
            X = vertex.Position.X,
            Y = vertex.Position.Y,
            Z = vertex.Position.Z,
            NormalX = vertex.Normal.X,
            NormalY = vertex.Normal.Y,
            NormalZ = vertex.Normal.Z,
            U = vertex.UV.U,
            V = vertex.UV.V,
            Influences = vertex.Influences.Select(influence => new InfluenceDto
            {
                Bone = influence.Bone,
                RawWeight = influence.RawWeight,
                Weight = influence.Weight
            }).ToList()
        }).ToList();

        var extraUvChannels = new List<List<UvDto>>();
        if (lod.NumTexCoords > 1)
        {
            var extra = lod.ExtraUV?.Value ?? [];
            for (var channel = 0; channel < Math.Min(lod.NumTexCoords - 1, extra.Length); channel++)
            {
                extraUvChannels.Add(extra[channel].Select(uv => new UvDto { U = uv.U, V = uv.V }).ToList());
            }
        }

        var uvIslands = BuildUvIslandIds(indices, vertices.Count);
        var triangles = BuildTriangles(indices, vertices, bones, uvIslands);
        var profileHash = HashProfile(options.TextureSize, assetPath, mesh.Name, bones, indices, vertices, triangles);
        var profile = new ProfileDto
        {
            ProfileSchemaVersion = 2,
            SchemaVersion = 2,
            ProfileId = $"{mesh.Name}:{assetPath}:lod0:v{vertices.Count}:i{indices.Length}:b{bones.Count}:{profileHash}",
            ProfileHash = profileHash,
            SourcePath = assetPath,
            Export = mesh.Name,
            TextureSize = options.TextureSize,
            VertexCount = vertices.Count,
            IndexCount = indices.Length,
            TriangleCount = triangles.Count,
            UvIslandCount = triangles.Select(triangle => triangle.UvIsland).Distinct().Count(),
            Bones = bones,
            Lod0 = new LodDto
            {
                LodIndex = 0,
                NumTexCoords = lod.NumTexCoords,
                VertexCount = vertices.Count,
                IndexCount = indices.Length,
                SectionCount = lod.Sections.Value.Length,
                Indices = indices.ToList(),
                Vertices = vertices,
                ExtraUvChannels = extraUvChannels
            },
            Triangles = triangles
        };

        var outputDir = Path.GetDirectoryName(options.OutputPath);
        if (!string.IsNullOrWhiteSpace(outputDir))
            Directory.CreateDirectory(outputDir);

        var jsonOptions = new JsonSerializerOptions { WriteIndented = true };
        var json = JsonSerializer.Serialize(profile, jsonOptions) + Environment.NewLine;
        File.WriteAllText(options.OutputPath, json, new UTF8Encoding(encoderShouldEmitUTF8Identifier: true));

        Console.WriteLine($"wrote={options.OutputPath}");
        Console.WriteLine($"profile_id={profile.ProfileId}");
        Console.WriteLine($"vertices={profile.VertexCount} indices={profile.IndexCount} triangles={profile.TriangleCount} bones={profile.Bones.Count} uv_islands={profile.UvIslandCount}");
    }

    private static void ValidateCount(string name, int actual, int expected)
    {
        if (expected > 0 && actual != expected)
            throw new InvalidOperationException($"{name} mismatch: expected {expected}, got {actual}");
    }

    private static string NormalizeAssetPath(string path)
    {
        var normalized = path.Replace('\\', '/').Trim();
        while (normalized.StartsWith("/", StringComparison.Ordinal))
            normalized = normalized[1..];
        if (normalized.StartsWith("Game/", StringComparison.OrdinalIgnoreCase))
            normalized = "Chameleon/Content/" + normalized["Game/".Length..];
        if (normalized.StartsWith("/Game/", StringComparison.OrdinalIgnoreCase))
            normalized = "Chameleon/Content/" + normalized["/Game/".Length..];
        return normalized;
    }

    private static List<int> BuildUvIslandIds(int[] indices, int vertexCount)
    {
        var triangleCount = indices.Length / 3;
        var parent = Enumerable.Range(0, triangleCount).ToArray();
        var firstTriangleByVertex = new Dictionary<int, int>();

        int Find(int value)
        {
            while (parent[value] != value)
            {
                parent[value] = parent[parent[value]];
                value = parent[value];
            }
            return value;
        }

        void Union(int left, int right)
        {
            var l = Find(left);
            var r = Find(right);
            if (l == r)
                return;
            if (l < r)
                parent[r] = l;
            else
                parent[l] = r;
        }

        for (var tri = 0; tri < triangleCount; tri++)
        {
            for (var slot = 0; slot < 3; slot++)
            {
                var vertex = indices[tri * 3 + slot];
                if (vertex < 0 || vertex >= vertexCount)
                    throw new InvalidOperationException($"Triangle {tri} references invalid vertex {vertex}");
                if (firstTriangleByVertex.TryGetValue(vertex, out var owner))
                    Union(tri, owner);
                else
                    firstTriangleByVertex.Add(vertex, tri);
            }
        }

        var roots = Enumerable.Range(0, triangleCount).Select(Find).Distinct().OrderBy(root => root).ToList();
        var islandByRoot = roots.Select((root, island) => new { root, island }).ToDictionary(item => item.root, item => item.island);
        return Enumerable.Range(0, triangleCount).Select(tri => islandByRoot[Find(tri)]).ToList();
    }

    private static List<TriangleDto> BuildTriangles(IReadOnlyList<int> indices, IReadOnlyList<VertexDto> vertices, IReadOnlyList<BoneDto> bones, IReadOnlyList<int> uvIslands)
    {
        var triangles = new List<TriangleDto>(indices.Count / 3);
        for (var tri = 0; tri < indices.Count / 3; tri++)
        {
            var i0 = indices[tri * 3 + 0];
            var i1 = indices[tri * 3 + 1];
            var i2 = indices[tri * 3 + 2];
            var v0 = vertices[i0];
            var v1 = vertices[i1];
            var v2 = vertices[i2];
            var normal = TriangleNormal(v0, v1, v2);
            var dominantBone = DominantBone(v0, v1, v2);
            triangles.Add(new TriangleDto
            {
                Index = tri,
                I0 = i0,
                I1 = i1,
                I2 = i2,
                UvIsland = uvIslands[tri],
                BodyRegion = BodyRegion(dominantBone, bones),
                DominantBone = dominantBone,
                LocalNormalX = normal.x,
                LocalNormalY = normal.y,
                LocalNormalZ = normal.z,
                UvArea = UvArea(v0, v1, v2)
            });
        }
        return triangles;
    }

    private static (double x, double y, double z) TriangleNormal(VertexDto v0, VertexDto v1, VertexDto v2)
    {
        var ax = v1.X - v0.X;
        var ay = v1.Y - v0.Y;
        var az = v1.Z - v0.Z;
        var bx = v2.X - v0.X;
        var by = v2.Y - v0.Y;
        var bz = v2.Z - v0.Z;
        var x = ay * bz - az * by;
        var y = az * bx - ax * bz;
        var z = ax * by - ay * bx;
        var len = Math.Sqrt(x * x + y * y + z * z);
        if (len <= double.Epsilon)
            return (0.0, 0.0, 0.0);
        return (x / len, y / len, z / len);
    }

    private static double UvArea(VertexDto v0, VertexDto v1, VertexDto v2)
    {
        return Math.Abs((v1.U - v0.U) * (v2.V - v0.V) - (v2.U - v0.U) * (v1.V - v0.V)) * 0.5;
    }

    private static int DominantBone(params VertexDto[] vertices)
    {
        var weights = new Dictionary<int, (double weight, int raw)>();
        foreach (var vertex in vertices)
        {
            foreach (var influence in vertex.Influences)
            {
                if (!weights.TryGetValue(influence.Bone, out var current))
                    current = (0.0, 0);
                weights[influence.Bone] = (current.weight + influence.Weight, current.raw + influence.RawWeight);
            }
        }
        if (weights.Count == 0)
            return -1;
        return weights.OrderByDescending(item => item.Value.weight)
            .ThenByDescending(item => item.Value.raw)
            .ThenBy(item => item.Key)
            .First().Key;
    }

    private static string BodyRegion(int boneIndex, IReadOnlyList<BoneDto> bones)
    {
        if (boneIndex < 0 || boneIndex >= bones.Count)
            return "unknown";
        var name = bones[boneIndex].Name.ToLowerInvariant();
        if (name.Contains("head", StringComparison.Ordinal) || name.Contains("neck", StringComparison.Ordinal))
            return "head";
        if (name.Contains("shoulder", StringComparison.Ordinal) || name.Contains("arm", StringComparison.Ordinal) || name.Contains("hand", StringComparison.Ordinal))
            return "arm";
        if (name.Contains("hip", StringComparison.Ordinal) || name.Contains("leg", StringComparison.Ordinal) || name.Contains("foot", StringComparison.Ordinal))
            return "leg";
        return "torso";
    }

    private static string HashProfile(
        int textureSize,
        string assetPath,
        string exportName,
        IReadOnlyList<BoneDto> bones,
        IReadOnlyList<int> indices,
        IReadOnlyList<VertexDto> vertices,
        IReadOnlyList<TriangleDto> triangles)
    {
        using var sha = SHA256.Create();
        void AddString(string value) => AddBytes(Encoding.UTF8.GetBytes(value));
        void AddInt(int value) => AddString(value.ToString(CultureInfo.InvariantCulture));
        void AddDouble(double value) => AddString(value.ToString("R", CultureInfo.InvariantCulture));
        void AddBytes(byte[] bytes)
        {
            sha.TransformBlock(bytes, 0, bytes.Length, null, 0);
            sha.TransformBlock([0], 0, 1, null, 0);
        }

        AddInt(textureSize);
        AddString(assetPath);
        AddString(exportName);
        foreach (var bone in bones)
        {
            AddInt(bone.Index);
            AddString(bone.Name);
            AddInt(bone.ParentIndex);
            AddDouble(bone.X);
            AddDouble(bone.Y);
            AddDouble(bone.Z);
            AddDouble(bone.RotationX);
            AddDouble(bone.RotationY);
            AddDouble(bone.RotationZ);
            AddDouble(bone.RotationW);
        }
        foreach (var index in indices)
            AddInt(index);
        foreach (var vertex in vertices)
        {
            AddDouble(vertex.X);
            AddDouble(vertex.Y);
            AddDouble(vertex.Z);
            AddDouble(vertex.NormalX);
            AddDouble(vertex.NormalY);
            AddDouble(vertex.NormalZ);
            AddDouble(vertex.U);
            AddDouble(vertex.V);
            foreach (var influence in vertex.Influences)
            {
                AddInt(influence.Bone);
                AddInt(influence.RawWeight);
                AddDouble(influence.Weight);
            }
        }
        foreach (var triangle in triangles)
        {
            AddInt(triangle.Index);
            AddInt(triangle.I0);
            AddInt(triangle.I1);
            AddInt(triangle.I2);
            AddInt(triangle.UvIsland);
            AddString(triangle.BodyRegion);
            AddInt(triangle.DominantBone);
            AddDouble(triangle.LocalNormalX);
            AddDouble(triangle.LocalNormalY);
            AddDouble(triangle.LocalNormalZ);
            AddDouble(triangle.UvArea);
        }
        sha.TransformFinalBlock([], 0, 0);
        return Convert.ToHexString(sha.Hash!).ToLowerInvariant();
    }
}

internal sealed class CliOptions
{
    public required string PaksPath { get; init; }
    public required string MappingsPath { get; init; }
    public required string OutputPath { get; init; }
    public required string AssetPath { get; init; }
    public required string ExportName { get; init; }
    public required string GameVersion { get; init; }
    public required string OodlePath { get; init; }
    public required string ZlibPath { get; init; }
    public required int TextureSize { get; init; }
    public required int ExpectedVertices { get; init; }
    public required int ExpectedIndices { get; init; }
    public required int ExpectedBones { get; init; }

    public static CliOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException($"Unexpected argument: {key}");
            if (i + 1 >= args.Length)
                throw new ArgumentException($"Missing value for argument: {key}");
            values[key[2..]] = args[++i];
        }

        string Required(string key) =>
            values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
                ? value
                : throw new ArgumentException($"Missing required argument --{key}");
        int IntValue(string key) => int.Parse(Required(key), CultureInfo.InvariantCulture);

        return new CliOptions
        {
            PaksPath = Required("paks"),
            MappingsPath = Required("mappings"),
            OutputPath = Required("output"),
            AssetPath = Required("asset"),
            ExportName = Required("export"),
            GameVersion = Required("game-version"),
            OodlePath = Required("oodle"),
            ZlibPath = Required("zlib"),
            TextureSize = IntValue("texture-size"),
            ExpectedVertices = IntValue("expected-vertices"),
            ExpectedIndices = IntValue("expected-indices"),
            ExpectedBones = IntValue("expected-bones")
        };
    }
}

internal sealed class ProfileDto
{
    public int ProfileSchemaVersion { get; init; }
    public int SchemaVersion { get; init; }
    public required string ProfileId { get; init; }
    public required string ProfileHash { get; init; }
    public required string SourcePath { get; init; }
    public required string Export { get; init; }
    public int TextureSize { get; init; }
    public int VertexCount { get; init; }
    public int IndexCount { get; init; }
    public int TriangleCount { get; init; }
    public int UvIslandCount { get; init; }
    public required List<BoneDto> Bones { get; init; }
    public required LodDto Lod0 { get; init; }
    public required List<TriangleDto> Triangles { get; init; }
}

internal sealed class BoneDto
{
    public int Index { get; init; }
    public required string Name { get; init; }
    public int ParentIndex { get; init; }
    public double X { get; init; }
    public double Y { get; init; }
    public double Z { get; init; }
    public double RotationX { get; init; }
    public double RotationY { get; init; }
    public double RotationZ { get; init; }
    public double RotationW { get; init; }
}

internal sealed class LodDto
{
    public int LodIndex { get; init; }
    public int NumTexCoords { get; init; }
    public int VertexCount { get; init; }
    public int IndexCount { get; init; }
    public int SectionCount { get; init; }
    public required List<int> Indices { get; init; }
    public required List<VertexDto> Vertices { get; init; }
    public required List<List<UvDto>> ExtraUvChannels { get; init; }
}

internal sealed class VertexDto
{
    public double X { get; init; }
    public double Y { get; init; }
    public double Z { get; init; }
    public double NormalX { get; init; }
    public double NormalY { get; init; }
    public double NormalZ { get; init; }
    public double U { get; init; }
    public double V { get; init; }
    public required List<InfluenceDto> Influences { get; init; }
}

internal sealed class InfluenceDto
{
    public int Bone { get; init; }
    public int RawWeight { get; init; }
    public double Weight { get; init; }
}

internal sealed class UvDto
{
    public double U { get; init; }
    public double V { get; init; }
}

internal sealed class TriangleDto
{
    public int Index { get; init; }
    public int I0 { get; init; }
    public int I1 { get; init; }
    public int I2 { get; init; }
    public int UvIsland { get; init; }
    public required string BodyRegion { get; init; }
    public int DominantBone { get; init; }
    public double LocalNormalX { get; init; }
    public double LocalNormalY { get; init; }
    public double LocalNormalZ { get; init; }
    public double UvArea { get; init; }
}
'@

$CsprojPath = Join-Path $ToolDir "MecchaMeshProfileTool.csproj"
$ProgramPath = Join-Path $ToolDir "Program.cs"
[System.IO.File]::WriteAllText($CsprojPath, $Csproj, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText($ProgramPath, $Program, [System.Text.UTF8Encoding]::new($false))

$OutputFullPath = if ([System.IO.Path]::IsPathRooted($OutputPath)) { $OutputPath } else { Join-Path $RepoRoot $OutputPath }

& $Dotnet.Source run --project $CsprojPath --configuration Release -- `
    --paks $PaksPath `
    --mappings $MappingsPath `
    --output $OutputFullPath `
    --asset $AssetPath `
    --export $ExportName `
    --game-version $GameVersion `
    --oodle $OodlePath `
    --zlib $ZlibPath `
    --texture-size $TextureSize `
    --expected-vertices $ExpectedVertices `
    --expected-indices $ExpectedIndices `
    --expected-bones $ExpectedBones

if ($LASTEXITCODE -ne 0) {
    throw "mesh profile generator failed with exit code $LASTEXITCODE"
}
