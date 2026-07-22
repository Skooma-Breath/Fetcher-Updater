[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $InstallDir,
    [Parameter(Mandatory = $true)]
    [string] $ClientCommit,
    [string] $ProtectionPolicyPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$repositoryRoot = Split-Path -Parent $PSScriptRoot

function ConvertTo-NormalizedRelativePath {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $FullName
    )

    return $FullName.Substring($Root.Length).TrimStart("\", "/").Replace("\", "/")
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

function Test-FetcherProtectedPath {
    param(
        [Parameter(Mandatory = $true)][string] $RelativePath,
        [Parameter(Mandatory = $true)] $Policy
    )

    $path = $RelativePath.Replace("\", "/").TrimStart("/").ToLowerInvariant()
    if (@($Policy.exactPaths) -contains $path) {
        return $true
    }
    foreach ($prefix in @($Policy.prefixes)) {
        if ($path.StartsWith(([string]$prefix).ToLowerInvariant(), [StringComparison]::OrdinalIgnoreCase)) {
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

if ([string]::IsNullOrWhiteSpace($ProtectionPolicyPath)) {
    $ProtectionPolicyPath = Join-Path $repositoryRoot "release-root\fetcher-client-protection-policy.json"
}
$root = (Resolve-Path -LiteralPath $InstallDir).Path.TrimEnd("\", "/")
$resolvedPolicyPath = (Resolve-Path -LiteralPath $ProtectionPolicyPath).Path
$policy = Read-ClientProtectionPolicy -Path $resolvedPolicyPath
if ($ClientCommit -notmatch "^[0-9a-fA-F]{40}$") {
    throw "ClientCommit must be a full 40-character Git commit hash."
}

$records = New-Object System.Collections.Generic.List[object]
foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -Force -File) {
    $relativePath = ConvertTo-NormalizedRelativePath -Root $root -FullName $file.FullName
    if (Test-FetcherProtectedPath -RelativePath $relativePath -Policy $policy) {
        continue
    }

    $records.Add([ordered]@{
        path = $relativePath
        size = [int64]$file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    })
}

$inventory = [ordered]@{
    schemaVersion = 1
    protectionPolicyVersion = [int]$policy.schemaVersion
    clientCommit = $ClientCommit.ToLowerInvariant()
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    files = @($records | Sort-Object { $_.path })
}

$inventoryPath = Join-Path $root "fetcher-client-files.json"
$inventory | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $inventoryPath -Encoding UTF8
Write-Output "Fetcher client inventory written: $inventoryPath ($($records.Count) managed files)"
