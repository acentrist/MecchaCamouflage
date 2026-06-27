param(
    [string]$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$ExeName = "meccha-camouflage.exe",
    [string[]]$RuntimeArgs,
    [string]$RuntimeArgString = "",
    [string]$NativeApplyMode = "template_brush_paint",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$RuntimeName = [System.IO.Path]::GetFileNameWithoutExtension($ExeName)

function Invoke-PipelineStep {
    param([string]$Name, [scriptblock]$ScriptBlock)
    $global:LASTEXITCODE = 0
    & $ScriptBlock
    if (-not $?) { throw "$Name failed." }
    if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit code $LASTEXITCODE." }
}

function Resolve-RuntimeExe {
    param([string]$RuntimeRoot, [string]$RuntimeName)
    $candidate = Join-Path $RuntimeRoot ".build\bin\$RuntimeName.exe"
    if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    return ""
}

function Convert-RuntimeArgString {
    param([string]$RuntimeArgString)
    if ([string]::IsNullOrWhiteSpace($RuntimeArgString)) { return @() }
    $tokens = New-Object System.Collections.Generic.List[string]
    $builder = New-Object System.Text.StringBuilder
    $state = "Normal"
    $inEscape = $false
    foreach ($char in $RuntimeArgString.ToCharArray()) {
        if ($inEscape) { [void]$builder.Append($char); $inEscape = $false; continue }
        if ($char -eq '\') { $inEscape = $true; continue }
        switch ($state) {
            "SingleQuote" { if ($char -eq "'") { $state = "Normal" } else { [void]$builder.Append($char) }; continue }
            "DoubleQuote" { if ($char -eq '"') { $state = "Normal" } else { [void]$builder.Append($char) }; continue }
        }
        if ($char -eq "'") { $state = "SingleQuote"; continue }
        if ($char -eq '"') { $state = "DoubleQuote"; continue }
        if ([char]::IsWhiteSpace($char)) {
            if ($builder.Length -gt 0) { $tokens.Add($builder.ToString()); $builder.Clear() | Out-Null }
            continue
        }
        [void]$builder.Append($char)
    }
    if ($builder.Length -gt 0) { $tokens.Add($builder.ToString()) }
    return $tokens.ToArray()
}

function Convert-ToProcessArgument {
    param([string]$Value)
    if ($null -eq $Value) { return '""' }
    if ($Value -notmatch '[\s"]') { return $Value }
    return '"' + ($Value -replace '"', '\"') + '"'
}

function Invoke-ForegroundRuntime {
    param(
        [string]$ExePath,
        [string[]]$Arguments
    )
    $argumentLine = ($Arguments | ForEach-Object { Convert-ToProcessArgument $_ }) -join " "
    $process = $null
    try {
        $process = Start-Process -FilePath $ExePath -ArgumentList $argumentLine -PassThru
        while (-not $process.HasExited) {
            Start-Sleep -Milliseconds 200
            $process.Refresh()
        }
        $global:LASTEXITCODE = $process.ExitCode
    }
    finally {
        if ($process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($LASTEXITCODE -ne 0) { throw "runtime execution failed with exit code $LASTEXITCODE." }
}

if ($RuntimeArgString) {
    $stringArgs = Convert-RuntimeArgString -RuntimeArgString $RuntimeArgString
    $RuntimeArgs = @($stringArgs + $RuntimeArgs)
}

$ExePath = Resolve-RuntimeExe -RuntimeRoot $RuntimeRoot -RuntimeName $RuntimeName
if (-not (Test-Path $ExePath)) { throw "Executable not found: $ExePath. Run make build first." }
if (-not $RuntimeArgs -or $RuntimeArgs.Count -eq 0) {
    $RuntimeArgs = @("--mode", "service", "--native-apply-mode", $NativeApplyMode, "--parent-pid", "$PID")
}

Write-Host "Using runtime exe: $ExePath"
Write-Host "Runtime args: $($RuntimeArgs -join ' ')"
if ($DryRun) { return }
Invoke-ForegroundRuntime -ExePath $ExePath -Arguments $RuntimeArgs
