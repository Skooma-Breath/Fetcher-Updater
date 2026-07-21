[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $ArchivePath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function ConvertTo-SafeRelativePath {
    param([Parameter(Mandatory)][string] $Path)

    if ([IO.Path]::IsPathRooted($Path)) {
        throw "Archive contains an absolute path: $Path"
    }
    $normalized = $Path.Replace("\", "/").TrimStart("/")
    $segments = @($normalized.Split("/", [StringSplitOptions]::RemoveEmptyEntries))
    if ($segments.Count -eq 0 -or $normalized.Contains(":")) {
        throw "Archive contains an invalid path: $Path"
    }
    foreach ($segment in $segments) {
        if ($segment -eq "." -or $segment -eq "..") {
            throw "Archive path escapes its root: $Path"
        }
    }
    return ($segments -join "/")
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
foreach ($scriptPath in Get-ChildItem -LiteralPath (Join-Path $repositoryRoot "release-root") -Filter *.ps1 -File) {
    [void][ScriptBlock]::Create((Get-Content -LiteralPath $scriptPath.FullName -Raw))
}

$resolvedArchive = (Resolve-Path -LiteralPath $ArchivePath).Path
$workRoot = Join-Path ([IO.Path]::GetTempPath()) ("fetcher-package-test-" + [Guid]::NewGuid().ToString("N"))
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($resolvedArchive)
    try {
        foreach ($entry in $zip.Entries) {
            if (-not [string]::IsNullOrWhiteSpace($entry.FullName)) {
                [void](ConvertTo-SafeRelativePath -Path $entry.FullName.TrimEnd("/", "\"))
            }
        }
    }
    finally {
        $zip.Dispose()
    }

    Expand-Archive -LiteralPath $resolvedArchive -DestinationPath $workRoot
    $manifestPath = Join-Path $workRoot "fetcher-tester-tools.json"
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ([int]$manifest.schemaVersion -ne 1 -or [string]$manifest.channel -ne "fetcher-simulator-test") {
        throw "Package has an unsupported manifest."
    }

    $manifestPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($record in @($manifest.files)) {
        $relativePath = ConvertTo-SafeRelativePath -Path ([string]$record.path)
        if (-not $manifestPaths.Add($relativePath)) {
            throw "Manifest contains a duplicate path: $relativePath"
        }
        $filePath = Join-Path $workRoot $relativePath.Replace("/", "\")
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "Manifest file is missing: $relativePath"
        }
        if ((Get-Item -LiteralPath $filePath).Length -ne [int64]$record.size) {
            throw "Manifest size mismatch: $relativePath"
        }
        $actualHash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne ([string]$record.sha256).ToLowerInvariant()) {
            throw "Manifest hash mismatch: $relativePath"
        }
    }

    $payloadPaths = @(Get-ChildItem -LiteralPath $workRoot -File -Recurse | ForEach-Object {
        $_.FullName.Substring($workRoot.Length).TrimStart("\", "/").Replace("\", "/")
    } | Where-Object { $_ -ne "fetcher-tester-tools.json" })
    if ($payloadPaths.Count -ne $manifestPaths.Count) {
        throw "Manifest file count does not match the package payload."
    }
    foreach ($payloadPath in $payloadPaths) {
        if (-not $manifestPaths.Contains($payloadPath)) {
            throw "Package contains an unmanifested payload: $payloadPath"
        }
    }
}
finally {
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}

Write-Host "Fetcher tester-tools package is valid: $resolvedArchive"
