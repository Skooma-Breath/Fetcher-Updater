[CmdletBinding()]
param(
    [string] $InstallRoot = "",
    [Alias("Repository")]
    [string] $TesterToolsRepository = "Skooma-Breath/Fetcher-Updater",
    [string] $ReleaseTag = "fetcher-tester-tools",
    [string] $AssetName = "fetcher-tester-tools.zip",
    [string] $GitHubApiBaseUrl = "https://api.github.com",
    [string] $GitHubDownloadBaseUrl = "https://github.com",
    [string] $ToolsArchivePath = "",
    [string] $ClientModBundleRepository = "Skooma-Breath/Fetcher-Updater",
    [string] $ClientModBundleReleaseTag = "openmw-client-mods-mp-clients",
    [string] $ClientModBundleAssetName = "openmw-client-mods.zip",
    [string] $ClientModBundleArchivePath = "",
    [switch] $SkipClientModBundle,
    [switch] $SkipUpdater
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$headers = @{ "User-Agent" = "Fetcher-Tester-Tools-Bootstrap" }
$scriptDirectory = $PSScriptRoot

function Select-InstallRoot {
    if (-not [string]::IsNullOrWhiteSpace($InstallRoot)) {
        return (Resolve-Path -LiteralPath $InstallRoot).Path
    }

    if (Test-Path -LiteralPath (Join-Path $scriptDirectory "openmw.exe") -PathType Leaf) {
        return (Resolve-Path -LiteralPath $scriptDirectory).Path
    }

    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = "Select your Fetcher Simulator folder (the folder containing openmw.exe)."
    $dialog.ShowNewFolderButton = $false
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        throw "No Fetcher Simulator folder was selected."
    }
    return (Resolve-Path -LiteralPath $dialog.SelectedPath).Path
}

function ConvertTo-SafeRelativePath {
    param([Parameter(Mandatory = $true)][string] $Path)

    if ([IO.Path]::IsPathRooted($Path)) {
        throw "Tester tools archive contains an absolute path: $Path"
    }
    $normalized = $Path.Replace("\", "/").TrimStart("/")
    $segments = @($normalized.Split("/", [StringSplitOptions]::RemoveEmptyEntries))
    if ($segments.Count -eq 0 -or $normalized.Contains(":")) {
        throw "Tester tools archive contains an invalid path: $Path"
    }
    foreach ($segment in $segments) {
        if ($segment -eq "." -or $segment -eq "..") {
            throw "Tester tools archive path escapes the install root: $Path"
        }
    }
    return ($segments -join "/")
}

$root = Select-InstallRoot
if (-not (Test-Path -LiteralPath (Join-Path $root "openmw.exe") -PathType Leaf)) {
    throw "The selected folder does not contain openmw.exe: $root"
}

$workRoot = Join-Path ([IO.Path]::GetTempPath()) ("fetcher-tester-bootstrap-" + [Guid]::NewGuid().ToString("N"))
$archivePath = Join-Path $workRoot $AssetName
$extractRoot = Join-Path $workRoot "extract"
try {
    New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
    if ($ToolsArchivePath) {
        Copy-Item -LiteralPath $ToolsArchivePath -Destination $archivePath -Force
    }
    else {
        $encodedTag = [Uri]::EscapeDataString($ReleaseTag)
        $releaseUrl = "$($GitHubApiBaseUrl.TrimEnd('/'))/repos/$TesterToolsRepository/releases/tags/$encodedTag"
        $release = Invoke-RestMethod -UseBasicParsing -Uri $releaseUrl -Headers $headers
        $asset = @($release.assets | Where-Object { [string]$_.name -eq $AssetName })
        if ($asset.Count -ne 1 -or [string]$asset[0].digest -notmatch "^sha256:([0-9a-fA-F]{64})$") {
            throw "The $ReleaseTag release does not contain one digest-backed $AssetName asset."
        }
        $expectedHash = $Matches[1].ToLowerInvariant()
        $downloadUrl = "$($GitHubDownloadBaseUrl.TrimEnd('/'))/$TesterToolsRepository/releases/download/$encodedTag/$([Uri]::EscapeDataString($AssetName))"
        Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -Headers $headers -OutFile $archivePath
        $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "Tester tools checksum mismatch. Expected $expectedHash, got $actualHash."
        }
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $archivePaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $zip.Entries) {
            if (-not [string]::IsNullOrWhiteSpace($entry.FullName)) {
                $trimmedPath = $entry.FullName.TrimEnd("/", "\")
                if ([string]::IsNullOrWhiteSpace($trimmedPath)) {
                    continue
                }
                $relativePath = ConvertTo-SafeRelativePath -Path $trimmedPath
                if (-not $archivePaths.Add($relativePath)) {
                    throw "Tester tools archive contains a duplicate path: $relativePath"
                }
            }
        }
    }
    finally {
        $zip.Dispose()
    }
    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot

    $manifestPath = Join-Path $extractRoot "fetcher-tester-tools.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Tester tools archive does not contain fetcher-tester-tools.json."
    }
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ([int]$manifest.schemaVersion -ne 1 -or
        [string]$manifest.channel -ne "fetcher-simulator-test" -or
        [string]$manifest.sourceCommit -notmatch "^[0-9a-fA-F]{40}$" -or
        $null -eq $manifest.PSObject.Properties["files"] -or
        @($manifest.files).Count -eq 0) {
        throw "Unsupported Fetcher tester tools manifest."
    }
    $manifestPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($record in @($manifest.files | ForEach-Object { $_ })) {
        $relativePath = ConvertTo-SafeRelativePath -Path ([string]$record.path)
        if ($relativePath.Equals("fetcher-tester-tools.json", [StringComparison]::OrdinalIgnoreCase) -or
            -not $manifestPaths.Add($relativePath)) {
            throw "Tester tools manifest contains a duplicate or reserved path: $relativePath"
        }
        $expectedHash = [string]$record.sha256
        if ([int64]$record.size -lt 0 -or $expectedHash -notmatch "^[0-9a-fA-F]{64}$") {
            throw "Tester tools manifest contains an invalid record: $relativePath"
        }
        $source = Join-Path $extractRoot $relativePath.Replace("/", "\")
        if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or
            (Get-Item -LiteralPath $source).Length -ne [int64]$record.size -or
            (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash.ToLowerInvariant()) {
            throw "Tester tool failed manifest validation: $relativePath"
        }
    }
    $payloadPaths = @(Get-ChildItem -LiteralPath $extractRoot -File -Recurse | ForEach-Object {
        $_.FullName.Substring($extractRoot.Length).TrimStart("\", "/").Replace("\", "/")
    } | Where-Object { -not $_.Equals("fetcher-tester-tools.json", [StringComparison]::OrdinalIgnoreCase) })
    if ($payloadPaths.Count -ne $manifestPaths.Count) {
        throw "Tester tools manifest does not cover the complete archive payload."
    }
    foreach ($payloadPath in $payloadPaths) {
        if (-not $manifestPaths.Contains($payloadPath)) {
            throw "Tester tools archive contains an unmanifested payload: $payloadPath"
        }
    }
    foreach ($record in @($manifest.files | ForEach-Object { $_ })) {
        $relativePath = ConvertTo-SafeRelativePath -Path ([string]$record.path)
        $source = Join-Path $extractRoot $relativePath.Replace("/", "\")
        $destination = Join-Path $root $relativePath.Replace("/", "\")
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $root "fetcher-tester-tools.json") -Force
}
finally {
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}

Write-Host "Fetcher tester tools installed to: $root"
if (-not $SkipClientModBundle) {
    $bundleInstaller = Join-Path $root "Install-Fetcher-Client-Mod-Bundle.ps1"
    if (-not (Test-Path -LiteralPath $bundleInstaller -PathType Leaf)) {
        throw "Fetcher client mod bundle installer was not found: $bundleInstaller"
    }
    $bundleParameters = @{
        InstallRoot = $root
        Repository = $ClientModBundleRepository
        ReleaseTag = $ClientModBundleReleaseTag
        AssetName = $ClientModBundleAssetName
        GitHubApiBaseUrl = $GitHubApiBaseUrl
        GitHubDownloadBaseUrl = $GitHubDownloadBaseUrl
    }
    if (-not [string]::IsNullOrWhiteSpace($ClientModBundleArchivePath)) {
        $bundleParameters.BundleArchivePath = $ClientModBundleArchivePath
    }
    & $bundleInstaller @bundleParameters
    if (-not $?) {
        throw "Fetcher client mod bundle installation failed."
    }
}
$modCompatibilityScript = Join-Path $root "Apply-Fetcher-ZHI-Compatibility.ps1"
if (Test-Path -LiteralPath $modCompatibilityScript -PathType Leaf) {
    & $modCompatibilityScript -InstallRoot $root
    if (-not $?) {
        throw "Fetcher client mod compatibility fixes failed."
    }
}
if (-not $SkipUpdater) {
    & (Join-Path $root "Update-Fetcher-Simulator.ps1") -InstallRoot $root
}
