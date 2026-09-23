param(
    [Parameter(Mandatory=$true)][string]$App,
    [Parameter(Mandatory=$true)][string[]]$Projects,
    [string]$OutputDir = (Join-Path $PSScriptRoot ('../../build/project-export-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$App = (Resolve-Path -LiteralPath $App).Path
$Projects = @($Projects | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
Push-Location (Split-Path -Parent $App)
try {
    foreach ($project in $Projects) {
        $name = [IO.Path]::GetFileNameWithoutExtension($project)
        $gcode = Join-Path $OutputDir ($name + '.gcode')
        $log = Join-Path $OutputDir ($name + '.log')
        if (Test-Path -LiteralPath $gcode) { throw "Use a fresh output directory: $gcode already exists" }
        & $App --datadir (Join-Path $OutputDir 'data') --loglevel 4 --export-gcode --output $gcode $project *> $log
        if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $gcode)) {
            throw "$name failed; see $log"
        }
        if (!(Select-String -LiteralPath $gcode -Pattern '^G1 .*E[0-9]' -Quiet)) {
            throw "$name exported no extrusion moves"
        }
        if (Select-String -LiteralPath $log -Pattern 'Assertion failed|Custom G-code error|Unhandled exception' -Quiet) {
            throw "$name reported an export error; see $log"
        }
        Write-Output "$name passed: $((Get-Item -LiteralPath $gcode).Length) bytes"
    }
} finally {
    Pop-Location
}
