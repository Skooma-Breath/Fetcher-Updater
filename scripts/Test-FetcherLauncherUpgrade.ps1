[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $CandidateArchivePath,
    [Parameter(Mandatory = $true)][string] $OutputDir,
    [string] $LegacyArchivePath = "",
    [string] $LegacySha256 = "",
    [switch] $Gui,
    [switch] $LeavePreview
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$candidateArchive = (Resolve-Path -LiteralPath $CandidateArchivePath).Path
$outputPath = [IO.Path]::GetFullPath($OutputDir)
$workspacePrefix = $repositoryRoot.TrimEnd('\') + '\'
if (-not $outputPath.StartsWith($workspacePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Upgrade evidence must use a new dedicated directory inside this repository."
}
if (Test-Path -LiteralPath $outputPath) {
    throw "Use a new OutputDir. Existing test installations are never deleted or overwritten."
}
if ($LeavePreview -and -not $Gui) { throw "LeavePreview requires Gui." }
if ($LegacyArchivePath -and $LegacySha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw "A retained legacy archive requires its independently recorded LegacySha256."
}
if ($LegacyArchivePath) {
    $legacyArchive = (Resolve-Path -LiteralPath $LegacyArchivePath).Path
    $source = [ordered]@{ path = $legacyArchive; sha256 = $LegacySha256.ToLowerInvariant() }
} else {
    $headers = @{ Accept = 'application/vnd.github+json'; 'User-Agent' = 'Fetcher-Launcher-Upgrade-Acceptance' }
    $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/Skooma-Breath/Fetcher-Updater/releases/tags/fetcher-tester-tools' -Headers $headers
    $assets = @($release.assets | Where-Object { $_.name -eq 'fetcher-tester-tools.zip' })
    if ($assets.Count -ne 1 -or $assets[0].digest -notmatch '^sha256:([0-9a-fA-F]{64})$') {
        throw "Public tools release must contain exactly one SHA-256-backed archive."
    }
    $LegacySha256 = $Matches[1].ToLowerInvariant()
    $source = [ordered]@{
        repository = 'Skooma-Breath/Fetcher-Updater'
        tag = 'fetcher-tester-tools'
        targetCommitish = $release.target_commitish
        url = $assets[0].browser_download_url
        sha256 = $LegacySha256
    }
    New-Item -ItemType Directory -Path $outputPath | Out-Null
    $legacyArchive = Join-Path $outputPath 'public-legacy-tools.zip'
    Invoke-WebRequest -Uri $source.url -Headers $headers -OutFile $legacyArchive
}
$actualHash = (Get-FileHash -LiteralPath $legacyArchive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $LegacySha256.ToLowerInvariant()) { throw "Legacy archive checksum mismatch." }
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$source | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath 'legacy-source.json') -Encoding UTF8
$arguments = @(
    (Join-Path $repositoryRoot 'tests/test_launcher_upgrade.py'),
    '--legacy-archive', $legacyArchive,
    '--legacy-sha256', $actualHash,
    '--candidate-archive', $candidateArchive,
    '--root', (Join-Path $outputPath 'test-installations')
)
if ($Gui) { $arguments += '--gui' }
if ($LeavePreview) { $arguments += '--leave-preview' }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw "Legacy-to-native launcher acceptance failed. Evidence is retained in $outputPath" }
Write-Host "Legacy-to-native launcher acceptance passed. Evidence: $outputPath"
