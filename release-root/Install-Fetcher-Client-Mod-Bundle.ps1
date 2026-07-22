[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $InstallRoot,
    [string] $Repository = "Skooma-Breath/Fetcher-Updater",
    [string] $ReleaseTag = "openmw-client-mods-mp-clients",
    [string] $AssetName = "openmw-client-mods.zip",
    [string] $GitHubApiBaseUrl = "https://api.github.com",
    [string] $GitHubDownloadBaseUrl = "https://github.com",
    [string] $BundleArchivePath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$headers = @{ "User-Agent" = "Fetcher-Client-Mod-Bundle-Installer" }
$requiredPlugins = @(
    "surf_mesa_mw.omwaddon",
    "surf_utopia_mw.omwaddon",
    "surf_kitsune.omwaddon",
    "surf_kitsune.omwscripts",
    "surf_kitsune2.omwaddon",
    "mp_phase7_test.omwscripts"
)

function ConvertTo-SafeRelativePath {
    param([Parameter(Mandatory = $true)][string] $Path)

    if ([IO.Path]::IsPathRooted($Path)) {
        throw "Client mod bundle contains an absolute path: $Path"
    }
    $normalized = $Path.Replace("\", "/").TrimStart("/")
    $segments = @($normalized.Split("/", [StringSplitOptions]::RemoveEmptyEntries))
    if ($segments.Count -eq 0 -or $normalized.Contains(":")) {
        throw "Client mod bundle contains an invalid path: $Path"
    }
    foreach ($segment in $segments) {
        if ($segment -eq "." -or $segment -eq "..") {
            throw "Client mod bundle path escapes the install root: $Path"
        }
    }
    return ($segments -join "/")
}

$root = (Resolve-Path -LiteralPath $InstallRoot).Path.TrimEnd("\", "/")
if (-not (Test-Path -LiteralPath (Join-Path $root "openmw.exe") -PathType Leaf)) {
    throw "The selected folder does not contain openmw.exe: $root"
}

$workRoot = Join-Path ([IO.Path]::GetTempPath()) ("fetcher-client-mods-" + [Guid]::NewGuid().ToString("N"))
$archivePath = Join-Path $workRoot $AssetName
$extractRoot = Join-Path $workRoot "extract"
try {
    New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
    $expectedHash = ""
    $assetDigest = ""
    if (-not [string]::IsNullOrWhiteSpace($BundleArchivePath)) {
        Copy-Item -LiteralPath $BundleArchivePath -Destination $archivePath -Force
        $expectedHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        $assetDigest = "sha256:$expectedHash"
    }
    else {
        $encodedTag = [Uri]::EscapeDataString($ReleaseTag)
        $releaseUrl = "$($GitHubApiBaseUrl.TrimEnd('/'))/repos/$Repository/releases/tags/$encodedTag"
        $release = Invoke-RestMethod -UseBasicParsing -Uri $releaseUrl -Headers $headers
        $assets = @($release.assets | Where-Object { [string]$_.name -eq $AssetName })
        if ($assets.Count -ne 1 -or [string]$assets[0].digest -notmatch "^sha256:([0-9a-fA-F]{64})$") {
            throw "The $ReleaseTag release does not contain one digest-backed $AssetName asset."
        }
        $expectedHash = $Matches[1].ToLowerInvariant()
        $assetDigest = "sha256:$expectedHash"
        $downloadUrl = "$($GitHubDownloadBaseUrl.TrimEnd('/'))/$Repository/releases/download/$encodedTag/$([Uri]::EscapeDataString($AssetName))"
        Write-Host "Downloading Fetcher client mod bundle..."
        Write-Host "  $downloadUrl"
        Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -Headers $headers -OutFile $archivePath
        $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "Client mod bundle checksum mismatch. Expected $expectedHash, got $actualHash."
        }
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $archivePaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $zip.Entries) {
            $trimmedPath = $entry.FullName.TrimEnd("/", "\")
            if ([string]::IsNullOrWhiteSpace($trimmedPath)) {
                continue
            }
            $relativePath = ConvertTo-SafeRelativePath -Path $trimmedPath
            if (-not $archivePaths.Add($relativePath)) {
                throw "Client mod bundle contains a duplicate path: $relativePath"
            }
            if (-not $relativePath.Equals("openmw-client-package.json", [StringComparison]::OrdinalIgnoreCase) -and
                -not $relativePath.StartsWith("Data Files/", [StringComparison]::OrdinalIgnoreCase) -and
                -not $relativePath.Equals("Data Files", [StringComparison]::OrdinalIgnoreCase)) {
                throw "Client mod bundle contains an unexpected path: $relativePath"
            }
        }
    }
    finally {
        $zip.Dispose()
    }

    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot
    $manifestPath = Join-Path $extractRoot "openmw-client-package.json"
    $dataRoot = Join-Path $extractRoot "Data Files"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $dataRoot -PathType Container)) {
        throw "Client mod bundle is missing its package manifest or Data Files directory."
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    foreach ($plugin in $requiredPlugins) {
        if (@($manifest.content) -notcontains $plugin -or
            -not (Test-Path -LiteralPath (Join-Path $dataRoot $plugin) -PathType Leaf)) {
            throw "Client mod bundle is missing required Fetcher content: $plugin"
        }
    }

    $records = New-Object System.Collections.Generic.List[object]
    foreach ($source in Get-ChildItem -LiteralPath $dataRoot -File -Recurse) {
        $relativeDataPath = $source.FullName.Substring($dataRoot.Length).TrimStart("\", "/")
        $destination = Join-Path (Join-Path $root "Data Files") $relativeDataPath
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source.FullName -Destination $destination -Force
        $installedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        $sourceHash = (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($installedHash -ne $sourceHash) {
            throw "Installed client mod file failed verification: $relativeDataPath"
        }
        $records.Add([ordered]@{
            path = ("Data Files/" + $relativeDataPath.Replace("\", "/"))
            size = [int64]$source.Length
            sha256 = $sourceHash
        })
    }

    $receiptRoot = Join-Path $root "_fetcher_update"
    $receiptPath = Join-Path $receiptRoot "client-mod-bundle.json"
    New-Item -ItemType Directory -Force -Path $receiptRoot | Out-Null
    [ordered]@{
        schemaVersion = 1
        repository = $Repository
        releaseTag = $ReleaseTag
        assetName = $AssetName
        assetDigest = $assetDigest
        installedAtUtc = [DateTime]::UtcNow.ToString("o")
        files = @($records | ForEach-Object { $_ })
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
}
finally {
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}

Write-Host "Fetcher client mod bundle installed to: $root"
