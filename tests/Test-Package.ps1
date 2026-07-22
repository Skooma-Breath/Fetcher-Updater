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
    $protectionPolicy = Get-Content -LiteralPath (Join-Path $workRoot "fetcher-client-protection-policy.json") -Raw |
        ConvertFrom-Json
    if ([int]$protectionPolicy.schemaVersion -ne 1 -or
        @($protectionPolicy.exactPaths).Count -eq 0 -or
        @($protectionPolicy.prefixes).Count -eq 0) {
        throw "Package contains an unsupported client protection policy."
    }

    $umoListPath = Join-Path $workRoot "fetcher-bardcraft-umo.json"
    if (-not (Test-Path -LiteralPath $umoListPath -PathType Leaf)) {
        throw "Package is missing fetcher-bardcraft-umo.json."
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
        if (@($mod.download_info).Count -eq 0 -or @($mod.data_paths).Count -eq 0 -or
            -not (@($mod.on_lists) -contains "fetcher-bardcraft")) {
            throw "UMO mod entry is incomplete: $($mod.name)"
        }
    }

    $requiredUmoMods = @(
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
            Url = "https://www.nexusmods.com/morrowind/mods/26309"
            FileIds = @(46321)
            Plugins = @("FF7 Tsurugi Resource.esp")
        }
    )

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
    $configScript = Get-Content -LiteralPath $configScriptPath -Raw
    $fduPosition = $configScript.IndexOf('"FollowerDetectionUtil.omwscripts"', [StringComparison]::Ordinal)
    $bffPosition = $configScript.IndexOf('"BestFriendsForever.omwscripts"', [StringComparison]::Ordinal)
    if ($fduPosition -lt 0 -or $bffPosition -lt 0 -or $fduPosition -ge $bffPosition) {
        throw "Fetcher load order must place Follower Detection Util before Best Friends Forever."
    }
}
finally {
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}

Write-Host "Fetcher tester-tools package is valid: $resolvedArchive"
