# Native launcher migration and public rollout

## Published public-test build

The native launcher public test is published at signed source `862081d`, with the matching
`Fetcher-Simulator-Studio` Windows client and live Fetcher Vanilla VPS profile. The normal
`fetcher-simulator` installation passes published native setup, real VPS world/Studio entry,
MIDI UI playback, scaled fullscreen Mixer/Piano navigation and server project-save persistence.
See VANILLA-VPS-ACCEPTANCE.md for exact versions, checks and rollback locations.
Vanilla also serves its 600-song MIDI library through Studio Lua v30: catalogues merge with
personal MIDI files and selected songs download over the game connection. Testers no longer
need the separately copied MIDI pack. The compatible published Windows client remains unchanged.

Native client/tools updater, verified package cache/install, exception rollback, public server browser,
favorites/direct connect, imported server profiles, isolated mod configurations, plugin master ordering,
optional local mod controls, reviewed/namespaced server manifests and previous-manifest rollback,
UMO ZIP/7z/RAR normalization and contained copy/rename/remove layouts, Nexus API-key/manual archive
routes and external archives. Server-pinned XML FOMOD choices install required/selected files into
isolated layouts with option, flag and priority validation.
The published legacy launcher has passed isolated old-to-native tools upgrade acceptance, including
a running staged GUI, reopening, personal-file preservation and rejected invalid packages.
Live Nexus validation with Hackle-Lo Pipes confirms the earlier native free-account automatic-download
refusal and exact-file manual import. The selected normal route now uses a pinned external UMO
helper for Nexus sign-in/downloads, with source/license delivery and opt-in NXM handoff. Native
personal keys remain under advanced private testing. Standalone Fetcher SSO is a future option.
Actual UMO SSO and the free-account Hackle-Lo download/preparation pass; the original NXM handler
is restored after acceptance. Premium entitlement, broader browsers and missing-prerequisite trials remain.
Fresh installation on PillowPC now passes with its existing Windows 11/WebView2/VC prerequisites:
real desktop UI/bridge, live server browser, HTTPS combined-content preparation, client runtime,
UMO runtime, cached repair, legacy upgrade and 32 bootstrap/recovery checks. See WINDOWS-ACCEPTANCE.md.
The installed candidate also joins an isolated world on PillowPC using Intel HD Graphics 615:
server Lua loads, Studio scripts stage, the keyboard arrives and the actor roster completes.
Native updates now journal complete backup/hash plans before mutation, recover interrupted process
updates on restart, and expose completed client/tools snapshots in the Updates restore panel.
Standalone native first installation now obtains verified launcher/client releases without PowerShell.
First-run guidance and per-server account/character choices are implemented; see BOOTSTRAP.md.
Hash-pinned native compatibility deltas and exact-version UMO cache authoring are implemented.
The optional Fetcher experimental-server preset omits Mario Race and the Zelda beta mod at the operator's request;
the existing installation and legacy list retain them. Its 90 remaining mods resolve to 99 archives.
All 99 source archives pass native preparation. Native application of 19 existing Fetcher fixes and
18 Bardcraft multiplayer fixes passes; remaining installed overrides/full preset parity are open.
Model attribution is retained in local records and content packages. A combined surf/truck/Studio bundle is built
locally and passes native integration. The web interface is retained; its backend uses C++.

## Remaining broader acceptance

1. Finish general server-list parity beyond the implemented reviewed manifests, pinned declarative
   UMO imports and basic native layouts, ZIP/7z/RAR extraction, Nexus API-key downloads and exact-file
   manual import. Add advanced FOMOD environment dependencies/player wizard and finish live Nexus
   Premium-account, broader-browser and clean-machine UMO-helper acceptance. UMO sign-in/free-account
   NXM download handoff passes live acceptance and isolated fixtures. Native bootstrap and second-device
   fresh installation pass; missing-prerequisite acceptance remains. The legacy installer
   remains available. Fetcher's experimental-server preset is separate optional work below.
2. A fresh native install and real game join using the generated combined-content config now pass
   against a matching staged local server. Second-device fresh installation/UI/runtime and real
   world-join acceptance pass, including server Lua/Studio delivery. Validate missing-prerequisite Windows installations and the
   final published release sources. Native process-interruption recovery and operator rollback are implemented;
   broaden acceptance to clean machines, storage failures and the final coordinated release payload.
3. The coordinated Vanilla server/client/content switch is complete. The VPS builds its own ARM
   executable; no ARM GitHub asset was published. Keep the original content archive, Vehicles client
   release and private rollback snapshots. Choose wider public-world keyboard placement separately
   from the initial ToddTest acceptance keyboard.
4. Collect small-group soak-test results for the published payload. Legacy upgrade acceptance passes
   on the development PC and PillowPC; final public HTTPS setup/check passes on the normal development
   installation. Repeat wider-device and failure acceptance as needed. Tell players to close and reopen
   the old launcher after updating.

## Optional Fetcher experimental-server preset

Starwind compatibility is an explicit server-profile layer, never an automatic fix for a Nexus
Starwind install. It is not included in the built-in general/Vanilla profiles. Only reviewing and
preparing a profile that declares the layer downloads its package and restores owned-game assets.
Another server owner using Starwind receives its original archive unless they explicitly author
and publish such a layer. Plain and experimental configurations/raw/derived layouts remain separate.

All 99 sources and 37 native compatibility fixes are verified; see PRESET-AUTHORING.md. Finish this
profile's remaining layout adjustments, Starwind layer and plugin/groundcover/game-join acceptance
independently of the general launcher rollout. The original game installation is retained.

## Subsequent mod-manager/browser improvements

- Server-advertised profile URLs, manifest identity/version display and protocol/build compatibility.
- Rich dependency/conflict view, optional-mod presets, package removal/cache management and disk-space UI.
- More server operators/profile formats, connection diagnostics and large-list UX checks.
- Choose a content-specific attribution display (in-game Studio Help/Credits or server-profile
  content details). Local source/license/adaptation records live in docs/content-attribution;
  bundled notices remain in each content package. The general launcher has no model Credits tab.

The 2026-10-22 public test publishes the five launcher assets and matching Windows client, updates
the separate Vanilla Studio manifest, and enables the combined content/Studio runtime on Vanilla.
The original combined-release archive and older client release remain available for rollback.
