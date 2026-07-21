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
  -OutputDir .\release-assets `
  -SourceCommit (git rev-parse HEAD)

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
