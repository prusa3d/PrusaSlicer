param(
    [Parameter(Mandatory=$true)][string]$App,
    [string]$OutputDir = (Join-Path $PSScriptRoot ('../../build/orca-parity-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$App = (Resolve-Path -LiteralPath $App).Path
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$model = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../data/test_stl/ASCII/20mmbox-LF.stl')).Path
$cases = @(
    @('a1-extra-fine', 'Bambu Lab A1 mini 0.4 nozzle 0.400000 mm', '0.08mm Extra Fine @BBL A1M', 'Generic PLA @BBL A1M'),
    @('a1-high-quality', 'Bambu Lab A1 mini 0.4 nozzle 0.400000 mm', '0.08mm High Quality @BBL A1M', 'Generic PLA @BBL A1M'),
    @('a1-bambu-pla', 'Bambu Lab A1 mini 0.4 nozzle 0.400000 mm', '0.08mm Extra Fine @BBL A1M', 'Bambu PLA Basic @BBL A1M'),
    @('sovol-zero', 'Sovol Zero 0.4 nozzle 0.400000 mm', '0.20mm Standard @Sovol Zero 0.4 nozzle', 'Sovol Zero PLA - Brass'),
    @('snapmaker-u1', 'Snapmaker U1 (0.4 nozzle) 4T 0.400000 mm, 0.400000 mm, 0.400000 mm, 0.400000 mm', '0.20 Standard @Snapmaker U1 (0.4 nozzle)', 'Generic PLA @System,Generic PLA @System,Generic PLA @System,Generic PLA @System')
)
foreach ($case in $cases) {
    $name, $printer, $process, $filament = $case
    $gcode = Join-Path $OutputDir ($name + '.gcode')
    if (Test-Path -LiteralPath $gcode) { throw "Use a fresh output directory: $gcode already exists" }
    $log = Join-Path $OutputDir ($name + '.log')
    & $App --datadir (Join-Path $OutputDir ($name + '-data')) --printer-profile $printer --print-profile $process --material-profile $filament --export-gcode --output $gcode $model *> $log
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $gcode)) { throw "$name failed; see $log" }
    $text = [IO.File]::ReadAllText($gcode)
    $layers = [regex]::Matches($text, '(?m)^;LAYER_CHANGE\r?$').Count
    if ($layers -lt 90 -or $text -notmatch '(?m)^G1 .*E[0-9]') { throw "$name did not export a sliced 20 mm cube" }
    if ($name.StartsWith('a1-') -and ($layers -lt 240 -or $text -notmatch '(?m)^;HEIGHT:0\.08\r?$')) {
        throw "$name did not preserve 0.08 mm layers"
    }
    if ([IO.File]::ReadAllText($log) -match 'Custom G-code error|Unhandled exception') { throw "$name reported a slicing or G-code error" }
    Write-Output "$name passed: $layers layers, $((Get-Item -LiteralPath $gcode).Length) bytes"
}
