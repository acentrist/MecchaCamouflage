param(
    [string]$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$ExeName = "meccha-camouflage.exe",
    [string[]]$RuntimeArgs,
    [string]$RuntimeArgString = ""
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

if ($RuntimeArgString) {
    $stringArgs = Convert-RuntimeArgString -RuntimeArgString $RuntimeArgString
    $RuntimeArgs = @($stringArgs + $RuntimeArgs)
}

$ExePath = Resolve-RuntimeExe -RuntimeRoot $RuntimeRoot -RuntimeName $RuntimeName
if (-not (Test-Path $ExePath)) { throw "Executable not found: $ExePath. Run make build first." }
if (-not $RuntimeArgs -or $RuntimeArgs.Count -eq 0) {
    $RuntimeArgs = @("--mode", "service")
}

Write-Host "Using runtime exe: $ExePath"
Write-Host "Runtime args: $($RuntimeArgs -join ' ')"
Invoke-PipelineStep -Name "runtime execution" -ScriptBlock { & $ExePath @RuntimeArgs }
