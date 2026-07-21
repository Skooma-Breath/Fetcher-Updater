# Fetcher Updater

This repository owns the independently released Fetcher Simulator updater and tester tools. Installed filenames and release asset names remain compatible with existing Fetcher Simulator installations.

## Layout

- `release-root/` contains the files installed beside `openmw.exe`.
- `scripts/` contains package and stable-prerelease publishing scripts.
- `tests/` contains package and installer validation.
- `.github/workflows/release.yml` builds and replaces the stable prerelease.

## Build

From PowerShell 7 or Windows PowerShell 5.1:

```powershell
.\scripts\Build-FetcherTesterTools.ps1 `
  -OutputDir .\release-assets

.\tests\Test-Package.ps1 `
  -ArchivePath .\release-assets\fetcher-tester-tools.zip
```

The build preserves these public release artifacts:

- tag: `fetcher-tester-tools`
- archive: `fetcher-tester-tools.zip`
- bootstrap: `Join-Fetcher-Test-Channel.bat`
- installer: `Install-Fetcher-Tester-Tools.ps1`
- UMO list: `fetcher-bardcraft-umo.json`

Publishing is intentionally performed only by the GitHub Actions workflow or by an operator who provides `GH_TOKEN`. Local validation does not push tags or create releases.

## Release routing

The installed updater keeps each release source independent:

- `ClientRepository` defaults to `Skooma-Breath/Fetcher-Simulator`.
- `TesterToolsRepository` defaults to `Skooma-Breath/Fetcher-Updater`.
- Bardcraft and Starwind repositories are required in `fetcher-client-patches.json`.

Legacy updater calls that pass `-Repository` still work: it is an alias for `ClientRepository` only and cannot redirect tester-tools or compatibility-patch downloads.

Every GitHub download requires the SHA-256 digest supplied by the release API. The installer also rejects unsafe or duplicate archive paths, unsupported manifests, unmanifested payloads, and file hash or size mismatches. The updater preserves mutex locking, receipt/marker verification, and atomic state replacement.

## Migration

The last `fetcher-tester-tools` release produced from `Skooma-Breath/Fetcher-Simulator` must be built from the routing-bridge commit in OpenMW. That bridge keeps the installed `Update-Fetcher-Simulator.bat` filename but directs its next tester-tools lookup here. Migration order is:

1. Publish and validate this repository's `fetcher-tester-tools` prerelease with the same asset names.
2. Publish and validate the old-repository bridge release; its next lookup can now resolve immediately here.
3. Pin this repository's archive digest in the OpenMW `FETCHER_TESTER_TOOLS_SHA256` repository variable.
4. Enable the OpenMW cleanup commit that removes its updater source/workflow ownership.

Do not publish the cleanup before the bridge release is available to existing installations.

## Rollback

Release assets use a stable prerelease tag, so rollback means rebuilding a known-good commit here and replacing all four public assets together. Update the OpenMW digest pin to the rolled-back `fetcher-tester-tools.zip` digest before its next client package. If the split itself must be rolled back, restore the bridge commit's old-repository workflow and assets; installed users continue launching the same BAT file.
