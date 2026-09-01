[CmdletBinding()]
param(
    [string] $SourceDir = "",
    [Parameter(Mandatory = $true)][string] $OutputDir,
    [string] $LauncherDir = "",
    [string] $SourceCommit = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$repositoryRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $repositoryRoot "release-root"
}
if ([string]::IsNullOrWhiteSpace($LauncherDir)) {
    $LauncherDir = Join-Path $repositoryRoot "release-assets\fetcher-launcher"
}
if ([string]::IsNullOrWhiteSpace($SourceCommit)) {
    $SourceCommit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Could not resolve the tester-tools source commit."
    }
}

$releaseRootFiles = @(
    "Apply-Fetcher-Public-Test-Config.bat",
    "Apply-Fetcher-Public-Test-Config.ps1",
    "fetcher-canonical-fallbacks.cfg",
    "Apply-Fetcher-Mod-Compatibility.ps1",
    "Apply-Fetcher-ZHI-Compatibility.ps1",
    "fetcher-simulator-umo.json",
    "fetcher-client-patches.json",
    "fetcher-client-protection-policy.json",
    "fetcher-mod-compatibility-patches.json",
    "fetcher-update-channel.json",
    "FETCHER_SIMULATOR_README.txt",
    "Install-Fetcher-Bardcraft-With-UMO.bat",
    "Install-Fetcher-Bardcraft-With-UMO.ps1",
    "Install-Fetcher-Client-Mod-Bundle.ps1",
    "Install-Fetcher-Tester-Tools.ps1",
    "Setup-Fetcher-Updater.bat",
    "Launch-Fetcher-Character.bat",
    "Launch-Fetcher-Character.ps1",
    "Update-Fetcher-Simulator.bat",
    "Update-Fetcher-Simulator.ps1"
)

$launcherFiles = @(
    "FetcherLauncher.exe",
    "FetcherLauncher-THIRD-PARTY-NOTICES.txt",
    "ui\index.html",
    "ui\assets\fetcher-float.gif",
    "ui\assets\fetcher-float-right.gif",
    "ui\assets\potm2504a.jpg"
)

if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
    throw "Tester tools source directory was not found: $SourceDir"
}
if (-not (Test-Path -LiteralPath $LauncherDir -PathType Container)) {
    throw "Fetcher Launcher build directory was not found: $LauncherDir"
}
if ($SourceCommit -notmatch "^[0-9a-fA-F]{40}$") {
    throw "SourceCommit must be a full 40-character Git commit hash."
}

$payloads = New-Object System.Collections.Generic.List[object]
foreach ($relativePath in $releaseRootFiles) {
    $payloads.Add([pscustomobject]@{
        Source = Join-Path $SourceDir $relativePath
        Destination = $relativePath
    })
}
foreach ($relativePath in $launcherFiles) {
    $payloads.Add([pscustomobject]@{
        Source = Join-Path $LauncherDir $relativePath
        Destination = $relativePath
    })
}

$outputPath = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$obsoleteBootstrap = Join-Path $outputPath "Join-Fetcher-Test-Channel.bat"
if (Test-Path -LiteralPath $obsoleteBootstrap -PathType Leaf) {
    Remove-Item -LiteralPath $obsoleteBootstrap -Force
}
$stage = Join-Path ([IO.Path]::GetTempPath()) ("fetcher-tester-tools-" + [Guid]::NewGuid().ToString("N"))
$archive = Join-Path $outputPath "fetcher-tester-tools.zip"

try {
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    $records = New-Object System.Collections.Generic.List[object]
    foreach ($payload in $payloads) {
        $source = [string]$payload.Source
        $relativePath = [string]$payload.Destination
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Required tester tool was not found: $source"
        }

        $destination = Join-Path $stage $relativePath
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
        $item = Get-Item -LiteralPath $destination
        $records.Add([ordered]@{
            path = $relativePath.Replace("\", "/")
            size = [int64]$item.Length
            sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }

    $manifest = [ordered]@{
        schemaVersion = 1
        channel = "fetcher-simulator-test"
        sourceCommit = $SourceCommit.ToLowerInvariant()
        generatedAtUtc = [DateTime]::UtcNow.ToString("o")
        files = @($records | ForEach-Object { $_ })
    }
    $manifest | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath (Join-Path $stage "fetcher-tester-tools.json") -Encoding UTF8

    if (Test-Path -LiteralPath $archive) {
        Remove-Item -LiteralPath $archive -Force
    }
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $archive -CompressionLevel Optimal
    Copy-Item -LiteralPath (Join-Path $SourceDir "Install-Fetcher-Tester-Tools.ps1") `
        -Destination (Join-Path $outputPath "Install-Fetcher-Tester-Tools.ps1") -Force
    Copy-Item -LiteralPath (Join-Path $SourceDir "Setup-Fetcher-Updater.bat") `
        -Destination (Join-Path $outputPath "Setup-Fetcher-Updater.bat") -Force
    Copy-Item -LiteralPath (Join-Path $SourceDir "fetcher-simulator-umo.json") `
        -Destination (Join-Path $outputPath "fetcher-simulator-umo.json") -Force
}
finally {
    if (Test-Path -LiteralPath $stage -PathType Container) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}

Write-Host "Fetcher tester tools written: $archive"
