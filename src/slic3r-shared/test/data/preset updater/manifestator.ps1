param (
    [Parameter(Mandatory=$true)]
    [string]$Version
)

$searchPath = "./$Version"
$outputFile = "./$Version/manifest.json"

# Resolve paths to absolute paths for comparison and string replacement
$fullOutputPath = (Resolve-Path -Path $outputFile).Path
$fullSearchPath = (Resolve-Path -Path $searchPath).Path

Get-ChildItem -Path $fullSearchPath -Recurse -File | Where-Object { $_.FullName -ne $fullOutputPath } | ForEach-Object {
    # Calculate relative path, trim leading separators, and standardize to forward slashes
    $relativePath = $_.FullName.Replace($fullSearchPath, "").TrimStart('\/')
    $relativePath = $relativePath.Replace('\', '/')

    [PSCustomObject]@{
        filename = $relativePath
        filehash = (Get-FileHash -Path $_.FullName -Algorithm SHA256).Hash
    }
} | ConvertTo-Json | Out-File -FilePath $fullOutputPath -Encoding utf8