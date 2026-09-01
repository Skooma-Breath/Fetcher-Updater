param(
    [string] $UmoPath = "",
    [string] $ModListName = "fetcher-simulator",
    [string] $ModListFile = "",
    [string] $ModListAssetName = "fetcher-simulator-umo.json",
    [string] $ModListUrl = "",
    [string] $TesterToolsRepository = "Skooma-Breath/Fetcher-Updater",
    [string] $TesterToolsReleaseTag = "fetcher-tester-tools",
    [string] $GitHubDownloadBaseUrl = "https://github.com",
    [string] $UmoBasePath = "",
    [bool] $DownloadUmoIfMissing = $true,
    [string] $Tes3cmdPath = "",
    [string] $Tes3cmdUrl = "https://gitlab.com/modding-openmw/tes3cmd/-/jobs/artifacts/master/raw/tes3cmd.0.40-PRE-RELEASE-2-win.zip?job=build_win",
    [bool] $DownloadTes3cmdIfMissing = $true,
    [string] $SevenZipPath = "",
    [string] $SevenZipReleaseApiUrl = "https://api.github.com/repos/ip7z/7zip/releases/latest",
    [bool] $DownloadSevenZipIfMissing = $true,
    [bool] $RequireMorrowindData = $true,
    [bool] $ApplyBardcraftMultiplayerPatch = $true,
    [bool] $ApplyPublicTestConfig = $true,
    [string] $BardcraftPatchAssetName = "fetcher-bardcraft-mp-patch-v2.zip",
    [string] $BardcraftPatchUrl = "https://github.com/Skooma-Breath/Fetcher-Bardcraft/releases/download/fetcher-bardcraft-mp-patch-v2/fetcher-bardcraft-mp-patch-v2.zip",
    [string] $BardcraftPatchReleaseApiUrl = "https://api.github.com/repos/Skooma-Breath/Fetcher-Bardcraft/releases/tags/fetcher-bardcraft-mp-patch-v2",
    [string] $BardcraftPatchSha256 = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($ModListUrl)) {
    $ModListUrl = "$($GitHubDownloadBaseUrl.TrimEnd('/'))/$TesterToolsRepository/releases/download/$([Uri]::EscapeDataString($TesterToolsReleaseTag))/$([Uri]::EscapeDataString($ModListAssetName))"
}
if ([string]::IsNullOrWhiteSpace($UmoBasePath)) {
    $UmoBasePath = Join-Path $root "Data Files"
}
New-Item -ItemType Directory -Force -Path $UmoBasePath | Out-Null
$UmoBasePath = (Resolve-Path -LiteralPath $UmoBasePath).Path

function Resolve-UmoPath {
    param([string] $Candidate)

    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Candidate)) {
        $candidates.Add($Candidate)
    }
    $candidates.Add((Join-Path $root "umo.exe"))
    $candidates.Add((Join-Path $root "umo\umo.exe"))
    $candidates.Add((Join-Path $root "tools\umo\umo.exe"))

    foreach ($path in $candidates) {
        if (Test-Path -LiteralPath $path) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "umo.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    $command = Get-Command "umo" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    if ($DownloadUmoIfMissing) {
        return Install-UmoIfMissing
    }

    throw "Could not find umo.exe. Put umo.exe next to this BAT file, add it to PATH, or run with -UmoPath C:\path\to\umo.exe."
}

function Install-UmoIfMissing {
    $targetPath = Join-Path $root "umo.exe"
    if (Test-Path -LiteralPath $targetPath) {
        return (Resolve-Path -LiteralPath $targetPath).Path
    }

    $downloadRoot = Join-Path $root "_fetcher_umo"
    $extractRoot = Join-Path $downloadRoot "umo-win"
    $zipPath = Join-Path $downloadRoot "umo-win.zip"
    New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null

    function ConvertTo-FlatArray {
        param($Value)

        if ($null -eq $Value) {
            return @()
        }

        if ($Value -is [System.Array]) {
            return @($Value | ForEach-Object { $_ })
        }

        return @($Value)
    }

    $packagesUri = "https://gitlab.com/api/v4/projects/modding-openmw%2Fumo/packages?package_name=umo&sort=desc&per_page=5"
    Write-Host "umo.exe was not found. Downloading UMO for Windows..."
    Write-Host "  $packagesUri"

    $packages = @(ConvertTo-FlatArray (Invoke-RestMethod -Uri $packagesUri))
    $package = $packages |
        Where-Object { $_.package_type -eq "generic" -and $_.status -eq "default" } |
        Select-Object -First 1
    if (-not $package) {
        throw "Could not find a current UMO generic package from GitLab."
    }

    [long] $packageId = 0
    if (-not [long]::TryParse(([string] $package.id), [ref] $packageId)) {
        throw "GitLab returned an invalid UMO package id: $($package.id)"
    }
    $packageVersion = [string] $package.version
    if ([string]::IsNullOrWhiteSpace($packageVersion)) {
        throw "GitLab returned UMO package $packageId without a version."
    }

    $packageFile = $null
    $packageFilesUri = "https://gitlab.com/api/v4/projects/modding-openmw%2Fumo/packages/$packageId/package_files?per_page=100"
    try {
        $packageFiles = @(ConvertTo-FlatArray (Invoke-RestMethod -Uri $packageFilesUri))
        $packageFile = $packageFiles |
            Where-Object { $_.file_name -eq "umo-win.zip" } |
            Select-Object -First 1
    }
    catch {
        Write-Warning "Could not query UMO package files by id; falling back to GitLab generic package download. Details: $($_.Exception.Message)"
    }

    if ($packageFile -and $packageFile.id) {
        $downloadUri = "https://gitlab.com/modding-openmw/umo/-/package_files/$($packageFile.id)/download"
    }
    else {
        $escapedVersion = [System.Uri]::EscapeDataString($packageVersion)
        $downloadUri = "https://gitlab.com/api/v4/projects/modding-openmw%2Fumo/packages/generic/umo/$escapedVersion/umo-win.zip"
    }

    Write-Host "Downloading UMO ${packageVersion}:"
    Write-Host "  $downloadUri"
    Invoke-WebRequest -UseBasicParsing -Uri $downloadUri -OutFile $zipPath

    if ($packageFile -and -not [string]::IsNullOrWhiteSpace($packageFile.file_sha256)) {
        $actualHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $expectedHash = ([string]$packageFile.file_sha256).ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "Downloaded UMO checksum mismatch. Expected $expectedHash but got $actualHash."
        }
    }

    if (Test-Path -LiteralPath $extractRoot) {
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractRoot -Force

    $extractedUmo = Get-ChildItem -LiteralPath $extractRoot -Recurse -File -Filter "umo.exe" |
        Select-Object -First 1
    if (-not $extractedUmo) {
        throw "UMO downloaded, but umo.exe was not found inside $zipPath."
    }

    Copy-Item -LiteralPath $extractedUmo.FullName -Destination $targetPath -Force
    Write-Host "UMO installed to: $targetPath"
    return (Resolve-Path -LiteralPath $targetPath).Path
}

function Resolve-Tes3cmdPath {
    param([string] $Candidate)

    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Candidate)) {
        $candidates.Add($Candidate)
    }
    $candidates.Add((Join-Path $root "tes3cmd.exe"))
    $candidates.Add((Join-Path $root "tes3cmd\tes3cmd.exe"))
    $candidates.Add((Join-Path $root "tools\tes3cmd\tes3cmd.exe"))

    foreach ($path in $candidates) {
        if (Test-Path -LiteralPath $path) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "tes3cmd.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    $command = Get-Command "tes3cmd" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    if ($DownloadTes3cmdIfMissing) {
        return Install-Tes3cmdIfMissing
    }

    throw "Could not find tes3cmd.exe. Put tes3cmd.exe next to this BAT file, add it to PATH, or run with -Tes3cmdPath C:\path\to\tes3cmd.exe."
}

function Install-Tes3cmdIfMissing {
    $targetPath = Join-Path $root "tes3cmd.exe"
    if (Test-Path -LiteralPath $targetPath) {
        return (Resolve-Path -LiteralPath $targetPath).Path
    }

    $downloadRoot = Join-Path $root "_fetcher_umo"
    $extractRoot = Join-Path $downloadRoot "tes3cmd-win"
    $zipPath = Join-Path $downloadRoot "tes3cmd-win.zip"
    New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null

    Write-Host "tes3cmd.exe was not found. Downloading tes3cmd for Windows..."
    Write-Host "  $Tes3cmdUrl"
    Invoke-WebRequest -UseBasicParsing -Uri $Tes3cmdUrl -OutFile $zipPath

    if (Test-Path -LiteralPath $extractRoot) {
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractRoot -Force

    $extractedTes3cmd = Get-ChildItem -LiteralPath $extractRoot -Recurse -File -Filter "tes3cmd*.exe" |
        Select-Object -First 1
    if (-not $extractedTes3cmd) {
        throw "tes3cmd downloaded, but no tes3cmd executable was found inside $zipPath."
    }

    Copy-Item -LiteralPath $extractedTes3cmd.FullName -Destination $targetPath -Force
    Write-Host "tes3cmd installed to: $targetPath"
    return (Resolve-Path -LiteralPath $targetPath).Path
}

function Resolve-SevenZipDirectory {
    param([string] $Candidate)

    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Candidate)) {
        $candidates.Add($Candidate)
    }
    $candidates.Add((Join-Path $root "_fetcher_umo\7zip\7z.exe"))
    $candidates.Add((Join-Path $root "7z.exe"))
    if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
        $candidates.Add((Join-Path $env:ProgramFiles "7-Zip\7z.exe"))
    }
    if (-not [string]::IsNullOrWhiteSpace(${env:ProgramFiles(x86)})) {
        $candidates.Add((Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe"))
    }

    foreach ($path in $candidates) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Split-Path -Parent (Resolve-Path -LiteralPath $path).Path)
        }
    }

    $command = Get-Command "7z.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return (Split-Path -Parent $command.Source)
    }

    if ($DownloadSevenZipIfMissing) {
        return Install-SevenZipIfMissing
    }

    throw "Could not find the full 7z.exe. Install 7-Zip, put 7z.exe on PATH, or run with -SevenZipPath C:\path\to\7z.exe."
}

function Find-ManualModArchive {
    param([Parameter(Mandatory = $true)][string] $ArchiveName)

    $candidates = @(
        (Join-Path $root $ArchiveName),
        (Join-Path (Join-Path $root "_fetcher_umo\manual-downloads") $ArchiveName),
        (Join-Path (Join-Path ([Environment]::GetFolderPath("UserProfile")) "Downloads") $ArchiveName)
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Test-ManualModInstalled {
    param([Parameter(Mandatory = $true)] $Mod)

    $dataRoots = @()
    foreach ($dataPath in @($Mod.data_paths)) {
        $candidate = Join-Path (Join-Path (Join-Path $UmoBasePath $ModListName) ([string]$Mod.category)) ([string]$dataPath)
        if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
            return $false
        }
        $dataRoots += $candidate
    }
    foreach ($plugin in @($Mod.plugins)) {
        if (-not ($dataRoots | Where-Object { Test-Path -LiteralPath (Join-Path $_ ([string]$plugin)) -PathType Leaf })) {
            return $false
        }
    }
    return $dataRoots.Count -gt 0
}

function Install-ManualModDownloads {
    param(
        [Parameter(Mandatory = $true)][string] $ListPath,
        [Parameter(Mandatory = $true)][string] $SevenZipDirectory
    )

    $sevenZipExecutable = Join-Path $SevenZipDirectory "7z.exe"
    # Windows PowerShell 5.1 writes a top-level JSON array as one pipeline
    # object. Enumerate it explicitly so foreach receives each mod entry.
    $parsedMods = Get-Content -Raw -LiteralPath $ListPath | ConvertFrom-Json
    $mods = @($parsedMods | ForEach-Object { $_ })
    foreach ($mod in $mods) {
        if ([string]$mod.url -notmatch "^https://www\.moddb\.com/") {
            continue
        }
        if (Test-ManualModInstalled -Mod $mod) {
            Write-Host "$($mod.name) is already installed."
            continue
        }

        foreach ($download in @($mod.download_info)) {
            $archiveName = [string]$download.file_name
            $archivePath = Find-ManualModArchive -ArchiveName $archiveName
            if ($null -eq $archivePath) {
                Write-Host "Opening the official ModDB download page for $($mod.name):"
                Write-Host "  $($mod.url)"
                Start-Process ([string]$mod.url)
                [void](Read-Host "Download $archiveName into your Windows Downloads folder, then press Enter")
                $archivePath = Find-ManualModArchive -ArchiveName $archiveName
            }
            if ($null -eq $archivePath) {
                throw "Could not find $archiveName. Download it from $($mod.url) and run the installer again."
            }

            if ($download.PSObject.Properties.Name -contains "sha256") {
                $expectedHash = ([string]$download.sha256).ToLowerInvariant()
                $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
                if ($actualHash -ne $expectedHash) {
                    throw "$archiveName checksum mismatch. Expected $expectedHash but got $actualHash."
                }
            }

            $listRoot = [IO.Path]::GetFullPath((Join-Path $UmoBasePath $ModListName)).TrimEnd("\") + "\"
            $targetRoot = [IO.Path]::GetFullPath((Join-Path (Join-Path $listRoot ([string]$mod.category)) ([string]$download.extract_to)))
            if (-not $targetRoot.StartsWith($listRoot, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Manual mod extraction path escapes the UMO list root: $targetRoot"
            }
            if (Test-Path -LiteralPath $targetRoot) {
                Remove-Item -LiteralPath $targetRoot -Recurse -Force
            }
            New-Item -ItemType Directory -Force -Path $targetRoot | Out-Null
            Write-Host "Installing $($mod.name) from $archivePath..."
            & $sevenZipExecutable x $archivePath "-o$targetRoot" -y -aoa | Out-Host
            if ($LASTEXITCODE -ne 0) {
                throw "$($mod.name) extraction failed with exit code $LASTEXITCODE."
            }
        }

        if (-not (Test-ManualModInstalled -Mod $mod)) {
            throw "$($mod.name) extracted, but its declared data path or plugin was not found."
        }
    }
}

function Assert-GitHubAssetChecksum {
    param(
        [Parameter(Mandatory = $true)] $Asset,
        [Parameter(Mandatory = $true)][string] $FilePath
    )

    $digest = [string] $Asset.digest
    if ([string]::IsNullOrWhiteSpace($digest) -or $digest -notmatch "^sha256:([0-9a-fA-F]{64})$") {
        Write-Warning "GitHub did not provide a SHA-256 digest for $($Asset.name); continuing after HTTPS download."
        return
    }

    $expectedHash = $Matches[1].ToLowerInvariant()
    $actualHash = (Get-FileHash -LiteralPath $FilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "Downloaded $($Asset.name) checksum mismatch. Expected $expectedHash but got $actualHash."
    }
}

function Install-SevenZipIfMissing {
    $downloadRoot = Join-Path $root "_fetcher_umo\7zip-bootstrap"
    $targetRoot = Join-Path $root "_fetcher_umo\7zip"
    $targetPath = Join-Path $targetRoot "7z.exe"
    $targetDll = Join-Path $targetRoot "7z.dll"
    if ((Test-Path -LiteralPath $targetPath -PathType Leaf) -and
        (Test-Path -LiteralPath $targetDll -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $targetRoot).Path
    }

    New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $targetRoot | Out-Null

    Write-Host "7-Zip with RAR support was not found. Downloading a portable copy..."
    Write-Host "  $SevenZipReleaseApiUrl"
    $headers = @{ "User-Agent" = "Fetcher-Simulator-Installer" }
    $release = Invoke-RestMethod -Uri $SevenZipReleaseApiUrl -Headers $headers
    $bootstrapAsset = @($release.assets) |
        Where-Object { $_.name -eq "7zr.exe" } |
        Select-Object -First 1
    $installerAsset = @($release.assets) |
        Where-Object { $_.name -match "^7z[0-9]+-x64\.exe$" } |
        Select-Object -First 1
    if (-not $bootstrapAsset -or -not $installerAsset) {
        throw "The latest official 7-Zip release does not contain the expected 7zr.exe and x64 installer assets."
    }

    $bootstrapPath = Join-Path $downloadRoot "7zr.exe"
    $installerPath = Join-Path $downloadRoot ([string] $installerAsset.name)
    Invoke-WebRequest -UseBasicParsing -Uri $bootstrapAsset.browser_download_url -Headers $headers -OutFile $bootstrapPath
    Invoke-WebRequest -UseBasicParsing -Uri $installerAsset.browser_download_url -Headers $headers -OutFile $installerPath
    Assert-GitHubAssetChecksum -Asset $bootstrapAsset -FilePath $bootstrapPath
    Assert-GitHubAssetChecksum -Asset $installerAsset -FilePath $installerPath

    & $bootstrapPath x $installerPath "-o$targetRoot" -y -aoa | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Portable 7-Zip extraction failed with exit code $LASTEXITCODE."
    }
    if (-not (Test-Path -LiteralPath $targetPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $targetDll -PathType Leaf)) {
        throw "7-Zip extraction completed, but 7z.exe or 7z.dll was not found under $targetRoot."
    }

    Write-Host "Portable 7-Zip installed to: $targetRoot"
    return (Resolve-Path -LiteralPath $targetRoot).Path
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock] $Command,
        [Parameter(Mandatory = $true)]
        [string] $Description
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Move-LegacyUmoList {
    param(
        [Parameter(Mandatory = $true)][string] $UmoExecutable,
        [Parameter(Mandatory = $true)][string] $BasePath,
        [Parameter(Mandatory = $true)][string] $CurrentListName,
        [string] $LegacyListName = "fetcher-bardcraft"
    )

    if ($CurrentListName -ne "fetcher-simulator" -or $LegacyListName -eq $CurrentListName) {
        return
    }

    $legacyRoot = Join-Path $BasePath $LegacyListName
    $currentRoot = Join-Path $BasePath $CurrentListName
    $legacyRootExists = Test-Path -LiteralPath $legacyRoot -PathType Container
    $currentRootExists = Test-Path -LiteralPath $currentRoot -PathType Container

    $quarantineLegacyRoot = $false
    if ($legacyRootExists -and $currentRootExists) {
        $legacyPrefix = $legacyRoot.TrimEnd([char[]]"\/") + [IO.Path]::DirectorySeparatorChar
        $unrepresentedActiveFiles = New-Object System.Collections.Generic.List[string]
        foreach ($legacyFile in @(Get-ChildItem -LiteralPath $legacyRoot -Recurse -File)) {
            $relativePath = $legacyFile.FullName.Substring($legacyPrefix.Length)
            $portableRelativePath = $relativePath.Replace("\", "/")
            if ($portableRelativePath -match '(^|/)\.fetcher-bardcraft-backups(/|$)') {
                continue
            }
            $currentPath = Join-Path $currentRoot $relativePath
            if (-not (Test-Path -LiteralPath $currentPath -PathType Leaf)) {
                $unrepresentedActiveFiles.Add($portableRelativePath)
            }
        }
        if ($unrepresentedActiveFiles.Count -gt 0) {
            $sample = @($unrepresentedActiveFiles | Select-Object -First 10) -join ', '
            throw "Both legacy and current UMO list folders exist, and the current tree is missing $($unrepresentedActiveFiles.Count) active legacy file(s). Refusing automatic migration. Examples: $sample"
        }
        $quarantineLegacyRoot = $true
    }

    $knownLists = @(& $UmoExecutable list local | ForEach-Object { ([string]$_).Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($LASTEXITCODE -ne 0) {
        throw "Could not inspect UMO list state before migrating '$LegacyListName'."
    }
    $legacyKnown = $knownLists -contains $LegacyListName
    $currentKnown = $knownLists -contains $CurrentListName

    if ($legacyKnown -and $currentKnown) {
        throw "Both legacy and current UMO list cache entries exist. Refusing to overwrite '$CurrentListName' with '$LegacyListName'."
    }

    $movedFolder = $false
    $quarantinedFolder = $false
    $legacyBackupPath = $null
    try {
        if ($quarantineLegacyRoot) {
            $installRoot = Split-Path -Parent $BasePath
            $backupRoot = Join-Path $installRoot "backups\umo-layout-migration"
            New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
            $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
            $legacyBackupPath = Join-Path $backupRoot ("{0}-{1}" -f $LegacyListName, $stamp)
            $suffix = 0
            while (Test-Path -LiteralPath $legacyBackupPath) {
                ++$suffix
                $legacyBackupPath = Join-Path $backupRoot ("{0}-{1}-{2}" -f $LegacyListName, $stamp, $suffix)
            }
            Write-Host "Current UMO tree already contains every active legacy path. Preserving the legacy tree as a rollback backup:"
            Write-Host "  $legacyRoot"
            Write-Host "  -> $legacyBackupPath"
            Move-Item -LiteralPath $legacyRoot -Destination $legacyBackupPath
            $quarantinedFolder = $true
        }
        elseif ($legacyRootExists) {
            Write-Host "Migrating UMO list folder:"
            Write-Host "  $legacyRoot"
            Write-Host "  -> $currentRoot"
            Move-Item -LiteralPath $legacyRoot -Destination $currentRoot
            $movedFolder = $true
        }

        if ($legacyKnown) {
            Write-Host "Migrating UMO cached list state: $LegacyListName -> $CurrentListName"
            Invoke-Checked -Description "umo list rename $LegacyListName $CurrentListName" -Command {
                & $UmoExecutable list rename $LegacyListName $CurrentListName
            }
        }
    }
    catch {
        if ($movedFolder -and (Test-Path -LiteralPath $currentRoot -PathType Container) -and -not (Test-Path -LiteralPath $legacyRoot)) {
            Move-Item -LiteralPath $currentRoot -Destination $legacyRoot
        }
        if ($quarantinedFolder -and -not [string]::IsNullOrWhiteSpace($legacyBackupPath) -and
            (Test-Path -LiteralPath $legacyBackupPath -PathType Container) -and -not (Test-Path -LiteralPath $legacyRoot)) {
            Move-Item -LiteralPath $legacyBackupPath -Destination $legacyRoot
        }
        throw
    }
}

function Apply-UmoInstalledDescriptorComparisonPatch {
    param([Parameter(Mandatory = $true)][string] $UmoExecutable)

    $resolvedUmo = (Resolve-Path -LiteralPath $UmoExecutable).Path
    $handlersPath = Join-Path (Split-Path -Parent $resolvedUmo) "umo\handlers.py"
    if (-not (Test-Path -LiteralPath $handlersPath -PathType Leaf)) {
        throw "UMO runtime handlers.py was not found next to $resolvedUmo. Run UMO once, then retry. Expected: $handlersPath"
    }

    $source = [IO.File]::ReadAllText($handlersPath)
    $vulnerable = '    imod_data = installed.get("mod_data")'
    $fixed = '    imod_data = copy.deepcopy(installed.get("mod_data"))'

    if ($source.Contains($fixed)) {
        if ($source -notmatch '(?m)^import copy\r?$') {
            throw "UMO handlers.py contains the Fetcher deep-copy fix but is missing import copy: $handlersPath"
        }
        Write-Host "UMO installed-descriptor comparison patch is current."
        return
    }

    $matches = [regex]::Matches($source, [regex]::Escape($vulnerable))
    if ($matches.Count -eq 0) {
        Write-Host "UMO installed-descriptor comparison patch is not required for this UMO version."
        return
    }
    if ($matches.Count -ne 1) {
        throw "Expected exactly one vulnerable UMO installed-descriptor comparison, found $($matches.Count): $handlersPath"
    }

    if ($source -notmatch '(?m)^import copy\r?$') {
        $importMatch = [regex]::Match($source, '(?m)^import json\r?$')
        if (-not $importMatch.Success) {
            throw "Could not locate UMO import block in: $handlersPath"
        }
        $newline = if ($source.Contains("`r`n")) { "`r`n" } else { "`n" }
        $source = $source.Insert($importMatch.Index, "import copy$newline")
    }

    $source = $source.Replace($vulnerable, $fixed)
    [IO.File]::WriteAllText($handlersPath, $source, [Text.UTF8Encoding]::new($false))
    Write-Host "Applied Fetcher UMO installed-descriptor comparison patch:"
    Write-Host "  $handlersPath"
}

function Apply-UmoKnownBadDigestPatch {
    param([Parameter(Mandatory = $true)][string] $UmoExecutable)

    $resolvedUmo = (Resolve-Path -LiteralPath $UmoExecutable).Path
    $dlPath = Join-Path (Split-Path -Parent $resolvedUmo) "umo\dl.py"
    if (-not (Test-Path -LiteralPath $dlPath -PathType Leaf)) {
        throw "UMO runtime dl.py was not found next to $resolvedUmo. Expected: $dlPath"
    }

    $source = [IO.File]::ReadAllText($dlPath)
    $anchorHash = '"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"'
    $doorsHash = '"17c9b2b43ca7a297359bb2076ddd8afef75d6197c3cc3b581e4b14e182ec1f88"'

    if ($source.Contains($doorsHash)) {
        Write-Host "UMO known-bad Nexus digest patch is current."
        return
    }

    $matches = [regex]::Matches($source, [regex]::Escape($anchorHash))
    if ($matches.Count -ne 1) {
        throw "Could not safely locate the UMO known-bad digest list in: $dlPath"
    }

    $newline = if ($source.Contains("`r`n")) { "`r`n" } else { "`n" }
    $replacement = $anchorHash + "," + $newline +
        "    # Fetcher: Nexus advertises a stale SHA-256 for The Doors of Oblivion 1.4 (file 1000010485)." + $newline +
        "    # Skip that digest so UMO validates the completed Nexus archive through its MD5 lookup path instead." + $newline +
        "    " + $doorsHash + ","
    $source = $source.Replace($anchorHash, $replacement)
    [IO.File]::WriteAllText($dlPath, $source, [Text.UTF8Encoding]::new($false))
    Write-Host "Applied Fetcher UMO known-bad Nexus digest patch:"
    Write-Host "  $dlPath"
}

function Get-UmoProtocolHandlerPath {
    param([Parameter(Mandatory = $true)][string] $UmoExecutable)

    $resolvedUmo = (Resolve-Path -LiteralPath $UmoExecutable).Path
    $umoRoot = Split-Path -Parent (Split-Path -Parent $resolvedUmo)
    return (Join-Path $umoRoot "umo-protocol-handler.cmd")
}

function Get-UmoProtocolHandlerCommand {
    param([Parameter(Mandatory = $true)][string] $HandlerPath)

    $cmdExe = Join-Path $env:SystemRoot "System32\cmd.exe"
    return $cmdExe + ' /d /s /c ""' + $HandlerPath + '" "%1""'
}

function Write-UmoProtocolHandler {
    param([Parameter(Mandatory = $true)][string] $UmoExecutable)

    $resolvedUmo = (Resolve-Path -LiteralPath $UmoExecutable).Path
    $handlerPath = Get-UmoProtocolHandlerPath -UmoExecutable $resolvedUmo
    $lines = @(
        '@echo off',
        'setlocal',
        ('set "UMO_BASEPATH=' + $env:UMO_BASEPATH + '"'),
        ('set "UMO_CACHE_DIR=' + $env:UMO_CACHE_DIR + '"'),
        ('set "UMO_CONF_DIR=' + $env:UMO_CONF_DIR + '"'),
        ('set "UMO_CONF_FILE=' + $env:UMO_CONF_FILE + '"'),
        ('set "UMO_TES3CMD=' + $env:UMO_TES3CMD + '"'),
        ('"' + $resolvedUmo + '" "%~1"'),
        'exit /b %ERRORLEVEL%'
    )
    Set-Content -LiteralPath $handlerPath -Value $lines -Encoding ASCII
    return $handlerPath
}

function Test-UmoNxmHandler {
    param([Parameter(Mandatory = $true)][string] $UmoExecutable)

    $commandKey = "Registry::HKEY_CURRENT_USER\Software\Classes\nxm\shell\open\command"
    try {
        $command = [string] (Get-Item -LiteralPath $commandKey -ErrorAction Stop).GetValue("")
    }
    catch {
        return $false
    }

    if ([string]::IsNullOrWhiteSpace($command)) {
        return $false
    }

    $handlerPath = Get-UmoProtocolHandlerPath -UmoExecutable $UmoExecutable
    if (-not (Test-Path -LiteralPath $handlerPath -PathType Leaf)) {
        return $false
    }
    $expectedCommand = Get-UmoProtocolHandlerCommand -HandlerPath $handlerPath
    return $command.Equals($expectedCommand, [System.StringComparison]::OrdinalIgnoreCase)
}

function Set-UmoUrlHandler {
    param(
        [Parameter(Mandatory = $true)][string] $Scheme,
        [Parameter(Mandatory = $true)][string] $HandlerPath
    )

    $schemeKey = "Registry::HKEY_CURRENT_USER\Software\Classes\$Scheme"
    $commandKey = Join-Path $schemeKey "shell\open\command"
    New-Item -Path $commandKey -Force | Out-Null
    Set-Item -LiteralPath $schemeKey -Value "URL:$Scheme Protocol"
    New-ItemProperty -LiteralPath $schemeKey -Name "URL Protocol" -Value "" -PropertyType String -Force | Out-Null
    Set-Item -LiteralPath $commandKey -Value (Get-UmoProtocolHandlerCommand -HandlerPath $HandlerPath)
}

function Initialize-UmoProtocolHandler {
    param([Parameter(Mandatory = $true)][string] $UmoExecutable)

    $handlerPath = Write-UmoProtocolHandler -UmoExecutable $UmoExecutable
    if (Test-UmoNxmHandler -UmoExecutable $UmoExecutable) {
        return
    }

    Write-Host ""
    Write-Host "Registering this portable UMO as the Nexus download handler..."
    Set-UmoUrlHandler -Scheme "nxm" -HandlerPath $handlerPath
    Set-UmoUrlHandler -Scheme "momw" -HandlerPath $handlerPath

    if (-not (Test-UmoNxmHandler -UmoExecutable $UmoExecutable)) {
        throw "Windows did not register the Fetcher UMO protocol wrapper for nxm:// links."
    }
}

function Test-MorrowindDataConfigured {
    $cfgPath = Join-Path $root "openmw.cfg"
    if (-not (Test-Path -LiteralPath $cfgPath -PathType Leaf)) {
        return $false
    }

    foreach ($line in Get-Content -LiteralPath $cfgPath) {
        if ($line -notmatch "^\s*data\s*=\s*(.+?)\s*$") {
            continue
        }

        $dataPath = $Matches[1].Trim()
        if (($dataPath.StartsWith('"') -and $dataPath.EndsWith('"')) -or
            ($dataPath.StartsWith("'") -and $dataPath.EndsWith("'"))) {
            $dataPath = $dataPath.Substring(1, $dataPath.Length - 2)
        }
        $dataPath = [Environment]::ExpandEnvironmentVariables($dataPath)
        if (-not [System.IO.Path]::IsPathRooted($dataPath)) {
            $dataPath = Join-Path $root $dataPath
        }

        if (Test-Path -LiteralPath (Join-Path $dataPath "Morrowind.esm") -PathType Leaf) {
            return $true
        }
    }

    return $false
}

function Resolve-BardcraftDataRoot {
    $parsedMods = Get-Content -Raw -LiteralPath $ModListFile | ConvertFrom-Json
    if ($parsedMods -is [System.Array]) {
        $mods = @($parsedMods | ForEach-Object { $_ })
    }
    else {
        $mods = @($parsedMods)
    }
    $bardcraft = $mods |
        Where-Object { @($_.plugins) -contains "Bardcraft.ESP" } |
        Select-Object -First 1
    if (-not $bardcraft) {
        throw "The UMO modlist does not define the Bardcraft.ESP data root."
    }

    foreach ($dataPath in @($bardcraft.data_paths)) {
        if ([string]::IsNullOrWhiteSpace([string]$dataPath)) {
            continue
        }
        $candidate = Join-Path (Join-Path (Join-Path $UmoBasePath $ModListName) ([string]$bardcraft.category)) ([string]$dataPath)
        if ((Test-Path -LiteralPath (Join-Path $candidate "Bardcraft.ESP") -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $candidate "scripts\Bardcraft") -PathType Container)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "UMO finished, but the installed Bardcraft data root could not be found under $UmoBasePath\$ModListName."
}

function Install-BardcraftMultiplayerPatch {
    param([Parameter(Mandatory = $true)][string] $BardcraftDataRoot)

    $expectedHash = ([string]$BardcraftPatchSha256).Trim().ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($expectedHash)) {
        Write-Host "Reading current Bardcraft multiplayer patch checksum from GitHub..."
        $release = Invoke-RestMethod -UseBasicParsing -Uri $BardcraftPatchReleaseApiUrl `
            -Headers @{ "User-Agent" = "Fetcher-Simulator-Installer" }
        if ($release -is [string]) {
            $release = $release | ConvertFrom-Json
        }
        $assets = @($release.assets | Where-Object { [string]$_.name -eq $BardcraftPatchAssetName })
        if ($assets.Count -ne 1) {
            throw "Expected one GitHub release asset named $BardcraftPatchAssetName, found $($assets.Count)."
        }
        $digest = [string]$assets[0].digest
        if ($digest -notmatch "^sha256:([0-9a-fA-F]{64})$") {
            throw "GitHub did not provide a SHA-256 digest for $BardcraftPatchAssetName."
        }
        $expectedHash = $Matches[1].ToLowerInvariant()
    }
    elseif ($expectedHash -notmatch "^[0-9a-f]{64}$") {
        throw "BardcraftPatchSha256 must be a 64-character SHA-256 hash."
    }

    $patchRoot = Join-Path $umoWorkRoot "bardcraft-mp-patch"
    $extractRoot = Join-Path $patchRoot "extracted"
    $zipPath = Join-Path $patchRoot $BardcraftPatchAssetName
    New-Item -ItemType Directory -Force -Path $patchRoot | Out-Null

    $downloadRequired = $true
    if (Test-Path -LiteralPath $zipPath -PathType Leaf) {
        $downloadRequired = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash
    }

    if ($downloadRequired) {
        Write-Host "Downloading Fetcher Bardcraft multiplayer patch:"
        Write-Host "  $BardcraftPatchUrl"
        Invoke-WebRequest -UseBasicParsing -Uri $BardcraftPatchUrl -OutFile $zipPath
    }

    $actualHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "Downloaded Bardcraft patch checksum mismatch. Expected $expectedHash but got $actualHash."
    }

    if (Test-Path -LiteralPath $extractRoot) {
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractRoot -Force

    $appliers = @(Get-ChildItem -LiteralPath $extractRoot -Recurse -File -Filter "Apply-Fetcher-Bardcraft-MPPatch.ps1")
    if ($appliers.Count -ne 1) {
        throw "Expected exactly one Bardcraft patch applier in $zipPath, found $($appliers.Count)."
    }

    Write-Host "Applying Fetcher Bardcraft multiplayer compatibility patch..."
    & $appliers[0].FullName -BardcraftDataRoot $BardcraftDataRoot
}

if ($RequireMorrowindData -and -not (Test-MorrowindDataConfigured)) {
    throw "Morrowind.esm is not configured for this portable install. Run openmw-wizard.exe, point it at your Morrowind installation, then run this installer again."
}

$umo = Resolve-UmoPath $UmoPath
$tes3cmd = Resolve-Tes3cmdPath $Tes3cmdPath
$sevenZipDirectory = Resolve-SevenZipDirectory $SevenZipPath
$umoWorkRoot = Join-Path $root "_fetcher_umo"
$umoConfigDir = Join-Path $umoWorkRoot "config"
$umoCacheDir = Join-Path $umoWorkRoot "cache"
$umoConfigFile = Join-Path $umoConfigDir "config.json"
New-Item -ItemType Directory -Force -Path $umoConfigDir | Out-Null
New-Item -ItemType Directory -Force -Path $umoCacheDir | Out-Null

Write-Host "Using UMO: $umo"
Write-Host "Using tes3cmd: $tes3cmd"
Write-Host "Using 7-Zip: $sevenZipDirectory"
Write-Host "UMO mod install root: $UmoBasePath"
$env:PATH = "$sevenZipDirectory;$root;$env:PATH"
$env:UMO_BASEPATH = $UmoBasePath
$env:UMO_CACHE_DIR = $umoCacheDir
$env:UMO_CONF_DIR = $umoConfigDir
$env:UMO_CONF_FILE = $umoConfigFile
$env:UMO_TES3CMD = $tes3cmd

if ([string]::IsNullOrWhiteSpace($ModListFile)) {
    $localModListCandidates = @(
        (Join-Path $root $ModListAssetName),
        (Join-Path $root "$ModListName.json")
    )
    foreach ($localModList in $localModListCandidates) {
        if (Test-Path -LiteralPath $localModList) {
            $ModListFile = $localModList
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($ModListFile)) {
    $cacheDir = Join-Path $root "_fetcher_umo"
    New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
    $ModListFile = Join-Path $cacheDir $ModListAssetName

    Write-Host "Downloading UMO modlist:"
    Write-Host "  $ModListUrl"
    try {
        Invoke-WebRequest -UseBasicParsing -Uri $ModListUrl -OutFile $ModListFile
    }
    catch {
        throw "Could not download $ModListUrl. The Fetcher Simulator UMO modlist may not be published yet. You can also place $ModListAssetName next to this BAT and run it again. Details: $($_.Exception.Message)"
    }
}

if (-not (Test-Path -LiteralPath $ModListFile)) {
    throw "Could not find UMO modlist file: $ModListFile"
}

$ModListFile = (Resolve-Path -LiteralPath $ModListFile).Path
Write-Host "Using modlist: $ModListFile"
Write-Host ""
Write-Host "Checking UMO setup..."
Invoke-Checked -Description "umo check" -Command {
    if (Test-Path -LiteralPath $umoConfigFile) {
        & $umo check
    }
    else {
        $firstRunAnswers = @(
            $UmoBasePath,
            $umoCacheDir
        )
        $firstRunAnswers | & $umo check
    }
}
Apply-UmoInstalledDescriptorComparisonPatch -UmoExecutable $umo
Apply-UmoKnownBadDigestPatch -UmoExecutable $umo
Initialize-UmoProtocolHandler -UmoExecutable $umo
Move-LegacyUmoList -UmoExecutable $umo -BasePath $UmoBasePath -CurrentListName $ModListName

Write-Host ""
Write-Host "Registering UMO modlist..."
Invoke-Checked -Description "umo list add" -Command {
    & $umo list add $ModListFile --list-name $ModListName
}

Write-Host ""
Write-Host "Syncing UMO mod metadata..."
Invoke-Checked -Description "umo sync $ModListName" -Command {
    & $umo sync $ModListName --skip-momw
}


Write-Host ""
Write-Host "Installing UMO modlist. Non-premium Nexus users may need to click the Nexus download pages that UMO opens."
Write-Host "Large Nexus downloads may look quiet in this console while UMO is working. Let this window keep running."
Write-Host "Mods will be extracted under: $UmoBasePath\$ModListName"
Invoke-Checked -Description "umo install $ModListName" -Command {
    & $umo install $ModListName
}

Install-ManualModDownloads -ListPath $ModListFile -SevenZipDirectory $sevenZipDirectory

if ($ApplyBardcraftMultiplayerPatch) {
    Write-Host ""
    $bardcraftDataRoot = Resolve-BardcraftDataRoot
    Install-BardcraftMultiplayerPatch -BardcraftDataRoot $bardcraftDataRoot
}

if ($ApplyPublicTestConfig) {
    $applyConfig = Join-Path $root "Apply-Fetcher-Public-Test-Config.ps1"
    if (Test-Path -LiteralPath $applyConfig) {
        Write-Host ""
        Write-Host "Applying Fetcher public test OpenMW load order..."
        & $applyConfig -AllowMissingContent
    }
    else {
        Write-Warning "Could not find Apply-Fetcher-Public-Test-Config.ps1. Run Apply-Fetcher-Public-Test-Config.bat manually after installing mods."
    }
}

Write-Host ""
Write-Host "Done. If OpenMW still reports missing content, rerun Apply-Fetcher-Public-Test-Config.bat after UMO finishes extracting all mods."
