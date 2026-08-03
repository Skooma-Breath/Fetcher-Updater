[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$workRoot = Join-Path ([IO.Path]::GetTempPath()) ("fetcher-client-inventory-test-" + [Guid]::NewGuid().ToString("N"))

function Assert-True {
    param([Parameter(Mandatory)][bool] $Condition, [Parameter(Mandatory)][string] $Message)
    if (-not $Condition) {
        throw $Message
    }
}

try {
    New-Item -ItemType Directory -Force -Path (Join-Path $workRoot "resources") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $workRoot "userdata") | Out-Null
    Set-Content -LiteralPath (Join-Path $workRoot "openmw.exe") -Value "managed" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $workRoot "resources\asset.bin") -Value "managed asset" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $workRoot "openmw.cfg") -Value "protected config" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $workRoot "userdata\settings.cfg") -Value "protected user data" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $workRoot "Setup-Fetcher-Updater.bat") -Value "protected updater" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $workRoot "embedded-protected.txt") -Value "protected by installed policy" -Encoding ASCII
    $installedPolicy = Get-Content -LiteralPath (Join-Path $repositoryRoot "release-root\fetcher-client-protection-policy.json") -Raw | ConvertFrom-Json
    $installedPolicy.exactPaths = @($installedPolicy.exactPaths) + "embedded-protected.txt"
    $installedPolicy | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $workRoot "fetcher-client-protection-policy.json") -Encoding UTF8

    & (Join-Path $repositoryRoot "scripts\Build-FetcherClientInventory.ps1") `
        -InstallDir $workRoot -ClientCommit ("a" * 40)
    $inventory = Get-Content -LiteralPath (Join-Path $workRoot "fetcher-client-files.json") -Raw | ConvertFrom-Json
    $paths = @($inventory.files | ForEach-Object { [string]$_.path })

    Assert-True -Condition ([int]$inventory.schemaVersion -eq 1) -Message "Unexpected inventory schema."
    Assert-True -Condition ([int]$inventory.protectionPolicyVersion -eq 1) -Message "Unexpected protection policy version."
    Assert-True -Condition ([string]$inventory.clientCommit -eq ("a" * 40)) -Message "Inventory commit mismatch."
    Assert-True -Condition ($paths -contains "openmw.exe") -Message "Managed executable is missing from inventory."
    Assert-True -Condition ($paths -contains "resources/asset.bin") -Message "Managed resource is missing from inventory."
    foreach ($protectedPath in @("openmw.cfg", "userdata/settings.cfg", "Setup-Fetcher-Updater.bat", "fetcher-client-protection-policy.json", "embedded-protected.txt")) {
        Assert-True -Condition ($paths -notcontains $protectedPath) -Message "Protected path was inventoried: $protectedPath"
    }
}
finally {
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}

Write-Host "Fetcher client inventory tests passed."
