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

function Test-ProtectedPath {
    param(
        [Parameter(Mandatory)][string] $RelativePath,
        [Parameter(Mandatory)] $Policy
    )

    $path = $RelativePath.Replace("\", "/").TrimStart("/").ToLowerInvariant()
    $exactPaths = @($Policy.exactPaths | ForEach-Object {
        ([string]$_).Replace("\", "/").TrimStart("/").ToLowerInvariant()
    })
    if ($exactPaths -contains $path) {
        return $true
    }
    foreach ($prefix in @($Policy.prefixes)) {
        $normalizedPrefix = ([string]$prefix).Replace("\", "/").TrimStart("/").ToLowerInvariant()
        if ($path.StartsWith($normalizedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    foreach ($suffix in @($Policy.suffixes)) {
        if ($path.EndsWith(([string]$suffix).ToLowerInvariant(), [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
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
    if ([int]$manifest.schemaVersion -ne 1 -or
        [string]$manifest.channel -ne "fetcher-simulator-test" -or
        [string]$manifest.sourceCommit -notmatch "^[0-9a-fA-F]{40}$") {
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
    if (-not $manifestPaths.Contains("Setup-Fetcher-Updater.bat")) {
        throw "Package is missing Setup-Fetcher-Updater.bat."
    }
    if ($manifestPaths.Contains("Join-Fetcher-Test-Channel.bat")) {
        throw "Package still contains the obsolete Join-Fetcher-Test-Channel.bat."
    }
    if (-not $manifestPaths.Contains("fetcher-client-protection-policy.json")) {
        throw "Package is missing fetcher-client-protection-policy.json."
    }
    if (-not $manifestPaths.Contains("fetcher-canonical-fallbacks.cfg")) {
        throw "Package is missing fetcher-canonical-fallbacks.cfg."
    }
    if (-not $manifestPaths.Contains("Apply-Fetcher-Mod-Compatibility.ps1")) {
        throw "Package is missing the managed mod compatibility applier."
    }
    if (-not $manifestPaths.Contains("fetcher-mod-compatibility-patches.json")) {
        throw "Package is missing the managed mod compatibility manifest."
    }
    if (-not $manifestPaths.Contains("fetcher-update-channel.json")) {
        throw "Package is missing fetcher-update-channel.json."
    }
    if (-not $manifestPaths.Contains("FetcherLauncher.exe")) {
        throw "Package is missing FetcherLauncher.exe."
    }
    if (-not $manifestPaths.Contains("FetcherLauncher-THIRD-PARTY-NOTICES.txt")) {
        throw "Package is missing Fetcher Launcher third-party notices."
    }
    if (-not $manifestPaths.Contains("ui/index.html")) {
        throw "Package is missing the Fetcher Launcher HTML interface."
    }
    if (-not $manifestPaths.Contains("ui/assets/fetcher-float.gif")) {
        throw "Package is missing the Fetcher Launcher background animation."
    }
    if ((Get-Item -LiteralPath (Join-Path $workRoot "ui\assets\fetcher-float.gif")).Length -le 0) {
        throw "Fetcher Launcher background animation is empty."
    }
    if (-not $manifestPaths.Contains("ui/assets/fetcher-float-right.gif")) {
        throw "Package is missing the right-facing Fetcher Launcher background animation."
    }
    if ((Get-Item -LiteralPath (Join-Path $workRoot "ui\assets\fetcher-float-right.gif")).Length -le 0) {
        throw "Right-facing Fetcher Launcher background animation is empty."
    }
    if (-not $manifestPaths.Contains("ui/assets/potm2504a.jpg")) {
        throw "Package is missing the Fetcher Launcher space background."
    }
    if ((Get-Item -LiteralPath (Join-Path $workRoot "ui\assets\potm2504a.jpg")).Length -le 0) {
        throw "Fetcher Launcher space background is empty."
    }
    if ((Get-Item -LiteralPath (Join-Path $workRoot "FetcherLauncher.exe")).Length -le 0) {
        throw "FetcherLauncher.exe is empty."
    }
    $channelConfiguration = Get-Content -LiteralPath (Join-Path $workRoot "fetcher-update-channel.json") -Raw | ConvertFrom-Json
    if ([int]$channelConfiguration.schemaVersion -ne 1 -or
        [string]$channelConfiguration.channel -ne "vehicles" -or
        [string]$channelConfiguration.clientRepository -ne "Fetcher-Simulator/Fetcher-Simulator" -or
        [string]$channelConfiguration.clientReleaseTag -ne "Fetcher-Simulator-Vehicles" -or
        [string]$channelConfiguration.clientAssetName -ne "fetcher-simulator.zip") {
        throw "Package contains an invalid vehicle update channel."
    }
    $protectionPolicy = Get-Content -LiteralPath (Join-Path $workRoot "fetcher-client-protection-policy.json") -Raw |
        ConvertFrom-Json
    if ([int]$protectionPolicy.schemaVersion -ne 1 -or
        @($protectionPolicy.exactPaths).Count -eq 0 -or
        @($protectionPolicy.prefixes).Count -eq 0) {
        throw "Package contains an unsupported client protection policy."
    }
    if (@($protectionPolicy.exactPaths) -notcontains "fetcher-update-channel.json") {
        throw "Client protection policy does not protect fetcher-update-channel.json."
    }
    if (@($protectionPolicy.exactPaths) -notcontains "fetcher-canonical-fallbacks.cfg") {
        throw "Client protection policy does not protect fetcher-canonical-fallbacks.cfg."
    }
    foreach ($relativePath in @($manifestPaths | ForEach-Object { $_ })) {
        if (-not (Test-ProtectedPath -RelativePath $relativePath -Policy $protectionPolicy)) {
            throw "Tester-tools path is not protected from managed-client cleanup: $relativePath"
        }
    }
    if (-not (Test-ProtectedPath -RelativePath "fetcher-tester-tools.json" -Policy $protectionPolicy)) {
        throw "Client protection policy does not protect fetcher-tester-tools.json."
    }
    foreach ($launcherPath in @(
        "fetcherlauncher.exe",
        "fetcherlauncher-third-party-notices.txt",
        "ui/index.html",
        "ui/assets/fetcher-float.gif",
        "ui/assets/fetcher-float-right.gif",
        "ui/assets/potm2504a.jpg"
    )) {
        if (@($protectionPolicy.exactPaths) -notcontains $launcherPath) {
            throw "Client protection policy does not protect $launcherPath."
        }
    }

    $umoListPath = Join-Path $workRoot "fetcher-simulator-umo.json"
    if (-not (Test-Path -LiteralPath $umoListPath -PathType Leaf)) {
        throw "Package is missing fetcher-simulator-umo.json."
    }
    $parsedUmoMods = Get-Content -LiteralPath $umoListPath -Raw | ConvertFrom-Json
    $umoMods = @($parsedUmoMods | ForEach-Object { $_ })
    $seenUmoSlugs = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($mod in $umoMods) {
        foreach ($propertyName in @("name", "url", "category", "dir", "slug")) {
            $property = $mod.PSObject.Properties[$propertyName]
            if ($null -eq $property -or [string]::IsNullOrWhiteSpace([string]$property.Value)) {
                throw "UMO mod entry is missing required property $propertyName."
            }
        }
        if (-not $seenUmoSlugs.Add([string]$mod.slug)) {
            throw "UMO modlist contains duplicate slug: $($mod.slug)"
        }
        $reviewRequired = @($mod.tags) -contains "fetcher-review-required"
        if (@($mod.download_info).Count -eq 0 -or
            (-not $reviewRequired -and @($mod.data_paths).Count -eq 0) -or
            -not (@($mod.on_lists) -contains "fetcher-simulator")) {
            throw "UMO mod entry is incomplete: $($mod.name)"
        }
    }

    $groundcoverFixture = Join-Path $workRoot "groundcover-fixture"
    New-Item -ItemType Directory -Force -Path $groundcoverFixture | Out-Null
    foreach ($fixtureFile in @(
        "Apply-Fetcher-Public-Test-Config.ps1",
        "fetcher-canonical-fallbacks.cfg",
        "fetcher-client-patches.json"
    )) {
        Copy-Item -LiteralPath (Join-Path $workRoot $fixtureFile) -Destination (Join-Path $groundcoverFixture $fixtureFile) -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $groundcoverFixture "resources\vfs-mw") | Out-Null
    $groundcoverDataRoot = Join-Path $groundcoverFixture "Data Files\fetcher-simulator\Groundcover\Probe"
    New-Item -ItemType Directory -Force -Path $groundcoverDataRoot | Out-Null
    Set-Content -LiteralPath (Join-Path $groundcoverDataRoot "ProbeGrass.esp") -Value "groundcover fixture" -Encoding ASCII
    $starwindCompatOverlayRoot = Join-Path $groundcoverFixture "Data Files\fetcher-starwind-compat\Starwind Vanilla Compat"
    $starwindCompatDataRoot = Join-Path $groundcoverFixture "Data Files\fetcher-starwind-compat\Data Files"
    $rawStarwindDataRoot = Join-Path $groundcoverFixture "Data Files\raw-starwind"
    New-Item -ItemType Directory -Force -Path $starwindCompatOverlayRoot, $starwindCompatDataRoot, $rawStarwindDataRoot | Out-Null
    $projectAtlasTextureRoot = Join-Path $groundcoverFixture "Data Files\fetcher-simulator\Performance\ProjectAtlas\01 Textures - Vanilla\Textures\atl"
    $glassDomesFixtureRoot = Join-Path $groundcoverFixture "Data Files\fetcher-simulator\ModelsAndTextures\GlassDomesofVivec"
    New-Item -ItemType Directory -Force -Path $projectAtlasTextureRoot, $glassDomesFixtureRoot | Out-Null
    Set-Content -LiteralPath (Join-Path $projectAtlasTextureRoot "atlas_velothi_01.dds") -Value "legacy atlas fixture" -Encoding ASCII
    @(
        [ordered]@{
            name = "Groundcover Array Sentinel"
            url = "https://example.invalid/groundcover-sentinel"
            category = "Groundcover"
            dir = "Probe"
            slug = "groundcover-array-sentinel"
            download_info = @([ordered]@{ file_name = "sentinel.7z"; extract_to = "Probe" })
            tags = @()
            on_lists = @("fetcher-simulator")
            data_paths = @("Probe")
            plugins = @()
        },
        [ordered]@{
            name = "Groundcover Probe"
            url = "https://example.invalid/groundcover-probe"
            category = "Groundcover"
            dir = "Probe"
            slug = "groundcover-probe"
            download_info = @([ordered]@{ file_name = "probe.7z"; extract_to = "Probe" })
            tags = @()
            on_lists = @("fetcher-simulator")
            data_paths = @("Probe")
            plugins = @()
            groundcover = @("ProbeGrass.esp")
        },
        [ordered]@{
            name = "Link Override Ordering Probe"
            url = "https://example.invalid/link-order-probe"
            category = "Gameplay"
            dir = "LinkOrder"
            slug = "link-order-probe"
            download_info = @()
            tags = @()
            on_lists = @("fetcher-simulator")
            data_paths = @()
            plugins = @("fargoth.esp", "Link_(Fixed).esp")
        },
        [ordered]@{
            name = "MWZ Ordering Probe"
            url = "https://example.invalid/mwz-order-probe"
            category = "Gameplay"
            dir = "MWZOrder"
            slug = "mwz-order-probe"
            download_info = @()
            tags = @()
            on_lists = @("fetcher-simulator")
            data_paths = @()
            plugins = @("MWZ 2.26 Hardcore Mode.ESP", "Jw_Survival_Container_Module.esp")
        }
    ) | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $groundcoverFixture "fetcher-simulator-umo.json") -Encoding UTF8
    @(
        "data=./Data Files",
        'data="./Data Files/fetcher-starwind-compat/Starwind Vanilla Compat"',
        'data="./Data Files/fetcher-starwind-compat/Data Files"',
        "data=./Data Files/raw-starwind",
        "data=./Data Files/fetcher-bardcraft/Groundcover/Probe",
        'data="./Data Files/fetcher-bardcraft/Groundcover/Probe"'
    ) | Set-Content -LiteralPath (Join-Path $groundcoverFixture "openmw.cfg") -Encoding ASCII
    $groundcoverApply = Join-Path $groundcoverFixture "Apply-Fetcher-Public-Test-Config.ps1"
    & $groundcoverApply -AllowMissingContent | Out-Null
    & $groundcoverApply -AllowMissingContent | Out-Null
    $legacyVelothiTexture = Join-Path $projectAtlasTextureRoot "atlas_velothi.dds"
    if (-not (Test-Path -LiteralPath $legacyVelothiTexture -PathType Leaf) -or
        (Get-FileHash -LiteralPath $legacyVelothiTexture -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $projectAtlasTextureRoot "atlas_velothi_01.dds") -Algorithm SHA256).Hash) {
        throw "Public-test config did not reconstruct the legacy Project Atlas Velothi texture required by Glass Domes."
    }
    $groundcoverCfg = @(Get-Content -LiteralPath (Join-Path $groundcoverFixture "openmw.cfg"))
    if (@($groundcoverCfg | Where-Object { $_ -match 'fetcher-bardcraft' }).Count -ne 0) {
        throw "Public-test config left legacy fetcher-bardcraft data paths in openmw.cfg."
    }
    $starwindCompatOverlayLine = "data=./Data Files/fetcher-starwind-compat/Starwind Vanilla Compat"
    $starwindCompatDataLine = "data=./Data Files/fetcher-starwind-compat/Data Files"
    if (@($groundcoverCfg | Where-Object { $_.Trim() -eq $starwindCompatOverlayLine }).Count -ne 1 -or
        @($groundcoverCfg | Where-Object { $_.Trim() -eq $starwindCompatDataLine }).Count -ne 1) {
        throw "Public-test config did not canonicalize stale Fetcher-Starwind data paths exactly once."
    }
    $rawStarwindIndex = [Array]::IndexOf([string[]]$groundcoverCfg, "data=./Data Files/raw-starwind")
    $starwindCompatOverlayIndex = [Array]::IndexOf([string[]]$groundcoverCfg, $starwindCompatOverlayLine)
    $starwindCompatDataIndex = [Array]::IndexOf([string[]]$groundcoverCfg, $starwindCompatDataLine)
    if ($rawStarwindIndex -lt 0 -or $starwindCompatOverlayIndex -le $rawStarwindIndex -or $starwindCompatDataIndex -le $rawStarwindIndex) {
        throw "Public-test config did not move Fetcher-Starwind compatibility data paths after raw Starwind data."
    }
    if (@($groundcoverCfg | Where-Object { $_.Trim() -eq "data=./Data Files/fetcher-simulator/Groundcover/Probe" }).Count -ne 1) {
        throw "Public-test config did not deduplicate the canonical managed data path."
    }
    if (@($groundcoverCfg | Where-Object { $_.Trim() -eq "groundcover=ProbeGrass.esp" }).Count -ne 1) {
        throw "Public-test config did not write exactly one managed groundcover entry."
    }
    if (@($groundcoverCfg | Where-Object { $_.Trim() -eq "content=ProbeGrass.esp" }).Count -ne 0) {
        throw "Public-test config incorrectly activated groundcover as normal content."
    }
    $mwzIndex = [Array]::IndexOf([string[]]$groundcoverCfg, "content=MWZ 2.26 Hardcore Mode.ESP")
    $fargothIndex = [Array]::IndexOf([string[]]$groundcoverCfg, "content=fargoth.esp")
    $linkIndex = [Array]::IndexOf([string[]]$groundcoverCfg, "content=Link_(Fixed).esp")
    $survivalContainerIndex = [Array]::IndexOf([string[]]$groundcoverCfg, "content=Jw_Survival_Container_Module.esp")
    if ($mwzIndex -lt 0 -or $fargothIndex -ne ($mwzIndex + 1) -or
        $linkIndex -ne ($fargothIndex + 1) -or $survivalContainerIndex -le $linkIndex) {
        throw "Public-test config did not preserve the tested MWZ -> fargoth -> Link override order."
    }
    $groundcoverSettings = @(Get-Content -LiteralPath (Join-Path $groundcoverFixture "settings.cfg"))
    if (@($groundcoverSettings | Where-Object { $_.Trim() -eq "[Groundcover]" }).Count -ne 1 -or
        @($groundcoverSettings | Where-Object { $_.Trim() -eq "enabled = true" }).Count -ne 1 -or
        @($groundcoverSettings | Where-Object { $_.Trim() -eq "stomp mode = 2" }).Count -ne 1 -or
        @($groundcoverSettings | Where-Object { $_.Trim() -eq "stomp intensity = 1" }).Count -ne 1) {
        throw "Public-test config did not create the required OpenMW Groundcover settings exactly once."
    }

    $requiredUmoMods = @(
        # Tamriel Data 25.05 is intentionally pinned. Nexus reuses the
        # "Tamriel Data (Vanilla)" display name for old releases, and an
        # unpinned name-only lookup can resolve to v8.0's incompatible
        # "00 Core" layout instead of the required "00 Data Files" layout.
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/44537"
            FileIds = @(1000052327)
            DataPaths = @("TamrielData/00 Data Files")
            Plugins = @("Tamriel_Data.esm", "Tamriel_Data.omwscripts")
            Pinned = $true
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/58053"
            FileIds = @(1000067256)
            Plugins = @("FollowerDetectionUtil.omwscripts")
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/59384"
            FileIds = @(1000067277)
            Plugins = @("BestFriendsForever.omwscripts")
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/57728"
            FileIds = @(1000058125)
            Plugins = @()
        },
        [pscustomobject]@{
            Url = "https://www.moddb.com/games/morrowind/addons/the-legend-of-zelda-beta-mod"
            FileIds = @()
            Plugins = @("The Legend of Zelda.ESP")
            Sha256 = "aaae1c95e8e70b831c00383dc933b80c69e0766bc60983b6e071ede643252f66"
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/55806"
            FileIds = @(1000049568, 1000051133, 1000051134)
            Plugins = @("fargoth.esp", "Link_(Fixed).esp")
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/59612"
            FileIds = @(1000067129)
            Plugins = @()
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/46370"
            FileIds = @(1000022367)
            Plugins = @("skeleton.esp")
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/45838"
            FileIds = @(1000010954)
            Plugins = @()
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/59576"
            FileIds = @(1000066946)
            Plugins = @("Held Light Boost.omwscripts")
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/52013"
            FileIds = @()
            DataPaths = @("WaresUltimate/00 Core")
            Plugins = @("Wares-base.esm")
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/43213"
            FileIds = @(1000049587)
            DataPaths = @("AdventurersBackpacks")
            Plugins = @("Adventurer's backback.ESP")
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/58527"
            FileIds = @(1000064638)
            Plugins = @(
                "OMWFW_compilation.omwaddon",
                "OMWFW_compilation.omwscripts",
                "Fashionwind Horns and Antlers.omwaddon",
                "Piercing&Earrings.omwaddon"
            )
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/56583"
            FileIds = @(1000054106)
            Plugins = @("ZerkishHotkeysImproved.omwscripts")
            Pinned = $true
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/59276"
            FileIds = @(1000065733)
            Plugins = @(
                "removeSpellFix(modified).omwaddon",
                "StatsWindow(modified).ESP",
                "ChooseControl.omwscripts",
                "InventoryExtender  (modified).omwscripts",
                "MagicWindowExtender(modified).omwscripts",
                "StatsWindow(modified).omwscripts",
                "Yet Another HUD (modified).omwscripts",
                "TakeControl.omwscripts"
            )
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/44398"
            FileIds = @(1000006047, 1000010485, 1000013634)
            DataPaths = @(
                "TheDoorsofOblivion/The Doors of Oblivion 1.0/Data Files",
                "TheDoorsofOblivion/The Doors of Oblivion 1.4/Data Files",
                "TheDoorsofOblivion/The Doors of Oblivion 1.4 missing files+extras/data files"
            )
            Plugins = @("The Doors of Oblivion 1.4.esp")
            Pinned = $true
            Ordered = $true
        },
        [pscustomobject]@{
            Url = "https://www.nexusmods.com/morrowind/mods/26309"
            FileIds = @(46321)
            Plugins = @("FF7 Tsurugi Resource.esp")
        }
    )

    $umoInstallerPath = Join-Path $workRoot "Install-Fetcher-Bardcraft-With-UMO.ps1"
    $umoInstallerSource = Get-Content -LiteralPath $umoInstallerPath -Raw
    $checkIndex = $umoInstallerSource.IndexOf('Invoke-Checked -Description "umo check"', [StringComparison]::Ordinal)
    $patchIndex = $umoInstallerSource.IndexOf('Apply-UmoInstalledDescriptorComparisonPatch -UmoExecutable $umo', [StringComparison]::Ordinal)
    $syncIndex = $umoInstallerSource.IndexOf('& $umo sync $ModListName --skip-momw', [StringComparison]::Ordinal)
    $installIndex = $umoInstallerSource.IndexOf('& $umo install $ModListName', [StringComparison]::Ordinal)
    if ($checkIndex -lt 0 -or $patchIndex -le $checkIndex -or $syncIndex -le $patchIndex -or $installIndex -le $syncIndex) {
        throw "UMO installed-descriptor fix is not ordered as check -> patch -> sync -> install."
    }
    if (-not $umoInstallerSource.Contains('imod_data = copy.deepcopy(installed.get("mod_data"))') -or
        -not $umoInstallerSource.Contains('import copy$newline') -or
        -not $umoInstallerSource.Contains('"umo\handlers.py"')) {
        throw "UMO installed-descriptor fix is missing the required deep-copy source patch."
    }
    if ($umoInstallerSource.Contains('Repair-UmoPinnedInstalledDescriptorsAfterSync') -or
        $umoInstallerSource.Contains('cache patch installed "$entryPath.mod_data.pinned"')) {
        throw "Legacy UMO cache-repair workaround is still present."
    }

    $protocolHandlerMatch = [regex]::Match(
        $umoInstallerSource,
        '(?ms)^(function Get-UmoProtocolHandlerPath \{.*?^})\r?\n\r?\nfunction Test-UmoNxmHandler'
    )
    if (-not $protocolHandlerMatch.Success) {
        throw "Could not extract UMO protocol-handler functions for package regression tests."
    }
    Invoke-Expression $protocolHandlerMatch.Groups[1].Value
    $protocolHandlerFixture = Join-Path $workRoot "umo-protocol-handler-fixture"
    New-Item -ItemType Directory -Force -Path $protocolHandlerFixture | Out-Null
    $protocolHandlerUmo = Join-Path $protocolHandlerFixture "umo.exe"
    Set-Content -LiteralPath $protocolHandlerUmo -Value "fixture" -Encoding ASCII
    $expectedProtocolHandler = Join-Path $protocolHandlerFixture "umo-protocol-handler.cmd"
    $resolvedProtocolHandler = Get-UmoProtocolHandlerPath -UmoExecutable $protocolHandlerUmo
    if (-not $resolvedProtocolHandler.Equals($expectedProtocolHandler, [StringComparison]::OrdinalIgnoreCase)) {
        throw "UMO protocol handler is not colocated with umo.exe: $resolvedProtocolHandler"
    }
    $writtenProtocolHandler = Write-UmoProtocolHandler -UmoExecutable $protocolHandlerUmo
    if (-not $writtenProtocolHandler.Equals($expectedProtocolHandler, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $expectedProtocolHandler -PathType Leaf)) {
        throw "UMO protocol handler was not written beside umo.exe."
    }

    $moveLegacyMatch = [regex]::Match(
        $umoInstallerSource,
        '(?ms)^(function Move-LegacyUmoList \{.*?^})\r?\n\r?\nfunction Apply-UmoInstalledDescriptorComparisonPatch'
    )
    if (-not $moveLegacyMatch.Success) {
        throw "Could not extract Move-LegacyUmoList for dual-root migration regression tests."
    }
    Invoke-Expression $moveLegacyMatch.Groups[1].Value

    $dualRootFixture = Join-Path $workRoot "umo-dual-root-fixture"
    $dualBasePath = Join-Path $dualRootFixture "Data Files"
    $dualLegacyRoot = Join-Path $dualBasePath "fetcher-bardcraft"
    $dualCurrentRoot = Join-Path $dualBasePath "fetcher-simulator"
    $dualLegacyActive = Join-Path $dualLegacyRoot "Gameplay\Probe\active.txt"
    $dualCurrentActive = Join-Path $dualCurrentRoot "Gameplay\Probe\active.txt"
    $dualLegacyBackupOnly = Join-Path $dualLegacyRoot "Gameplay\BardcraftOpenMW\.fetcher-bardcraft-backups\2.0.21\player.lua"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dualLegacyActive), (Split-Path -Parent $dualCurrentActive), (Split-Path -Parent $dualLegacyBackupOnly) | Out-Null
    Set-Content -LiteralPath $dualLegacyActive -Value "legacy-active" -Encoding ASCII
    Set-Content -LiteralPath $dualCurrentActive -Value "current-active" -Encoding ASCII
    Set-Content -LiteralPath $dualLegacyBackupOnly -Value "legacy-backup-only" -Encoding ASCII
    $fakeUmo = Join-Path $dualRootFixture "umo.cmd"
    Set-Content -LiteralPath $fakeUmo -Value @('@echo off', 'exit /b 0') -Encoding ASCII

    Move-LegacyUmoList -UmoExecutable $fakeUmo -BasePath $dualBasePath -CurrentListName "fetcher-simulator"
    if (Test-Path -LiteralPath $dualLegacyRoot) {
        throw "Safe dual-root migration left the legacy UMO tree active."
    }
    if ((Get-Content -LiteralPath $dualCurrentActive -Raw).Trim() -ne "current-active") {
        throw "Safe dual-root migration overwrote the authoritative current UMO file."
    }
    $dualBackups = @(Get-ChildItem -LiteralPath (Join-Path $dualRootFixture "backups\umo-layout-migration") -Directory -Filter "fetcher-bardcraft-*")
    if ($dualBackups.Count -ne 1) {
        throw "Safe dual-root migration did not preserve exactly one legacy rollback tree."
    }
    if ((Get-Content -LiteralPath (Join-Path $dualBackups[0].FullName "Gameplay\Probe\active.txt") -Raw).Trim() -ne "legacy-active" -or
        -not (Test-Path -LiteralPath (Join-Path $dualBackups[0].FullName "Gameplay\BardcraftOpenMW\.fetcher-bardcraft-backups\2.0.21\player.lua") -PathType Leaf)) {
        throw "Safe dual-root migration did not preserve the complete legacy tree in rollback storage."
    }

    $unsafeRootFixture = Join-Path $workRoot "umo-dual-root-unsafe-fixture"
    $unsafeBasePath = Join-Path $unsafeRootFixture "Data Files"
    $unsafeLegacyRoot = Join-Path $unsafeBasePath "fetcher-bardcraft"
    $unsafeCurrentRoot = Join-Path $unsafeBasePath "fetcher-simulator"
    New-Item -ItemType Directory -Force -Path (Join-Path $unsafeLegacyRoot "Gameplay\Probe"), (Join-Path $unsafeCurrentRoot "Gameplay\Probe") | Out-Null
    Set-Content -LiteralPath (Join-Path $unsafeLegacyRoot "Gameplay\Probe\legacy-only.txt") -Value "must-not-drop" -Encoding ASCII
    $unsafeFakeUmo = Join-Path $unsafeRootFixture "umo.cmd"
    Set-Content -LiteralPath $unsafeFakeUmo -Value @('@echo off', 'exit /b 0') -Encoding ASCII
    $unsafeRejected = $false
    try {
        Move-LegacyUmoList -UmoExecutable $unsafeFakeUmo -BasePath $unsafeBasePath -CurrentListName "fetcher-simulator"
    }
    catch {
        $unsafeRejected = $true
    }
    if (-not $unsafeRejected -or -not (Test-Path -LiteralPath $unsafeLegacyRoot -PathType Container)) {
        throw "Dual-root migration did not reject an unrepresented active legacy file safely."
    }
    foreach ($expected in $requiredUmoMods) {
        $matches = @($umoMods | Where-Object { [string]$_.url -eq [string]$expected.Url })
        if ($matches.Count -ne 1) {
            throw "Expected one UMO entry for $($expected.Url), found $($matches.Count)."
        }
        $mod = $matches[0]
        $actualFileIds = @($mod.download_info | ForEach-Object {
            if ($null -ne $_.nexus_file_id) { [int64]$_.nexus_file_id }
        })
        foreach ($fileId in @($expected.FileIds)) {
            if ($actualFileIds -notcontains [int64]$fileId) {
                throw "$($mod.name) does not pin expected Nexus file id $fileId."
            }
        }
        if ($expected.PSObject.Properties.Name -contains "Ordered" -and [bool]$expected.Ordered) {
            if (($actualFileIds -join ",") -ne (@($expected.FileIds) -join ",")) {
                throw "$($mod.name) does not preserve the required download order."
            }
            if (($mod.data_paths -join "|") -ne (@($expected.DataPaths) -join "|")) {
                throw "$($mod.name) does not preserve the required data-path override order."
            }
        }
        if ($expected.PSObject.Properties.Name -contains "Pinned" -and
            @($mod.download_info | Where-Object { [bool]$_.pinned }).Count -ne @($expected.FileIds).Count) {
            throw "$($mod.name) does not mark every required artifact as pinned."
        }
        if ($expected.PSObject.Properties.Name -contains "DataPaths") {
            foreach ($dataPath in @($expected.DataPaths)) {
                if (@($mod.data_paths) -notcontains [string]$dataPath) {
                    throw "$($mod.name) is missing required data path: $dataPath"
                }
            }
        }
        foreach ($plugin in @($expected.Plugins)) {
            if (@($mod.plugins) -notcontains [string]$plugin) {
                throw "$($mod.name) is missing required plugin: $plugin"
            }
        }
        if ($expected.PSObject.Properties.Name -contains "Sha256") {
            $hashes = @($mod.download_info | ForEach-Object { [string]$_.sha256 })
            if ($hashes -notcontains [string]$expected.Sha256) {
                throw "$($mod.name) is missing its verified manual-download SHA-256."
            }
        }
    }

    $configScriptPath = Join-Path $workRoot "Apply-Fetcher-Public-Test-Config.ps1"
    $canonicalFallbackPath = Join-Path $workRoot "fetcher-canonical-fallbacks.cfg"
    $canonicalFallbackLines = @(Get-Content -LiteralPath $canonicalFallbackPath |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $_.Trim() })
    $canonicalFallbackUnique = @($canonicalFallbackLines | Sort-Object -Unique)
    if ($canonicalFallbackLines.Count -ne 655 -or $canonicalFallbackUnique.Count -ne 655) {
        throw "Canonical fallback template must contain exactly 655 unique entries."
    }
    if (@($canonicalFallbackLines | Where-Object { $_ -notmatch '^fallback\s*=' }).Count -ne 0) {
        throw "Canonical fallback template contains a non-fallback line."
    }
    $canonicalFallbackText = [string]::Join([Environment]::NewLine, $canonicalFallbackLines)
    $canonicalFallbackSha = [Security.Cryptography.SHA256]::Create()
    try {
        $canonicalFallbackHash = ([BitConverter]::ToString($canonicalFallbackSha.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($canonicalFallbackText)))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $canonicalFallbackSha.Dispose()
    }
    if ($canonicalFallbackHash -ne "405faafd00d23f32c57d96ed5b6289c8d3810b84ec238042a870ce6acc166153") {
        throw "Canonical fallback template hash is not the expected Fetcher authority hash."
    }

    $configFixtureRoot = Join-Path $workRoot "config-canonicalization-fixture"
    New-Item -ItemType Directory -Force -Path $configFixtureRoot | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $configFixtureRoot "resources\vfs-mw") | Out-Null
    Copy-Item -LiteralPath $configScriptPath -Destination (Join-Path $configFixtureRoot "Apply-Fetcher-Public-Test-Config.ps1")
    Copy-Item -LiteralPath $canonicalFallbackPath -Destination (Join-Path $configFixtureRoot "fetcher-canonical-fallbacks.cfg")
    $legacyFallbacks = @($canonicalFallbackLines | Select-Object -First 492)
    $fixtureDataRoot = Join-Path $configFixtureRoot "Data Files"
    New-Item -ItemType Directory -Force -Path $fixtureDataRoot | Out-Null
    foreach ($fixtureContent in @(
        "Morrowind.esm",
        "Tribunal.esm",
        "Bloodmoon.esm",
        "surf_mesa_mw.omwaddon",
        "surf_utopia_mw.omwaddon",
        "surf_kitsune.omwaddon",
        "surf_kitsune.omwscripts",
        "surf_kitsune2.omwaddon",
        "mp_phase7_test.omwscripts",
        "FetcherVehicles.omwaddon"
    )) {
        Set-Content -LiteralPath (Join-Path $fixtureDataRoot $fixtureContent) -Value "fixture" -Encoding ASCII
    }
    $fixtureConfigLines = @(
        'resources="./resources"'
    ) + $legacyFallbacks + @(
        'fallback=Fetcher_Test_NonCanonical,1',
        'content=OldNonCanonicalPlugin.esp'
    )
    $fixtureConfigPath = Join-Path $configFixtureRoot "openmw.cfg"
    Set-Content -LiteralPath $fixtureConfigPath -Value $fixtureConfigLines -Encoding ASCII
    & (Join-Path $configFixtureRoot "Apply-Fetcher-Public-Test-Config.ps1") 6>$null
    $repairedConfigLines = @(Get-Content -LiteralPath $fixtureConfigPath)
    $repairedFallbackLines = @($repairedConfigLines |
        Where-Object { $_ -match '^\s*fallback\s*=' } |
        ForEach-Object { $_.Trim() })
    $repairedFallbackText = [string]::Join([Environment]::NewLine, $repairedFallbackLines)
    $repairedFallbackSha = [Security.Cryptography.SHA256]::Create()
    try {
        $repairedFallbackHash = ([BitConverter]::ToString($repairedFallbackSha.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($repairedFallbackText)))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $repairedFallbackSha.Dispose()
    }
    if ($repairedFallbackLines.Count -ne 655 -or $repairedFallbackHash -ne $canonicalFallbackHash) {
        throw "Public-test config repair did not replace a 492-line fallback block with the canonical 655-line block."
    }
    if ($repairedConfigLines -contains 'fallback=Fetcher_Test_NonCanonical,1') {
        throw "Public-test config repair preserved an unmanaged fallback entry."
    }
    if (@($repairedConfigLines | Where-Object { $_ -eq '# BEGIN Fetcher Simulator canonical fallbacks' }).Count -ne 1 -or
        @($repairedConfigLines | Where-Object { $_ -eq '# END Fetcher Simulator canonical fallbacks' }).Count -ne 1) {
        throw "Public-test config repair did not emit exactly one bounded canonical fallback block."
    }
    if (@($repairedConfigLines | Where-Object { $_ -eq 'data=./Data Files' }).Count -ne 1) {
        throw "Public-test config repair did not restore the Fetcher client mod bundle data path."
    }
    if (@($repairedConfigLines | Where-Object { $_ -eq 'data=./resources/vfs-mw' }).Count -ne 1) {
        throw "Public-test config repair did not restore exactly one canonical OpenMW base VFS data path."
    }
    $baseVfsIndex = [Array]::IndexOf([string[]]$repairedConfigLines, 'data=./resources/vfs-mw')
    $clientDataIndex = [Array]::IndexOf([string[]]$repairedConfigLines, 'data=./Data Files')
    if ($baseVfsIndex -lt 0 -or $clientDataIndex -lt 0 -or $baseVfsIndex -ge $clientDataIndex) {
        throw "Public-test config repair did not place the OpenMW base VFS before Fetcher mod data paths."
    }

    $legacyBaseVfsLines = @(
        'data=".\resources\vfs-mw\"',
        ('data="{0}"' -f [IO.Path]::Combine([IO.Path]::GetPathRoot($configFixtureRoot), 'retired-fetcher-install', 'resources', 'vfs-mw'))
    )
    Set-Content -LiteralPath $fixtureConfigPath -Value @($legacyBaseVfsLines + $repairedConfigLines) -Encoding ASCII
    & (Join-Path $configFixtureRoot "Apply-Fetcher-Public-Test-Config.ps1") 6>$null
    $canonicalizedConfigLines = @(Get-Content -LiteralPath $fixtureConfigPath)
    if (@($canonicalizedConfigLines | Where-Object { $_ -match '(?i)^\s*data\s*=.*resources[\\/]vfs-mw' }).Count -ne 1 -or
        @($canonicalizedConfigLines | Where-Object { $_ -eq 'data=./resources/vfs-mw' }).Count -ne 1) {
        throw "Public-test config repair did not canonicalize legacy or duplicate OpenMW base VFS data paths."
    }

    Remove-Item -LiteralPath (Join-Path $fixtureDataRoot "FetcherVehicles.omwaddon") -Force
    $strictConfigRejectedMissingContent = $false
    try {
        & (Join-Path $configFixtureRoot "Apply-Fetcher-Public-Test-Config.ps1") 6>$null
    }
    catch {
        $strictConfigRejectedMissingContent = $true
    }
    if (-not $strictConfigRejectedMissingContent) {
        throw "Strict public-test config repair reported success with required content missing."
    }

    $configScript = Get-Content -LiteralPath $configScriptPath -Raw
    $fduPosition = $configScript.IndexOf('"FollowerDetectionUtil.omwscripts"', [StringComparison]::Ordinal)
    $bffPosition = $configScript.IndexOf('"BestFriendsForever.omwscripts"', [StringComparison]::Ordinal)
    if ($fduPosition -lt 0 -or $bffPosition -lt 0 -or $fduPosition -ge $bffPosition) {
        throw "Fetcher load order must place Follower Detection Util before Best Friends Forever."
    }

    $waresPosition = $configScript.IndexOf('"Wares-base.esm"', [StringComparison]::Ordinal)
    $backpackPosition = $configScript.IndexOf('"Adventurer''s backback.ESP"', [StringComparison]::Ordinal)
    $fashionwindAddonPosition = $configScript.IndexOf('"OMWFW_compilation.omwaddon"', [StringComparison]::Ordinal)
    $fashionwindScriptsPosition = $configScript.IndexOf('"OMWFW_compilation.omwscripts"', [StringComparison]::Ordinal)
    if ($waresPosition -lt 0 -or $backpackPosition -lt 0 -or
        $fashionwindAddonPosition -lt 0 -or $fashionwindScriptsPosition -lt 0 -or
        $waresPosition -ge $backpackPosition -or
        $backpackPosition -ge $fashionwindAddonPosition -or
        $fashionwindAddonPosition -ge $fashionwindScriptsPosition) {
        throw "Fetcher load order must place Wares before Adventurer's Backpacks and Fashionwind."
    }

    $compatibilityScriptPath = Join-Path $workRoot "Apply-Fetcher-ZHI-Compatibility.ps1"
    $managedCompatibilityScriptPath = Join-Path $workRoot "Apply-Fetcher-Mod-Compatibility.ps1"
    $managedCompatibilityManifestPath = Join-Path $workRoot "fetcher-mod-compatibility-patches.json"
    $managedCompatibilityManifest = Get-Content -LiteralPath $managedCompatibilityManifestPath -Raw | ConvertFrom-Json
    if ([int]$managedCompatibilityManifest.formatVersion -ne 1 -or
        [string]$managedCompatibilityManifest.patchVersion -ne "2026.09.01") {
        throw "Package has an unsupported managed mod compatibility manifest."
    }
    $expectedManagedOutputs = [ordered]@{
        "Data Files/fetcher-simulator/Items/FashionwindExpanded/scripts/Fashionwind_circlets/npc_circlets.lua" = "0e67dcc62e9463928cb45e814d224590a8c5398fd662e8bc9f2fdc93ebb7e344"
        "Data Files/fetcher-simulator/Items/FashionwindExpanded/scripts/Fashionwind_earrings/npc_earrings.lua" = "b4f6f5bf01dfb01809b1a565e30dcae0f96af5b1bfb0771070d282e9ecd79286"
        "Data Files/fetcher-simulator/Items/FashionwindExpanded/scripts/Fashionwind_glasses/npc_glasses.lua" = "fd7b8c08a76bde830fad0ee9fd91c191dee3d823afb26a8affdb2ca93450b6f9"
        "Data Files/fetcher-simulator/Items/FashionwindExpanded/scripts/Fashionwind_horns/npc_horny.lua" = "62a61e990cf28e3648d70bd33c305f9744b0c4e5a7ade8afb42469da58f769fd"
        "Data Files/fetcher-simulator/Items/FashionwindExpanded/scripts/Fashionwind_masks/npc_masks.lua" = "0e7f1b432cd974c70471dbe3cd009159fe8d3639994054e638d3531e264a1aa9"
        "Data Files/fetcher-simulator/Items/FashionwindExpanded/scripts/Fashionwind_scarves/npc_scarves.lua" = "d9429e9be406d00a8655a30ab7f87376305d4573b041d6295eb482beeb516ceb"
        "Data Files/fetcher-simulator/Items/FashionwindExpanded/scripts/OMWBackpacks/npc_backpacks.lua" = "2f769ceb492d45a1adbc7fba9dd5ada2ebd15e8bc2a9035fcb4f96f7e8937bcb"
        "Data Files/fetcher-simulator/Quests/DevilishTouchOfMadness/scripts/devilish_cliffracer_global.lua" = "b5c47ad91c1641d919befe59c425e0706b11a6e74c46b74af02a8a37365cb175"
        "resources/vfs/scripts/omw/input/playercontrols.lua" = "a5acc206e16403880fa08d61f408cbd3152416f95eb4568b07005ecc730b2ffa"
        "Data Files/fetcher-simulator/Races/Ratkinnrace/ratkinn/Clean ratkinn.ESP" = "ea59d8740e4a9dc73d0f56a29479db287290936a0419eb14311cef0f9046ee33"
        "Data Files/fetcher-simulator/Races/Ratkinnrace/ratkinn/data/meshes/rtf/b/rtf_empty_hair.nif" = "74cb03b220941ddf7f6ea4616ee67b75a5edbeabcf00c4d8b4d8c579e3ca5740"
    }
    if (@($managedCompatibilityManifest.files).Count -ne $expectedManagedOutputs.Count) {
        throw "Managed compatibility manifest does not contain the expected eleven compatibility patches."
    }
    foreach ($record in @($managedCompatibilityManifest.files)) {
        $relativePath = (ConvertTo-SafeRelativePath -Path ([string]$record.path)).Replace("\", "/")
        if (-not $expectedManagedOutputs.Contains($relativePath) -or
            ([string]$record.outputSha256).ToLowerInvariant() -ne $expectedManagedOutputs[$relativePath]) {
            throw "Managed mod compatibility manifest contains an unexpected output: $relativePath"
        }
        if ([string]$record.sourceSha256 -notmatch "^[0-9a-f]{64}$" -or
            [int64]$record.sourceSize -lt 0 -or
            [int64]$record.outputSize -le 0 -or
            @($record.operations).Count -eq 0) {
            throw "Managed mod compatibility record is incomplete: $relativePath"
        }
    }
    $ratkinnEmptyHairCompatibility = @($managedCompatibilityManifest.files | Where-Object {
        ([string]$_.path).Replace("\", "/") -eq "Data Files/fetcher-simulator/Races/Ratkinnrace/ratkinn/data/meshes/rtf/b/rtf_empty_hair.nif"
    })[0]
    if ([string]$ratkinnEmptyHairCompatibility.sourceSha256 -ne "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" -or
        [int64]$ratkinnEmptyHairCompatibility.sourceSize -ne 0 -or
        -not [bool]$ratkinnEmptyHairCompatibility.createIfMissing -or
        [string]$ratkinnEmptyHairCompatibility.requiresPath -ne "Data Files/fetcher-simulator/Races/Ratkinnrace/ratkinn/Clean ratkinn.ESP" -or
        [int64]$ratkinnEmptyHairCompatibility.outputSize -ne 178 -or
        @($ratkinnEmptyHairCompatibility.operations).Count -ne 1) {
        throw "Ratkinn empty-hair compatibility asset is not safely scoped as a create-only file."
    }
    $playerControlsCompatibility = @($managedCompatibilityManifest.files | Where-Object {
        ([string]$_.path).Replace("\", "/") -eq "resources/vfs/scripts/omw/input/playercontrols.lua"
    })[0]
    if ([string]$playerControlsCompatibility.sourceSha256 -ne "6337b3799a6e6fa5b192bc47a504fb77bbe4ad9eacd08f4ed9f1985914d03989" -or
        [int64]$playerControlsCompatibility.sourceSize -ne 10380 -or
        -not [bool]$playerControlsCompatibility.allowUnknownSource -or
        -not [bool]$playerControlsCompatibility.updateClientInventory) {
        throw "Player-controls compatibility repair is not safely scoped to the legacy line-ending mismatch."
    }
    $playerControlOps = @($playerControlsCompatibility.operations)
    if ($playerControlOps.Count -ne 9 -or
        [int]$playerControlOps[0].copyOffset -ne 0 -or [int]$playerControlOps[0].copyLength -ne 2237 -or
        [string]$playerControlOps[1].data -ne "DQo=" -or
        [int]$playerControlOps[2].copyOffset -ne 2238 -or [int]$playerControlOps[2].copyLength -ne 2198 -or
        [string]$playerControlOps[3].data -ne "DQo=" -or
        [int]$playerControlOps[4].copyOffset -ne 4437 -or [int]$playerControlOps[4].copyLength -ne 4131 -or
        [string]$playerControlOps[5].data -ne "DQo=" -or
        [int]$playerControlOps[6].copyOffset -ne 8569 -or [int]$playerControlOps[6].copyLength -ne 3 -or
        [string]$playerControlOps[7].data -ne "DQo=" -or
        [int]$playerControlOps[8].copyOffset -ne 8573 -or [int]$playerControlOps[8].copyLength -ne 1807) {
        throw "Player-controls compatibility repair does not reconstruct the canonical CRLF bytes exactly."
    }

    $managedFixtureRoot = Join-Path $workRoot "managed-compatibility-fixture"
    $managedFixtureTarget = Join-Path $managedFixtureRoot "Data Files\fixture\script.lua"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $managedFixtureTarget) | Out-Null
    $managedSourceBytes = [Text.Encoding]::UTF8.GetBytes("before`n")
    $managedOutputBytes = [Text.Encoding]::UTF8.GetBytes("after`n")
    [IO.File]::WriteAllBytes($managedFixtureTarget, $managedSourceBytes)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $managedSourceHash = ([BitConverter]::ToString($sha.ComputeHash($managedSourceBytes))).Replace("-", "").ToLowerInvariant()
        $managedOutputHash = ([BitConverter]::ToString($sha.ComputeHash($managedOutputBytes))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
    $managedFixtureManifestPath = Join-Path $workRoot "managed-compatibility-fixture.json"
    [ordered]@{
        schemaVersion = 1
        protectionPolicyVersion = 1
        clientCommit = ("a" * 40)
        generatedAtUtc = [DateTime]::UtcNow.ToString("o")
        files = @([ordered]@{
            path = "Data Files/fixture/script.lua"
            size = $managedSourceBytes.Length
            sha256 = $managedSourceHash
        })
    } | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $managedFixtureRoot "fetcher-client-files.json") -Encoding UTF8
    [ordered]@{
        formatVersion = 1
        patchVersion = "fixture"
        files = @([ordered]@{
            path = "Data Files/fixture/script.lua"
            sourceSha256 = $managedSourceHash
            sourceSize = $managedSourceBytes.Length
            outputSha256 = $managedOutputHash
            outputSize = $managedOutputBytes.Length
            updateClientInventory = $true
            operations = @([ordered]@{ data = [Convert]::ToBase64String($managedOutputBytes) })
        })
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $managedFixtureManifestPath -Encoding UTF8

    & $managedCompatibilityScriptPath -InstallRoot $managedFixtureRoot -ManifestPath $managedFixtureManifestPath | Out-Null
    if ((Get-FileHash -LiteralPath $managedFixtureTarget -Algorithm SHA256).Hash.ToLowerInvariant() -ne $managedOutputHash) {
        throw "Managed mod compatibility fixture did not install its verified output."
    }
    $managedFixtureInventoryRecord = @(Get-Content -LiteralPath (Join-Path $managedFixtureRoot "fetcher-client-files.json") -Raw |
        ConvertFrom-Json | Select-Object -ExpandProperty files)[0]
    if ([string]$managedFixtureInventoryRecord.sha256 -ne $managedOutputHash -or
        [int64]$managedFixtureInventoryRecord.size -ne $managedOutputBytes.Length) {
        throw "Managed compatibility fixture did not update the client inventory for its patched output."
    }
    & $managedCompatibilityScriptPath -InstallRoot $managedFixtureRoot -ManifestPath $managedFixtureManifestPath | Out-Null
    $managedBackupPath = Join-Path $managedFixtureRoot "_fetcher_update\compatibility-backups\fixture\Data Files\fixture\script.lua"
    if (-not (Test-Path -LiteralPath $managedBackupPath -PathType Leaf) -or
        (Get-FileHash -LiteralPath $managedBackupPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $managedSourceHash) {
        throw "Managed mod compatibility fixture did not preserve its verified upstream backup."
    }
    [IO.File]::WriteAllBytes($managedFixtureTarget, [Text.Encoding]::UTF8.GetBytes("local edit`n"))
    $managedRejectedLocalEdit = $false
    try {
        & $managedCompatibilityScriptPath -InstallRoot $managedFixtureRoot -ManifestPath $managedFixtureManifestPath | Out-Null
    }
    catch {
        $managedRejectedLocalEdit = $true
    }
    if (-not $managedRejectedLocalEdit) {
        throw "Managed mod compatibility fixture did not reject an unknown local edit."
    }
    $managedForwardCompatibleManifest = Get-Content -LiteralPath $managedFixtureManifestPath -Raw | ConvertFrom-Json
    $managedForwardCompatibleManifest.files[0] | Add-Member -NotePropertyName allowUnknownSource -NotePropertyValue $true
    $managedForwardCompatibleManifest | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $managedFixtureManifestPath -Encoding UTF8
    & $managedCompatibilityScriptPath -InstallRoot $managedFixtureRoot -ManifestPath $managedFixtureManifestPath | Out-Null

    $managedUnsafeManifestPath = Join-Path $workRoot "managed-compatibility-unsafe.json"
    [ordered]@{
        formatVersion = 1
        patchVersion = "unsafe-fixture"
        files = @([ordered]@{
            path = "../escape.lua"
            sourceSha256 = $managedSourceHash
            sourceSize = $managedSourceBytes.Length
            outputSha256 = $managedOutputHash
            outputSize = $managedOutputBytes.Length
            operations = @([ordered]@{ data = [Convert]::ToBase64String($managedOutputBytes) })
        })
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $managedUnsafeManifestPath -Encoding UTF8
    $managedRejectedUnsafePath = $false
    try {
        & $managedCompatibilityScriptPath -InstallRoot $managedFixtureRoot -ManifestPath $managedUnsafeManifestPath | Out-Null
    }
    catch {
        $managedRejectedUnsafePath = $true
    }
    if (-not $managedRejectedUnsafePath) {
        throw "Managed mod compatibility fixture accepted a target outside the installation root."
    }

    $managedCreateRoot = Join-Path $workRoot "managed-create-fixture"
    New-Item -ItemType Directory -Force -Path $managedCreateRoot | Out-Null
    $managedCreateTarget = Join-Path $managedCreateRoot "Data Files\fixture\created.bin"
    $managedCreateAnchor = Join-Path $managedCreateRoot "Data Files\fixture\anchor.mod"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $managedCreateAnchor) | Out-Null
    Set-Content -LiteralPath $managedCreateAnchor -Encoding ASCII -Value "anchor"
    $managedCreateBytes = [Text.Encoding]::UTF8.GetBytes("created`n")
    $managedCreateHashProvider = [Security.Cryptography.SHA256]::Create()
    try {
        $managedCreateHash = ([BitConverter]::ToString($managedCreateHashProvider.ComputeHash($managedCreateBytes))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $managedCreateHashProvider.Dispose()
    }
    $managedCreateManifestPath = Join-Path $workRoot "managed-compatibility-create.json"
    [ordered]@{
        formatVersion = 1
        patchVersion = "create-fixture"
        files = @([ordered]@{
            path = "Data Files/fixture/created.bin"
            sourceSha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
            sourceSize = 0
            outputSha256 = $managedCreateHash
            outputSize = $managedCreateBytes.Length
            createIfMissing = $true
            requiresPath = "Data Files/fixture/anchor.mod"
            operations = @([ordered]@{ data = [Convert]::ToBase64String($managedCreateBytes) })
        })
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $managedCreateManifestPath -Encoding UTF8

    & $managedCompatibilityScriptPath -InstallRoot $managedCreateRoot -ManifestPath $managedCreateManifestPath | Out-Null
    if (-not (Test-Path -LiteralPath $managedCreateTarget -PathType Leaf) -or
        (Get-FileHash -LiteralPath $managedCreateTarget -Algorithm SHA256).Hash.ToLowerInvariant() -ne $managedCreateHash) {
        throw "Managed mod compatibility fixture did not create its verified missing target."
    }
    $managedCreateBackup = Join-Path $managedCreateRoot "_fetcher_update\compatibility-backups\create-fixture\Data Files\fixture\created.bin"
    if (Test-Path -LiteralPath $managedCreateBackup) {
        throw "Managed create-only compatibility fixture unexpectedly backed up a file that did not exist."
    }
    & $managedCompatibilityScriptPath -InstallRoot $managedCreateRoot -ManifestPath $managedCreateManifestPath | Out-Null
    $managedMissingPrerequisiteRoot = Join-Path $workRoot "managed-create-missing-prerequisite-fixture"
    New-Item -ItemType Directory -Force -Path $managedMissingPrerequisiteRoot | Out-Null
    & $managedCompatibilityScriptPath -InstallRoot $managedMissingPrerequisiteRoot -ManifestPath $managedCreateManifestPath | Out-Null
    $managedMissingPrerequisiteTarget = Join-Path $managedMissingPrerequisiteRoot "Data Files\fixture\created.bin"
    if (Test-Path -LiteralPath $managedMissingPrerequisiteTarget) {
        throw "Managed create-only compatibility fixture ignored its missing prerequisite."
    }
    [IO.File]::WriteAllBytes($managedCreateTarget, [Text.Encoding]::UTF8.GetBytes("foreign`n"))
    $managedCreateRejectedForeignFile = $false
    try {
        & $managedCompatibilityScriptPath -InstallRoot $managedCreateRoot -ManifestPath $managedCreateManifestPath | Out-Null
    }
    catch {
        $managedCreateRejectedForeignFile = $true
    }
    if (-not $managedCreateRejectedForeignFile) {
        throw "Managed create-only compatibility fixture overwrote an unexpected existing file."
    }

    $zhiFixtureRoot = Join-Path $workRoot "zhi-fixture"
    $zhiPlayerPath = Join-Path $zhiFixtureRoot "scripts\ZerkishHotkeysImproved\zhi_player.lua"
    $zhiHotbarPath = Join-Path $zhiFixtureRoot "scripts\ZerkishHotkeysImproved\zhi_hotbarhud.lua"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $zhiPlayerPath) | Out-Null
    Set-Content -LiteralPath $zhiPlayerPath -Encoding UTF8 -Value @'
local ZHISaveData = {}
local sDisableFirstTimeNotification = false
if not (ZHISaveData.onCloseQuickKeyMenuFirstTimeFlag or sDisableFirstTimeNotification) then
    openFirstTimePopup()
end
'@
    Set-Content -LiteralPath $zhiHotbarPath -Encoding UTF8 -Value @'
local icon = {
    name = "icon",
    props = {
        autoSize = true,
        inheritAlpha = false,
    },
}
'@

    $zhiLegacyFixtureRoot = Join-Path $workRoot "zhi-legacy-fixture"
    $zhiLegacyPlayerPath = Join-Path $zhiLegacyFixtureRoot "scripts\ZerkishHotkeysImproved\zhi_player.lua"
    $zhiLegacyHotbarPath = Join-Path $zhiLegacyFixtureRoot "scripts\ZerkishHotkeysImproved\zhi_hotbarhud.lua"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $zhiLegacyPlayerPath) | Out-Null
    Set-Content -LiteralPath $zhiLegacyPlayerPath -Encoding UTF8 -Value @'
local ZHISaveData = {}
local sDisableFirstTimeNotification = false
if false and not (ZHISaveData.onCloseQuickKeyMenuFirstTimeFlag or sDisableFirstTimeNotification) then -- Fetcher multiplayer compatibility: suppress the first-time modal during character creation.
    openFirstTimePopup()
end
'@
    Set-Content -LiteralPath $zhiLegacyHotbarPath -Encoding UTF8 -Value @'
local icon = {
    name = "icon",
    props = {
        inheritAlpha = false,
    },
}
'@

    $zhiBomFixtureRoot = Join-Path $workRoot "zhi-current-bom-fixture"
    $zhiBomPlayerPath = Join-Path $zhiBomFixtureRoot "scripts\ZerkishHotkeysImproved\zhi_player.lua"
    $zhiBomHotbarPath = Join-Path $zhiBomFixtureRoot "scripts\ZerkishHotkeysImproved\zhi_hotbarhud.lua"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $zhiBomPlayerPath) | Out-Null
    $utf8WithBom = [Text.UTF8Encoding]::new($true)
    [IO.File]::WriteAllText($zhiBomPlayerPath, @'
local ZHISaveData = {}
local sDisableFirstTimeNotification = false
if false and not (ZHISaveData.onCloseQuickKeyMenuFirstTimeFlag or sDisableFirstTimeNotification) then -- Fetcher multiplayer compatibility: suppress the automatic first-time modal; onboarding still occurs when Quick Keys is opened.
    openFirstTimePopup()
end
'@, $utf8WithBom)
    [IO.File]::WriteAllText($zhiBomHotbarPath, @'
local icon = {
    name = "icon",
    props = {
        inheritAlpha = false,
    },
}
'@, $utf8WithBom)

    $fduFixtureRoot = Join-Path $workRoot "fdu-fixture"
    $fduActorPath = Join-Path $fduFixtureRoot "scripts\FollowerDetectionUtil\actor.lua"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $fduActorPath) | Out-Null
    Set-Content -LiteralPath $fduActorPath -Encoding UTF8 -Value @'
local settings = {}
local updateTime = math.random() * settings.checkFollowersEvery
local function onUpdate(dt)
    local interval = settings.checkFollowersEvery
    updateTime = updateTime + dt
    if updateTime < interval then return end
end
'@

    $bffFixtureRoot = Join-Path $workRoot "bff-fixture"
    $bffSettingsPath = Join-Path $bffFixtureRoot "scripts\BestFriendsForever\settingsPlayer.lua"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $bffSettingsPath) | Out-Null
    Set-Content -LiteralPath $bffSettingsPath -Encoding UTF8 -Value @'
local I = require('openmw.interfaces')
local util = require("openmw.util")
local ui = require("openmw.ui")

I.Settings.registerPage {
    key = 'BestFriendsForever',
    l10n = 'BestFriendsForever',
    name = 'page_name',
    description = 'page_description',
}

I.Settings.registerGroup {
    key = 'SettingsBestFriendsForever_call',
    page = 'BestFriendsForever',
    l10n = 'BestFriendsForever',
    name = 'call_groupName',
    permanentStorage = true,
    settings = {},
}
'@

    $takeControlFixtureRoot = Join-Path $workRoot "take-control-fixture"
    $takeControlPlayerPath = Join-Path $takeControlFixtureRoot "Scripts\TakeControl\Player.lua"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $takeControlPlayerPath) | Out-Null
    Set-Content -LiteralPath $takeControlPlayerPath -Encoding UTF8 -Value @'
local CoopActor=self
local CamDistance=0
local CamFixed=true
local RayObject

local function onSave()
    return{
        CoopActorSaved=CoopActor,
        CamDistanceSaved=CamDistance}
end

local function onLoad(data)
    if data then
        CoopActor=data.CoopActorSaved
        CamDistance=data.CamDistanceSaved
    end
    I.UI.setHudVisibility(false)
    core.sendGlobalEvent("Activations",{state=false, player=self})
end

local ItemDescription={layout={props={}},update=function() end}

local function TakeControl(data)
    if CoopActor and CoopActor~=self then
        CoopActor:sendEvent("StopControl")
    end
    CoopActor=data.actor
    ui.showMessage(CoopActor.type.records[CoopActor.recordId].name)
    I.Controls.overrideMovementControls(true)
    I.Controls.overrideCombatControls(true)
    if I.InventoryExtender then I.InventoryExtender.ChangeActor(CoopActor) end
    if I.MagicWindow then I.MagicWindow.ChangeActor(CoopActor) end
    if I.StatsWindow then I.StatsWindow.ChangeActor(CoopActor) end
    if I.YetAnotherHUD then I.YetAnotherHUD.ChangeActor(CoopActor) end
    I.UI.setHudVisibility(false)
    core.sendGlobalEvent("Activations",{state=false, player=self})
    CamDistance=CoopActor:getBoundingBox().halfSize.z*3
    CamFixed=false
end

local function QuiteControl()
    I.Controls.overrideMovementControls(false)
    I.Controls.overrideCombatControls(false)
    I.UI.setHudVisibility(true)
    core.sendGlobalEvent("Activations",{state=true, player=self})
    camera.setMode(camera.MODE.ThirdPerson)
    CoopActor:sendEvent("StopControl")
    CoopActor=self
    if I.InventoryExtender then I.InventoryExtender.ChangeActor(self) end
    if I.MagicWindow then I.MagicWindow.ChangeActor(self) end
    if I.StatsWindow then I.StatsWindow.ChangeActor(self) end
    if I.YetAnotherHUD then I.YetAnotherHUD.ChangeActor(self) end
    ItemDescription.layout.props.visible=false
    ItemDescription:update()
end

input.registerTriggerHandler("Idle2", async:callback(function ()
    if CoopActor~=self then
        CoopActor:sendEvent("Idle",{Num=2})
    end
end))

local function onUpdate(dt)
    if CoopActor and CoopActor.id~=self.id then
        camera.setMode(camera.MODE.Static)
        camera.setStaticPosition(ActorCamPosition)
    end
end
'@

    $compatibilityParameters = @{
        InstallRoot = $workRoot
        ZhiDataRoot = $zhiFixtureRoot
        FollowerDetectionUtilDataRoot = $fduFixtureRoot
        BestFriendsForeverDataRoot = $bffFixtureRoot
        TakeControlDataRoot = $takeControlFixtureRoot
    }
    & $compatibilityScriptPath @compatibilityParameters | Out-Null
    if (-not $?) {
        throw "Fetcher multiplayer compatibility fixtures failed."
    }
    & $compatibilityScriptPath @compatibilityParameters | Out-Null
    if (-not $?) {
        throw "Fetcher multiplayer compatibility fixtures were not idempotent."
    }

    $legacyCompatibilityParameters = $compatibilityParameters.Clone()
    $legacyCompatibilityParameters["ZhiDataRoot"] = $zhiLegacyFixtureRoot
    & $compatibilityScriptPath @legacyCompatibilityParameters | Out-Null
    if (-not $?) {
        throw "Fetcher legacy ZHI compatibility fixture migration failed."
    }
    & $compatibilityScriptPath @legacyCompatibilityParameters | Out-Null
    if (-not $?) {
        throw "Fetcher legacy ZHI compatibility fixture migration was not idempotent."
    }

    $bomCompatibilityParameters = $compatibilityParameters.Clone()
    $bomCompatibilityParameters["ZhiDataRoot"] = $zhiBomFixtureRoot
    & $compatibilityScriptPath @bomCompatibilityParameters | Out-Null
    if (-not $?) {
        throw "Fetcher current ZHI UTF-8 BOM normalization fixture failed."
    }
    & $compatibilityScriptPath @bomCompatibilityParameters | Out-Null
    if (-not $?) {
        throw "Fetcher current ZHI UTF-8 BOM normalization fixture was not idempotent."
    }

    $zhiPlayerSource = Get-Content -LiteralPath $zhiPlayerPath -Raw
    $zhiPopupMarker = "Fetcher multiplayer compatibility: suppress the automatic first-time modal; onboarding still occurs when Quick Keys is opened."
    $zhiPopupReplacement = "if false and not (ZHISaveData.onCloseQuickKeyMenuFirstTimeFlag or sDisableFirstTimeNotification) then -- $zhiPopupMarker"
    if (([regex]::Matches($zhiPlayerSource, [regex]::Escape($zhiPopupReplacement))).Count -ne 1) {
        throw "Zerkish Hotkeys Improved compatibility fix did not suppress exactly one automatic first-time popup."
    }
    $zhiHotbarSource = Get-Content -LiteralPath $zhiHotbarPath -Raw
    if ($zhiHotbarSource.Contains("autoSize = true")) {
        throw "Zerkish Hotkeys Improved compatibility fix did not remove the invalid hotbar icon autoSize property."
    }

    $zhiLegacyPlayerSource = Get-Content -LiteralPath $zhiLegacyPlayerPath -Raw
    $zhiLegacyPopupMarker = "Fetcher multiplayer compatibility: suppress the first-time modal during character creation."
    if ($zhiLegacyPlayerSource.Contains($zhiLegacyPopupMarker)) {
        throw "Zerkish Hotkeys Improved compatibility fix did not remove the legacy popup marker."
    }
    if (([regex]::Matches($zhiLegacyPlayerSource, [regex]::Escape($zhiPopupReplacement))).Count -ne 1) {
        throw "Zerkish Hotkeys Improved compatibility fix did not migrate the legacy popup suppression exactly once."
    }
    foreach ($canonicalZhiPath in @($zhiPlayerPath, $zhiHotbarPath, $zhiLegacyPlayerPath,
            $zhiLegacyHotbarPath, $zhiBomPlayerPath, $zhiBomHotbarPath)) {
        $canonicalBytes = [IO.File]::ReadAllBytes($canonicalZhiPath)
        if ($canonicalBytes.Length -ge 3 -and $canonicalBytes[0] -eq 0xEF -and
            $canonicalBytes[1] -eq 0xBB -and $canonicalBytes[2] -eq 0xBF) {
            throw "Zerkish Hotkeys Improved compatibility fix left a UTF-8 BOM in $canonicalZhiPath."
        }
    }

    $fduActorSource = Get-Content -LiteralPath $fduActorPath -Raw
    $expectedFduStartupFallback = "local updateTime = math.random() * (settings.checkFollowersEvery or 0.2) -- Fetcher multiplayer compatibility: storage defaults can be unavailable before the server mirror arrives."
    $expectedFduUpdateFallback = "local interval = settings.checkFollowersEvery or 0.2 -- Fetcher multiplayer compatibility: storage defaults can be unavailable before the server mirror arrives."
    foreach ($expectedFallback in @($expectedFduStartupFallback, $expectedFduUpdateFallback)) {
        if (([regex]::Matches($fduActorSource, [regex]::Escape($expectedFallback))).Count -ne 1) {
            throw "Follower Detection Util compatibility fix did not add exactly one required multiplayer interval fallback."
        }
    }

    $bffSettingsSource = Get-Content -LiteralPath $bffSettingsPath -Raw
    $bffMarker = "Fetcher multiplayer compatibility: wait for mirrored global settings groups before registering this page."
    foreach ($expectedGroup in @(
        "SettingsBestFriendsForever_toggles",
        "SettingsBestFriendsForever_blacklist",
        "SettingsBestFriendsForever_immortality",
        "SettingsBestFriendsForever_catchUp"
    )) {
        if (-not $bffSettingsSource.Contains($expectedGroup)) {
            throw "Best Friends Forever compatibility fix is missing required mirrored group $expectedGroup."
        }
    }
    if (([regex]::Matches($bffSettingsSource, [regex]::Escape($bffMarker))).Count -ne 1 -or
        -not $bffSettingsSource.Contains("tryRegisterBestFriendsForeverPage()") -or
        -not $bffSettingsSource.Contains("storage.globalSection('OmwSettingGroups')")) {
        throw "Best Friends Forever compatibility fix did not defer page registration until global groups are mirrored."
    }

    $takeControlPlayerSource = Get-Content -LiteralPath $takeControlPlayerPath -Raw
    $takeControlStaleMarker = "Fetcher multiplayer compatibility: object handles are session-local and must not survive reconnects."
    $takeControlCameraMarker = "Fetcher multiplayer compatibility: setStaticPosition requires Static mode to be active."
    $remainingControlModeDisable = 'core.sendGlobalEvent("Activations",{state=false, player=self})'
    if (([regex]::Matches($takeControlPlayerSource, [regex]::Escape($takeControlStaleMarker))).Count -ne 1 -or
        ([regex]::Matches($takeControlPlayerSource, [regex]::Escape($takeControlCameraMarker))).Count -ne 1 -or
        -not $takeControlPlayerSource.Contains("local function changeInterfaceActor(actor)") -or
        -not $takeControlPlayerSource.Contains("local function resetControlToPlayer()") -or
        -not $takeControlPlayerSource.Contains("local function isControllingOtherActor()") -or
        -not $takeControlPlayerSource.Contains("return { CamDistanceSaved=CamDistance }") -or
        $takeControlPlayerSource.Contains("CoopActorSaved") -or
        -not $takeControlPlayerSource.Contains("CamDistance=data.CamDistanceSaved or 0") -or
        -not $takeControlPlayerSource.Contains("if not data or not data.actor or not data.actor:isValid()") -or
        -not $takeControlPlayerSource.Contains("or not types.Actor.objectIsInstance(data.actor) then") -or
        -not $takeControlPlayerSource.Contains("changeInterfaceActor(CoopActor)") -or
        -not $takeControlPlayerSource.Contains("if isControllingOtherActor() then") -or
        $takeControlPlayerSource.Contains("if CoopActor~=self then") -or
        $takeControlPlayerSource.Contains("if CoopActor and CoopActor.id~=self.id then") -or
        -not $takeControlPlayerSource.Contains("if camera.getMode() ~= camera.MODE.Static then return end") -or
        ([regex]::Matches($takeControlPlayerSource, [regex]::Escape($remainingControlModeDisable))).Count -ne 1) {
        throw "Take Control compatibility fix did not prevent stale actor restoration while preserving control-mode activation and Static camera sequencing."
    }
}
finally {
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}

Write-Host "Fetcher tester-tools package is valid: $resolvedArchive"
