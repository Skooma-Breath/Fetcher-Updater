# Fetcher Updater

This repository owns the independently released Fetcher Simulator updater and tester tools. Installed filenames and release asset names remain compatible with existing Fetcher Simulator installations.

## Layout

- `release-root/` contains the files installed beside `openmw.exe`.
- `scripts/` contains package, client-inventory, and stable-prerelease publishing scripts.
- `.github/actions/build-client-inventory/` exposes the pinned inventory generator used by the client repository.
- `tests/` contains package and installer validation.
- `.github/workflows/release.yml` builds and replaces the stable prerelease.

## Build

From PowerShell 7 or Windows PowerShell 5.1:

```powershell
.\scripts\Build-FetcherTesterTools.ps1 `
  -OutputDir .\release-assets

.\tests\Test-Package.ps1 `
  -ArchivePath .\release-assets\fetcher-tester-tools.zip

.\tests\Test-ClientInventory.ps1
```

The build preserves these public release artifacts:

- tag: `fetcher-tester-tools`
- archive: `fetcher-tester-tools.zip`
- bootstrap: `Setup-Fetcher-Updater.bat`
- installer: `Install-Fetcher-Tester-Tools.ps1`
- UMO list: `fetcher-bardcraft-umo.json`

Publishing is intentionally performed only by the GitHub Actions workflow or by an operator who provides `GH_TOKEN`. Local validation does not push tags or create releases.

## Release routing

The installed updater keeps each release source independent:

- `ClientRepository` defaults to `Fetcher-Simulator/Fetcher-Simulator`.
- Tester installations receive `fetcher-update-channel.json`, which currently routes them to the `Fetcher-Simulator-Vehicles` release and `fetcher-simulator.zip` asset. Explicit client repository/tag/asset arguments override the channel file.
- The normal public `Fetcher-Simulator` release remains available for users who are not participating in the multiplayer vehicle test.
- Tester tools refresh before client resolution, so a newly installed channel file can redirect the same updater invocation. Installations running the older updater order require one additional updater run for this initial migration.
- The Fetcher map/client-mod bundle is independently published here as the `openmw-client-mods-mp-clients` prerelease and installed by the tester-tools bootstrap.
- `TesterToolsRepository` defaults to `Skooma-Breath/Fetcher-Updater`.
- Bardcraft and Starwind repositories are required in `fetcher-client-patches.json`.

Legacy updater calls that pass `-Repository` still work: it is an alias for `ClientRepository` only and cannot redirect tester-tools or compatibility-patch downloads.

The updater validates `fetcher-client-files.json` to recognize a managed client installation. A missing or invalid inventory forces one complete client refresh. Tester tools, mods, and compatibility patches remain protected overlays managed here.

Every GitHub download requires the SHA-256 digest supplied by the release API. The installer also rejects unsafe or duplicate archive paths, unsupported manifests, unmanifested payloads, and file hash or size mismatches. The updater preserves mutex locking, receipt/marker verification, and atomic state replacement.

## Vehicle client-mod bundle

Build a candidate bundle by overlaying the redistributable vehicle runtime files onto the currently published client-mod archive:

```powershell
.\scripts\Build-FetcherClientModBundle.ps1 `
  -BaseArchivePath .\release-assets\client-mod-base\openmw-client-mods.zip `
  -VehicleDataRoot "C:\serena_workspaces_directory\fetcher-simulator\Data Files\fetcher-bardcraft\Gameplay\FetcherVehicles" `
  -OutputPath .\release-assets\openmw-client-mods.zip
```

The builder excludes development source JSON, preserves the truck's CC BY 4.0 attribution, and adds `FetcherVehicles.omwaddon` to the package manifest.

## Rollback

Release assets use stable prerelease tags, so rollback means rebuilding a known-good commit and replacing the affected release assets together. Installed users continue launching the same updater BAT file.
