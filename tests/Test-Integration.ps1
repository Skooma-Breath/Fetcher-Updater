[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $ArchivePath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-True {
    param([Parameter(Mandatory)][bool] $Condition, [Parameter(Mandatory)][string] $Message)
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Throws {
    param([Parameter(Mandatory)][scriptblock] $Action, [Parameter(Mandatory)][string] $Message)
    try {
        & $Action
    }
    catch {
        return
    }
    throw $Message
}

function Get-Sha256 {
    param([Parameter(Mandatory)][string] $Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function New-ToolsPackage {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $Probe
    )

    $stage = "$Path.stage"
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    try {
        $probePath = Join-Path $stage "migration-probe.txt"
        Set-Content -LiteralPath $probePath -Encoding UTF8 -Value $Probe
        $manifest = [ordered]@{
            schemaVersion = 1
            channel = "fetcher-simulator-test"
            sourceCommit = ("0" * 40)
            generatedAtUtc = [DateTime]::UtcNow.ToString("o")
            files = @([ordered]@{
                path = "migration-probe.txt"
                size = (Get-Item -LiteralPath $probePath).Length
                sha256 = Get-Sha256 -Path $probePath
            })
        }
        $manifest | ConvertTo-Json -Depth 5 |
            Set-Content -LiteralPath (Join-Path $stage "fetcher-tester-tools.json") -Encoding UTF8
        Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $Path -CompressionLevel Optimal
    }
    finally {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}

function New-ClientModBundle {
    param([Parameter(Mandatory)][string] $Path)

    $stage = "$Path.stage"
    $dataRoot = Join-Path $stage "Data Files"
    New-Item -ItemType Directory -Force -Path $dataRoot | Out-Null
    try {
        $plugins = @(
            "surf_mesa_mw.omwaddon",
            "surf_utopia_mw.omwaddon",
            "surf_kitsune.omwaddon",
            "surf_kitsune.omwscripts",
            "surf_kitsune2.omwaddon",
            "mp_phase7_test.omwscripts"
        )
        foreach ($plugin in $plugins) {
            Set-Content -LiteralPath (Join-Path $dataRoot $plugin) -Value "fixture:$plugin" -Encoding UTF8
        }
        [ordered]@{
            fallbackArchives = @("Morrowind.bsa", "Tribunal.bsa", "Bloodmoon.bsa")
            dataDirs = @("./Data Files")
            content = @("Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm") + $plugins
            userData = "./userdata"
        } | ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath (Join-Path $stage "openmw-client-package.json") -Encoding UTF8
        Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $Path -CompressionLevel Optimal
    }
    finally {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}

function New-PatchArchive {
    param(
        [Parameter(Mandatory)][ValidateSet("Bardcraft", "Starwind")][string] $Kind,
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $Variant
    )

    $stage = "$Path.stage"
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    try {
        if ($Kind -eq "Bardcraft") {
            $manifestName = "fetcher-bardcraft-mp-patch.json"
            $version = "2.0.20"
        }
        else {
            $manifestName = "fetcher-starwind-compat-patch.json"
            $version = "2.2.1"
        }
        $manifestPath = Join-Path $stage $manifestName
        [ordered]@{ schemaVersion = 1; patchVersion = $version; testVariant = $Variant } |
            ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        $manifestHash = Get-Sha256 -Path $manifestPath

        if ($Kind -eq "Bardcraft") {
            $applier = @"
param([Parameter(Mandatory=`$true)][string] `$BardcraftDataRoot)
`$receipt = [ordered]@{
    schemaVersion = 1
    patchVersion = "2.0.20"
    manifestSha256 = "$manifestHash"
    testVariant = "$Variant"
}
`$receipt | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path `$BardcraftDataRoot "fetcher-bardcraft-mp-patch.json") -Encoding UTF8
"@
            Set-Content -LiteralPath (Join-Path $stage "Apply-Fetcher-Bardcraft-MPPatch.ps1") -Value $applier -Encoding UTF8
        }
        else {
            $applier = @"
param(
    [Parameter(Mandatory=`$true)][string] `$StarwindDataRoot,
    [Parameter(Mandatory=`$true)][string] `$InstallRoot
)
`$markerRoot = Join-Path `$InstallRoot "Data Files\fetcher-starwind-compat"
New-Item -ItemType Directory -Force -Path `$markerRoot | Out-Null
Copy-Item -LiteralPath (Join-Path `$PSScriptRoot "fetcher-starwind-compat-patch.json") -Destination (Join-Path `$markerRoot "fetcher-starwind-compat-patch.json") -Force
"@
            Set-Content -LiteralPath (Join-Path $stage "Apply-Fetcher-Starwind-CompatibilityPatch.ps1") -Value $applier -Encoding UTF8
        }
        Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $Path -CompressionLevel Optimal
        return [pscustomobject]@{
            ArchivePath = $Path
            ArchiveSha256 = Get-Sha256 -Path $Path
            ArchiveSize = (Get-Item -LiteralPath $Path).Length
            ManifestSha256 = $manifestHash
            Version = $version
        }
    }
    finally {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}

function Set-TestRoutes {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][hashtable] $Routes
    )
    $temporaryPath = "$Path.$([Guid]::NewGuid().ToString('N')).tmp"
    $Routes | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $temporaryPath -Encoding UTF8
    Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
}

function Add-ReleaseRoute {
    param(
        [Parameter(Mandatory)][hashtable] $Routes,
        [Parameter(Mandatory)][string] $Repository,
        [Parameter(Mandatory)][string] $Tag,
        [Parameter(Mandatory)][string] $AssetName,
        [Parameter(Mandatory)][string] $AssetPath,
        [string] $TargetCommit = ("a" * 40)
    )

    $assetHash = Get-Sha256 -Path $AssetPath
    $release = [ordered]@{
        target_commitish = $TargetCommit
        assets = @([ordered]@{
            name = $AssetName
            digest = "sha256:$assetHash"
            size = (Get-Item -LiteralPath $AssetPath).Length
        })
    }
    $Routes["/repos/$Repository/releases/tags/$Tag"] = @{
        contentType = "application/json"
        body = ($release | ConvertTo-Json -Depth 5 -Compress)
    }
    $Routes["/$Repository/releases/download/$Tag/$AssetName"] = @{
        contentType = "application/octet-stream"
        file = $AssetPath
    }
}

function Start-TestServer {
    param(
        [Parameter(Mandatory)][string] $RoutesPath,
        [Parameter(Mandatory)][string] $LogPath
    )

    $listenerProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listenerProbe.Start()
    $port = ([Net.IPEndPoint]$listenerProbe.LocalEndpoint).Port
    $listenerProbe.Stop()
    $prefix = "http://127.0.0.1:$port/"
    $job = Start-Job -ArgumentList $prefix, $RoutesPath, $LogPath -ScriptBlock {
        param($Prefix, $RoutesPath, $LogPath)
        $ErrorActionPreference = "Stop"
        $listener = [Net.HttpListener]::new()
        $listener.Prefixes.Add($Prefix)
        $listener.Start()
        try {
            while ($listener.IsListening) {
                $context = $listener.GetContext()
                $requestPath = $context.Request.Url.AbsolutePath
                $stopAfterResponse = $false
                [IO.File]::AppendAllText($LogPath, "$requestPath`n")
                if ($requestPath -eq "/__ready") {
                    $bytes = [Text.Encoding]::UTF8.GetBytes("ready")
                }
                elseif ($requestPath -eq "/__stop") {
                    $bytes = [Text.Encoding]::UTF8.GetBytes("stopping")
                    $stopAfterResponse = $true
                }
                else {
                    $routes = Get-Content -LiteralPath $RoutesPath -Raw | ConvertFrom-Json
                    $property = $routes.PSObject.Properties[$requestPath]
                    if ($null -eq $property) {
                        $context.Response.StatusCode = 404
                        $bytes = [Text.Encoding]::UTF8.GetBytes("Not found: $requestPath")
                    }
                    else {
                        $route = $property.Value
                        $context.Response.ContentType = [string]$route.contentType
                        if ($null -ne $route.PSObject.Properties["file"]) {
                            $bytes = [IO.File]::ReadAllBytes([string]$route.file)
                        }
                        else {
                            $bytes = [Text.Encoding]::UTF8.GetBytes([string]$route.body)
                        }
                    }
                }
                $context.Response.ContentLength64 = $bytes.Length
                $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
                $context.Response.Close()
                if ($stopAfterResponse) {
                    $listener.Stop()
                }
            }
        }
        finally {
            if ($listener.IsListening) {
                $listener.Stop()
            }
            $listener.Close()
        }
    }

    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        try {
            Invoke-WebRequest -UseBasicParsing -Uri "${prefix}__ready" | Out-Null
            return [pscustomobject]@{ Prefix = $prefix.TrimEnd("/"); Job = $job }
        }
        catch {
            Start-Sleep -Milliseconds 100
        }
    }
    Receive-Job -Job $job
    throw "The local integration-test server did not start."
}

function Stop-TestServer {
    param([Parameter(Mandatory)] $Server)
    try {
        Invoke-WebRequest -UseBasicParsing -Uri "$($Server.Prefix)/__stop" | Out-Null
    }
    catch {
        # The server may close the listener before the client observes a response.
    }
    Wait-Job -Job $Server.Job -Timeout 10 | Out-Null
    Receive-Job -Job $Server.Job
    Remove-Job -Job $Server.Job -Force
}

function New-ClientRoot {
    param([Parameter(Mandatory)][string] $Path)
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
    Set-Content -LiteralPath (Join-Path $Path "openmw.exe") -Value "integration-test-placeholder" -Encoding ASCII
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$releaseRoot = Join-Path $repositoryRoot "release-root"
$resolvedArchive = (Resolve-Path -LiteralPath $ArchivePath).Path
$workRoot = Join-Path ([IO.Path]::GetTempPath()) ("fetcher-integration-" + [Guid]::NewGuid().ToString("N"))
$server = $null
try {
    New-Item -ItemType Directory -Force -Path $workRoot | Out-Null

    # Fresh install from the real package, including per-file manifest validation.
    $freshRoot = Join-Path $workRoot "fresh-client"
    $clientModBundle = Join-Path $workRoot "openmw-client-mods.zip"
    New-ClientModBundle -Path $clientModBundle
    New-ClientRoot -Path $freshRoot
    & (Join-Path $releaseRoot "Install-Fetcher-Tester-Tools.ps1") `
        -InstallRoot $freshRoot -ToolsArchivePath $resolvedArchive `
        -ClientModBundleArchivePath $clientModBundle -SkipUpdater
    $freshManifest = Get-Content -LiteralPath (Join-Path $freshRoot "fetcher-tester-tools.json") -Raw | ConvertFrom-Json
    foreach ($record in @($freshManifest.files)) {
        $installedPath = Join-Path $freshRoot ([string]$record.path).Replace("/", "\")
        Assert-True -Condition (Test-Path -LiteralPath $installedPath -PathType Leaf) `
            -Message "Fresh install is missing $($record.path)."
        Assert-True -Condition ((Get-Item -LiteralPath $installedPath).Length -eq [int64]$record.size) `
            -Message "Fresh install size mismatch for $($record.path)."
        Assert-True -Condition ((Get-Sha256 -Path $installedPath) -eq ([string]$record.sha256).ToLowerInvariant()) `
            -Message "Fresh install hash mismatch for $($record.path)."
    }
    Assert-True -Condition (Test-Path -LiteralPath (Join-Path $freshRoot "Data Files\surf_mesa_mw.omwaddon") -PathType Leaf) `
        -Message "Fresh tester-tools install did not install the Fetcher client mod bundle."
    $clientModReceipt = Get-Content -LiteralPath (Join-Path $freshRoot "_fetcher_update\client-mod-bundle.json") -Raw | ConvertFrom-Json
    Assert-True -Condition ([string]$clientModReceipt.assetDigest -eq "sha256:$(Get-Sha256 -Path $clientModBundle)") `
        -Message "Client mod bundle receipt did not record the verified archive digest."

    $unsupportedArchive = Join-Path $workRoot "unsupported-tools.zip"
    New-ToolsPackage -Path $unsupportedArchive -Probe "unsupported"
    $unsupportedStage = Join-Path $workRoot "unsupported-stage"
    Expand-Archive -LiteralPath $unsupportedArchive -DestinationPath $unsupportedStage
    $unsupportedManifestPath = Join-Path $unsupportedStage "fetcher-tester-tools.json"
    $unsupportedManifest = Get-Content -LiteralPath $unsupportedManifestPath -Raw | ConvertFrom-Json
    $unsupportedManifest.schemaVersion = 99
    $unsupportedManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $unsupportedManifestPath -Encoding UTF8
    Remove-Item -LiteralPath $unsupportedArchive -Force
    Compress-Archive -Path (Join-Path $unsupportedStage "*") -DestinationPath $unsupportedArchive
    Assert-Throws -Message "Installer accepted an unsupported tester-tools manifest." -Action {
        & (Join-Path $releaseRoot "Install-Fetcher-Tester-Tools.ps1") `
            -InstallRoot $freshRoot -ToolsArchivePath $unsupportedArchive -SkipUpdater
    }

    $unsafeArchive = Join-Path $workRoot "unsafe-tools.zip"
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $unsafeZip = [IO.Compression.ZipFile]::Open($unsafeArchive, [IO.Compression.ZipArchiveMode]::Create)
    try {
        $unsafeEntry = $unsafeZip.CreateEntry("../escape.txt")
        $writer = New-Object IO.StreamWriter($unsafeEntry.Open())
        try { $writer.Write("escape") } finally { $writer.Dispose() }
    }
    finally {
        $unsafeZip.Dispose()
    }
    Assert-Throws -Message "Installer accepted an archive path that escapes its root." -Action {
        & (Join-Path $releaseRoot "Install-Fetcher-Tester-Tools.ps1") `
            -InstallRoot $freshRoot -ToolsArchivePath $unsafeArchive -SkipUpdater
    }
    Assert-True -Condition (-not (Test-Path -LiteralPath (Join-Path $workRoot "escape.txt"))) `
        -Message "Unsafe archive wrote outside its extraction root."

    $routesPath = Join-Path $workRoot "routes.json"
    $logPath = Join-Path $workRoot "requests.log"
    Set-Content -LiteralPath $logPath -Value "" -Encoding UTF8
    Set-TestRoutes -Path $routesPath -Routes @{}
    $server = Start-TestServer -RoutesPath $routesPath -LogPath $logPath

    # Existing installations can keep passing legacy -Repository for the client;
    # the bridge updater still resolves tester tools against Fetcher-Updater.
    $toolsV1 = Join-Path $workRoot "tools-v1.zip"
    $toolsV2 = Join-Path $workRoot "tools-v2.zip"
    New-ToolsPackage -Path $toolsV1 -Probe "bridge-v1"
    New-ToolsPackage -Path $toolsV2 -Probe "bridge-v2"
    $routes = @{}
    Add-ReleaseRoute -Routes $routes -Repository "Skooma-Breath/Fetcher-Updater" `
        -Tag "fetcher-tester-tools" -AssetName "fetcher-tester-tools.zip" -AssetPath $toolsV1
    Set-TestRoutes -Path $routesPath -Routes $routes

    $migrationRoot = Join-Path $workRoot "migration-client"
    New-ClientRoot -Path $migrationRoot
    Copy-Item -LiteralPath (Join-Path $releaseRoot "Install-Fetcher-Tester-Tools.ps1") -Destination $migrationRoot
    $migrationUpdater = Join-Path $releaseRoot "Update-Fetcher-Simulator.ps1"
    $migrationOutput = & $migrationUpdater -InstallRoot $migrationRoot `
        -Repository "Skooma-Breath/Fetcher-Simulator" `
        -GitHubApiBaseUrl $server.Prefix -GitHubDownloadBaseUrl $server.Prefix `
        -SkipClientUpdate -SkipClientModBundle -SkipUmoMods -SkipModPatches 6>&1 | Out-String
    Assert-True -Condition ((Get-Content -LiteralPath (Join-Path $migrationRoot "migration-probe.txt") -Raw).Trim() -eq "bridge-v1") `
        -Message "Migration bridge did not install the new-repository tester tools."
    $requests = Get-Content -LiteralPath $logPath -Raw
    Assert-True -Condition ($requests.Contains("/repos/Skooma-Breath/Fetcher-Updater/releases/tags/fetcher-tester-tools")) `
        -Message "Migration bridge did not query Skooma-Breath/Fetcher-Updater."
    Assert-True -Condition (-not $requests.Contains("/repos/Skooma-Breath/Fetcher-Simulator/releases/tags/fetcher-tester-tools")) `
        -Message "Migration bridge queried tester tools in the old repository."

    Add-ReleaseRoute -Routes $routes -Repository "Skooma-Breath/Fetcher-Updater" `
        -Tag "fetcher-tester-tools" -AssetName "fetcher-tester-tools.zip" -AssetPath $toolsV2
    Set-TestRoutes -Path $routesPath -Routes $routes
    $changedToolsOutput = & $migrationUpdater -InstallRoot $migrationRoot `
        -Repository "Skooma-Breath/Fetcher-Simulator" `
        -GitHubApiBaseUrl $server.Prefix -GitHubDownloadBaseUrl $server.Prefix `
        -SkipClientUpdate -SkipClientModBundle -SkipUmoMods -SkipModPatches 6>&1 | Out-String
    Assert-True -Condition ((Get-Content -LiteralPath (Join-Path $migrationRoot "migration-probe.txt") -Raw).Trim() -eq "bridge-v2") `
        -Message "Changed tester-tools digest was not downloaded and installed."

    # Create realistic Bardcraft and Starwind fixtures and digest-backed releases.
    $bardV1 = New-PatchArchive -Kind Bardcraft -Path (Join-Path $workRoot "bard-v1.zip") -Variant "initial"
    $bardV2 = New-PatchArchive -Kind Bardcraft -Path (Join-Path $workRoot "bard-v2.zip") -Variant "changed"
    $starV1 = New-PatchArchive -Kind Starwind -Path (Join-Path $workRoot "star-v1.zip") -Variant "initial"
    Add-ReleaseRoute -Routes $routes -Repository "Skooma-Breath/Fetcher-Bardcraft" `
        -Tag "fetcher-bardcraft-mp-patch-v2" -AssetName "fetcher-bardcraft-mp-patch-v2.zip" -AssetPath $bardV1.ArchivePath
    Add-ReleaseRoute -Routes $routes -Repository "Skooma-Breath/Fetcher-Starwind" `
        -Tag "fetcher-starwind-compat-patch-v2" -AssetName "fetcher-starwind-compat-patch-v2.zip" -AssetPath $starV1.ArchivePath
    Set-TestRoutes -Path $routesPath -Routes $routes

    $patchRoot = Join-Path $workRoot "patch-client"
    New-ClientRoot -Path $patchRoot
    $bardRoot = Join-Path $patchRoot "Data Files\Bardcraft"
    $starRoot = Join-Path $patchRoot "Data Files\Starwind"
    New-Item -ItemType Directory -Force -Path (Join-Path $bardRoot "scripts\Bardcraft"), (Join-Path $starRoot "Meshes") | Out-Null
    Set-Content -LiteralPath (Join-Path $bardRoot "Bardcraft.ESP") -Value "plugin" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $starRoot "StarwindRemasteredV1.15.esm") -Value "plugin" -Encoding ASCII
    $bardReceipt = [ordered]@{ patchVersion = "2.0.20"; manifestSha256 = $bardV1.ManifestSha256 }
    $bardReceipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $bardRoot "fetcher-bardcraft-mp-patch.json") -Encoding UTF8
    $starMarkerRoot = Join-Path $patchRoot "Data Files\fetcher-starwind-compat"
    New-Item -ItemType Directory -Force -Path $starMarkerRoot | Out-Null
    Expand-Archive -LiteralPath $starV1.ArchivePath -DestinationPath (Join-Path $workRoot "star-initial")
    Copy-Item -LiteralPath (Join-Path $workRoot "star-initial\fetcher-starwind-compat-patch.json") `
        -Destination (Join-Path $starMarkerRoot "fetcher-starwind-compat-patch.json")
    Copy-Item -LiteralPath (Join-Path $releaseRoot "fetcher-client-patches.json") -Destination $patchRoot
    Set-Content -LiteralPath (Join-Path $patchRoot "Apply-Fetcher-Public-Test-Config.ps1") `
        -Value 'param(); Write-Host "Test configuration unchanged."' -Encoding UTF8
    $patchState = [ordered]@{
        schemaVersion = 1
        client = [ordered]@{}
        patches = [ordered]@{
            "bardcraft-mp-v2" = [ordered]@{
                assetDigest = "sha256:$($bardV1.ArchiveSha256)"
                patchVersion = "2.0.20"
                manifestSha256 = $bardV1.ManifestSha256
                target = $bardRoot
            }
            "starwind-vanilla-mp-v2" = [ordered]@{
                assetDigest = "sha256:$($starV1.ArchiveSha256)"
                patchVersion = "2.2.1"
                manifestSha256 = Get-Sha256 -Path (Join-Path $starMarkerRoot "fetcher-starwind-compat-patch.json")
                target = $starRoot
            }
        }
    }
    $patchState | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $patchRoot "fetcher-update-state.json") -Encoding UTF8

    $patchOutput = & $migrationUpdater -InstallRoot $patchRoot `
        -GitHubApiBaseUrl $server.Prefix -GitHubDownloadBaseUrl $server.Prefix `
        -SkipClientUpdate -SkipTesterToolsUpdate -SkipUmoMods 6>&1 | Out-String
    Assert-True -Condition ($patchOutput.Contains("Fetcher Bardcraft multiplayer compatibility is current at patch 2.0.20.")) `
        -Message "Bardcraft current-patch message was not emitted."
    Assert-True -Condition ($patchOutput.Contains("Fetcher Starwind vanilla/multiplayer compatibility is current at patch 2.2.1.")) `
        -Message "Starwind current-patch message was not emitted."
    foreach ($unexpected in @("Downloading Fetcher Bardcraft", "Applying Fetcher Bardcraft", "Downloading Fetcher Starwind", "Applying Fetcher Starwind")) {
        Assert-True -Condition (-not $patchOutput.Contains($unexpected)) -Message "Unchanged patch performed work: $unexpected"
    }

    Add-ReleaseRoute -Routes $routes -Repository "Skooma-Breath/Fetcher-Bardcraft" `
        -Tag "fetcher-bardcraft-mp-patch-v2" -AssetName "fetcher-bardcraft-mp-patch-v2.zip" -AssetPath $bardV2.ArchivePath
    Set-TestRoutes -Path $routesPath -Routes $routes
    $changedPatchOutput = & $migrationUpdater -InstallRoot $patchRoot `
        -GitHubApiBaseUrl $server.Prefix -GitHubDownloadBaseUrl $server.Prefix `
        -SkipClientUpdate -SkipTesterToolsUpdate -SkipUmoMods 6>&1 | Out-String
    Assert-True -Condition ($changedPatchOutput.Contains("Downloading Fetcher Bardcraft")) `
        -Message "Changed Bardcraft digest was not downloaded."
    Assert-True -Condition ($changedPatchOutput.Contains("Applying Fetcher Bardcraft")) `
        -Message "Changed Bardcraft digest was not applied."
    $secondPatchOutput = & $migrationUpdater -InstallRoot $patchRoot `
        -GitHubApiBaseUrl $server.Prefix -GitHubDownloadBaseUrl $server.Prefix `
        -SkipClientUpdate -SkipTesterToolsUpdate -SkipUmoMods 6>&1 | Out-String
    Assert-True -Condition (-not $secondPatchOutput.Contains("Downloading Fetcher Bardcraft")) `
        -Message "Second updater run redownloaded unchanged Bardcraft patch."
    Assert-True -Condition (-not $secondPatchOutput.Contains("Applying Fetcher Bardcraft")) `
        -Message "Second updater run reapplied unchanged Bardcraft patch."

    Set-Content -LiteralPath (Join-Path $bardRoot "fetcher-bardcraft-mp-patch.json") -Value "corrupt" -Encoding ASCII
    Remove-Item -LiteralPath (Join-Path $starMarkerRoot "fetcher-starwind-compat-patch.json") -Force
    $receiptOutput = & $migrationUpdater -InstallRoot $patchRoot `
        -GitHubApiBaseUrl $server.Prefix -GitHubDownloadBaseUrl $server.Prefix `
        -SkipClientUpdate -SkipTesterToolsUpdate -SkipUmoMods 6>&1 | Out-String
    Assert-True -Condition ($receiptOutput.Contains("Applying Fetcher Bardcraft")) `
        -Message "Corrupt Bardcraft receipt did not trigger reapplication."
    Assert-True -Condition ($receiptOutput.Contains("Applying Fetcher Starwind")) `
        -Message "Missing Starwind marker did not trigger reapplication."

    # Client release lookup remains isolated to Fetcher-Simulator.
    $clientRoot = Join-Path $workRoot "client-routing"
    New-ClientRoot -Path $clientRoot
    $clientCommit = "b" * 40
    Set-Content -LiteralPath (Join-Path $clientRoot "CI-ID.txt") -Value "Commit $clientCommit" -Encoding ASCII
    [ordered]@{ schemaVersion = 1; channel = "test" } | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $clientRoot "fetcher-client-channel.json") -Encoding UTF8
    $clientDigest = "c" * 64
    [ordered]@{
        schemaVersion = 1
        client = [ordered]@{
            commit = $clientCommit
            releaseTag = "Fetcher-Simulator"
            assetName = "fetcher-simulator.zip"
            assetDigest = "sha256:$clientDigest"
        }
        patches = [ordered]@{}
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $clientRoot "fetcher-update-state.json") -Encoding UTF8
    $clientRelease = [ordered]@{
        target_commitish = $clientCommit
        assets = @([ordered]@{
            name = "fetcher-simulator.zip"
            digest = "sha256:$clientDigest"
            size = 1
        })
    }
    $routes["/repos/Skooma-Breath/Fetcher-Simulator/releases/tags/Fetcher-Simulator"] = @{
        contentType = "application/json"
        body = ($clientRelease | ConvertTo-Json -Depth 5 -Compress)
    }
    Set-TestRoutes -Path $routesPath -Routes $routes
    Set-Content -LiteralPath $logPath -Value "" -Encoding UTF8
    $clientOutput = & $migrationUpdater -InstallRoot $clientRoot `
        -GitHubApiBaseUrl $server.Prefix -GitHubDownloadBaseUrl $server.Prefix `
        -SkipTesterToolsUpdate -SkipUmoMods -SkipModPatches 6>&1 | Out-String
    $clientRequests = Get-Content -LiteralPath $logPath -Raw
    Assert-True -Condition ($clientRequests.Contains("/repos/Skooma-Breath/Fetcher-Simulator/releases/tags/Fetcher-Simulator")) `
        -Message "Client release did not resolve against Skooma-Breath/Fetcher-Simulator."
    Assert-True -Condition ($clientOutput.Contains("Client is current at commit $clientCommit.")) `
        -Message "Current client fixture unexpectedly attempted a client install."
}
finally {
    if ($null -ne $server) {
        Stop-TestServer -Server $server
    }
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}

Write-Host "Fetcher updater integration tests passed."
