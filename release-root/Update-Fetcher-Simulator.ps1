[CmdletBinding()]
param(
    [string] $InstallRoot = "",
    [Alias("Repository")]
    [string] $ClientRepository = "Fetcher-Simulator/Fetcher-Simulator",
    [string] $TesterToolsRepository = "Skooma-Breath/Fetcher-Updater",
    [string] $GitHubApiBaseUrl = "https://api.github.com",
    [string] $GitHubDownloadBaseUrl = "https://github.com",
    [string] $ClientReleaseTag = "Fetcher-Simulator",
    [string] $ClientAssetName = "fetcher-simulator.zip",
    [string] $ClientChannelPath = "",
    [string] $TesterToolsReleaseTag = "fetcher-tester-tools",
    [string] $TesterToolsAssetName = "fetcher-tester-tools.zip",
    [string] $TesterToolsArchivePath = "",
    [string] $ClientModBundleReleaseTag = "openmw-client-mods-mp-clients",
    [string] $ClientModBundleAssetName = "openmw-client-mods.zip",
    [string] $ClientModBundleArchivePath = "",
    [string] $PatchCatalogPath = "",
    [string] $UmoInstallerPath = "",
    [switch] $SkipClientUpdate,
    [switch] $SkipTesterToolsUpdate,
    [switch] $SkipClientModBundle,
    [switch] $SkipUmoMods,
    [switch] $SkipModPatches,
    [switch] $QuickCheck,
    [switch] $StatusOnly,
    [ValidateRange(1, 168)]
    [int] $QuickCheckUmoMaxAgeHours = 24
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$headers = @{ "User-Agent" = "Fetcher-Simulator-Updater" }
$clientRepositoryWasBound = $PSBoundParameters.ContainsKey("ClientRepository")
$clientReleaseTagWasBound = $PSBoundParameters.ContainsKey("ClientReleaseTag")
$clientAssetNameWasBound = $PSBoundParameters.ContainsKey("ClientAssetName")

function Apply-ClientChannelConfiguration {
    param([Parameter(Mandatory = $true)][string] $Root)

    $path = $ClientChannelPath
    if ([string]::IsNullOrWhiteSpace($path)) {
        $path = Join-Path $Root "fetcher-update-channel.json"
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Write-Host "Fetcher client channel: default ($ClientReleaseTag)"
        return
    }

    $configuration = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    $requiredProperties = @("channel", "clientRepository", "clientReleaseTag", "clientAssetName")
    if ([int]$configuration.schemaVersion -ne 1) {
        throw "Unsupported Fetcher update channel schema: $($configuration.schemaVersion)"
    }
    foreach ($propertyName in $requiredProperties) {
        $property = $configuration.PSObject.Properties[$propertyName]
        if ($null -eq $property -or [string]::IsNullOrWhiteSpace([string]$property.Value)) {
            throw "Fetcher update channel is missing required property: $propertyName"
        }
    }

    if (-not $clientRepositoryWasBound) {
        $script:ClientRepository = [string]$configuration.clientRepository
    }
    if (-not $clientReleaseTagWasBound) {
        $script:ClientReleaseTag = [string]$configuration.clientReleaseTag
    }
    if (-not $clientAssetNameWasBound) {
        $script:ClientAssetName = [string]$configuration.clientAssetName
    }

    Write-Host "Fetcher client channel: $([string]$configuration.channel)"
    Write-Host "  repository: $ClientRepository"
    Write-Host "  release tag: $ClientReleaseTag"
    Write-Host "  asset: $ClientAssetName"
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string] $Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Invoke-WithRetry {
    param(
        [Parameter(Mandatory = $true)][scriptblock] $Action,
        [Parameter(Mandatory = $true)][string] $Description,
        [int] $Attempts = 3
    )

    for ($attempt = 1; $attempt -le $Attempts; ++$attempt) {
        try {
            return & $Action
        }
        catch {
            if ($attempt -eq $Attempts) {
                throw
            }
            Write-Warning "$Description failed (attempt $attempt of $Attempts): $($_.Exception.Message)"
            Start-Sleep -Seconds (2 * $attempt)
        }
    }
}

function ConvertTo-NormalizedRelativePath {
    param([Parameter(Mandatory = $true)][string] $RelativePath)

    if ([IO.Path]::IsPathRooted($RelativePath)) {
        throw "Update manifest contains an absolute path: $RelativePath"
    }

    $path = $RelativePath.Replace("\", "/").TrimStart("/")
    $segments = @($path.Split("/", [StringSplitOptions]::RemoveEmptyEntries))
    if ($segments.Count -eq 0 -or $path.Contains(":")) {
        throw "Update manifest contains an invalid path: $RelativePath"
    }
    foreach ($segment in $segments) {
        if ($segment -eq "." -or $segment -eq "..") {
            throw "Update manifest path escapes the install root: $RelativePath"
        }
    }
    return ($segments -join "/")
}

function Read-ClientProtectionPolicy {
    param([Parameter(Mandatory = $true)][string] $Path)

    $policy = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ([int]$policy.schemaVersion -ne 1 -or
        $null -eq $policy.PSObject.Properties["exactPaths"] -or
        $null -eq $policy.PSObject.Properties["prefixes"] -or
        $null -eq $policy.PSObject.Properties["suffixes"]) {
        throw "Unsupported Fetcher client protection policy: $Path"
    }
    return $policy
}


function Test-FetcherMutablePath {
    param([Parameter(Mandatory = $true)][string] $RelativePath)

    $path = $RelativePath.Replace("\", "/").TrimStart("/").ToLowerInvariant()
    if (@($clientProtectionPolicy.exactPaths) -contains $path) {
        return $true
    }
    foreach ($prefix in @($clientProtectionPolicy.prefixes)) {
        if ($path.StartsWith(([string]$prefix).ToLowerInvariant(), [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    foreach ($suffix in @($clientProtectionPolicy.suffixes)) {
        if ($path.EndsWith(([string]$suffix).ToLowerInvariant(), [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Assert-SafeArchivePaths {
    param([Parameter(Mandatory = $true)][string] $ArchivePath)

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        foreach ($entry in $archive.Entries) {
            if ([string]::IsNullOrWhiteSpace($entry.FullName)) {
                continue
            }
            [void](ConvertTo-NormalizedRelativePath -RelativePath $entry.FullName.TrimEnd("/", "\"))
        }
    }
    finally {
        $archive.Dispose()
    }
}

function Get-GitHubRelease {
    param(
        [Parameter(Mandatory = $true)][string] $Tag,
        [string] $ReleaseRepository = $ClientRepository
    )

    $encodedTag = [Uri]::EscapeDataString($Tag)
    $url = "$($GitHubApiBaseUrl.TrimEnd('/'))/repos/$ReleaseRepository/releases/tags/$encodedTag"
    $response = Invoke-WithRetry -Description "Reading GitHub release $Tag" -Action {
        Invoke-RestMethod -UseBasicParsing -Uri $url -Headers $headers
    }
    if ($response -is [string]) {
        return $response | ConvertFrom-Json
    }
    return $response
}

function Get-ReleaseAsset {
    param(
        [Parameter(Mandatory = $true)] $Release,
        [Parameter(Mandatory = $true)][string] $AssetName,
        [Parameter(Mandatory = $true)][string] $ReleaseTag,
        [string] $ReleaseRepository = $ClientRepository
    )

    $assetMatches = @($Release.assets | Where-Object { [string]$_.name -eq $AssetName })
    if ($assetMatches.Count -ne 1) {
        throw "Expected one release asset named $AssetName, found $($assetMatches.Count)."
    }

    $digest = [string]$assetMatches[0].digest
    if ($digest -notmatch "^sha256:([0-9a-fA-F]{64})$") {
        throw "GitHub did not provide a SHA-256 digest for $AssetName."
    }

    return [pscustomobject]@{
        Name = $AssetName
        Url = "$($GitHubDownloadBaseUrl.TrimEnd('/'))/$ReleaseRepository/releases/download/$([Uri]::EscapeDataString($ReleaseTag))/$([Uri]::EscapeDataString($AssetName))"
        Sha256 = $Matches[1].ToLowerInvariant()
        Digest = $digest.ToLowerInvariant()
        Size = [int64]$assetMatches[0].size
    }
}

function Resolve-ReleaseCommit {
    param(
        [Parameter(Mandatory = $true)] $Release,
        [Parameter(Mandatory = $true)][string] $Tag,
        [string] $ReleaseRepository = $ClientRepository
    )

    $target = [string]$Release.target_commitish
    if ($target -match "^[0-9a-fA-F]{40}$") {
        return $target.ToLowerInvariant()
    }

    $encodedTag = [Uri]::EscapeDataString($Tag)
    $referenceUrl = "$($GitHubApiBaseUrl.TrimEnd('/'))/repos/$ReleaseRepository/git/ref/tags/$encodedTag"
    $reference = Invoke-WithRetry -Description "Resolving GitHub tag $Tag" -Action {
        Invoke-RestMethod -UseBasicParsing -Uri $referenceUrl -Headers $headers
    }
    if ([string]$reference.object.type -eq "commit") {
        return ([string]$reference.object.sha).ToLowerInvariant()
    }
    if ([string]$reference.object.type -eq "tag") {
        $tagUrl = "$($GitHubApiBaseUrl.TrimEnd('/'))/repos/$ReleaseRepository/git/tags/$($reference.object.sha)"
        $tagObject = Invoke-WithRetry -Description "Resolving annotated GitHub tag $Tag" -Action {
            Invoke-RestMethod -UseBasicParsing -Uri $tagUrl -Headers $headers
        }
        if ([string]$tagObject.object.type -eq "commit") {
            return ([string]$tagObject.object.sha).ToLowerInvariant()
        }
    }

    throw "Could not resolve release tag $Tag to a Git commit."
}

function Get-InstalledClientCommit {
    param([Parameter(Mandatory = $true)][string] $Root)

    $ciIdPath = Join-Path $Root "CI-ID.txt"
    if (-not (Test-Path -LiteralPath $ciIdPath -PathType Leaf)) {
        return $null
    }
    foreach ($line in Get-Content -LiteralPath $ciIdPath) {
        if ($line -match "^Commit\s+([0-9a-fA-F]{40})\s*$") {
            return $Matches[1].ToLowerInvariant()
        }
    }
    return $null
}

function Assert-OpenMwStopped {
    param([Parameter(Mandatory = $true)][string] $Root)

    $normalizedRoot = [IO.Path]::GetFullPath($Root).TrimEnd("\") + "\"
    $running = New-Object System.Collections.Generic.List[string]
    foreach ($processName in @("openmw", "openmw-launcher", "openmw-cs", "openmw-server")) {
        foreach ($process in Get-Process -Name $processName -ErrorAction SilentlyContinue) {
            try {
                $processPath = [IO.Path]::GetFullPath([string]$process.Path)
                if ($processPath.StartsWith($normalizedRoot, [StringComparison]::OrdinalIgnoreCase)) {
                    $running.Add("$($process.ProcessName) (PID $($process.Id))")
                }
            }
            catch {
                # Processes outside this portable install do not block its update.
            }
        }
    }
    if ($running.Count -gt 0) {
        throw "Close Fetcher Simulator before updating: $($running -join ', ')."
    }
}

function Read-ClientInventory {
    param([Parameter(Mandatory = $true)][string] $Path)

    $inventory = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ([int]$inventory.schemaVersion -ne 1 -or
        [int]$inventory.protectionPolicyVersion -ne [int]$clientProtectionPolicy.schemaVersion -or
        [string]$inventory.clientCommit -notmatch "^[0-9a-fA-F]{40}$" -or
        $null -eq $inventory.PSObject.Properties["files"] -or
        @($inventory.files).Count -eq 0) {
        throw "Unsupported Fetcher client inventory schema: $($inventory.schemaVersion)"
    }
    return $inventory
}

function Get-InstalledClientInventoryCommit {
    param([Parameter(Mandatory = $true)][string] $Root)

    $path = Join-Path $Root "fetcher-client-files.json"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return ""
    }
    try {
        $inventory = Read-ClientInventory -Path $path
        $paths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($record in @($inventory.files)) {
            $relativePath = ConvertTo-NormalizedRelativePath -RelativePath ([string]$record.path)
            if ((Test-FetcherMutablePath -RelativePath $relativePath) -or
                (-not $paths.Add($relativePath)) -or
                [int64]$record.size -lt 0 -or
                [string]$record.sha256 -notmatch "^[0-9a-fA-F]{64}$") {
                return ""
            }
        }
        return ([string]$inventory.clientCommit).ToLowerInvariant()
    }
    catch {
        Write-Warning "Installed client inventory is invalid: $($_.Exception.Message)"
        return ""
    }
}

function Install-ClientArchive {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)] $Asset,
        [Parameter(Mandatory = $true)][string] $RemoteCommit,
        [Parameter(Mandatory = $true)][string] $RunWorkRoot
    )

    Assert-OpenMwStopped -Root $Root
    $archivePath = Join-Path $RunWorkRoot $Asset.Name
    $extractRoot = Join-Path $RunWorkRoot "client"
    Write-Host "Downloading Fetcher Simulator client update..."
    Write-Host "  $($Asset.Url)"
    Invoke-WithRetry -Description "Downloading $($Asset.Name)" -Action {
        Invoke-WebRequest -UseBasicParsing -Uri $Asset.Url -Headers $headers -OutFile $archivePath
    } | Out-Null

    $archiveHash = Get-Sha256 -Path $archivePath
    if ($archiveHash -ne $Asset.Sha256) {
        throw "Client archive checksum mismatch. Expected $($Asset.Sha256), got $archiveHash."
    }
    Assert-SafeArchivePaths -ArchivePath $archivePath
    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot

    $newInventoryPath = Join-Path $extractRoot "fetcher-client-files.json"
    if (-not (Test-Path -LiteralPath $newInventoryPath -PathType Leaf)) {
        throw "The client archive does not contain fetcher-client-files.json."
    }
    $newInventory = Read-ClientInventory -Path $newInventoryPath
    if ([string]$newInventory.clientCommit -ne $RemoteCommit) {
        throw "Client inventory commit $($newInventory.clientCommit) does not match release commit $RemoteCommit."
    }

    $newFiles = @{}
    foreach ($record in @($newInventory.files)) {
        $relativePath = ConvertTo-NormalizedRelativePath -RelativePath ([string]$record.path)
        if (Test-FetcherMutablePath -RelativePath $relativePath) {
            throw "Client inventory attempts to manage protected path: $relativePath"
        }
        if ($newFiles.ContainsKey($relativePath)) {
            throw "Client inventory contains duplicate path: $relativePath"
        }
        $sourcePath = Join-Path $extractRoot $relativePath.Replace("/", "\")
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            throw "Client archive is missing managed file: $relativePath"
        }
        if ((Get-Item -LiteralPath $sourcePath).Length -ne [int64]$record.size -or
            (Get-Sha256 -Path $sourcePath) -ne ([string]$record.sha256).ToLowerInvariant()) {
            throw "Client archive file failed inventory verification: $relativePath"
        }
        $newFiles[$relativePath] = $record
    }

    $oldFiles = @{}
    $installedInventoryPath = Join-Path $Root "fetcher-client-files.json"
    if (Test-Path -LiteralPath $installedInventoryPath -PathType Leaf) {
        $oldInventory = Read-ClientInventory -Path $installedInventoryPath
        foreach ($record in @($oldInventory.files)) {
            $relativePath = ConvertTo-NormalizedRelativePath -RelativePath ([string]$record.path)
            if (-not (Test-FetcherMutablePath -RelativePath $relativePath)) {
                $oldFiles[$relativePath] = $record
            }
        }
    }

    $rollbackRoot = Join-Path $RunWorkRoot "rollback"
    $changes = New-Object System.Collections.Generic.List[object]
    try {
        foreach ($relativePath in @($newFiles.Keys | Sort-Object)) {
            $record = $newFiles[$relativePath]
            $sourcePath = Join-Path $extractRoot $relativePath.Replace("/", "\")
            $destinationPath = Join-Path $Root $relativePath.Replace("/", "\")
            if ((Test-Path -LiteralPath $destinationPath -PathType Leaf) -and
                (Get-Sha256 -Path $destinationPath) -eq ([string]$record.sha256).ToLowerInvariant()) {
                continue
            }

            $existed = Test-Path -LiteralPath $destinationPath -PathType Leaf
            $backupPath = Join-Path $rollbackRoot $relativePath.Replace("/", "\")
            if ($existed) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backupPath) | Out-Null
                Copy-Item -LiteralPath $destinationPath -Destination $backupPath -Force
            }
            $changes.Add([pscustomobject]@{ Destination = $destinationPath; Backup = $backupPath; Existed = $existed })
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destinationPath) | Out-Null
            Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
            if ((Get-Sha256 -Path $destinationPath) -ne ([string]$record.sha256).ToLowerInvariant()) {
                throw "Installed client file failed verification: $relativePath"
            }
        }

        foreach ($relativePath in @($oldFiles.Keys | Where-Object { -not $newFiles.ContainsKey($_) } | Sort-Object)) {
            if (Test-FetcherMutablePath -RelativePath $relativePath) {
                continue
            }
            $destinationPath = Join-Path $Root $relativePath.Replace("/", "\")
            if (-not (Test-Path -LiteralPath $destinationPath -PathType Leaf)) {
                continue
            }
            $backupPath = Join-Path $rollbackRoot $relativePath.Replace("/", "\")
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backupPath) | Out-Null
            Copy-Item -LiteralPath $destinationPath -Destination $backupPath -Force
            $changes.Add([pscustomobject]@{ Destination = $destinationPath; Backup = $backupPath; Existed = $true })
            Remove-Item -LiteralPath $destinationPath -Force
        }

        $inventoryBackup = Join-Path $rollbackRoot "fetcher-client-files.json"
        $inventoryExisted = Test-Path -LiteralPath $installedInventoryPath -PathType Leaf
        if ($inventoryExisted) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $inventoryBackup) | Out-Null
            Copy-Item -LiteralPath $installedInventoryPath -Destination $inventoryBackup -Force
        }
        $changes.Add([pscustomobject]@{ Destination = $installedInventoryPath; Backup = $inventoryBackup; Existed = $inventoryExisted })
        Copy-Item -LiteralPath $newInventoryPath -Destination $installedInventoryPath -Force
    }
    catch {
        for ($index = $changes.Count - 1; $index -ge 0; --$index) {
            $change = $changes[$index]
            if ($change.Existed -and (Test-Path -LiteralPath $change.Backup -PathType Leaf)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $change.Destination) | Out-Null
                Copy-Item -LiteralPath $change.Backup -Destination $change.Destination -Force
            }
            elseif (-not $change.Existed -and (Test-Path -LiteralPath $change.Destination -PathType Leaf)) {
                Remove-Item -LiteralPath $change.Destination -Force
            }
        }
        throw
    }

    Write-Host "Fetcher Simulator client updated to commit $RemoteCommit."
}

function Resolve-ConfiguredDataPath {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $Value
    )

    $path = $Value.Trim()
    if ($path.Length -ge 2 -and $path.StartsWith('"') -and $path.EndsWith('"')) {
        $path = $path.Substring(1, $path.Length - 2)
    }
    if (-not [IO.Path]::IsPathRooted($path)) {
        $path = Join-Path $Root $path
    }
    if (Test-Path -LiteralPath $path -PathType Container) {
        return (Resolve-Path -LiteralPath $path).Path
    }
    return $null
}

function Find-OpenMwPluginDataRoot {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $Plugin,
        [Parameter(Mandatory = $true)][string] $RequiredSubdirectory
    )

    $candidates = New-Object System.Collections.Generic.List[string]
    $seen = New-Object System.Collections.Generic.HashSet[string]([StringComparer]::OrdinalIgnoreCase)
    $managedRoot = [IO.Path]::GetFullPath((Join-Path $Root "Data Files\fetcher-bardcraft")).TrimEnd("\", "/") + "\"

    function Select-ManagedCandidate {
        param([Parameter(Mandatory = $true)] $Paths)
        $managed = @($Paths | Where-Object {
            [IO.Path]::GetFullPath([string]$_).StartsWith($managedRoot, [StringComparison]::OrdinalIgnoreCase)
        })
        if ($managed.Count -eq 1) {
            return $managed[0]
        }
        return $null
    }

    $configPath = Join-Path $Root "openmw.cfg"
    if (Test-Path -LiteralPath $configPath -PathType Leaf) {
        foreach ($line in Get-Content -LiteralPath $configPath) {
            if ($line -notmatch "^\s*data\s*=\s*(.+?)\s*$") {
                continue
            }
            $dataPath = Resolve-ConfiguredDataPath -Root $Root -Value $Matches[1]
            if ($null -ne $dataPath -and
                (Test-Path -LiteralPath (Join-Path $dataPath $Plugin) -PathType Leaf) -and
                (Test-Path -LiteralPath (Join-Path $dataPath $RequiredSubdirectory) -PathType Container) -and
                $seen.Add($dataPath)) {
                $candidates.Add($dataPath)
            }
        }
    }

    if ($candidates.Count -eq 1) {
        return $candidates[0]
    }
    if ($candidates.Count -gt 1) {
        $managedCandidate = Select-ManagedCandidate -Paths $candidates
        if ($null -ne $managedCandidate) {
            Write-Host "Using updater-managed $Plugin installation:"
            Write-Host "  $managedCandidate"
            return $managedCandidate
        }
        throw "Multiple active data paths contain $Plugin. Remove duplicate data= entries before updating."
    }

    $dataFilesRoot = Join-Path $Root "Data Files"
    if (Test-Path -LiteralPath $dataFilesRoot -PathType Container) {
        foreach ($pluginFile in Get-ChildItem -LiteralPath $dataFilesRoot -Recurse -Force -File -Filter $Plugin -ErrorAction SilentlyContinue) {
            $dataPath = $pluginFile.Directory.FullName
            if ((Test-Path -LiteralPath (Join-Path $dataPath $RequiredSubdirectory) -PathType Container) -and $seen.Add($dataPath)) {
                $candidates.Add($dataPath)
            }
        }
    }

    if ($candidates.Count -eq 1) {
        return $candidates[0]
    }
    if ($candidates.Count -gt 1) {
        $managedCandidate = Select-ManagedCandidate -Paths $candidates
        if ($null -ne $managedCandidate) {
            return $managedCandidate
        }
        throw "Found multiple installations of $Plugin. Add the intended folder to openmw.cfg and remove stale duplicate data= entries."
    }
    return $null
}

function Get-OptionalStringProperty {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name
    )

    if ($null -eq $Object) {
        return ""
    }
    if ($Object -is [Collections.IDictionary]) {
        if ($Object.Contains($Name) -and $null -ne $Object[$Name]) {
            return [string]$Object[$Name]
        }
        return ""
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return ""
    }
    return [string]$property.Value
}

function Assert-ClientModPatchDefinition {
    param([Parameter(Mandatory = $true)] $Patch)

    foreach ($name in @(
        "id", "name", "repository", "releaseTag", "assetName", "targetPlugin",
        "requiredSubdirectory", "applierPattern", "targetParameter"
    )) {
        if ([string]::IsNullOrWhiteSpace((Get-OptionalStringProperty -Object $Patch -Name $name))) {
            throw "Client patch definition is missing required property: $name"
        }
    }
    $repository = Get-OptionalStringProperty -Object $Patch -Name "repository"
    if ($repository -notmatch "^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$") {
        throw "Client patch definition contains an invalid repository: $repository"
    }
    foreach ($name in @("assetName", "targetPlugin", "applierPattern")) {
        $value = Get-OptionalStringProperty -Object $Patch -Name $name
        if ($value -ne [IO.Path]::GetFileName($value) -or $value.Contains("..")) {
            throw "Client patch definition contains an invalid $($name): $value"
        }
    }
    $markerPath = Get-OptionalStringProperty -Object $Patch -Name "markerPath"
    $markerFile = Get-OptionalStringProperty -Object $Patch -Name "markerFile"
    if ([string]::IsNullOrWhiteSpace($markerPath) -and [string]::IsNullOrWhiteSpace($markerFile)) {
        throw "Client patch definition must declare markerPath or markerFile: $($Patch.id)"
    }
    foreach ($path in @($markerPath, $markerFile) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) {
        [void](ConvertTo-NormalizedRelativePath -RelativePath $path)
    }
}

function Resolve-ClientModPatchMarkerPath {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $TargetRoot,
        [Parameter(Mandatory = $true)] $Patch
    )

    $configuredPath = Get-OptionalStringProperty -Object $Patch -Name "markerPath"
    if (-not [string]::IsNullOrWhiteSpace($configuredPath)) {
        $relativePath = ConvertTo-NormalizedRelativePath -RelativePath $configuredPath
        return [IO.Path]::GetFullPath((Join-Path $Root $relativePath.Replace("/", "\")))
    }

    $markerFile = Get-OptionalStringProperty -Object $Patch -Name "markerFile"
    if ([string]::IsNullOrWhiteSpace($markerFile)) {
        return $null
    }
    $relativeFile = ConvertTo-NormalizedRelativePath -RelativePath $markerFile
    return [IO.Path]::GetFullPath((Join-Path $TargetRoot $relativeFile.Replace("/", "\")))
}

function Test-ClientModPatchCurrent {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)] $Patch,
        [Parameter(Mandatory = $true)][string] $TargetRoot,
        [Parameter(Mandatory = $true)] $Asset,
        [Parameter(Mandatory = $true)][hashtable] $PatchStates
    )

    $patchId = [string]$Patch.id
    if (-not $PatchStates.ContainsKey($patchId)) {
        return $false
    }
    $state = $PatchStates[$patchId]
    $knownDigest = Get-OptionalStringProperty -Object $state -Name "assetDigest"
    if ([string]::IsNullOrWhiteSpace($knownDigest) -or
        -not $knownDigest.Equals([string]$Asset.Digest, [StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }

    $knownTarget = Get-OptionalStringProperty -Object $state -Name "target"
    if ([string]::IsNullOrWhiteSpace($knownTarget)) {
        return $false
    }
    $resolvedTarget = [IO.Path]::GetFullPath($TargetRoot).TrimEnd("\", "/")
    $resolvedKnownTarget = [IO.Path]::GetFullPath($knownTarget).TrimEnd("\", "/")
    if (-not $resolvedTarget.Equals($resolvedKnownTarget, [StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }

    $markerPath = Resolve-ClientModPatchMarkerPath -Root $Root -TargetRoot $TargetRoot -Patch $Patch
    if ([string]::IsNullOrWhiteSpace($markerPath) -or
        -not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
        return $false
    }
    $knownManifestHash = Get-OptionalStringProperty -Object $state -Name "manifestSha256"
    if ([string]::IsNullOrWhiteSpace($knownManifestHash)) {
        return $false
    }

    try {
        $marker = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json
    }
    catch {
        return $false
    }
    $recordedManifestHash = Get-OptionalStringProperty -Object $marker -Name "manifestSha256"
    if (-not [string]::IsNullOrWhiteSpace($recordedManifestHash)) {
        if (-not $recordedManifestHash.Equals($knownManifestHash, [StringComparison]::OrdinalIgnoreCase)) {
            return $false
        }
    }
    elseif (-not (Get-Sha256 -Path $markerPath).Equals($knownManifestHash, [StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }
    $knownVersion = Get-OptionalStringProperty -Object $state -Name "patchVersion"
    $installedVersion = Get-OptionalStringProperty -Object $marker -Name "patchVersion"
    if ([string]::IsNullOrWhiteSpace($knownVersion) -or
        [string]::IsNullOrWhiteSpace($installedVersion) -or
        -not $knownVersion.Equals($installedVersion, [StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }
    return $true
}

function Install-ClientModPatch {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)] $Patch,
        [Parameter(Mandatory = $true)][string] $UpdateRoot,
        [Parameter(Mandatory = $true)][string] $RunWorkRoot,
        [Parameter(Mandatory = $true)][hashtable] $PatchStates
    )

    $targetRoot = Find-OpenMwPluginDataRoot -Root $Root -Plugin ([string]$Patch.targetPlugin) `
        -RequiredSubdirectory ([string]$Patch.requiredSubdirectory)
    if ($null -eq $targetRoot) {
        Write-Warning "$($Patch.name) was not applied because $($Patch.targetPlugin) is not installed."
        return
    }

    $patchRepository = Get-OptionalStringProperty -Object $Patch -Name "repository"
    if ([string]::IsNullOrWhiteSpace($patchRepository)) {
        throw "Patch $($Patch.id) does not declare its release repository."
    }

    $release = Get-GitHubRelease -Tag ([string]$Patch.releaseTag) -ReleaseRepository $patchRepository
    $asset = Get-ReleaseAsset -Release $release -AssetName ([string]$Patch.assetName) `
        -ReleaseTag ([string]$Patch.releaseTag) -ReleaseRepository $patchRepository
    if (Test-ClientModPatchCurrent -Root $Root -Patch $Patch -TargetRoot $targetRoot `
        -Asset $asset -PatchStates $PatchStates) {
        $currentState = $PatchStates[[string]$Patch.id]
        Write-Host "$($Patch.name) is current at patch $(Get-OptionalStringProperty -Object $currentState -Name 'patchVersion')."
        Write-Host "  $targetRoot"
        return
    }

    $cacheRoot = Join-Path $UpdateRoot ("patches\{0}" -f [string]$Patch.id)
    New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
    $archivePath = Join-Path $cacheRoot ("{0}-{1}.zip" -f [string]$Patch.id, $asset.Sha256)
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf) -or (Get-Sha256 -Path $archivePath) -ne $asset.Sha256) {
        Write-Host "Downloading $($Patch.name)..."
        Write-Host "  $($asset.Url)"
        Invoke-WithRetry -Description "Downloading $($Patch.assetName)" -Action {
            Invoke-WebRequest -UseBasicParsing -Uri $asset.Url -Headers $headers -OutFile $archivePath
        } | Out-Null
    }
    if ((Get-Sha256 -Path $archivePath) -ne $asset.Sha256) {
        throw "$($Patch.name) archive checksum verification failed."
    }

    Assert-SafeArchivePaths -ArchivePath $archivePath
    $extractRoot = Join-Path $RunWorkRoot ("patch-{0}" -f [string]$Patch.id)
    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot
    # Patch archives keep their applier at the archive root. Do not recurse into
    # large payload trees here: cloud-synced installs can change those temporary
    # directories while Get-ChildItem is enumerating them.
    $appliers = @(Get-ChildItem -LiteralPath $extractRoot -File -Filter ([string]$Patch.applierPattern))
    if ($appliers.Count -ne 1) {
        throw "Expected one $($Patch.applierPattern) in $($Patch.assetName), found $($appliers.Count)."
    }

    $manifestName = "fetcher-bardcraft-mp-patch.json"
    $manifestNameProperty = $Patch.PSObject.Properties["manifestName"]
    if ($null -ne $manifestNameProperty -and -not [string]::IsNullOrWhiteSpace([string]$manifestNameProperty.Value)) {
        $manifestName = [string]$manifestNameProperty.Value
    }
    $manifestPath = Join-Path $extractRoot $manifestName
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "$($Patch.assetName) does not contain its patch manifest."
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $invokeParameters = @{}
    $invokeParameters[[string]$Patch.targetParameter] = $targetRoot
    $installRootProperty = $Patch.PSObject.Properties["installRootParameter"]
    if ($null -ne $installRootProperty -and
        -not [string]::IsNullOrWhiteSpace([string]$installRootProperty.Value)) {
        $invokeParameters[[string]$installRootProperty.Value] = $Root
    }
    Write-Host "Applying $($Patch.name) to:"
    Write-Host "  $targetRoot"
    & $appliers[0].FullName @invokeParameters

    $PatchStates[[string]$Patch.id] = [ordered]@{
        assetDigest = $asset.Digest
        patchVersion = [string]$manifest.patchVersion
        manifestSha256 = Get-Sha256 -Path $manifestPath
        target = $targetRoot
        updatedAtUtc = [DateTime]::UtcNow.ToString("o")
    }
}

function Install-UmoModList {
    param([Parameter(Mandatory = $true)][string] $Root)

    $installer = $UmoInstallerPath
    if ([string]::IsNullOrWhiteSpace($installer)) {
        $installer = Join-Path $Root "Install-Fetcher-Bardcraft-With-UMO.ps1"
    }
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
        throw "UMO mod installer was not found: $installer"
    }

    Assert-OpenMwStopped -Root $Root
    Write-Host "Checking required Fetcher mods and dependencies through UMO..."
    & $installer `
        -UmoBasePath (Join-Path $Root "Data Files") `
        -TesterToolsRepository $TesterToolsRepository `
        -TesterToolsReleaseTag $TesterToolsReleaseTag `
        -GitHubDownloadBaseUrl $GitHubDownloadBaseUrl `
        -ApplyBardcraftMultiplayerPatch $false `
        -ApplyPublicTestConfig $true
    if (-not $?) {
        throw "Fetcher UMO mod installation failed."
    }
}

function Get-UmoCheckState {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $StatePath
    )

    $modListPath = Join-Path $Root "fetcher-bardcraft-umo.json"
    if (-not (Test-Path -LiteralPath $modListPath -PathType Leaf)) {
        return [pscustomobject]@{
            IsCurrent = $false
            Reason = "the installed UMO modlist is missing"
            ModListPath = $modListPath
            ModListSha256 = ""
            CheckedAtUtc = $null
        }
    }

    $modListSha256 = Get-Sha256 -Path $modListPath
    if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) {
        $legacyStatePath = Join-Path $Root "fetcher-update-state.json"
        if (Test-Path -LiteralPath $legacyStatePath -PathType Leaf) {
            try {
                $legacyState = Get-Content -LiteralPath $legacyStatePath -Raw | ConvertFrom-Json
                $legacyCheckedAt = [DateTimeOffset]::Parse(
                    [string]$legacyState.checkedAtUtc,
                    [Globalization.CultureInfo]::InvariantCulture,
                    [Globalization.DateTimeStyles]::RoundtripKind)
                $legacyAge = [DateTimeOffset]::UtcNow - $legacyCheckedAt.ToUniversalTime()
                $modListWriteTime = [DateTimeOffset](Get-Item -LiteralPath $modListPath).LastWriteTimeUtc
                if ($legacyAge.TotalHours -le $QuickCheckUmoMaxAgeHours -and
                    $legacyAge.TotalSeconds -ge -300 -and
                    $modListWriteTime -le $legacyCheckedAt.ToUniversalTime().AddMinutes(5)) {
                    return [pscustomobject]@{
                        IsCurrent = $true
                        Reason = ""
                        ModListPath = $modListPath
                        ModListSha256 = $modListSha256
                        CheckedAtUtc = $legacyCheckedAt
                        NeedsMigration = $true
                    }
                }
            }
            catch {
                # Fall through and perform a real UMO check.
            }
        }

        return [pscustomobject]@{
            IsCurrent = $false
            Reason = "no previous successful UMO check is recorded"
            ModListPath = $modListPath
            ModListSha256 = $modListSha256
            CheckedAtUtc = $null
        }
    }

    try {
        $state = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
        if ([int]$state.schemaVersion -ne 1 -or
            [string]$state.modListSha256 -ne $modListSha256 -or
            [string]::IsNullOrWhiteSpace([string]$state.checkedAtUtc)) {
            return [pscustomobject]@{
                IsCurrent = $false
                Reason = "the UMO modlist changed or its state is unsupported"
                ModListPath = $modListPath
                ModListSha256 = $modListSha256
                CheckedAtUtc = $null
            }
        }

        $checkedAt = [DateTimeOffset]::Parse(
            [string]$state.checkedAtUtc,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind)
        $age = [DateTimeOffset]::UtcNow - $checkedAt.ToUniversalTime()
        if ($age.TotalHours -gt $QuickCheckUmoMaxAgeHours -or $age.TotalSeconds -lt -300) {
            return [pscustomobject]@{
                IsCurrent = $false
                Reason = "the previous successful UMO check is older than $QuickCheckUmoMaxAgeHours hours"
                ModListPath = $modListPath
                ModListSha256 = $modListSha256
                CheckedAtUtc = $checkedAt
            }
        }

        return [pscustomobject]@{
            IsCurrent = $true
            Reason = ""
            ModListPath = $modListPath
            ModListSha256 = $modListSha256
            CheckedAtUtc = $checkedAt
        }
    }
    catch {
        return [pscustomobject]@{
            IsCurrent = $false
            Reason = "the previous UMO check state is unreadable"
            ModListPath = $modListPath
            ModListSha256 = $modListSha256
            CheckedAtUtc = $null
        }
    }
}

function Write-UmoCheckState {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $ModListSha256
    )

    $temporaryPath = "$Path.$([Guid]::NewGuid().ToString('N')).tmp"
    [ordered]@{
        schemaVersion = 1
        modListSha256 = $ModListSha256
        checkedAtUtc = [DateTime]::UtcNow.ToString("o")
    } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $temporaryPath -Encoding UTF8
    Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
}

function Get-LauncherUpdateStatus {
    param([Parameter(Mandatory = $true)][string] $Root)

    Apply-ClientChannelConfiguration -Root $Root 6>$null

    $channel = "default"
    $channelPath = if ([string]::IsNullOrWhiteSpace($ClientChannelPath)) {
        Join-Path $Root "fetcher-update-channel.json"
    }
    else {
        $ClientChannelPath
    }
    if (Test-Path -LiteralPath $channelPath -PathType Leaf) {
        try {
            $channelConfiguration = Get-Content -LiteralPath $channelPath -Raw | ConvertFrom-Json
            if (-not [string]::IsNullOrWhiteSpace([string]$channelConfiguration.channel)) {
                $channel = [string]$channelConfiguration.channel
            }
        }
        catch {
            # The normal updater reports an invalid channel file in detail.
        }
    }

    $reasons = New-Object 'System.Collections.Generic.List[string]'

    $clientRelease = Get-GitHubRelease -Tag $ClientReleaseTag -ReleaseRepository $ClientRepository
    $clientAsset = Get-ReleaseAsset -Release $clientRelease -AssetName $ClientAssetName `
        -ReleaseTag $ClientReleaseTag -ReleaseRepository $ClientRepository
    $availableClientCommit = Resolve-ReleaseCommit -Release $clientRelease -Tag $ClientReleaseTag `
        -ReleaseRepository $ClientRepository
    $installedClientCommit = Get-InstalledClientCommit -Root $Root
    $installedInventoryCommit = Get-InstalledClientInventoryCommit -Root $Root
    $installedClientDigest = ""
    $updateState = $null
    $updateStatePath = Join-Path $Root "fetcher-update-state.json"
    if (Test-Path -LiteralPath $updateStatePath -PathType Leaf) {
        try {
            $updateState = Get-Content -LiteralPath $updateStatePath -Raw | ConvertFrom-Json
            if ($null -ne $updateState.client) {
                $installedClientDigest = [string]$updateState.client.assetDigest
            }
        }
        catch {
            # An unreadable state file makes the client status conservative below.
        }
    }
    $clientCurrent = $installedClientCommit -eq $availableClientCommit -and
        $installedInventoryCommit -eq $availableClientCommit -and
        $installedClientDigest -eq $clientAsset.Digest
    if (-not $clientCurrent) {
        $reasons.Add("Fetcher Simulator client")
    }

    $testerToolsRelease = Get-GitHubRelease -Tag $TesterToolsReleaseTag `
        -ReleaseRepository $TesterToolsRepository
    $availableTesterToolsCommit = Resolve-ReleaseCommit -Release $testerToolsRelease `
        -Tag $TesterToolsReleaseTag -ReleaseRepository $TesterToolsRepository
    $installedTesterToolsCommit = ""
    $testerToolsManifestPath = Join-Path $Root "fetcher-tester-tools.json"
    if (Test-Path -LiteralPath $testerToolsManifestPath -PathType Leaf) {
        try {
            $testerToolsManifest = Get-Content -LiteralPath $testerToolsManifestPath -Raw | ConvertFrom-Json
            $installedTesterToolsCommit = [string]$testerToolsManifest.sourceCommit
        }
        catch {
            # An unreadable manifest is treated as out of date.
        }
    }
    $testerToolsCurrent = $installedTesterToolsCommit -eq $availableTesterToolsCommit
    if (-not $testerToolsCurrent) {
        $reasons.Add("launcher and tester tools")
    }

    $clientModRelease = Get-GitHubRelease -Tag $ClientModBundleReleaseTag `
        -ReleaseRepository $TesterToolsRepository
    $clientModAsset = Get-ReleaseAsset -Release $clientModRelease `
        -AssetName $ClientModBundleAssetName -ReleaseTag $ClientModBundleReleaseTag `
        -ReleaseRepository $TesterToolsRepository
    $installedClientModDigest = ""
    $clientModReceiptPath = Join-Path $Root "_fetcher_update\client-mod-bundle.json"
    if (Test-Path -LiteralPath $clientModReceiptPath -PathType Leaf) {
        try {
            $clientModReceipt = Get-Content -LiteralPath $clientModReceiptPath -Raw | ConvertFrom-Json
            $installedClientModDigest = [string]$clientModReceipt.assetDigest
        }
        catch {
            # An unreadable receipt is treated as out of date.
        }
    }
    $clientModBundleCurrent = $installedClientModDigest -eq $clientModAsset.Digest
    if (-not $clientModBundleCurrent) {
        $reasons.Add("Fetcher client mods")
    }

    $patchStates = @{}
    if ($null -ne $updateState -and $null -ne $updateState.patches) {
        foreach ($property in $updateState.patches.PSObject.Properties) {
            $patchStates[$property.Name] = $property.Value
        }
    }
    $patchResults = New-Object 'System.Collections.Generic.List[object]'
    $patchesCurrent = $true
    $resolvedPatchCatalogPath = if ([string]::IsNullOrWhiteSpace($PatchCatalogPath)) {
        Join-Path $Root "fetcher-client-patches.json"
    }
    else {
        $PatchCatalogPath
    }
    if (Test-Path -LiteralPath $resolvedPatchCatalogPath -PathType Leaf) {
        $catalog = Get-Content -LiteralPath $resolvedPatchCatalogPath -Raw | ConvertFrom-Json
        if ([int]$catalog.schemaVersion -ne 1 -or
            $null -eq $catalog.PSObject.Properties["patches"] -or
            @($catalog.patches).Count -eq 0) {
            throw "Unsupported Fetcher client patch catalog schema: $($catalog.schemaVersion)"
        }
        foreach ($patch in @($catalog.patches)) {
            Assert-ClientModPatchDefinition -Patch $patch
            $targetRoot = Find-OpenMwPluginDataRoot -Root $Root -Plugin ([string]$patch.targetPlugin) `
                -RequiredSubdirectory ([string]$patch.requiredSubdirectory)
            if ($null -eq $targetRoot) {
                continue
            }
            $patchRepository = Get-OptionalStringProperty -Object $patch -Name "repository"
            $patchRelease = Get-GitHubRelease -Tag ([string]$patch.releaseTag) `
                -ReleaseRepository $patchRepository
            $patchAsset = Get-ReleaseAsset -Release $patchRelease -AssetName ([string]$patch.assetName) `
                -ReleaseTag ([string]$patch.releaseTag) -ReleaseRepository $patchRepository
            $patchCurrent = Test-ClientModPatchCurrent -Root $Root -Patch $patch -TargetRoot $targetRoot `
                -Asset $patchAsset -PatchStates $patchStates
            if (-not $patchCurrent) {
                $patchesCurrent = $false
            }
            $patchResults.Add([ordered]@{
                id = [string]$patch.id
                name = [string]$patch.name
                current = $patchCurrent
                availableDigest = $patchAsset.Digest
            })
        }
    }
    else {
        $patchesCurrent = $false
    }
    if (-not $patchesCurrent) {
        $reasons.Add("multiplayer compatibility patches")
    }

    $upToDate = $clientCurrent -and $testerToolsCurrent -and $clientModBundleCurrent -and $patchesCurrent
    return [ordered]@{
        schemaVersion = 1
        status = if ($upToDate) { "current" } else { "updateAvailable" }
        upToDate = $upToDate
        checkedAtUtc = [DateTime]::UtcNow.ToString("o")
        channel = $channel
        message = if ($upToDate) {
            "Fetcher Simulator is up to date."
        }
        else {
            "Update available: $($reasons -join ', ')."
        }
        reasons = $reasons.ToArray()
        client = [ordered]@{
            current = $clientCurrent
            installedCommit = [string]$installedClientCommit
            availableCommit = $availableClientCommit
            releaseTag = $ClientReleaseTag
        }
        testerTools = [ordered]@{
            current = $testerToolsCurrent
            installedCommit = $installedTesterToolsCommit
            availableCommit = $availableTesterToolsCommit
        }
        clientModBundle = [ordered]@{
            current = $clientModBundleCurrent
            installedDigest = $installedClientModDigest
            availableDigest = $clientModAsset.Digest
        }
        patches = $patchResults.ToArray()
    }
}

function Install-TesterTools {
    param([Parameter(Mandatory = $true)][string] $Root)

    $bootstrap = Join-Path $Root "Install-Fetcher-Tester-Tools.ps1"
    if (-not (Test-Path -LiteralPath $bootstrap -PathType Leaf)) {
        throw "Fetcher tester tools bootstrap was not found: $bootstrap"
    }

    Write-Host "Refreshing Fetcher tester tools..."
    $parameters = @{
        InstallRoot = $Root
        TesterToolsRepository = $TesterToolsRepository
        ReleaseTag = $TesterToolsReleaseTag
        AssetName = $TesterToolsAssetName
        GitHubApiBaseUrl = $GitHubApiBaseUrl
        GitHubDownloadBaseUrl = $GitHubDownloadBaseUrl
        ClientModBundleRepository = $TesterToolsRepository
        ClientModBundleReleaseTag = $ClientModBundleReleaseTag
        ClientModBundleAssetName = $ClientModBundleAssetName
        SkipClientModBundle = $SkipClientModBundle
        QuickCheck = $QuickCheck
        SkipUpdater = $true
    }
    if (-not [string]::IsNullOrWhiteSpace($TesterToolsArchivePath)) {
        $parameters.ToolsArchivePath = $TesterToolsArchivePath
    }
    if (-not [string]::IsNullOrWhiteSpace($ClientModBundleArchivePath)) {
        $parameters.ClientModBundleArchivePath = $ClientModBundleArchivePath
    }
    & $bootstrap @parameters
    if (-not $?) {
        throw "Fetcher tester tools refresh failed."
    }
}

function Write-UpdateState {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)] $ClientState,
        [Parameter(Mandatory = $true)][hashtable] $PatchStates
    )

    $orderedPatches = [ordered]@{}
    foreach ($key in @($PatchStates.Keys | Sort-Object)) {
        $orderedPatches[$key] = $PatchStates[$key]
    }
    $state = [ordered]@{
        schemaVersion = 1
        client = $ClientState
        patches = $orderedPatches
        checkedAtUtc = [DateTime]::UtcNow.ToString("o")
    }
    $temporaryPath = "$Path.$([Guid]::NewGuid().ToString('N')).tmp"
    $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporaryPath -Encoding UTF8
    Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
}

if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
}
$root = (Resolve-Path -LiteralPath $InstallRoot).Path.TrimEnd("\", "/")
$clientProtectionPolicyCandidates = @(
    (Join-Path $root "fetcher-client-protection-policy.json")
    (Join-Path $PSScriptRoot "fetcher-client-protection-policy.json")
) | Select-Object -Unique
$clientProtectionPolicyPath = @($clientProtectionPolicyCandidates | Where-Object {
    Test-Path -LiteralPath $_ -PathType Leaf
} | Select-Object -First 1)
if ($clientProtectionPolicyPath.Count -eq 0) {
    throw "Fetcher client protection policy was not found. Checked: $($clientProtectionPolicyCandidates -join '; ')"
}
$clientProtectionPolicyPath = [string]$clientProtectionPolicyPath[0]
$clientProtectionPolicy = Read-ClientProtectionPolicy -Path $clientProtectionPolicyPath

if ($StatusOnly) {
    try {
        Get-LauncherUpdateStatus -Root $root |
            ConvertTo-Json -Depth 6 -Compress |
            Write-Output
    }
    catch {
        [ordered]@{
            schemaVersion = 1
            status = "error"
            upToDate = $false
            checkedAtUtc = [DateTime]::UtcNow.ToString("o")
            message = $_.Exception.Message
        } | ConvertTo-Json -Depth 6 -Compress | Write-Output
    }
    return
}

$rootHashAlgorithm = [Security.Cryptography.SHA256]::Create()
try {
    $rootHashBytes = $rootHashAlgorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($root.ToLowerInvariant()))
}
finally {
    $rootHashAlgorithm.Dispose()
}
$rootHash = ([BitConverter]::ToString($rootHashBytes)).Replace("-", "").ToLowerInvariant()
$updateMutex = New-Object Threading.Mutex($false, "Local\FetcherSimulatorUpdater-$rootHash")
$mutexAcquired = $false
try {
    try {
        $mutexAcquired = $updateMutex.WaitOne(0)
    }
    catch [Threading.AbandonedMutexException] {
        $mutexAcquired = $true
    }
    if (-not $mutexAcquired) {
        throw "Another Fetcher Simulator updater is already running for $root."
    }

$updateRoot = Join-Path $root "_fetcher_update"
$workParent = Join-Path $updateRoot "work"
if (Test-Path -LiteralPath $workParent -PathType Container) {
    foreach ($staleWork in Get-ChildItem -LiteralPath $workParent -Force -Directory) {
        Remove-Item -LiteralPath $staleWork.FullName -Recurse -Force
    }
}
$runWorkRoot = Join-Path $workParent ([Guid]::NewGuid().ToString("N"))
$statePath = Join-Path $root "fetcher-update-state.json"
$umoStatePath = Join-Path $updateRoot "umo-check-state.json"
New-Item -ItemType Directory -Force -Path $runWorkRoot | Out-Null

$clientState = [ordered]@{}
$patchStates = @{}
if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    try {
        $previousState = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        if ($null -ne $previousState.client) {
            foreach ($property in $previousState.client.PSObject.Properties) {
                $clientState[$property.Name] = $property.Value
            }
        }
        if ($null -ne $previousState.patches) {
            foreach ($property in $previousState.patches.PSObject.Properties) {
                $patchStates[$property.Name] = $property.Value
            }
        }
    }
    catch {
        Write-Warning "Ignoring unreadable updater state: $($_.Exception.Message)"
    }
}

try {
    # Refresh tester tools before resolving the client channel. The BAT launcher
    # runs this script from a temporary copy, so installed tools can be replaced
    # safely while this invocation continues.
    if (-not $SkipTesterToolsUpdate) {
        Install-TesterTools -Root $root
    }

    Apply-ClientChannelConfiguration -Root $root

    if (-not $SkipClientUpdate) {
        Write-Host "Checking Fetcher Simulator client release..."
        $clientRelease = Get-GitHubRelease -Tag $ClientReleaseTag -ReleaseRepository $ClientRepository
        $clientAsset = Get-ReleaseAsset -Release $clientRelease -AssetName $ClientAssetName `
            -ReleaseTag $ClientReleaseTag -ReleaseRepository $ClientRepository
        $remoteCommit = Resolve-ReleaseCommit -Release $clientRelease -Tag $ClientReleaseTag `
            -ReleaseRepository $ClientRepository
        $localCommit = Get-InstalledClientCommit -Root $root
        $installedInventoryCommit = Get-InstalledClientInventoryCommit -Root $root
        $knownAssetDigest = if ($clientState.Contains("assetDigest")) { [string]$clientState["assetDigest"] } else { "" }
        $knownReleaseTag = if ($clientState.Contains("releaseTag")) { [string]$clientState["releaseTag"] } else { "" }
        if ($installedInventoryCommit -ne $remoteCommit -or $localCommit -ne $remoteCommit -or
            $knownReleaseTag -ne $ClientReleaseTag -or
            (-not [string]::IsNullOrWhiteSpace($knownAssetDigest) -and $knownAssetDigest -ne $clientAsset.Digest)) {
            Install-ClientArchive -Root $root -Asset $clientAsset -RemoteCommit $remoteCommit -RunWorkRoot $runWorkRoot
        }
        else {
            Write-Host "Client is current at commit $remoteCommit."
        }
        $clientState = [ordered]@{
            commit = $remoteCommit
            releaseTag = $ClientReleaseTag
            assetName = $ClientAssetName
            assetDigest = $clientAsset.Digest
            checkedAtUtc = [DateTime]::UtcNow.ToString("o")
        }
    }

    if (-not $SkipUmoMods) {
        $umoCheckState = Get-UmoCheckState -Root $root -StatePath $umoStatePath
        if ($QuickCheck -and $umoCheckState.IsCurrent) {
            $checkedAtLocal = $umoCheckState.CheckedAtUtc.ToLocalTime().ToString("g")
            Write-Host "Required Fetcher mods were checked through UMO at $checkedAtLocal."
            Write-Host "Skipping the full UMO scan during this quick check."
            Write-Host "Use Full Mod Check / Repair in the launcher to force it now."
            if ($null -ne $umoCheckState.PSObject.Properties["NeedsMigration"] -and
                [bool]$umoCheckState.NeedsMigration) {
                Write-UmoCheckState -Path $umoStatePath -ModListSha256 ([string]$umoCheckState.ModListSha256)
            }
        }
        else {
            if ($QuickCheck) {
                Write-Host "Running the full UMO scan because $($umoCheckState.Reason)."
            }
            Install-UmoModList -Root $root
            $completedUmoState = Get-UmoCheckState -Root $root -StatePath $umoStatePath
            $modListSha256 = if ([string]::IsNullOrWhiteSpace([string]$completedUmoState.ModListSha256)) {
                Get-Sha256 -Path (Join-Path $root "fetcher-bardcraft-umo.json")
            }
            else {
                [string]$completedUmoState.ModListSha256
            }
            Write-UmoCheckState -Path $umoStatePath -ModListSha256 $modListSha256
        }
    }

    $modCompatibilityScript = Join-Path $root "Apply-Fetcher-ZHI-Compatibility.ps1"
    if (Test-Path -LiteralPath $modCompatibilityScript -PathType Leaf) {
        & $modCompatibilityScript -InstallRoot $root
        if (-not $?) {
            throw "Fetcher client mod compatibility fixes failed."
        }
    }

    if (-not $SkipModPatches) {
        if ([string]::IsNullOrWhiteSpace($PatchCatalogPath)) {
            $PatchCatalogPath = Join-Path $root "fetcher-client-patches.json"
        }
        if (Test-Path -LiteralPath $PatchCatalogPath -PathType Leaf) {
            $catalog = Get-Content -LiteralPath $PatchCatalogPath -Raw | ConvertFrom-Json
            if ([int]$catalog.schemaVersion -ne 1 -or
                $null -eq $catalog.PSObject.Properties["patches"] -or
                @($catalog.patches).Count -eq 0) {
                throw "Unsupported Fetcher client patch catalog schema: $($catalog.schemaVersion)"
            }
            $patchIds = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
            foreach ($patch in @($catalog.patches)) {
                Assert-ClientModPatchDefinition -Patch $patch
                if (-not $patchIds.Add([string]$patch.id)) {
                    throw "Client patch catalog contains a duplicate id: $($patch.id)"
                }
                Install-ClientModPatch -Root $root -Patch $patch -UpdateRoot $updateRoot `
                    -RunWorkRoot $runWorkRoot -PatchStates $patchStates
            }
        }
        else {
            Write-Warning "Client patch catalog was not found: $PatchCatalogPath"
        }

        $publicConfigScript = Join-Path $root "Apply-Fetcher-Public-Test-Config.ps1"
        if (-not (Test-Path -LiteralPath $publicConfigScript -PathType Leaf)) {
            throw "Public test configuration script was not found: $publicConfigScript"
        }
        Write-Host "Regenerating openmw.cfg after compatibility patches..."
        & $publicConfigScript
        if (-not $?) {
            throw "Fetcher public test configuration regeneration failed."
        }
    }

    Write-UpdateState -Path $statePath -ClientState $clientState -PatchStates $patchStates
    Write-Host "Update check completed successfully."
}
finally {
    if (Test-Path -LiteralPath $runWorkRoot -PathType Container) {
        Remove-Item -LiteralPath $runWorkRoot -Recurse -Force
    }
}
}
finally {
    if ($mutexAcquired) {
        [void]$updateMutex.ReleaseMutex()
    }
    $updateMutex.Dispose()
}
