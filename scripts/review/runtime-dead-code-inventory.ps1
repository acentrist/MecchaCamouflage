param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path,
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

function Require-Tool {
    param([Parameter(Mandatory = $true)][string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required tool not found: $Name"
    }
}

function Has-Tool {
    param([Parameter(Mandatory = $true)][string]$Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Test-WslTool {
    param([Parameter(Mandatory = $true)][string]$Name)
    if (-not (Get-Command "wsl.exe" -ErrorAction SilentlyContinue)) {
        return $false
    }
    & wsl.exe sh -lc "command -v $Name >/dev/null 2>&1"
    return $LASTEXITCODE -eq 0
}

function Resolve-RgMode {
    if (Has-Tool "rg") {
        return "native"
    }
    if (Test-WslTool "rg") {
        return "wsl"
    }
    throw "Required tool not found: rg"
}

function Convert-ToWslPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ($Path -match '^([A-Za-z]):\\(.*)$') {
        $drive = $matches[1].ToLowerInvariant()
        $rest = $matches[2] -replace "\\", "/"
        return "/mnt/$drive/$rest"
    }
    $converted = & wsl.exe wslpath -a $Path
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($converted)) {
        throw "wslpath failed for: $Path"
    }
    return $converted.Trim()
}

function Write-Utf8Lines {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [AllowEmptyCollection()][string[]]$Lines
    )
    $parent = Split-Path -Parent $Path
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    if ($Lines.Count -eq 0) {
        $Lines = @("no matches")
    }
    Set-Content -LiteralPath $Path -Value $Lines -Encoding UTF8
}

function To-RepoRelative {
    param([Parameter(Mandatory = $true)][string]$Path)
    $full = [System.IO.Path]::GetFullPath($Path)
    if ($full.StartsWith($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($RepoRoot.Length).TrimStart([char[]]@("\", "/")) -replace "\\", "/"
    }
    return $full
}

function Invoke-RgReport {
    param(
        [Parameter(Mandatory = $true)][string]$OutputFile,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string[]]$Paths
    )
    $args = @(
        "-n",
        "--hidden",
        "--glob", "!**/bin/**",
        "--glob", "!**/obj/**",
        "--glob", "!**/node_modules/**",
        "--glob", "!third_party/**",
        $Pattern
    )
    $args += $Paths
    if ($script:RgMode -eq "wsl") {
        $wslRoot = Convert-ToWslPath $RepoRoot
        $wslArgs = @("--exec", "rg")
        $wslArgs += $args[0..($args.Count - $Paths.Count - 1)]
        foreach ($path in $Paths) {
            $wslArgs += ($wslRoot.TrimEnd("/") + "/" + ($path -replace "\\", "/"))
        }
        $output = @(& wsl.exe @wslArgs 2>&1)
        $exitCode = $LASTEXITCODE
    }
    else {
        Push-Location $RepoRoot
        try {
            $output = @(& rg @args 2>&1)
            $exitCode = $LASTEXITCODE
        }
        finally {
            Pop-Location
        }
    }
    if ($exitCode -ne 0 -and $exitCode -ne 1) {
        $output = @("rg failed with exit code $exitCode") + $output
    }
    Write-Utf8Lines -Path (Join-Path $OutDir $OutputFile) -Lines @($output | ForEach-Object { $_.ToString() })
}

function Invoke-OptionalReport {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$OutputFile,
        [Parameter(Mandatory = $true)][scriptblock]$Command
    )
    $started = Get-Date -Format "o"
    $lines = @("tool: $Name", "started: $started", "")
    try {
        Push-Location $RepoRoot
        try {
            $oldErrorActionPreference = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            $output = @(& $Command 2>&1)
            $exitCode = $LASTEXITCODE
            if ($null -eq $exitCode) {
                $exitCode = 0
            }
        }
        finally {
            $ErrorActionPreference = $oldErrorActionPreference
            Pop-Location
        }
        $lines += "exit_code: $exitCode"
        $lines += ""
        $lines += @($output | ForEach-Object { $_.ToString() })
    }
    catch {
        $lines += "exit_code: exception"
        $lines += ""
        $lines += $_.Exception.ToString()
    }
    Write-Utf8Lines -Path (Join-Path $OutDir $OutputFile) -Lines $lines
}

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $RepoRoot "artifacts/review/runtime-dead-code"
}
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Require-Tool git
$script:RgMode = Resolve-RgMode

Push-Location $RepoRoot
try {
    Write-Utf8Lines -Path (Join-Path $OutDir "commit.txt") -Lines @((& git rev-parse HEAD 2>&1) | ForEach-Object { $_.ToString() })
    Write-Utf8Lines -Path (Join-Path $OutDir "status.txt") -Lines @((& git status --short 2>&1) | ForEach-Object { $_.ToString() })
    Write-Utf8Lines -Path (Join-Path $OutDir "source-files.txt") -Lines @((& git ls-files src resources scripts docs Makefile 2>&1) | ForEach-Object { $_.ToString() })
}
finally {
    Pop-Location
}

$tools = @("git", "rg", "dotnet", "cppcheck", "clang-tidy", "run-clang-tidy", "Invoke-ScriptAnalyzer")
$availability = foreach ($tool in $tools) {
    $command = Get-Command $tool -ErrorAction SilentlyContinue
    if ($command) {
        "$tool`tavailable`t$($command.Source)"
    }
    elseif ($tool -eq "rg" -and $script:RgMode -eq "wsl") {
        "$tool`tavailable-via-wsl"
    }
    else {
        "$tool`tmissing"
    }
}
Write-Utf8Lines -Path (Join-Path $OutDir "tool-availability.txt") -Lines $availability

$reviewPaths = @("src/native", "src/csharp", "resources", "scripts", "docs") |
    Where-Object { Test-Path (Join-Path $RepoRoot $_) }

Invoke-RgReport -OutputFile "native-hotspots.txt" -Paths $reviewPaths -Pattern "ServerPackedPaintBatch|ServerRelayPackedStrokeBatch|ServerCompactPaintBatch|SendCustomStrokeBatchToServer|ProcessEvent|paint_component|RuntimePaintable|mesh_first|runtime_triangle|preview_only|unpreview_only|PackedPaint|FCompactPaintStroke"

Invoke-RgReport -OutputFile "dynamic-entry-hotspots.txt" -Paths $reviewPaths -Pattern "DllMain|__declspec\s*\(\s*dllexport|GetProcAddress|CreateThread|SetWindowsHookEx|WM_APP|capabilities|paint_full_route|cancel_paint|shutdown|paint_replication_probe|paint_replication_pressure_probe|paint_packed_replay_probe|chrome\.webview|postMessage|WebMessageReceived"

Invoke-RgReport -OutputFile "research-probes.txt" -Paths $reviewPaths -Pattern "RESEARCH|research|probe|pressure_probe|packed_replay_probe|paint_replication_probe|MECCHA_RESEARCH_ARTIFACTS|debug_artifact|event_watch|dump|trace"

Invoke-RgReport -OutputFile "legacy-fallbacks.txt" -Paths $reviewPaths -Pattern "legacy|fallback|compact|adaptive|replication_pacing|ServerCompactPaintBatch|SendCustomStrokeBatchToServer|server_batch_limit|server_batch_delay|adaptive_batch|serverBatchLimit|adaptiveBatch"

Invoke-RgReport -OutputFile "ui-native-command-surface.txt" -Paths $reviewPaths -Pattern 'chrome\.webview|WebMessageReceived|PostWebMessageAsJson|Invoke|command|type"\s*:\s*"|paint_full_route|cancel_paint|preview|unpreview|capabilities|diagnostic|ServerPackedPaintBatch|replication_pacing|adaptive|serverBatch'

$csprojRoot = Join-Path $RepoRoot "src/csharp"
if (Test-Path $csprojRoot) {
    $projects = Get-ChildItem -LiteralPath $csprojRoot -Recurse -Filter "*.csproj" |
        Sort-Object FullName |
        ForEach-Object { To-RepoRelative $_.FullName }
    Write-Utf8Lines -Path (Join-Path $OutDir "csharp-projects.txt") -Lines @($projects)
}

if (Has-Tool "cppcheck") {
    Invoke-OptionalReport -Name "cppcheck" -OutputFile "cppcheck.txt" -Command {
        cppcheck --enable=warning,style,performance,portability,information,unusedFunction --inline-suppr --suppress=missingIncludeSystem src/native
    }
}

if ((Has-Tool "clang-tidy") -and (Test-Path (Join-Path $RepoRoot "compile_commands.json"))) {
    Invoke-OptionalReport -Name "clang-tidy" -OutputFile "clang-tidy.txt" -Command {
        clang-tidy -p . src/native/bridge/bridge.cpp src/native/injector/injector.cpp src/native/loader/loader.cpp
    }
}
elseif (Has-Tool "clang-tidy") {
    Write-Utf8Lines -Path (Join-Path $OutDir "clang-tidy.txt") -Lines @("skipped: compile_commands.json not found")
}

if (Has-Tool "dotnet") {
    Invoke-OptionalReport -Name "dotnet format verify" -OutputFile "dotnet-format.txt" -Command {
        $projects = Get-ChildItem -LiteralPath "src/csharp" -Recurse -Filter "*.csproj" | Sort-Object FullName
        foreach ($project in $projects) {
            Write-Output "## $((Resolve-Path -LiteralPath $project.FullName).Path)"
            dotnet format $project.FullName --verify-no-changes --severity warn
            Write-Output ""
        }
    }
}

if (Has-Tool "Invoke-ScriptAnalyzer") {
    Invoke-OptionalReport -Name "PSScriptAnalyzer" -OutputFile "psscriptanalyzer.txt" -Command {
        Invoke-ScriptAnalyzer -Path scripts -Recurse
    }
}

Write-Host "Runtime dead-code inventory written to $OutDir"
