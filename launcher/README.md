# Fetcher community launcher — native backend candidate

Windows C++17 backend with the existing WebView2/web-interface approach. The new
launcher does not run PowerShell for its update/mod-profile actions.

Implemented in this candidate:

- Public server browser using the game's live master service, search, favorites and direct connect.
- Current-public vanilla and separate surf/truck/Studio preview profiles; generic operator profile URLs.
- Native WinHTTP release checks/downloads, GitHub SHA-256 validation, ZIP extraction, file receipts/cache,
  client and launcher/tools installation, update mutex and exception rollback with retained backups.
- Per-server data paths/plugin order, TES3 master dependency checks, optional local mods and isolated configs.
- Explicit model/artist credits, source links, CC BY 4.0 links and adaptation descriptions.
- Reviewed server manifests, owner/source isolation, previous-manifest selection and ordinary archive layouts.
- Declarative pinned UMO ZIP lists, Nexus API-key downloads where eligible and exact-file manual import.
- Windows-protected local Nexus keys, explicit redirect policy and mod executable/script rejection.
- Existing-installation mode launches an already configured full mod setup unchanged.

**Migration boundary:** 7z/RAR archives, arbitrary UMO actions/FOMOD choices, third-party tool bootstrap and legacy
compatibility appliers have not yet been ported. Nexus authentication is fixture-tested; live account
acceptance and public application registration remain required. Existing script entry points remain supported during
migration. The native launcher does not claim to install/repair that entire historical mod list.

## Build and validate

```powershell
.\scripts\Build-FetcherLauncher.ps1 -OutputDir release-assets\fetcher-launcher-native-candidate
python tests\test_native_backend.py --exe launcher\build\bin\FetcherLauncher.exe `
  --bundle release-assets\openmw-client-mods-studio-candidate.zip
```

The build runs CTest, local HTTP/package integration and the launcher self-test. The optional
bundle test exercises the real combined surf/truck/Studio archive. Dependencies are pinned through
CMake: webview 0.12.0, nlohmann/json 3.11.3 and miniz 3.0.2. Their licenses ship in the notices file.
The Microsoft Edge WebView2 Runtime is required. C++ handles backend logic; HTML/JavaScript renders UI.

## Isolated preview

```powershell
.\release-assets\fetcher-launcher-native-candidate\FetcherLauncher.exe `
  --install-root "C:\serena_workspaces_directory\Fetcher-Updater\release-assets\native-preview"
```

Use this isolated directory when reviewing the candidate. Update buttons follow the **currently
published** client/tools tags, which still contain the older public launcher until this candidate is
released. Do not use a development game installation as a disposable update test.

There is no automatic Nexus/UMO installation on startup. Server discovery is read-only. Managed
profile preparation downloads its declared packages and builds a separate config before launch.
Current public servers have not been changed by this candidate. The preview asset profile deliberately
pins a distinct combined candidate archive; it cannot silently use the older public bundle.

See [PROFILE-FORMAT.md](PROFILE-FORMAT.md) for operator profiles and launch isolation,
[TEST-REPORT.md](TEST-REPORT.md) for evidence/limits, and [ROADMAP.md](ROADMAP.md) for remaining migration work.

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
