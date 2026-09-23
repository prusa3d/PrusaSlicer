param(
    [Parameter(Mandatory=$true)][string]$TestApp,
    [string]$NativeVendor,
    [string]$OrcaProfiles,
    [string]$Report
)
$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (!$NativeVendor) { $NativeVendor = Join-Path $scriptDir '../../resources/presets/prusa-research-fff/PrusaResearch' }
if (!$OrcaProfiles) { $OrcaProfiles = Join-Path $scriptDir '../../resources/profiles' }
if (!$Report) { $Report = Join-Path $scriptDir '../../build/prusa-comparison.json' }
$TestApp = (Resolve-Path -LiteralPath $TestApp).Path
$NativeVendor = (Resolve-Path -LiteralPath $NativeVendor).Path
$OrcaProfiles = (Resolve-Path -LiteralPath $OrcaProfiles).Path
$Report = [IO.Path]::GetFullPath($Report)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Report) | Out-Null
& $TestApp --compare-prusa $NativeVendor $OrcaProfiles $Report *> "$Report.log"
if ($LASTEXITCODE -ne 0) { throw "Prusa comparison failed; see $Report.log" }
Write-Output "Prusa comparison passed: $Report"
