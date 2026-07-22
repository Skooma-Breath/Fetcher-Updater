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

- `ClientRepository` defaults to `Fetcher-Simulator/Fetcher-Simulator`, using the unified clean `Fetcher-Simulator` release and `fetcher-simulator.zip` asset.
- The Fetcher map/client-mod bundle is independently published here as the `openmw-client-mods-mp-clients` prerelease and installed by the tester-tools bootstrap.
- `TesterToolsRepository` defaults to `Skooma-Breath/Fetcher-Updater`.
- Bardcraft and Starwind repositories are required in `fetcher-client-patches.json`.

Legacy updater calls that pass `-Repository` still work: it is an alias for `ClientRepository` only and cannot redirect tester-tools or compatibility-patch downloads.

The updater validates `fetcher-client-files.json` to recognize a managed client installation. A missing or invalid inventory forces one complete client refresh. Tester tools, mods, and compatibility patches remain protected overlays managed here.

Every GitHub download requires the SHA-256 digest supplied by the release API. The installer also rejects unsafe or duplicate archive paths, unsupported manifests, unmanifested payloads, and file hash or size mismatches. The updater preserves mutex locking, receipt/marker verification, and atomic state replacement.

## Rollback

Release assets use stable prerelease tags, so rollback means rebuilding a known-good commit and replacing the affected release assets together. Installed users continue launching the same updater BAT file.
