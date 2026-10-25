# Fetcher community launcher — native backend

Server operators: start with [Server setup and launcher mod lists](SERVER-OWNER-GUIDE.md)
for the engine setup guide, UMO JSON authoring and automatic publication through the master.

Windows C++17 backend with the existing WebView2/web-interface approach. The new
launcher does not run PowerShell for its update/mod-profile actions.

For first installation, select your owned Morrowind `Data Files` under Mods and press
**Save folder**, or use **Setup wizard** to import an existing Morrowind installation.
The Windows client package must include `openmw-wizard.exe`, `openmw-iniimporter.exe`
and the local Qt Windows/SVG plugins with `qt.conf`. CI and native Setup reject packages
missing those files. If an older install says the wizard is missing, close the game and
use **Updates → Install updates**.

The native bootstrap regression uses recording executables to test handoff and package
rejection. Release acceptance must also start the real Qt wizard through the installed
launcher's `wizard` action, confirm its `OpenMW Wizard ready` log and local Windows
plugin, and run the packaged INI importer from an isolated fresh install.

Implemented:

- Appearance picker with Graphite (default), Obsidian, Deep Space and Ember themes, themed
  controls/panels and shadowed headings. Floating skooma rockets remain in every theme (subject to
  the existing reduced-motion preference). The local theme choice survives launcher updates and
  restarts in `_fetcher_launcher/settings.json`; no remote themes or fonts are loaded.
- Updates → Storage: actual-volume free space, profile estimates and guarded unused-cache
  preview/removal. Saved configurations and manual mod folders remain protected. See [STORAGE.md](STORAGE.md).
- Public server browser using the game's live master service, search, favorites and direct connect.
- Measured ICMP/TCP host ping and sorting in both browsers; see [PING.md](PING.md).
- Automatic migration and removal of verified legacy TR/TD folders after preparing current
  replacements; no migration buttons. See [LEGACY-TRTD-MIGRATION.md](LEGACY-TRTD-MIGRATION.md).
- Public Vanilla surf/truck/Studio profile and separate existing-installation connection route.
- Automatic master-hosted owner profiles: no player JSON/URL paste, pinned content preparation on
  Play, endpoint/digest validation and retention of existing profile settings. See PROFILE-FORMAT.md.
- Native WinHTTP release checks/downloads, GitHub SHA-256 validation, native ZIP/7z/RAR extraction, file receipts/cache,
  client and launcher/tools installation, update mutex and exception rollback with retained backups.
- Per-server data paths/plugin order, TES3 master dependency checks, optional local mods and isolated configs.
- Local model/source/license/adaptation records and preserved content-bundle attribution notices.
- Reviewed server manifests, owner/source isolation, previous-manifest selection and ordinary archive layouts.
- Declarative pinned UMO lists with folder selection and native copy/rename/remove operations;
  external UMO Nexus sign-in/download helper and exact-file manual import.
- Server-pinned XML FOMOD selections, option/flag validation and isolated selected-file layouts.
- Windows-protected local Nexus keys, explicit redirect policy and mod executable/script rejection.
- Existing-installation mode launches an already configured full mod setup unchanged.
- Interrupted native client/tools updates restore their journaled files on restart. Updates exposes
  previous completed snapshots; restores refuse corrupt backups and post-update local edits.
- Package cache verification records size/write-time metadata after hashing. Unchanged files reuse
  that result; changed files and legacy receipts are hashed, and explicit repair checks full hashes.
- Hash-gated compatibility deltas reconstruct verified files in isolated layouts without running
  legacy scripts. Read-only preset authoring binds exact installed cache identities and audits sources.
- Explicit profile actions can restore hash-pinned assets from owned Morrowind BSAs/loose files.
  Fetcher's experimental Starwind layer is optional and is never injected into ordinary Starwind installs.

**Migration boundary:** the curated experimental 103-package snapshot passes native preparation
and real VPS world entry; see [EXPERIMENTAL-VPS-ACCEPTANCE.md](EXPERIMENTAL-VPS-ACCEPTANCE.md).
Arbitrary legacy recipes, encrypted/multipart archives, unsupported codecs, advanced FOMOD
dependencies/player wizards, plugin cleaning and third-party tool bootstrap remain unsupported.
Real free-account UMO/NXM and second-device acceptance pass; broader device/download coverage
and Premium entitlement trials remain. UMO is the selected public Nexus helper. A standalone
Fetcher SSO route would need its own application registration. Existing script entry points remain.

## Build and validate

```powershell
.\scripts\Build-FetcherLauncher.ps1 -OutputDir release-assets\fetcher-launcher-native-candidate
python tests\test_native_backend.py --exe launcher\build\bin\FetcherLauncher.exe `
  --bundle release-assets\openmw-client-mods-studio-candidate.zip
```

The build runs CTest, local HTTP/package integration and the launcher self-test. The optional
bundle test exercises the real combined surf/truck/Studio archive. Dependencies are pinned through
CMake: webview 0.12.0, nlohmann/json 3.11.3, miniz 3.0.2, libarchive 3.8.10,
liblzma 5.8.4, zlib 1.3.2 and bzip2 1.0.8. Their licenses ship in the notices file.
The Microsoft Edge WebView2 Runtime is required. C++ handles backend logic; HTML/JavaScript renders UI.

## Isolated preview

```powershell
.\release-assets\fetcher-launcher-native-candidate\FetcherLauncher.exe `
  --install-root "C:\serena_workspaces_directory\Fetcher-Updater\release-assets\native-preview"
```

Use this isolated directory when reviewing the candidate. Update buttons follow the **currently
published** client/tools tags, which still contain the older public launcher until this candidate is
released. Do not use a development game installation as a disposable update test.

Opening the GUI refreshes servers and checks launcher/client/profile updates without installing.
Successful updates restart the GUI only when its executable or interface changed. The verified
replacement window confirms readiness before the old window closes. See [AUTO-UPDATES.md](AUTO-UPDATES.md).
There is no automatic Nexus/UMO installation on startup. Server discovery is read-only. Managed
profile preparation downloads its declared packages and builds a separate config before launch.
Current public servers have not been changed by this candidate. The preview asset profile deliberately
pins a distinct combined candidate archive; it cannot silently use the older public bundle.

See [PROFILE-FORMAT.md](PROFILE-FORMAT.md) for operator profiles and launch isolation,
[TEST-REPORT.md](TEST-REPORT.md) for evidence/limits, and [ROADMAP.md](ROADMAP.md) for remaining migration work.
See [UPGRADE-ACCEPTANCE.md](UPGRADE-ACCEPTANCE.md) for the verified old-launcher update path and
the repeatable isolated upgrade test. Older builds need one manual reopen when upgrading to the
first build with automatic restart; subsequent GUI updates restart themselves when required.
See [BOOTSTRAP.md](BOOTSTRAP.md) for standalone native first installation and saved multiplayer
identities. [UMO-HELPER.md](UMO-HELPER.md) describes Nexus setup, distribution/source delivery and
the optional browser link association. [NEXUS-REGISTRATION.md](NEXUS-REGISTRATION.md) retains
the future standalone Fetcher SSO registration steps.
See [WINDOWS-ACCEPTANCE.md](WINDOWS-ACCEPTANCE.md) for the second-device fresh-install and UI bridge
checks, and the repeatable SSH acceptance tools.
See [RELEASE-REVIEW.md](RELEASE-REVIEW.md) for independent launcher publication, the separate Studio
cutover and the existing VPS build/deployment path; no ARM server GitHub asset is required.

## Published direct-download candidate

The first public server-owned manifest is available at:
https://github.com/Skooma-Breath/Fetcher-Updater/releases/download/openmw-client-mods-mp-clients/fetcher-vanilla-studio.json

The distinct openmw-client-mods-studio-candidate.zip asset is pinned by that manifest. Both were
added with explicit operator approval; openmw-client-mods.zip was not replaced. A fresh native
HTTPS test downloaded 33,598,923 bytes, verified SHA-256, extracted 354 files and validated the
surf/truck/Studio assets, credits and plugin order. No public launcher or server was deployed.

An opt-in repeatable test is available; choose a new test directory for each run:

```powershell
python tests/test_server_manifest_live.py --exe <candidate-Launcher.exe> `
  --root <new-test-directory> --game-data <owned-Data-Files-directory> `
  --url https://github.com/Skooma-Breath/Fetcher-Updater/releases/download/openmw-client-mods-mp-clients/fetcher-vanilla-studio.json
```
