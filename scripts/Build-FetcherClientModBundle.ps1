[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BaseArchivePath,
    [Parameter(Mandatory = $true)]
    [string] $VehicleDataRoot,
    [Parameter(Mandatory = $true)]
    [string] $OutputPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$baseArchive = (Resolve-Path -LiteralPath $BaseArchivePath).Path
$vehicleRoot = (Resolve-Path -LiteralPath $VehicleDataRoot).Path
$output = [IO.Path]::GetFullPath($OutputPath)
$workRoot = Join-Path ([IO.Path]::GetTempPath()) ("fetcher-client-mods-vehicles-" + [Guid]::NewGuid().ToString("N"))
$stage = Join-Path $workRoot "stage"

$requiredVehicleFiles = @(
    "FetcherVehicles.omwaddon",
    "vehicle_profiles.json",
    "CREDITS.txt",
    "Meshes\FetcherVehicles\fv_pickup_85_attached.nif",
    "Meshes\FetcherVehicles\fv_pickup_85_exhaust_haze.nif",
    "Meshes\FetcherVehicles\fv_pickup_85_parked.nif",
    "Textures\Fetcher\Vehicles\pickup_exhaust_haze.tga",
    "Textures\FetcherVehicles\fv_pickup_body.jpg",
    "Textures\FetcherVehicles\fv_pickup_bottom.jpg",
    "Textures\FetcherVehicles\fv_pickup_interior.jpg",
    "Textures\FetcherVehicles\fv_pickup_lights.jpg",
    "Textures\FetcherVehicles\fv_pickup_tire.jpg",
    "Sound\fetcher\vehicles\pickup_engine_idle.ogg",
    "Sound\fetcher\vehicles\pickup_gravel_roll.ogg",
    "Sound\fetcher\vehicles\pickup_gravel_skid.ogg",
    "Sound\fetcher\vehicles\pickup_suspension_thump.ogg"
)

try {
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Expand-Archive -LiteralPath $baseArchive -DestinationPath $stage

    $manifestPath = Join-Path $stage "openmw-client-package.json"
    $dataRoot = Join-Path $stage "Data Files"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $dataRoot -PathType Container)) {
        throw "Base client mod bundle must contain openmw-client-package.json and Data Files."
    }

    foreach ($relativePath in $requiredVehicleFiles) {
        $source = Join-Path $vehicleRoot $relativePath
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Vehicle package is missing required file: $source"
        }

        if ($relativePath -eq "CREDITS.txt") {
            $destination = Join-Path $dataRoot "FetcherVehicles\CREDITS.txt"
        }
        else {
            $destination = Join-Path $dataRoot $relativePath
        }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }

    $readmePath = Join-Path $dataRoot "FetcherVehicles\README.txt"
    Set-Content -LiteralPath $readmePath -Encoding UTF8 -Value @(
        "Fetcher Vehicles experimental client assets",
        "",
        "Plugin: FetcherVehicles.omwaddon",
        "Vehicle profile: fetcher.vehicles.pickup_85.v1",
        "",
        "These assets support the Fetcher Simulator experimental native-vehicle build.",
        "See CREDITS.txt in this directory for required CC BY 4.0 attribution."
    )

    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $content = New-Object System.Collections.Generic.List[string]
    foreach ($plugin in @($manifest.content)) {
        if (-not $content.Contains([string]$plugin)) {
            $content.Add([string]$plugin)
        }
    }
    if (-not $content.Contains("FetcherVehicles.omwaddon")) {
        $content.Add("FetcherVehicles.omwaddon")
    }
    $manifest.content = @($content)
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

    $parent = Split-Path -Parent $output
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    if (Test-Path -LiteralPath $output -PathType Leaf) {
        Remove-Item -LiteralPath $output -Force
    }
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $output -CompressionLevel Optimal

    Write-Host "Fetcher client mod bundle with vehicle assets written:"
    Write-Host "  $output"
    Write-Host "  sha256:$((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant())"
}
finally {
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}
