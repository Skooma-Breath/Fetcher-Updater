[CmdletBinding()]
param(
    [string] $InstallRoot = "",
    [string] $ZhiDataRoot = "",
    [string] $HookshotDataRoot = "",
    [string] $FollowerDetectionUtilDataRoot = "",
    [string] $BestFriendsForeverDataRoot = "",
    [string] $TakeControlDataRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# This filename is retained for compatibility with older tester-tool updaters.
# The script now applies small, idempotent compatibility fixes to multiple UMO mods.

function Test-DataRoot {
    param(
        [Parameter(Mandatory = $true)][string] $DataRoot,
        [Parameter(Mandatory = $true)][string[]] $RequiredFiles
    )

    foreach ($relativePath in $RequiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $DataRoot $relativePath) -PathType Leaf)) {
            return $false
        }
    }
    return $true
}

function Resolve-ModDataRoot {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $DisplayName,
        [Parameter(Mandatory = $true)][string] $ManagedRelativeRoot,
        [Parameter(Mandatory = $true)][string] $PrimaryRelativeFile,
        [Parameter(Mandatory = $true)][string[]] $RequiredFiles,
        [string] $ExplicitDataRoot = ""
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitDataRoot)) {
        $dataRoot = (Resolve-Path -LiteralPath $ExplicitDataRoot).Path
        if (-not (Test-DataRoot -DataRoot $dataRoot -RequiredFiles $RequiredFiles)) {
            throw "The selected $DisplayName data root is missing one or more required files: $dataRoot"
        }
        return $dataRoot
    }

    $managedDataRoot = Join-Path $Root $ManagedRelativeRoot
    if (Test-DataRoot -DataRoot $managedDataRoot -RequiredFiles $RequiredFiles) {
        return $managedDataRoot
    }

    $dataFilesRoot = Join-Path $Root "Data Files"
    if (-not (Test-Path -LiteralPath $dataFilesRoot -PathType Container)) {
        return $null
    }

    $primaryFileName = Split-Path -Leaf $PrimaryRelativeFile
    $normalizedSuffix = $PrimaryRelativeFile.Replace("/", "\").ToLowerInvariant()
    $candidates = New-Object System.Collections.Generic.List[string]
    foreach ($primaryFile in Get-ChildItem -LiteralPath $dataFilesRoot -Recurse -Force -File `
        -Filter $primaryFileName -ErrorAction SilentlyContinue) {
        $normalizedPath = $primaryFile.FullName.ToLowerInvariant()
        if (-not $normalizedPath.EndsWith($normalizedSuffix, [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }

        $candidateRoot = $primaryFile.FullName.Substring(0, $primaryFile.FullName.Length - $PrimaryRelativeFile.Length).TrimEnd("\", "/")
        if (Test-DataRoot -DataRoot $candidateRoot -RequiredFiles $RequiredFiles) {
            $candidates.Add($candidateRoot)
        }
    }

    $uniqueCandidates = @($candidates | Sort-Object -Unique)
    if ($uniqueCandidates.Count -eq 1) {
        return $uniqueCandidates[0]
    }
    if ($uniqueCandidates.Count -gt 1) {
        throw "Found multiple $DisplayName installations. Remove stale duplicate data paths before updating."
    }
    return $null
}

function Write-TextFileAtomically {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Text
    )

    $originalBytes = [IO.File]::ReadAllBytes($Path)
    $hasUtf8Bom = $originalBytes.Length -ge 3 -and
        $originalBytes[0] -eq 0xEF -and $originalBytes[1] -eq 0xBB -and $originalBytes[2] -eq 0xBF
    $encoding = [Text.UTF8Encoding]::new($hasUtf8Bom)
    $temporaryPath = "$Path.$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        [IO.File]::WriteAllText($temporaryPath, $Text, $encoding)
        Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}

function Apply-ZhiCompatibilityFixes {
    param([Parameter(Mandatory = $true)][string] $DataRoot)

    $playerScriptPath = Join-Path $DataRoot "scripts\ZerkishHotkeysImproved\zhi_player.lua"
    $hotbarScriptPath = Join-Path $DataRoot "scripts\ZerkishHotkeysImproved\zhi_hotbarhud.lua"

    $legacyMarker = "Fetcher multiplayer compatibility: suppress the first-time modal during character creation."
    $legacyOriginal = "if not (ZHISaveData.onCloseQuickKeyMenuFirstTimeFlag or sDisableFirstTimeNotification) then"
    $legacyReplacement = "if false and not (ZHISaveData.onCloseQuickKeyMenuFirstTimeFlag or sDisableFirstTimeNotification) then -- $legacyMarker"
    $playerSource = [IO.File]::ReadAllText($playerScriptPath)
    $playerUpdated = $playerSource
    if ($playerSource.Contains($legacyMarker)) {
        $occurrences = ([regex]::Matches($playerSource, [regex]::Escape($legacyReplacement))).Count
        if ($occurrences -ne 1) {
            throw "Expected one legacy Fetcher ZHI edit, found $occurrences. Refusing to modify an unexpected script."
        }
        $playerUpdated = $playerSource.Replace($legacyReplacement, $legacyOriginal)
    }
    if ($playerUpdated -ne $playerSource) {
        Write-TextFileAtomically -Path $playerScriptPath -Text $playerUpdated
        Write-Host "Removed the legacy Fetcher edit from Zerkish Hotkeys Improved:"
        Write-Host "  $playerScriptPath"
    }

    $hotbarSource = [IO.File]::ReadAllText($hotbarScriptPath)
    $invalidAutoSizePattern = '(?m)(?<prefix>^[ \t]*name[ \t]*=[ \t]*["'']icon["''],\r?\n[ \t]*props[ \t]*=[ \t]*\{\r?\n)[ \t]*autoSize[ \t]*=[ \t]*true,\r?\n(?=[ \t]*inheritAlpha[ \t]*=[ \t]*false,)'
    $invalidAutoSizeRegex = [regex]::new($invalidAutoSizePattern, [Text.RegularExpressions.RegexOptions]::Multiline)
    $hotbarMatches = $invalidAutoSizeRegex.Matches($hotbarSource)
    if ($hotbarMatches.Count -gt 1) {
        throw "Expected at most one invalid ZHI hotbar icon autoSize property, found $($hotbarMatches.Count)."
    }
    if ($hotbarMatches.Count -eq 1) {
        $hotbarUpdated = $invalidAutoSizeRegex.Replace($hotbarSource, '${prefix}', 1)
        Write-TextFileAtomically -Path $hotbarScriptPath -Text $hotbarUpdated
        Write-Host "Removed the invalid ZHI hotbar icon autoSize property:"
        Write-Host "  $hotbarScriptPath"
    }
    else {
        Write-Host "Zerkish Hotkeys Improved hotbar layout is already compatible."
    }
}

function Apply-HookshotCompatibilityFixes {
    param([Parameter(Mandatory = $true)][string] $DataRoot)

    $playerScriptPath = Join-Path $DataRoot "scripts\OpenMWHookshot\player.lua"
    $source = [IO.File]::ReadAllText($playerScriptPath)
    $invalidTopLevelSizePattern = '(?m)(?<prefix>^[ \t]*type[ \t]*=[ \t]*ui\.TYPE\.Image,\r?\n)[ \t]*size[ \t]*=[ \t]*util\.vector2\([ \t]*BASE_RETICLE_SIZE[ \t]*,[ \t]*BASE_RETICLE_SIZE[ \t]*\),[^\r\n]*\r?\n(?=[ \t]*props[ \t]*=[ \t]*\{)'
    $invalidTopLevelSizeRegex = [regex]::new($invalidTopLevelSizePattern, [Text.RegularExpressions.RegexOptions]::Multiline)
    $matches = $invalidTopLevelSizeRegex.Matches($source)
    if ($matches.Count -gt 1) {
        throw "Expected at most one invalid OpenMWHookshot top-level size property, found $($matches.Count)."
    }
    if ($matches.Count -eq 1) {
        $updated = $invalidTopLevelSizeRegex.Replace($source, '${prefix}', 1)
        Write-TextFileAtomically -Path $playerScriptPath -Text $updated
        Write-Host "Removed the invalid OpenMWHookshot top-level reticle size property:"
        Write-Host "  $playerScriptPath"
    }
    else {
        Write-Host "OpenMWHookshot reticle layout is already compatible."
    }
}

function Apply-FollowerDetectionUtilCompatibilityFixes {
    param([Parameter(Mandatory = $true)][string] $DataRoot)

    $actorScriptPath = Join-Path $DataRoot "scripts\FollowerDetectionUtil\actor.lua"
    $source = [IO.File]::ReadAllText($actorScriptPath)
    $updated = $source

    $startupOriginal = "local updateTime = math.random() * settings.checkFollowersEvery"
    $startupReplacement = "local updateTime = math.random() * (settings.checkFollowersEvery or 0.2) -- Fetcher multiplayer compatibility: storage defaults can be unavailable before the server mirror arrives."
    if (-not $updated.Contains($startupReplacement)) {
        $startupOccurrences = ([regex]::Matches($updated, [regex]::Escape($startupOriginal))).Count
        if ($startupOccurrences -ne 1) {
            throw "Expected one Follower Detection Util startup interval assignment, found $startupOccurrences. Refusing to modify an unexpected script."
        }
        $updated = $updated.Replace($startupOriginal, $startupReplacement)
    }

    $intervalOriginal = "local interval = settings.checkFollowersEvery"
    $intervalReplacement = "local interval = settings.checkFollowersEvery or 0.2 -- Fetcher multiplayer compatibility: storage defaults can be unavailable before the server mirror arrives."
    if (-not $updated.Contains($intervalReplacement)) {
        $intervalOccurrences = ([regex]::Matches($updated, [regex]::Escape($intervalOriginal))).Count
        if ($intervalOccurrences -ne 1) {
            throw "Expected one Follower Detection Util update interval assignment, found $intervalOccurrences. Refusing to modify an unexpected script."
        }
        $updated = $updated.Replace($intervalOriginal, $intervalReplacement)
    }

    if ($updated -eq $source) {
        Write-Host "Follower Detection Util multiplayer interval fallbacks are already compatible."
        return
    }

    Write-TextFileAtomically -Path $actorScriptPath -Text $updated
    Write-Host "Added the Follower Detection Util multiplayer interval fallbacks:"
    Write-Host "  $actorScriptPath"
}

function Apply-BestFriendsForeverCompatibilityFixes {
    param([Parameter(Mandatory = $true)][string] $DataRoot)

    $settingsScriptPath = Join-Path $DataRoot "scripts\BestFriendsForever\settingsPlayer.lua"
    $source = [IO.File]::ReadAllText($settingsScriptPath)
    $marker = "Fetcher multiplayer compatibility: wait for mirrored global settings groups before registering this page."
    if ($source.Contains($marker)) {
        Write-Host "Best Friends Forever settings registration is already compatible."
        return
    }

    $newline = if ($source.Contains("`r`n")) { "`r`n" } else { "`n" }
    $importOriginal = "local I = require('openmw.interfaces')"
    if (([regex]::Matches($source, [regex]::Escape($importOriginal))).Count -ne 1) {
        throw "Expected one Best Friends Forever interfaces import. Refusing to modify an unexpected script."
    }

    $pageOriginal = @'
I.Settings.registerPage {
    key = 'BestFriendsForever',
    l10n = 'BestFriendsForever',
    name = 'page_name',
    description = 'page_description',
}
'@.Replace("`r`n", "`n").Replace("`n", $newline)
    if (([regex]::Matches($source, [regex]::Escape($pageOriginal))).Count -ne 1) {
        throw "Expected one Best Friends Forever main settings page registration. Refusing to modify an unexpected script."
    }
    if ([regex]::IsMatch($source, '(?m)^return\s*\{')) {
        throw "Best Friends Forever settings script unexpectedly already returns handlers. Refusing to append another return block."
    }

    $importReplacement = $importOriginal + $newline +
        "local storage = require('openmw.storage') -- $marker"
    $pageReplacement = @'
local bffGlobalGroups = storage.globalSection('OmwSettingGroups')
local bffRequiredGlobalGroups = {
    'SettingsBestFriendsForever_toggles',
    'SettingsBestFriendsForever_blacklist',
    'SettingsBestFriendsForever_immortality',
    'SettingsBestFriendsForever_catchUp',
}
local bffPageRegistered = false
local function tryRegisterBestFriendsForeverPage()
    if bffPageRegistered then return end
    for _, groupKey in ipairs(bffRequiredGlobalGroups) do
        if not bffGlobalGroups:get(groupKey) then return end
    end
    I.Settings.registerPage {
        key = 'BestFriendsForever',
        l10n = 'BestFriendsForever',
        name = 'page_name',
        description = 'page_description',
    }
    bffPageRegistered = true
end
tryRegisterBestFriendsForeverPage()
'@.Replace("`r`n", "`n").Replace("`n", $newline)

    $updated = $source.Replace($importOriginal, $importReplacement).Replace($pageOriginal, $pageReplacement)
    $updated = $updated.TrimEnd("`r", "`n") + $newline + $newline + @'
return {
    engineHandlers = {
        onUpdate = function()
            tryRegisterBestFriendsForeverPage()
        end,
    },
}
'@.Replace("`r`n", "`n").Replace("`n", $newline)

    Write-TextFileAtomically -Path $settingsScriptPath -Text $updated
    Write-Host "Delayed Best Friends Forever settings page registration until global groups are mirrored:"
    Write-Host "  $settingsScriptPath"
}

function Apply-TakeControlCompatibilityFixes {
    param([Parameter(Mandatory = $true)][string] $DataRoot)

    $playerScriptPath = Join-Path $DataRoot "Scripts\TakeControl\Player.lua"
    $source = [IO.File]::ReadAllText($playerScriptPath)
    $updated = $source
    $newline = if ($source.Contains("`r`n")) { "`r`n" } else { "`n" }

    $activationMarker = "Fetcher multiplayer compatibility: normal player load must keep activation enabled."
    if (-not $updated.Contains($activationMarker)) {
        $activationPattern = '(?ms)(?<prefix>local function onLoad\(data\).*?)(?<indent>^[ \t]*)I\.UI\.setHudVisibility\(false\)\r?\n\k<indent>core\.sendGlobalEvent\("Activations",\{state=false, player=self\}\)'
        $activationRegex = [regex]::new($activationPattern, [Text.RegularExpressions.RegexOptions]::Multiline -bor [Text.RegularExpressions.RegexOptions]::Singleline)
        $activationMatches = $activationRegex.Matches($updated)
        if ($activationMatches.Count -ne 1) {
            throw "Expected one Take Control onLoad activation-disable block, found $($activationMatches.Count). Refusing to modify an unexpected script."
        }

        $activationReplacement = '${prefix}${indent}local controllingOtherActor = CoopActor and CoopActor.id ~= self.id -- ' + $activationMarker + $newline +
            '${indent}I.UI.setHudVisibility(not controllingOtherActor)' + $newline +
            '${indent}core.sendGlobalEvent("Activations",{state=not controllingOtherActor, player=self})'
        $updated = $activationRegex.Replace($updated, $activationReplacement, 1)
    }

    $cameraMarker = "Fetcher multiplayer compatibility: setStaticPosition requires Static mode to be active."
    if (-not $updated.Contains($cameraMarker)) {
        $cameraPattern = '(?m)^(?<indent>[ \t]*)camera\.setMode\(camera\.MODE\.Static\)[ \t]*\r?$'
        $cameraRegex = [regex]::new($cameraPattern, [Text.RegularExpressions.RegexOptions]::Multiline)
        $cameraMatches = $cameraRegex.Matches($updated)
        if ($cameraMatches.Count -ne 1) {
            throw "Expected one Take Control Static camera mode assignment, found $($cameraMatches.Count). Refusing to modify an unexpected script."
        }
        $cameraReplacement = '${indent}camera.setMode(camera.MODE.Static)' + $newline +
            '${indent}if camera.getMode() ~= camera.MODE.Static then return end -- ' + $cameraMarker
        $updated = $cameraRegex.Replace($updated, $cameraReplacement, 1)
    }

    if ($updated -eq $source) {
        Write-Host "Take Control multiplayer behavior is already compatible."
        return
    }

    Write-TextFileAtomically -Path $playerScriptPath -Text $updated
    Write-Host "Applied Take Control multiplayer activation and camera compatibility fixes:"
    Write-Host "  $playerScriptPath"
}

if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $InstallRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
}
$root = (Resolve-Path -LiteralPath $InstallRoot).Path

$zhiRequiredFiles = @(
    "scripts\ZerkishHotkeysImproved\zhi_player.lua",
    "scripts\ZerkishHotkeysImproved\zhi_hotbarhud.lua"
)
$zhiRoot = Resolve-ModDataRoot -Root $root -DisplayName "Zerkish Hotkeys Improved" `
    -ManagedRelativeRoot "Data Files\fetcher-bardcraft\UserInterface\ZerkishHotkeysImproved\ZerkishHotkeysImproved" `
    -PrimaryRelativeFile $zhiRequiredFiles[0] -RequiredFiles $zhiRequiredFiles -ExplicitDataRoot $ZhiDataRoot
if ($null -eq $zhiRoot) {
    Write-Host "Zerkish Hotkeys Improved is not installed; skipping its compatibility fixes."
}
else {
    Apply-ZhiCompatibilityFixes -DataRoot $zhiRoot
}

$hookshotRequiredFiles = @("scripts\OpenMWHookshot\player.lua")
$hookshotRoot = Resolve-ModDataRoot -Root $root -DisplayName "OpenMWHookshot" `
    -ManagedRelativeRoot "Data Files\fetcher-bardcraft\Gameplay\OpenMWHookshot\OpenMWHookshot" `
    -PrimaryRelativeFile $hookshotRequiredFiles[0] -RequiredFiles $hookshotRequiredFiles `
    -ExplicitDataRoot $HookshotDataRoot
if ($null -eq $hookshotRoot) {
    Write-Host "OpenMWHookshot is not installed; skipping its compatibility fix."
}
else {
    Apply-HookshotCompatibilityFixes -DataRoot $hookshotRoot
}

$fduRequiredFiles = @("scripts\FollowerDetectionUtil\actor.lua")
$fduRoot = Resolve-ModDataRoot -Root $root -DisplayName "Follower Detection Util" `
    -ManagedRelativeRoot "Data Files\fetcher-bardcraft\ModdingResources\FollowerDetectionUtil" `
    -PrimaryRelativeFile $fduRequiredFiles[0] -RequiredFiles $fduRequiredFiles `
    -ExplicitDataRoot $FollowerDetectionUtilDataRoot
if ($null -eq $fduRoot) {
    Write-Host "Follower Detection Util is not installed; skipping its compatibility fix."
}
else {
    Apply-FollowerDetectionUtilCompatibilityFixes -DataRoot $fduRoot
}

$bffRequiredFiles = @("scripts\BestFriendsForever\settingsPlayer.lua")
$bffRoot = Resolve-ModDataRoot -Root $root -DisplayName "Best Friends Forever" `
    -ManagedRelativeRoot "Data Files\fetcher-bardcraft\Gameplay\BestFriendsForever" `
    -PrimaryRelativeFile $bffRequiredFiles[0] -RequiredFiles $bffRequiredFiles `
    -ExplicitDataRoot $BestFriendsForeverDataRoot
if ($null -eq $bffRoot) {
    Write-Host "Best Friends Forever is not installed; skipping its compatibility fix."
}
else {
    Apply-BestFriendsForeverCompatibilityFixes -DataRoot $bffRoot
}

$takeControlRequiredFiles = @("Scripts\TakeControl\Player.lua")
$takeControlRoot = Resolve-ModDataRoot -Root $root -DisplayName "Take Control" `
    -ManagedRelativeRoot "Data Files\fetcher-bardcraft\Gameplay\TakeControl\TakeControl\Data_Files" `
    -PrimaryRelativeFile $takeControlRequiredFiles[0] -RequiredFiles $takeControlRequiredFiles `
    -ExplicitDataRoot $TakeControlDataRoot
if ($null -eq $takeControlRoot) {
    Write-Host "Take Control is not installed; skipping its compatibility fix."
}
else {
    Apply-TakeControlCompatibilityFixes -DataRoot $takeControlRoot
}

