# Vanilla VPS public test — 2026-10-22

The normal `C:\serena_workspaces_directory\fetcher-simulator` installation now uses the native
launcher and its isolated Fetcher Vanilla profile to connect to `163.192.106.225:25569`.
Its original main mod configuration, main settings and all 36 recorded settings/key/save files
remain byte-for-byte unchanged. The Synth fixtures on ports 25567/25568 were not changed.

## Published inputs

- Launcher implementation/source: signed `862081d7ebf13594e606b1ded326d66a15ac201f`.
  [Public test release](https://github.com/Skooma-Breath/Fetcher-Updater/releases/tag/fetcher-tester-tools)
  contains exactly the tools ZIP, FetcherSetup.exe, Install-Fetcher-Tester-Tools.ps1,
  Setup-Fetcher-Updater.bat and fetcher-simulator-umo.json. Tools ZIP SHA-256:
  `cf564606b8928bebaacc594d8f00de2a6eaaf9da490bcfa1b7d48ffb3ef617a1`.
- Matching Windows native client/server source: signed `f0241114ccaeb206cf94b102ae8472aa4273d736`.
  [Client prerelease](https://github.com/Fetcher-Simulator/Fetcher-Simulator/releases/tag/Fetcher-Simulator-Studio)
  contains fetcher-simulator.zip, SHA-256
  `76e67d5c83e83fcbd63d5b3b547dde469b4d0cfef6b3d27c05a2d52fa24b4072`.
  Installed openmw.exe SHA-256: `d53eaa2e513a9f9fcdbf1e55791699598c2d2c9ae1adfe7939dcfaa90670f313`.
- Server-delivered Studio Lua package v29 is from signed
  [6349b2c69f7c8343835aee45b3049e721a9eb0c4](https://github.com/Fetcher-Simulator/Fetcher-Simulator/tree/Fetcher-Studio-Lua-29).
  This Lua-only correction uses the scaled Windows GUI layer for window bounds. The same commit
  fixes the VPS deployment script to install the matching defaults.bin alongside its executable.
  Native client/server code remains the matching f024 build.
- The existing separate combined content ZIP remains SHA-256
  `8ad68f4741d08560cb5e6cb7887fc834ca7ad6408847fcf182499e638a822fdb`.
  [Vanilla profile](https://github.com/Skooma-Breath/Fetcher-Updater/releases/download/openmw-client-mods-mp-clients/fetcher-vanilla-studio.json)
  now targets the live Vanilla endpoint. Keyboard/stand and vehicle attribution files are retained.
  The original openmw-client-mods.zip and older Vehicles client release remain unchanged.

## Live checks

Published FetcherSetup.exe successfully installed from the public HTTPS tools/client endpoints into
the normal existing installation. The installed native launcher subsequently reported `upToDate=true`
for the published Studio client and Fetcher Vanilla profile. Every public launcher asset digest was
checked against its local build input.

Real keypair authentication, world entry and server package activation pass with a new operator-owned
`Studio Test` character in ToddTest. Client and server resolved content fingerprint:
`4c06e53c1248ef92eef1d05e50f1abd6271cf6b740fc121454f448ef21b0f52e`.
The server delivers one package, two scripts and 26 files. Final package set hash:
`cd07b95a4304b094a79e6e8bff463f4be70a500c0f21b8c0c0eb83bce0bbfd4c`.

The combined keyboard/stand renders and opens FETCHER STUDIO. An original four-note local diagnostic
MIDI starts and completes in the UI. Fullscreen controls, Mixer-to-Piano navigation and the fullscreen
piano work area were visually checked at the installation's existing 120% game GUI scale.
Audible output quality and extended multi-player band/soak acceptance remain tester checks.
All 21 Lua suites pass, including 15,584 client/UI assertions and the added GUI-layer regression.

The test project was saved through Studio. Its library (2,601 bytes) and recovery (5,248 bytes) are
stored in the server database under the test character's ProceduralSynth namespace; both hashes
were verified unchanged after server restart. SQLite quick_check passes. There is exactly one
persistent keyboard across restarts. Final local admin health returns HTTP 200/ok and Vanilla is
listed publicly. Temporary test input/display overrides were removed and the test client was closed.

## Runtime boundaries

Vanilla retains its existing health endpoint on localhost:8082. The initial adapter reused only
activation and surf/vehicle commands, which omitted shared commands such as /f and /placeat.
The command-restoration follow-up now reuses core's common chat handler and connect/disconnect
handlers, with an explicit Vanilla module/command allowlist. General commands, marks/recall,
vanilla speech, surf/vehicle and operator placement/travel commands share the engine implementation.
Help is filtered by installed commands and admin privileges. Experimental Bardcraft, test-spawner,
custom-race voice commands, Lua diagnostics and HTTP administration remain outside this profile.
Admin login fails closed when no password is configured; the operator requested a generated
password stored in a private local file and the server's private config, never a public package.
Static-item activation retains native mutation validation and the shared range/cell evaluator.

Install core.lua, intent_policy.lua, command_registry.lua, recordstore.lua, mark_recall.lua,
speech_commands.lua, speech_data.lua, starwind_speech_data.lua, custom_speech_data.lua,
surf_commands.lua and vehicle_commands.lua from the engine source, plus Studio's modules/package.
The extra speech data tables are metadata dependencies, not additional installed client mods.
Run `lua tests/test_vanilla_commands.lua ../openmw` from the updater repository to check actual
shared dispatch, help filtering, placement permissions, quoted arguments, disconnect cleanup,
minimal profiles, ordinary chat and the retained health endpoint (30 checks). The same suite
passes on the VPS under both Lua and LuaJIT; all 21 Studio suites also pass.
Deployed after the operator confirmed both players had saved and approved a Vanilla-only restart.
The shared command implementation is signed engine commit `65485ac4bc` (source tag
`Fetcher-Vanilla-Commands-20261022`); no native executable or public launcher asset changed.
All nine installed payload hashes match staging. Health is OK, both public listings remain
available, all four Studio storage rows match the shutdown backup, and the experimental process
is unchanged. The private password lives at
`%LOCALAPPDATA%\Fetcher\Operator\vanilla-admin.txt`; its directory ACL allows only the local user,
SYSTEM and Administrators. The server config is owner-readable/writable only (0600).
The private script/config/database/storage rollback snapshot is
`/srv/fetcher-release-backups/20261022-vanilla-commands`; restore scripts/config only if reverting,
not a stale player database over subsequent player progress.
The Vanilla server manifest loads vanilla_runtime.lua, procedural_synth.lua and studio_deployment.lua.
The last file is specifically the one-time ToddTest test placement, guarded by persistent storage;
it does not change all players' spawn positions. Wider public-world instrument placement is separate.

The VPS push hook built the ARM server directly; no ARM GitHub asset was published. Its shared
executable SHA-256 is `7135ee26eb3959d46a59464a82cdc89e30792b948c58e70f6f7fe93cb8d631d4`.
The original experimental process remains running, and its operator server-scripts and client Lua
package trees were restored and verified against their pre-build hashes. The shared defaults.bin
was updated to the matching build after the first Vanilla restart exposed the stale-defaults issue.

## Retained recovery state

Private VPS snapshots: `/srv/fetcher-release-backups/20261022-studio-cb93a1c`, including online SQLite
backups, old binary/resources/config/scripts/packages, hook/source references and post-acceptance
database/storage copies. Local normal-installation backup:
`C:\serena_workspaces_directory\backups\fetcher-studio-cutover-20261022`.
Public-release inputs, prior assets, inventories and live receipts are retained under ignored
`release-assets/vanilla-cutover-20261022` and `studio-coordinated-review-20261022-r2`.
Do not publish these private directories or restore an older database over later player work.

The public tools tag now points to signed source 862081d; its previous public target and original
four assets are retained for rollback. Client/profile/server rollback must be coordinated.
The VPS hook still copies generic operator scripts/packages, so preserve those trees around future
pushes; matching defaults installation is fixed in engine source 6349b2c.

Launch the normal FetcherLauncher.exe, choose Fetcher Vanilla and Play. The prepared operator
identity selects Studio Test. Players upgrading through the old launcher should wait for success,
close that window and reopen it to enter the native interface.
