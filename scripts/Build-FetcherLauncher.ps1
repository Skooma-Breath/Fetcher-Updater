[CmdletBinding()]
param(
    [string] $Configuration = "Release",
    [string] $Architecture = "x64",
    [string] $BuildDir = "",
    [string] $OutputDir = "",
    [switch] $Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$sourceDir = Join-Path $repoRoot "launcher"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $sourceDir "build"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "release-assets\fetcher-launcher"
}

foreach ($targetDirectory in @($BuildDir, $OutputDir)) {
    $resolvedTarget = [IO.Path]::GetFullPath($targetDirectory).TrimEnd('\')
    if (-not $resolvedTarget.StartsWith($repoRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $resolvedTarget -in @((Join-Path $repoRoot 'launcher'), (Join-Path $repoRoot 'release-assets'))) {
        throw "Build/clean output must be a dedicated directory inside this repository: $resolvedTarget"
    }
}

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

$cmake = Get-Command cmake -ErrorAction Stop
$previousToolchain = $env:CMAKE_TOOLCHAIN_FILE
try {
    Remove-Item Env:CMAKE_TOOLCHAIN_FILE -ErrorAction SilentlyContinue

    Write-Host "Configuring Fetcher Launcher..."
    & $cmake.Source `
        -Wno-dev `
        -S $sourceDir `
        -B $BuildDir `
        -G "Visual Studio 17 2022" `
        -A $Architecture
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed with exit code $LASTEXITCODE."
    }
}
finally {
    if ([string]::IsNullOrWhiteSpace($previousToolchain)) {
        Remove-Item Env:CMAKE_TOOLCHAIN_FILE -ErrorAction SilentlyContinue
    }
    else {
        $env:CMAKE_TOOLCHAIN_FILE = $previousToolchain
    }
}

Write-Host "Building Fetcher Launcher ($Configuration, $Architecture)..."
& $cmake.Source --build $BuildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Fetcher Launcher build failed with exit code $LASTEXITCODE."
}

$launcherExe = Join-Path $BuildDir "bin\FetcherLauncher.exe"
$launcherUi = Join-Path $BuildDir "bin\ui"
$launcherNotices = Join-Path $BuildDir "bin\FetcherLauncher-THIRD-PARTY-NOTICES.txt"
$launcherUiAssets = @(
    (Join-Path $launcherUi "assets\fetcher-float.gif"),
    (Join-Path $launcherUi "assets\fetcher-float-right.gif"),
    (Join-Path $launcherUi "assets\potm2504a.jpg")
)
if (-not (Test-Path -LiteralPath $launcherExe -PathType Leaf)) {
    throw "Built launcher was not found at: $launcherExe"
}
if (-not (Test-Path -LiteralPath (Join-Path $launcherUi "index.html") -PathType Leaf)) {
    throw "Built launcher UI was not found at: $launcherUi"
}
foreach ($launcherUiAsset in $launcherUiAssets) {
    if (-not (Test-Path -LiteralPath $launcherUiAsset -PathType Leaf)) {
        throw "Built launcher UI asset was not found at: $launcherUiAsset"
    }
    if ((Get-Item -LiteralPath $launcherUiAsset).Length -le 0) {
        throw "Built launcher UI asset is empty: $launcherUiAsset"
    }
}
if (-not (Test-Path -LiteralPath $launcherNotices -PathType Leaf)) {
    throw "Built launcher third-party notices were not found at: $launcherNotices"
}

if (Test-Path -LiteralPath $OutputDir) {
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
Copy-Item -LiteralPath $launcherExe -Destination (Join-Path $OutputDir "FetcherLauncher.exe") -Force
Copy-Item -LiteralPath $launcherUi -Destination (Join-Path $OutputDir "ui") -Recurse -Force
Copy-Item -LiteralPath $launcherNotices `
    -Destination (Join-Path $OutputDir "FetcherLauncher-THIRD-PARTY-NOTICES.txt") -Force

Write-Host "Running native C++ backend tests..."
& ctest --test-dir $BuildDir -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Native backend tests failed." }
& python (Join-Path $repoRoot 'tests\test_native_backend.py') --exe $launcherExe
if ($LASTEXITCODE -ne 0) { throw 'Native HTTP/package integration tests failed.' }

& python (Join-Path $repoRoot 'tests/test_server_manifests.py') --exe (Join-Path $OutputDir 'FetcherLauncher.exe')
if ($LASTEXITCODE -ne 0) { throw "Server manifest integration tests failed." }

& python (Join-Path $repoRoot 'tests/test_archive_layouts.py') --exe (Join-Path $OutputDir 'FetcherLauncher.exe')
if ($LASTEXITCODE -ne 0) { throw "Archive/layout integration tests failed." }

Write-Host "Running launcher self-test..."
$selfTest = Start-Process `
    -FilePath (Join-Path $OutputDir "FetcherLauncher.exe") `
    -ArgumentList @("--self-test", "--install-root", (Join-Path $repoRoot "release-root")) `
    -Wait `
    -PassThru
if ($selfTest.ExitCode -ne 0) {
    throw "Fetcher Launcher self-test failed with exit code $($selfTest.ExitCode)."
}

$binary = Get-Item -LiteralPath (Join-Path $OutputDir "FetcherLauncher.exe")
$digest = (Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256).Hash.ToLowerInvariant()

Write-Host ""
Write-Host "Fetcher Launcher build completed successfully."
Write-Host "  Output: $OutputDir"
Write-Host "  Binary: $($binary.FullName)"
Write-Host "  Size:   $($binary.Length) bytes"
Write-Host "  SHA256: $digest"
