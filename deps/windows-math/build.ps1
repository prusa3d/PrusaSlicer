# SPDX-License-Identifier: AGPL-3.0-or-later
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('arm64', 'x64')][string]$Architecture,
    [Parameter(Mandatory)][string]$WorkRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

function Get-VerifiedArchive {
    param($InputSpec)
    $name = ([Uri]$InputSpec.url).Segments[-1]
    $file = Join-Path $WorkRoot "downloads\$name"
    Write-Host "Downloading $($InputSpec.url)"
    Invoke-Checked curl.exe @('-4', '--fail', '--location', '--silent', '--show-error',
        '--retry', '3', '--retry-all-errors', '--connect-timeout', '30',
        '--output', $file, $InputSpec.url)
    $hash = (Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $InputSpec.sha256) { throw "SHA256 mismatch for $name" }
    return $file
}

function Assert-Machine {
    param([string]$File)
    $bytes = [IO.File]::ReadAllBytes($File)
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    if ([BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550) { throw "Not a PE file: $File" }
    $machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
    if ($machine -ne $script:ExpectedMachine) { throw "Wrong PE machine $machine in $File" }
}

function Invoke-MsvcSmoke {
    param([string]$PackageRoot, [string]$Source)
    Push-Location "$PackageRoot\bin"
    $savedPath = $env:PATH
    try {
        Invoke-Checked cl @('/nologo', '/std:c++17', '/EHsc', '/MD', '/W4', '/WX',
            "/I$PackageRoot\include", $Source, '/Fe:smoke-msvc.exe',
            '/link', "$PackageRoot\lib\libgmp-10.lib", "$PackageRoot\lib\libmpfr-6.lib") | Out-Host
        Assert-Machine "$PackageRoot\bin\smoke-msvc.exe"
        $env:PATH = "$PackageRoot\bin;$env:SystemRoot\System32;$env:SystemRoot"
        $result = @(& .\smoke-msvc.exe)
        if ($LASTEXITCODE) { throw 'MSVC native arithmetic/ABI smoke failed' }
        return $result
    } finally {
        $env:PATH = $savedPath
        Pop-Location
    }
}

function Export-ImportLibrary {
    param([string]$Dll, [string]$Name)
    Assert-Machine $Dll
    $report = @(& dumpbin /nologo /headers /exports /imports $Dll)
    if ($LASTEXITCODE) { throw "dumpbin failed for $Dll" }
    $report | Set-Content "$WorkRoot\logs\$Name-pe.txt"
    $exports = @(& dumpbin /nologo /exports $Dll)
    if ($LASTEXITCODE) { throw "Export inspection failed for $Dll" }
    $bytes = [IO.File]::ReadAllBytes($Dll)
    $pe = [BitConverter]::ToInt32($bytes, 0x3c)
    $sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
    $sectionStart = $pe + 24 + [BitConverter]::ToUInt16($bytes, $pe + 20)
    $definitions = @("LIBRARY $Name.dll", 'EXPORTS')
    foreach ($line in $exports) {
        if ($line -notmatch '^\s+\d+\s+[0-9A-F]+\s+([0-9A-F]{8})\s+(\S+)\s*$') { continue }
        $rva = [Convert]::ToUInt32($Matches[1], 16)
        $symbol = $Matches[2]
        $found = $false
        for ($i = 0; $i -lt $sectionCount; ++$i) {
            $section = $sectionStart + 40 * $i
            $size = [BitConverter]::ToUInt32($bytes, $section + 8)
            $address = [BitConverter]::ToUInt32($bytes, $section + 12)
            $flags = [BitConverter]::ToUInt32($bytes, $section + 36)
            if ($rva -ge $address -and $rva -lt ($address + $size)) {
                # Import-library data entries must not become function thunks.
                $suffix = if ($flags -band 0x20) { '' } else { ' DATA' }
                $definitions += "    $symbol$suffix"
                $found = $true
                break
            }
        }
        if (!$found) { throw "Export RVA outside sections: $symbol" }
    }
    if ($definitions.Count -lt 100) { throw "Missing exports in $Dll" }
    if ($Name -eq 'libgmp-10' -and $definitions -notcontains '    __gmp_version DATA') {
        throw 'GMP data exports were not recognized'
    }
    $def = "$WorkRoot\package\lib\$Name.def"
    $definitions | Set-Content $def -Encoding ascii
    Invoke-Checked lib @('/nologo', "/def:$def", "/machine:$Architecture", "/out:$WorkRoot\package\lib\$Name.lib")
    $exp = "$WorkRoot\package\lib\$Name.exp"
    if (Test-Path $exp) { Remove-Item $exp }
    $dependents = @(& dumpbin /nologo /dependents $Dll)
    if ($LASTEXITCODE) { throw "Dependency inspection failed for $Dll" }
    $dependents | Set-Content "$WorkRoot\logs\$Name-dependents.txt"
    $dependencies = @($dependents | ForEach-Object {
        if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $Matches[1] }
    })
    if (!$dependencies.Count) { throw "No dependency table found in $Dll" }
    foreach ($dependency in $dependencies) {
        if ($dependency -match '^(KERNEL32|ADVAPI32|ucrtbase|api-ms-win-crt-[a-z0-9-]+)\.dll$') { continue }
        if ($Name -eq 'libmpfr-6' -and $dependency -eq 'libgmp-10.dll') { continue }
        throw "Unexpected runtime dependency $dependency in $Dll"
    }
}

$native = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
$process = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString().ToLowerInvariant()
if ($native -ne $Architecture -or $process -ne $Architecture) {
    throw "Native $Architecture OS and PowerShell required; OS=$native process=$process"
}
$ExpectedMachine = if ($Architecture -eq 'arm64') { 0xaa64 } else { 0x8664 }
$target = if ($Architecture -eq 'arm64') { 'aarch64-w64-mingw32' } else { 'x86_64-w64-mingw32' }
$WorkRoot = [IO.Path]::GetFullPath($WorkRoot)
if (Test-Path $WorkRoot) { throw "Use a fresh work directory: $WorkRoot" }
if ($WorkRoot -match '\s') { throw 'Autotools work directory must not contain whitespace' }
$recipeStatus = @(& git -C $PSScriptRoot status --porcelain -- . ../../.github/workflows/build_windows_math.yml)
if ($LASTEXITCODE -ne 0 -or $recipeStatus.Count) {
    throw 'Commit the reviewed recipe and workflow before building; provenance requires a clean recipe'
}
foreach ($dir in @('downloads', 'src', 'tools', 'logs', 'package\bin', 'package\lib', 'package\include', 'artifacts')) {
    New-Item -ItemType Directory "$WorkRoot\$dir" -Force | Out-Null
}
Start-Transcript "$WorkRoot\logs\build-transcript.txt"
try {
    $spec = Get-Content "$PSScriptRoot\inputs.json" -Raw | ConvertFrom-Json
    $archives = @{}
    foreach ($name in @('gmp', 'mpfr', 'msys2', 'make', 'm4', 'diffutils')) {
        $archives[$name] = Get-VerifiedArchive $spec.$name
    }
    $archives.llvm = Get-VerifiedArchive $spec.llvm.$Architecture
    Invoke-Checked tar @('-xf', $archives.msys2, '-C', "$WorkRoot\tools")
    Expand-Archive $archives.llvm "$WorkRoot\tools"
    $llvm = @(Get-ChildItem "$WorkRoot\tools" -Directory -Filter 'llvm-mingw-*')
    if ($llvm.Count -ne 1) { throw 'Expected exactly one LLVM toolchain' }
    $llvm = $llvm[0].FullName
    Assert-Machine "$llvm\bin\clang.exe"
    $bash = "$WorkRoot\tools\msys64\usr\bin\bash.exe"
    # Only build orchestration uses MSYS x64 emulation on ARM64; all produced
    # executables and both compilers are checked for the native architecture.
    $env:MSYS2_PATH_TYPE = 'inherit'
    $env:CHERE_INVOKING = '1'
    $env:MSYSTEM = 'MSYS'
    foreach ($name in @('make', 'm4', 'diffutils')) {
        $archivePath = & "$WorkRoot\tools\msys64\usr\bin\cygpath.exe" -u $archives[$name]
        if ($LASTEXITCODE) { throw "Cannot convert archive path for $name" }
        $msysRoot = & "$WorkRoot\tools\msys64\usr\bin\cygpath.exe" -u "$WorkRoot\tools\msys64"
        if ($LASTEXITCODE) { throw 'Cannot convert MSYS root path' }
        Invoke-Checked "$WorkRoot\tools\msys64\usr\bin\tar.exe" @('-xf', $archivePath, '-C', $msysRoot)
    }
    foreach ($name in @('gmp', 'mpfr')) {
        Invoke-Checked tar @('-xf', $archives[$name], '-C', "$WorkRoot\src")
    }
    $patches = @(Get-ChildItem "$PSScriptRoot\patches" -Filter '*.patch' | Sort-Object Name)
    foreach ($patch in $patches) {
        Invoke-Checked git @('-C', "$WorkRoot\src", 'apply', '--check', $patch.FullName)
        Invoke-Checked git @('-C', "$WorkRoot\src", 'apply', $patch.FullName)
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsArgs = @('-latest', '-products', '*')
    if ($env:WINDOWS_MATH_RUNNER_LABEL -eq 'windows-11-vs2026-arm') {
        $vsArgs += @('-version', '[18.0,19.0)')
    }
    $vs = & $vswhere @vsArgs -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio is required' }
    $vsVersion = & $vswhere @vsArgs -property installationVersion
    if ($LASTEXITCODE -ne 0 -or !$vsVersion) { throw 'Cannot identify selected Visual Studio version' }
    Write-Host "Selected Visual Studio $vsVersion at $vs"
    Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=$Architecture -host_arch=$Architecture"
    Assert-Machine (Get-Command cl.exe).Source
    Invoke-Checked $bash @('--noprofile', '--norc', ($PSScriptRoot.Replace('\', '/') + '/build.sh'), $WorkRoot, $llvm, $target)

    $buildWarnings = @(foreach ($name in @('gmp-configure', 'gmp-build', 'gmp-check', 'gmp-install',
                                          'mpfr-configure', 'mpfr-build', 'mpfr-check', 'mpfr-install')) {
        Select-String -Path "$WorkRoot\logs\$name.log" -Pattern '\bwarning:' -CaseSensitive:$false
    })
    if ($buildWarnings.Count) {
        $buildWarnings | Select-Object -First 20 | ForEach-Object { Write-Host $_ }
        throw "$($buildWarnings.Count) build/test warnings remain; see complete diagnostic logs"
    }

    Copy-Item "$WorkRoot\install\bin\libgmp-10.dll", "$WorkRoot\install\bin\libmpfr-6.dll" "$WorkRoot\package\bin"
    Copy-Item "$WorkRoot\install\include\gmp.h", "$WorkRoot\install\include\mpfr.h", "$WorkRoot\install\include\mpf2mpfr.h" "$WorkRoot\package\include"
    Export-ImportLibrary "$WorkRoot\package\bin\libgmp-10.dll" 'libgmp-10'
    Export-ImportLibrary "$WorkRoot\package\bin\libmpfr-6.dll" 'libmpfr-6'
    $env:PATH = "$WorkRoot\package\bin;$env:PATH"
    Push-Location "$WorkRoot\package\bin"
    try {
        $msvcOutput = @(Invoke-MsvcSmoke "$WorkRoot\package" "$PSScriptRoot\smoke.cpp")
        $abiFlags = if ($Architecture -eq 'x64') { @('-mlong-double-64') } else { @() }
        Invoke-Checked "$llvm\bin\$target-clang.exe" (@('-x', 'c++', '-std=c++17', '-O2', '-D__USE_MINGW_ANSI_STDIO=0',
            "-I$WorkRoot\package\include", "$PSScriptRoot\smoke.cpp",
            "-L$WorkRoot\install\lib", '-lmpfr', '-lgmp', '-o', 'smoke-llvm.exe') + $abiFlags)
        Assert-Machine "$WorkRoot\package\bin\smoke-llvm.exe"
        $llvmOutput = @(& .\smoke-llvm.exe)
        if ($LASTEXITCODE) { throw 'LLVM native arithmetic/ABI smoke failed' }
        if (($msvcOutput -join "`n") -ne ($llvmOutput -join "`n")) { throw 'Compiler ABI descriptions differ' }
        $msvcOutput | Tee-Object "$WorkRoot\logs\native-abi.txt"
        Invoke-Checked dumpbin @('/nologo', '/headers', '/imports', 'smoke-msvc.exe')
    } finally { Pop-Location }

    $recipeHash = (Get-FileHash "$PSScriptRoot\inputs.json" -Algorithm SHA256).Hash.ToLowerInvariant()
    $commit = (& git -C $PSScriptRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE) { throw 'Recipe must be in a git checkout' }
    $msvcVersion = $env:VCToolsVersion.TrimEnd('\')
    $key = "gmp-$($spec.gmp.version)-mpfr-$($spec.mpfr.version)-$Architecture-llvm-$($spec.llvm.version)-msvc-$msvcVersion-$($recipeHash.Substring(0,12))-$($commit.Substring(0,12))"
    $sourceRoot = "$WorkRoot\package\source"
    New-Item -ItemType Directory "$sourceRoot\recipe", "$WorkRoot\package\licenses" -Force | Out-Null
    Copy-Item $archives.gmp, $archives.mpfr $sourceRoot
    Copy-Item "$PSScriptRoot\*" "$sourceRoot\recipe" -Recurse
    Copy-Item "$PSScriptRoot\..\..\.github\workflows\build_windows_math.yml" "$sourceRoot\recipe"
    Copy-Item "$PSScriptRoot\..\..\LICENSE" "$WorkRoot\package\licenses\recipe-AGPL-3.0.txt"
    foreach ($name in @('gmp', 'mpfr')) {
        $licenseDir = "$WorkRoot\package\licenses\$name"
        New-Item -ItemType Directory $licenseDir | Out-Null
        Copy-Item "$WorkRoot\src\$name-$($spec.$name.version)\COPYING*" $licenseDir
    }
    $toolLicenses = @(Get-ChildItem $llvm -Recurse -File | Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE)' })
    if (!$toolLicenses.Count) { throw 'Toolchain runtime license notices missing' }
    foreach ($file in $toolLicenses) {
        $relative = [IO.Path]::GetRelativePath($llvm, $file.FullName)
        $destination = "$WorkRoot\package\licenses\toolchain\$relative"
        New-Item -ItemType Directory (Split-Path $destination) -Force | Out-Null
        Copy-Item $file.FullName $destination
    }
    $compilerInfo = @(& "$llvm\bin\clang.exe" --version) -join "`n"
    $msvcInfo = @(& cl.exe 2>&1) -join "`n"
    $provenance = [ordered]@{
        schema = 1; package = $key; architecture = $Architecture; target = $target
        recipeCommit = $commit; inputs = $spec
        patches = @($patches | ForEach-Object {
            @{ name = $_.Name; sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        })
        llvm = $compilerInfo; msvc = $msvcInfo
        runnerLabel = $env:WINDOWS_MATH_RUNNER_LABEL
        runnerImageName = $env:ImageOS; runnerImage = $env:ImageVersion
        visualStudio = @{ version = $vsVersion; path = $vs; toolset = $msvcVersion }
        windows = [Environment]::OSVersion.VersionString
        nativeAbi = $msvcOutput; runUrl = "https://github.com/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID"
        limitations = 'Portable C, release DLLs, C ABI only; not application parity or bit-for-bit reproducibility.'
    }
    $provenance | ConvertTo-Json -Depth 10 | Set-Content "$WorkRoot\package\provenance.json"
    # Build executables are useful diagnostics, not redistributable library payload.
    Remove-Item "$WorkRoot\package\bin\smoke-msvc.exe", "$WorkRoot\package\bin\smoke-llvm.exe", "$WorkRoot\package\bin\smoke.obj"
    $files = Get-ChildItem "$WorkRoot\package" -Recurse -File | Sort-Object FullName
    $files | ForEach-Object {
        "$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $([IO.Path]::GetRelativePath("$WorkRoot\package", $_.FullName))"
    } | Set-Content "$WorkRoot\package\SHA256SUMS"
    Compress-Archive "$WorkRoot\package\*" "$WorkRoot\artifacts\$key.zip"
    $zip = "$WorkRoot\artifacts\$key.zip"
    "$((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant())  $key.zip" |
        Set-Content "$WorkRoot\artifacts\SHA256SUMS"
    $unpacked = "$WorkRoot\unpacked"
    Expand-Archive $zip $unpacked
    $manifest = @(Get-Content "$unpacked\SHA256SUMS")
    foreach ($line in $manifest) {
        if ($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw 'Invalid packaged SHA256 manifest' }
        $expectedHash = $Matches[1]
        $file = [IO.Path]::GetFullPath((Join-Path $unpacked $Matches[2]))
        if (!$file.StartsWith("$unpacked\", [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escapes package' }
        if ((Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
            throw "Unpacked checksum mismatch: $file"
        }
    }
    if (@(Get-ChildItem $unpacked -Recurse -File).Count -ne $manifest.Count + 1) {
        throw 'Unpacked files do not match the manifest'
    }
    $unpackedOutput = @(Invoke-MsvcSmoke $unpacked "$unpacked\source\recipe\smoke.cpp")
    if (($unpackedOutput -join "`n") -ne ($msvcOutput -join "`n")) {
        throw 'Clean unpacked package ABI result differs'
    }
    $unpackedOutput | Set-Content "$WorkRoot\logs\unpacked-native-abi.txt"
    if ($env:GITHUB_OUTPUT) { "artifact=$key" >> $env:GITHUB_OUTPUT }
} finally { Stop-Transcript }
