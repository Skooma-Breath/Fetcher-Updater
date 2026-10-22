# Fetcher Updater

This repository owns the independently released Fetcher Simulator updater and tester tools. Installed filenames and release asset names remain compatible with existing Fetcher Simulator installations.

## Layout

- `release-root/` contains the files installed beside `openmw.exe`.
- `launcher/` contains the Windows C++/WebView launcher with the native backend candidate.
- `scripts/` contains package, client-inventory, launcher-build, and stable-prerelease publishing scripts.
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
- UMO list: `fetcher-simulator-umo.json`

Publishing is intentionally performed only by the GitHub Actions workflow or by an operator who provides `GH_TOKEN`. Local validation does not push tags or create releases.

## Release routing

The installed updater keeps each release source independent:

- `ClientRepository` defaults to `Fetcher-Simulator/Fetcher-Simulator`.
- Tester installations receive `fetcher-update-channel.json`, which currently routes them to the `Fetcher-Simulator-Vehicles` release and `fetcher-simulator.zip` asset. Explicit client repository/tag/asset arguments override the channel file.
- The normal public `Fetcher-Simulator` release remains available for users who are not participating in the multiplayer vehicle test.
- Tester tools refresh before client resolution, so a newly installed channel file can redirect the same updater invocation. Installations running the older updater order require one additional updater run for this initial migration.
- The Fetcher map/client-mod bundle is independently published here as the `openmw-client-mods-mp-clients` prerelease and installed by the tester-tools bootstrap.
- Hash-gated Lua compatibility deltas for non-Bardcraft UMO mods are packaged with tester tools and owned here. Current records cover installer-level Fashionwind Expanded, Held Light Boost, player-controls, and Ratkinn compatibility; multiplayer Lua overrides such as Devilish Touch of Madness are delivered by the server package layer instead of rewriting third-party mod files. Unknown or locally edited source hashes are refused.
- `TesterToolsRepository` defaults to `Skooma-Breath/Fetcher-Updater`.
- Bardcraft and Starwind repositories are required in `fetcher-client-patches.json`.

Legacy updater calls that pass `-Repository` still work: it is an alias for `ClientRepository` only and cannot redirect tester-tools or compatibility-patch downloads.

The updater validates `fetcher-client-files.json` to recognize a managed client installation. A missing or invalid inventory forces one complete client refresh. Tester tools, mods, and compatibility patches remain protected overlays managed here.

Every GitHub download requires the SHA-256 digest supplied by the release API. The installer also rejects unsafe or duplicate archive paths, unsupported manifests, unmanifested payloads, and file hash or size mismatches. The updater preserves mutex locking, receipt/marker verification, and atomic state replacement.

## Hosting a multiplayer server

The Fetcher Simulator client/server releases themselves ship `Build-OpenMWServerAuthority.py` plus `server-setup-guide.md`; these are not dependent on the Fetcher updater. The Python builder is generic to this OpenMW multiplayer server branch: it reads a known-good client's `openmw.cfg`, resolves the effective VFS, and builds the portable `content-authority/` input that `ServerContentRegistry` hashes at server startup.

The canonical source and guide live in the Fetcher Simulator repository under `files/server/`. The full guide covers UDP port forwarding, host firewalls, `server.cfg`, public listing, authority generation/deployment, updates, and hash-mismatch troubleshooting.

## Native community launcher candidate

The launcher now has a C++17 backend for client/tools updates, verified package downloads,
server browsing, per-server mod profiles and generated plugin load orders. Its web interface
is retained and reorganized into Servers, Mods and Updates. It invokes no PowerShell
for these actions. Server owners can provide a generic profile URL; required content is kept
separate from optional local mods and the user's main configuration.

This is a local candidate. The current public vanilla profile remains separate from the new
surf/truck/Studio preview. Publishing the package and changing server content are coordinated
rollout steps. The combined candidate archive and its server manifest are now published as separate
release assets and passed a fresh native HTTPS download/preparation test. The original mod archive,
public launcher and public server configuration are unchanged.

Reviewed server manifests now accept pinned declarative UMO lists, native ZIP/7z/RAR archives and contained
copy/rename/remove layout operations.
Nexus API-key downloads and exact-file manual import are implemented; live Nexus account trials,
FOMOD/plugin-cleaning support and the full legacy compatibility pipeline remain migration work. Existing script entry points and existing-installation launch mode are preserved.

Build, isolated preview instructions, acceptance evidence and profile format are documented in
[launcher/README.md](launcher/README.md).

## Vehicle client-mod bundle

Build a candidate bundle by overlaying the redistributable vehicle runtime files onto the currently published client-mod archive:

```powershell
.\scripts\Build-FetcherClientModBundle.ps1 `
  -BaseArchivePath .\release-assets\client-mod-base\openmw-client-mods.zip `
  -VehicleDataRoot "C:\serena_workspaces_directory\fetcher-simulator\Data Files\fetcher-simulator\Gameplay\FetcherVehicles" `
  -StudioAssetsRoot "C:\serena_workspaces_directory\openmw\files\server\optional-assets\fetcher.procedural-synth" `
  -OutputPath .\release-assets\openmw-client-mods-studio-candidate.zip
```

The builder preserves the surf-map content, adds the vehicle runtime and `FetcherVehicles.omwaddon`, and optionally overlays the manifest-verified Studio meshes, textures and icon. Studio attribution/license/change notices ship under `Data Files/FetcherStudio`; truck credits remain under `Data Files/FetcherVehicles`. The candidate archive is not published automatically.

## Rollback

Release assets use stable prerelease tags, so rollback means rebuilding a known-good commit and replacing the affected release assets together. Installed users continue launching the same updater BAT file.

Model/source/license/adaptation records are retained locally in
[docs/content-attribution](docs/content-attribution/README.md) and in the distributed content bundle.
The general launcher has no Fetcher-specific Credits tab; a content-specific display is a roadmap item.
