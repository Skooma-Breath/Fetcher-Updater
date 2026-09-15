[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $ArchivePath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$resolvedArchive = (Resolve-Path -LiteralPath $ArchivePath).Path
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("fetcher-optional-launcher-test-" + [Guid]::NewGuid().ToString("N"))
$expandedRoot = Join-Path $tempRoot "expanded"
$installRoot = Join-Path $tempRoot "install"
$withoutLauncherArchive = Join-Path $tempRoot "fetcher-tester-tools-without-launcher.zip"

try {
    New-Item -ItemType Directory -Force -Path $expandedRoot, $installRoot | Out-Null
    Expand-Archive -LiteralPath $resolvedArchive -DestinationPath $expandedRoot

    $manifestPath = Join-Path $expandedRoot "fetcher-tester-tools.json"
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $launcherRecords = @($manifest.files | Where-Object {
        ([string]$_.path).Equals("FetcherLauncher.exe", [StringComparison]::OrdinalIgnoreCase)
    })
    if ($launcherRecords.Count -ne 1) {
        throw "Tester-tools manifest must contain exactly one FetcherLauncher.exe record."
    }
    $optionalProperty = $launcherRecords[0].PSObject.Properties["optional"]
    if ($null -eq $optionalProperty -or -not [bool]$optionalProperty.Value) {
        throw "FetcherLauncher.exe is not marked optional in the tester-tools manifest."
    }
    $unexpectedOptional = @($manifest.files | Where-Object {
        $p = $_.PSObject.Properties["optional"]
        $null -ne $p -and [bool]$p.Value -and
            -not ([string]$_.path).Equals("FetcherLauncher.exe", [StringComparison]::OrdinalIgnoreCase)
    })
    if ($unexpectedOptional.Count -ne 0) {
        throw "Only FetcherLauncher.exe may be optional in the tester-tools manifest."
    }

    Remove-Item -LiteralPath (Join-Path $expandedRoot "FetcherLauncher.exe") -Force
    Compress-Archive -Path (Join-Path $expandedRoot "*") -DestinationPath $withoutLauncherArchive -CompressionLevel Optimal

    New-Item -ItemType File -Path (Join-Path $installRoot "openmw.exe") -Force | Out-Null
    $installer = Join-Path $expandedRoot "Install-Fetcher-Tester-Tools.ps1"
    & $installer `
        -InstallRoot $installRoot `
        -ToolsArchivePath $withoutLauncherArchive `
        -SkipClientModBundle `
        -SkipUpdater
    if (-not $?) {
        throw "Tester-tools installer failed when the optional launcher was absent."
    }

    foreach ($required in @(
        "Update-Fetcher-Simulator.ps1",
        "Update-Fetcher-Simulator.bat",
        "fetcher-tester-tools.json"
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $installRoot $required) -PathType Leaf)) {
            throw "Required tester tool was not installed when the optional launcher was absent: $required"
        }
    }
    if (Test-Path -LiteralPath (Join-Path $installRoot "FetcherLauncher.exe") -PathType Leaf) {
        throw "FetcherLauncher.exe unexpectedly appeared in the optional-launcher fixture."
    }

    Write-Host "PASS: tester-tools installation survives a quarantined/missing optional FetcherLauncher.exe."
}
finally {
    if (Test-Path -LiteralPath $tempRoot -PathType Container) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
