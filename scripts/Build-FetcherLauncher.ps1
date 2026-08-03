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
if (-not (Test-Path -LiteralPath $launcherExe -PathType Leaf)) {
    throw "Built launcher was not found at: $launcherExe"
}
if (-not (Test-Path -LiteralPath (Join-Path $launcherUi "index.html") -PathType Leaf)) {
    throw "Built launcher UI was not found at: $launcherUi"
}

if (Test-Path -LiteralPath $OutputDir) {
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
Copy-Item -LiteralPath $launcherExe -Destination (Join-Path $OutputDir "FetcherLauncher.exe") -Force
Copy-Item -LiteralPath $launcherUi -Destination (Join-Path $OutputDir "ui") -Recurse -Force

Write-Host "Running launcher PowerShell backend smoke test..."
$backendTestRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "FetcherLauncherBackendTest-" + [Guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Path $backendTestRoot -Force | Out-Null
    $backendTestScript = Join-Path $backendTestRoot "Update-Fetcher-Simulator.ps1"
    $backendTestLog = Join-Path $backendTestRoot "launcher-output.log"
    $backendTestMarker = Join-Path $backendTestRoot "backend-smoke-result.txt"

    @'
param([string] $InstallRoot)
Write-Host "Fetcher launcher backend smoke test"
Set-Content -LiteralPath (Join-Path $InstallRoot "backend-smoke-result.txt") -Value $InstallRoot -Encoding UTF8
exit 0
'@ | Set-Content -LiteralPath $backendTestScript -Encoding UTF8

    $backendTest = Start-Process `
        -FilePath (Join-Path $OutputDir "FetcherLauncher.exe") `
        -ArgumentList @(
            "--run-updater",
            "--install-root", ('"' + $backendTestRoot + '"'),
            "--log-file", ('"' + $backendTestLog + '"')
        ) `
        -Wait `
        -PassThru
    if ($backendTest.ExitCode -ne 0) {
        throw "Fetcher Launcher backend smoke test failed with exit code $($backendTest.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $backendTestMarker -PathType Leaf)) {
        throw "Fetcher Launcher backend smoke test did not create its result marker."
    }
    $recordedInstallRoot = (Get-Content -LiteralPath $backendTestMarker -Raw).Trim()
    if ($recordedInstallRoot -ne $backendTestRoot) {
        throw "Fetcher Launcher passed the wrong install root to PowerShell: $recordedInstallRoot"
    }
    $backendLogText = Get-Content -LiteralPath $backendTestLog -Raw
    if ($backendLogText -notmatch "Fetcher launcher backend smoke test") {
        throw "Fetcher Launcher did not capture PowerShell output."
    }
}
finally {
    if (Test-Path -LiteralPath $backendTestRoot) {
        Remove-Item -LiteralPath $backendTestRoot -Recurse -Force
    }
}

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
