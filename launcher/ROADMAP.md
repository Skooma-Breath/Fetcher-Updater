# Native launcher migration and public rollout

## Implemented candidate

Native client/tools updater, verified package cache/install, exception rollback, public server browser,
favorites/direct connect, imported server profiles, isolated mod configurations, plugin master ordering,
optional local mod controls, reviewed/namespaced server manifests and previous-manifest rollback,
UMO ZIP/7z/RAR normalization and contained copy/rename/remove layouts, Nexus API-key/manual archive
routes and external archives. Server-pinned XML FOMOD choices install required/selected files into
isolated layouts with option, flag and priority validation.
The published legacy launcher has passed isolated old-to-native tools upgrade acceptance, including
a running staged GUI, reopening, personal-file preservation and rejected invalid packages.
Live Nexus validation with Hackle-Lo Pipes confirms the free-account automatic-download refusal
and exact-file manual import. Fetcher application registration and browser entitlement tests remain.
Native updates now journal complete backup/hash plans before mutation, recover interrupted process
updates on restart, and expose completed client/tools snapshots in the Updates restore panel.
Hash-pinned native compatibility deltas and exact-version UMO cache authoring are implemented.
The optional Fetcher experimental-server preset omits Mario Race and the Zelda beta mod at the operator's request;
the existing installation and legacy list retain them. Its 90 remaining mods resolve to 99 archives.
All 99 source archives pass native preparation. Native application of 19 existing Fetcher fixes and
18 Bardcraft multiplayer fixes passes; remaining installed overrides/full preset parity are open.
Model attribution is retained in local records and content packages. A combined surf/truck/Studio bundle is built
locally and passes native integration. The web interface is retained; its backend uses C++.

## Remaining work before replacing the public launcher

1. Finish general server-list parity beyond the implemented reviewed manifests, pinned declarative
   UMO imports and basic native layouts, ZIP/7z/RAR extraction, Nexus API-key downloads and exact-file
   manual import. Add advanced FOMOD environment dependencies/player wizard, Nexus app registration/SSO/NXM
   handoff and live eligible/free-account trials. Finish the native bootstrap. The legacy installer
   remains available. Fetcher's experimental-server preset is separate optional work below.
2. Validate a real game launch/join from the generated config with a staged matching server and clean
   Windows bootstrap. Native process-interruption recovery and operator rollback are implemented;
   broaden acceptance to clean machines, storage failures and the final coordinated release payload.
3. Stage the vanilla-server content/authority and Studio Lua/native-client release together. The
   combined archive/credits and server manifest are published as separate candidate assets and pass
   real HTTPS preparation. Promote the matching profile only with the coordinated server/client
   rollout. Keep the original archive and rollback copies.
4. Rerun UPGRADE-ACCEPTANCE.md against the final package, then release the native launcher candidate
   to a small public test group before replacing the stable tag. Tell players to reopen after updating.

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

The launcher build remains local. Two additional content-test assets were published with explicit
approval; the original release assets and public server configuration are unchanged. This roadmap
does not authorize an automatic launcher or public-server deployment.
