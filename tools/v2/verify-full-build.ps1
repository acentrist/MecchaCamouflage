param(
    [Parameter(Mandatory = $true)]
    [string]$BuildRoot,

    [Parameter(Mandatory = $true)]
    [string]$ReportPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$AcceptedUe4ssCommit = "6c26f038751b3d96059d4a9148f5d093012d55ad"
$ResolvedBuildRoot = (Resolve-Path -LiteralPath $BuildRoot).Path

function Get-SingleBinary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $Matches = @(
        Get-ChildItem -LiteralPath $ResolvedBuildRoot -Recurse -File -Filter $Name |
            Where-Object { $_.FullName -notmatch "[\\/]_deps[\\/]" }
    )
    if ($Matches.Count -ne 1) {
        throw "Expected exactly one $Name under $ResolvedBuildRoot; found $($Matches.Count)."
    }
    return $Matches[0]
}

function Invoke-Dumpbin {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Mode,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $Output = & dumpbin $Mode $Path 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin $Mode failed for $Path.`n$($Output -join [Environment]::NewLine)"
    }
    return @($Output)
}

function Assert-X64Pe {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileInfo]$Binary
    )

    $Headers = Invoke-Dumpbin -Mode "/headers" -Path $Binary.FullName
    if (($Headers -join "`n") -notmatch "(?im)^\s*8664 machine \(x64\)\s*$") {
        throw "$($Binary.Name) is not an x64 PE binary."
    }
}

function Get-Dependents {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileInfo]$Binary
    )

    $Output = Invoke-Dumpbin -Mode "/dependents" -Path $Binary.FullName
    return @(
        $Output |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ -match "^[A-Za-z0-9._-]+\.dll$" } |
            ForEach-Object { $_.ToLowerInvariant() } |
            Sort-Object -Unique
    )
}

$Main = Get-SingleBinary -Name "main.dll"
$Ue4ss = Get-SingleBinary -Name "UE4SS.dll"
$Proxy = Get-SingleBinary -Name "dwmapi.dll"
Assert-X64Pe -Binary $Main
Assert-X64Pe -Binary $Ue4ss
Assert-X64Pe -Binary $Proxy

$ExportOutput = Invoke-Dumpbin -Mode "/exports" -Path $Main.FullName
$Exports = @(
    $ExportOutput |
        ForEach-Object {
            if ($_ -match "^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$") {
                $Matches[1]
            }
        } |
        Where-Object { $_ } |
        Sort-Object -Unique
)
$ExpectedExports = @("start_mod", "uninstall_mod")
if (Compare-Object -ReferenceObject $ExpectedExports -DifferenceObject $Exports) {
    throw "main.dll exports '$($Exports -join ", ")'; expected only '$($ExpectedExports -join ", ")'."
}

$MainDependents = Get-Dependents -Binary $Main
$Ue4ssDependents = Get-Dependents -Binary $Ue4ss
$ProxyDependents = Get-Dependents -Binary $Proxy
if ($MainDependents -notcontains "ue4ss.dll") {
    throw "main.dll does not import UE4SS.dll from the same source graph."
}
if ($MainDependents -notcontains "vcruntime140.dll") {
    throw "main.dll is not linked to the expected dynamic MSVC runtime."
}
if ($Ue4ssDependents -notcontains "vcruntime140.dll") {
    throw "UE4SS.dll is not linked to the expected dynamic MSVC runtime."
}
if ($ProxyDependents -notcontains "vcruntime140.dll") {
    throw "dwmapi.dll is not linked to the expected dynamic MSVC runtime."
}

$Ue4ssHead = (
    git -C third_party/RE-UE4SS rev-parse HEAD
).Trim()
if ($LASTEXITCODE -ne 0 -or $Ue4ssHead -ne $AcceptedUe4ssCommit) {
    throw "The built UE4SS checkout is not $AcceptedUe4ssCommit."
}
$Ue4ssStatus = @(
    git -C third_party/RE-UE4SS -c core.filemode=false status --porcelain --untracked-files=no
)
if ($LASTEXITCODE -ne 0 -or $Ue4ssStatus.Count -ne 0) {
    throw "The built UE4SS checkout contains a tracked source modification."
}

$Compiler = (& cl 2>&1 | Select-Object -First 1).ToString().Trim()
$Report = [ordered]@{
    schema_version = 1
    product_version = "2.0.0"
    source_commit = (git rev-parse HEAD).Trim()
    ue4ss_commit = $Ue4ssHead
    configuration = "Game__Shipping__Win64"
    architecture = "x64"
    msvc_runtime = "MultiThreadedDLL"
    compiler = $Compiler
    cmake_version = (cmake --version | Select-Object -First 1).Trim()
    main = [ordered]@{
        path = $Main.FullName.Substring($ResolvedBuildRoot.Length).TrimStart("\", "/")
        size = $Main.Length
        sha256 = (Get-FileHash -LiteralPath $Main.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        exports = $Exports
        dependents = $MainDependents
    }
    ue4ss = [ordered]@{
        path = $Ue4ss.FullName.Substring($ResolvedBuildRoot.Length).TrimStart("\", "/")
        size = $Ue4ss.Length
        sha256 = (Get-FileHash -LiteralPath $Ue4ss.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        dependents = $Ue4ssDependents
    }
    proxy = [ordered]@{
        path = $Proxy.FullName.Substring($ResolvedBuildRoot.Length).TrimStart("\", "/")
        size = $Proxy.Length
        sha256 = (Get-FileHash -LiteralPath $Proxy.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        dependents = $ProxyDependents
    }
}

$ReportDirectory = Split-Path -Parent $ReportPath
if ($ReportDirectory) {
    New-Item -ItemType Directory -Path $ReportDirectory -Force | Out-Null
}
$Report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReportPath -Encoding utf8NoBOM
Write-Host "PASS full-build: main.dll and UE4SS.dll share the pinned x64 Shipping graph."
