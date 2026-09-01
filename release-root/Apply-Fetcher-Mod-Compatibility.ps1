[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $InstallRoot,
    [string] $ManifestPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string] $Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function ConvertTo-SafeRelativePath {
    param([Parameter(Mandatory = $true)][string] $Path)

    $normalized = $Path.Replace("/", "\")
    if ([string]::IsNullOrWhiteSpace($normalized) -or
        [IO.Path]::IsPathRooted($normalized) -or
        $normalized.Contains(":")) {
        throw "Compatibility patch contains an invalid target path: $Path"
    }
    $normalized = $normalized.TrimStart("\")
    $segments = @($normalized.Split("\", [StringSplitOptions]::RemoveEmptyEntries))
    if ($segments.Count -eq 0 -or
        @($segments | Where-Object { $_ -eq "." -or $_ -eq ".." }).Count -gt 0) {
        throw "Compatibility patch target escapes the installation root: $Path"
    }
    return ($segments -join "\")
}

function Resolve-TargetPath {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $RelativePath
    )

    $candidate = [IO.Path]::GetFullPath((Join-Path $Root $RelativePath))
    $prefix = $Root.TrimEnd("\", "/") + [IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Compatibility patch target escapes the installation root: $RelativePath"
    }
    return $candidate
}

function Get-OptionalBoolean {
    param(
        [Parameter(Mandatory = $true)] $Object,
        [Parameter(Mandatory = $true)][string] $Name
    )

    $property = $Object.PSObject.Properties[$Name]
    return $null -ne $property -and [bool]$property.Value
}

function Update-ClientInventoryRecords {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)] $Updates
    )

    if ($Updates.Count -eq 0) {
        return
    }
    $inventoryPath = Join-Path $Root "fetcher-client-files.json"
    if (-not (Test-Path -LiteralPath $inventoryPath -PathType Leaf)) {
        Write-Warning "Client inventory was not found; compatibility files are patched but a future client update may replace them."
        return
    }

    $inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
    if ([int]$inventory.schemaVersion -ne 1 -or
        $null -eq $inventory.PSObject.Properties["files"]) {
        throw "Unsupported Fetcher client inventory: $inventoryPath"
    }

    $changed = $false
    foreach ($update in $Updates) {
        $relativePath = $update.RelativePath.Replace("\", "/")
        $matches = @($inventory.files | Where-Object {
            ([string]$_.path).Replace("\", "/").Equals($relativePath, [StringComparison]::OrdinalIgnoreCase)
        })
        if ($matches.Count -ne 1) {
            throw "Expected one client inventory record for $relativePath, found $($matches.Count)."
        }
        $record = $matches[0]
        $recordHash = ([string]$record.sha256).ToLowerInvariant()
        if ($recordHash -ne $update.SourceHash -and $recordHash -ne $update.OutputHash) {
            throw "Client inventory contains an unsupported hash for ${relativePath}: $recordHash"
        }
        if ($recordHash -ne $update.OutputHash -or [int64]$record.size -ne $update.OutputSize) {
            $record.sha256 = $update.OutputHash
            $record.size = $update.OutputSize
            $changed = $true
        }
    }

    if ($changed) {
        $temporaryPath = "$inventoryPath.$([Guid]::NewGuid().ToString('N')).tmp"
        try {
            $json = $inventory | ConvertTo-Json -Depth 8
            [IO.File]::WriteAllText($temporaryPath, $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
            Move-Item -LiteralPath $temporaryPath -Destination $inventoryPath -Force
        }
        finally {
            if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) {
                Remove-Item -LiteralPath $temporaryPath -Force
            }
        }
        Write-Output "Updated client inventory for compatibility-patched files."
    }
}

function Write-ReconstructedFile {
    param(
        [Parameter(Mandatory = $true)] $Record,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][byte[]] $SourceBytes,
        [Parameter(Mandatory = $true)][string] $Destination
    )

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    $stream = [IO.File]::Open($Destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
    try {
        foreach ($operation in @($Record.operations)) {
            $properties = @($operation.PSObject.Properties.Name)
            $hasCopyOffset = $properties -contains "copyOffset"
            $hasCopyLength = $properties -contains "copyLength"
            $hasData = $properties -contains "data"
            if ($hasCopyOffset -and $hasCopyLength -and -not $hasData) {
                $offset = [int64]$operation.copyOffset
                $length = [int]$operation.copyLength
                if ($offset -lt 0 -or $length -lt 0 -or
                    $offset + $length -gt $SourceBytes.LongLength -or
                    $offset -gt [int]::MaxValue) {
                    throw "Compatibility patch contains an invalid source range for $($Record.path)."
                }
                $stream.Write($SourceBytes, [int]$offset, $length)
            }
            elseif ($hasData -and -not $hasCopyOffset -and -not $hasCopyLength) {
                $data = [Convert]::FromBase64String([string]$operation.data)
                $stream.Write($data, 0, $data.Length)
            }
            else {
                throw "Compatibility patch contains an unknown operation for $($Record.path)."
            }
        }
    }
    finally {
        $stream.Dispose()
    }
}

$root = (Resolve-Path -LiteralPath $InstallRoot).Path
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "fetcher-mod-compatibility-patches.json"
}
$resolvedManifest = (Resolve-Path -LiteralPath $ManifestPath).Path
$manifest = Get-Content -LiteralPath $resolvedManifest -Raw | ConvertFrom-Json
if ([int]$manifest.formatVersion -ne 1 -or
    $null -eq $manifest.PSObject.Properties["files"] -or
    @($manifest.files).Count -eq 0) {
    throw "Unsupported Fetcher mod compatibility patch format: $($manifest.formatVersion)"
}

$safeVersion = ([string]$manifest.patchVersion) -replace '[^A-Za-z0-9._-]', '_'
$stateRoot = Join-Path $root "_fetcher_update"
$workRoot = Join-Path $stateRoot ("compatibility-work-" + [Guid]::NewGuid().ToString("N"))
$stageRoot = Join-Path $workRoot "stage"
$backupRoot = Join-Path $stateRoot ("compatibility-backups\" + $safeVersion)
$pending = New-Object System.Collections.Generic.List[object]
$inventoryUpdates = New-Object System.Collections.Generic.List[object]
$seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)

try {
    New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
    foreach ($record in @($manifest.files)) {
        $relativePath = ConvertTo-SafeRelativePath -Path ([string]$record.path)
        if (-not $seen.Add($relativePath)) {
            throw "Compatibility patch contains a duplicate target: $($record.path)"
        }
        $targetPath = Resolve-TargetPath -Root $root -RelativePath $relativePath
        $outputHash = ([string]$record.outputSha256).ToLowerInvariant()
        $sourceHash = ([string]$record.sourceSha256).ToLowerInvariant()
        if ($sourceHash -notmatch '^[0-9a-f]{64}$' -or
            $outputHash -notmatch '^[0-9a-f]{64}$' -or
            [int64]$record.sourceSize -lt 0 -or
            [int64]$record.outputSize -lt 0 -or
            @($record.operations).Count -eq 0) {
            throw "Compatibility patch contains an incomplete record: $($record.path)"
        }

        $createIfMissing = Get-OptionalBoolean -Object $record -Name "createIfMissing"
        $targetExists = Test-Path -LiteralPath $targetPath -PathType Leaf
        if ((Test-Path -LiteralPath $targetPath) -and -not $targetExists) {
            throw "Compatibility target exists but is not a file: $($record.path)"
        }
        if (-not $targetExists -and -not $createIfMissing) {
            Write-Output "Compatibility target is not installed; skipping: $($record.path)"
            continue
        }
        if (-not $targetExists -and $createIfMissing) {
            $requiresPathProperty = $record.PSObject.Properties["requiresPath"]
            if ($null -ne $requiresPathProperty -and -not [string]::IsNullOrWhiteSpace([string]$requiresPathProperty.Value)) {
                $requiresRelativePath = ConvertTo-SafeRelativePath -Path ([string]$requiresPathProperty.Value)
                $requiresTargetPath = Resolve-TargetPath -Root $root -RelativePath $requiresRelativePath
                if (-not (Test-Path -LiteralPath $requiresTargetPath -PathType Leaf)) {
                    Write-Output "Compatibility prerequisite is not installed; skipping: $($record.path)"
                    continue
                }
            }
        }

        if ($targetExists) {
            $currentHash = Get-Sha256 -Path $targetPath
            if ($currentHash -eq $outputHash) {
                Write-Output "Already patched: $($record.path)"
                if (Get-OptionalBoolean -Object $record -Name "updateClientInventory") {
                    $inventoryUpdates.Add([pscustomobject]@{
                        RelativePath = $relativePath
                        SourceHash = $sourceHash
                        OutputHash = $outputHash
                        OutputSize = [int64]$record.outputSize
                    })
                }
                continue
            }
            if ($currentHash -ne $sourceHash) {
                if (Get-OptionalBoolean -Object $record -Name "allowUnknownSource") {
                    Write-Output "Compatibility patch does not apply to this file version; skipping: $($record.path)"
                    continue
                }
                throw "Unsupported or locally modified mod file: $($record.path) (sha256=$currentHash)"
            }

            $sourceBytes = [IO.File]::ReadAllBytes($targetPath)
        }
        else {
            $emptyHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
            if ([int64]$record.sourceSize -ne 0 -or $sourceHash -ne $emptyHash) {
                throw "createIfMissing compatibility targets must declare an empty source: $($record.path)"
            }
            $sourceBytes = [byte[]]::new(0)
        }

        if ($sourceBytes.LongLength -ne [int64]$record.sourceSize) {
            throw "Compatibility patch source size mismatch for $($record.path)."
        }
        $stagedPath = Join-Path $stageRoot $relativePath
        Write-ReconstructedFile -Record $record -SourceBytes $sourceBytes -Destination $stagedPath
        if ((Get-Item -LiteralPath $stagedPath).Length -ne [int64]$record.outputSize -or
            (Get-Sha256 -Path $stagedPath) -ne $outputHash) {
            throw "Reconstructed compatibility output failed verification for $($record.path)."
        }
        $pending.Add([pscustomobject]@{
            RelativePath = $relativePath
            TargetPath = $targetPath
            StagedPath = $stagedPath
            OutputHash = $outputHash
            SourceHash = $sourceHash
            OutputSize = [int64]$record.outputSize
            HadOriginal = $targetExists
            UpdateClientInventory = Get-OptionalBoolean -Object $record -Name "updateClientInventory"
        })
    }

    if ($pending.Count -gt 0) {
        foreach ($item in $pending) {
            if (-not $item.HadOriginal) {
                continue
            }
            $backupPath = Join-Path $backupRoot $item.RelativePath
            if (Test-Path -LiteralPath $backupPath -PathType Leaf) {
                if ((Get-Sha256 -Path $backupPath) -ne (Get-Sha256 -Path $item.TargetPath)) {
                    throw "A different compatibility backup already exists: $backupPath"
                }
            }
            else {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backupPath) | Out-Null
                Copy-Item -LiteralPath $item.TargetPath -Destination $backupPath
            }
        }

        $applied = New-Object System.Collections.Generic.List[object]
        try {
            foreach ($item in $pending) {
                $applied.Add($item)
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $item.TargetPath) | Out-Null
                Copy-Item -LiteralPath $item.StagedPath -Destination $item.TargetPath -Force
                if ((Get-Sha256 -Path $item.TargetPath) -ne $item.OutputHash) {
                    throw "Installed compatibility output failed verification: $($item.RelativePath)"
                }
                Write-Output "Applied compatibility patch: $($item.RelativePath)"
                if ($item.UpdateClientInventory) {
                    $inventoryUpdates.Add([pscustomobject]@{
                        RelativePath = $item.RelativePath
                        SourceHash = $item.SourceHash
                        OutputHash = $item.OutputHash
                        OutputSize = $item.OutputSize
                    })
                }
            }
        }
        catch {
            foreach ($item in $applied) {
                $backupPath = Join-Path $backupRoot $item.RelativePath
                if ($item.HadOriginal -and (Test-Path -LiteralPath $backupPath -PathType Leaf)) {
                    Copy-Item -LiteralPath $backupPath -Destination $item.TargetPath -Force
                }
                elseif (-not $item.HadOriginal -and (Test-Path -LiteralPath $item.TargetPath -PathType Leaf)) {
                    Remove-Item -LiteralPath $item.TargetPath -Force
                }
            }
            throw
        }
    }

    Update-ClientInventoryRecords -Root $root -Updates $inventoryUpdates

    Write-Output "Fetcher mod compatibility patch $($manifest.patchVersion) completed successfully."
}
finally {
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}
