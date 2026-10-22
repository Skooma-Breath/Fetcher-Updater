# Native launcher candidate validation — 2026-10-21

The launcher executable is a local candidate. The approved combined content candidate and server
manifest were subsequently published as separate assets; the public launcher/server remain unchanged.

- MSVC 2022 x64 Release build; static native backend, nlohmann/json 3.11.3 and miniz 3.0.2.
- CTest exercises SHA-256, atomic metadata replacement, bounded ZIP extraction, traversal rejection,
  path/deletion containment, endpoint validation, malformed master records, plugin master ordering,
  missing masters/cycles, client inventory validation, protected-file preservation, exception rollback,
  native launcher/tools ownership and profile/favorite persistence.
- Existing tester-tools package, optional-launcher quarantine, client-inventory and legacy updater
  integration tests pass with the native launcher candidate packaged alongside the preserved scripts.
- `tests/test_native_backend.py` runs the real launcher executable against a local HTTP server with
  Unicode/spaced installation paths. It tests digest verification, package caching and corrupt-cache
  repair, isolated generated config, favorites, and ZIP traversal/case duplicates/symlinks/bad digests.
- The same native integration test downloaded/extracted the real combined surf/truck/Studio candidate,
  verified its required model/icon/credit files, and generated its plugin order successfully.
- Read-only native WinHTTP checks fetched the actual public server listing and GitHub client/tools
  release metadata. Both Fetcher Simulator and Fetcher Vanilla appeared.
- Native WebView2 inspection verified the populated server browser and the Credits page with the
  Yamaha/Rapunzel123, X-stand/Michael Perez, Casio/AleixoAlonso and truck/Daniel Zhabotinsky cards.
  Source and license buttons use restricted native external-browser links. No public server was joined
  and no public package was installed through the GUI.
- Studio assets were validated against the existing asset manifest before bundling. All credited
  sources, CC BY 4.0 notices, change descriptions, NIFs, DDS textures and the inventory icon are included.

The launcher invokes no PowerShell update process. Build/packaging scripts and the separate legacy
UMO/Nexus installer are still PowerShell. Existing-installation mode preserves that setup; porting
the complete UMO/compatibility workflow is remaining migration work. Package profiles provide the
new native mod-install path and automatic plugin order now.

Remaining acceptance: real game launch/join with generated configs against a matching staged server;
new-client bootstrap on a clean Windows machine; interrupted-update recovery and operator rollback UI;
full UMO migration; profile URL advertising from the master service; broader server/operator trials.
The tested rollback is exception/failure rollback, not a claim of power-loss transaction recovery.

## Server-owned manifest candidate

- tests/test_server_manifests.py exercises the real executable through 57 CLI operations: review and
  token acceptance, no archive download during review, owner/source identity isolation, version
  changes and stale tokens, previous-manifest selection, ordinary ZIP layouts, embedded and
  hash-pinned external UMO exports, unsupported actions/unpinned files/bad layouts, exact Nexus
  manual imports, Windows-protected fake key storage, authenticated automatic download fixtures,
  API-to-CDN credential separation, HTTP 403 and credential-bearing redirect refusal, executable
  payload rejection and redirect downgrade refusal.
- Production mode refuses loopback download URLs without contacting the fixture. Loopback HTTP and
  the fake Nexus endpoint require an explicit CLI-only test flag; the GUI cannot enable it.
- The same test downloads the actual combined Vanilla bundle from its fixture, verifies the exact
  archive hash and credits/model files, and generates the isolated surf/truck/Studio load order.
- No real Nexus credentials were supplied, no Nexus live download eligibility was tested, and
  no game join or public server configuration change is implied by these fixture tests.
- Native WebView2 review verified the full-width source/hash/load-order panel and accepting the
  Vanilla manifest through the real UI. Acceptance selected its managed profile without downloading
  archives. The Webb image and floating rockets remain visible. An independent development preview
  was used; the earlier DevTools window was preserved.

## Public GitHub download acceptance

With explicit approval, uploaded only openmw-client-mods-studio-candidate.zip and
fetcher-vanilla-studio.json to the existing openmw-client-mods-mp-clients release. GitHub's API
reported the expected SHA-256 values. The original openmw-client-mods.zip retained its asset ID,
size and digest (1b4908ee52c05639d7dac00d62c085ffdd43413828f51f795dd5b6cbd9da53c4).

`tests/test_server_manifest_live.py` then ran the real candidate executable in a new installation
root with no caches and no local-fixture flags. It imported the manifest over public HTTPS,
reviewed/accepted its fingerprint, followed GitHub's archive redirect, downloaded 33,598,923 bytes,
verified SHA-256 8ad68f4741d08560cb5e6cb7887fc834ca7ad6408847fcf182499e638a822fdb,
extracted 354 files and validated the declared plugin order, keyboard model/icon and both credit files.
The test preserved its personal-config marker. No game was launched and no server settings changed.
The live test report is retained in release-assets/server-profiles-github-test/live-test-report.json.

## Native archive and UMO layout candidate — 2026-10-22

- MSVC x64 Release build and launcher self-test pass. The native suite passes 153 assertions.
  ZIP remains on miniz; 7z/RAR use pinned static libarchive 3.8.10 with liblzma 5.8.4,
  zlib 1.3.2 and bzip2 1.0.8. No installed archiver, DLL or interpreter is needed at runtime.
- Native tests decode LZMA/LZMA2, PPMd, Deflate and BZip2 7z files, independent Unicode-path
  fixtures, and stored/compressed/solid classic RAR/RAR5 regression fixtures. Positive RAR
  payloads match independently obtained 7-Zip hashes. Tests reject traversal, case collisions,
  truncation, damaged compressed data, format mismatches, links, encrypted archives and an
  incomplete multipart archive. Fixture provenance and notices accompany the test archives.
- Contained copy, rename/move and remove operations support scoped UMO download actions.
  Tests cover explicit overwrite requirements, metadata/path containment, unsupported commands,
  cross-mod references and recursive self-copy rejection. Derived layouts have separate recipe
  caches; original verified archive contents remain unchanged.
- `tests/test_archive_layouts.py` passes 23 real-executable operations: a mixed 7z/RAR UMO list,
  plugin master order, required assets, modern/classic variants from identical source bytes,
  same-size corrupt-layout repair without redownload, failure rollback, folder-only selection,
  and exact-file Nexus manual 7z import without an API key.
- Existing native backend integration and all 54 server-manifest fixture operations pass.
  A fresh public HTTPS run also downloads and verifies the unchanged 33,598,923-byte Vanilla
  candidate, extracts its 354 files, checks plugin order/model/icon/credit files and preserves
  the personal configuration marker. Evidence is retained in
  release-assets/archive-layouts-github-test/live-test-report.json.
- Native WebView2 inspection confirms archive support guidance and a review-only local fixture
  displays its format, normalized copy/move/remove operations and explicit replacement flag before
  acceptance. The fixture was neither accepted nor downloaded. The Webb background, floating
  rockets and three-tab navigation remain present.

Encrypted/multipart archives, FOMOD choices, external plugin cleaners, complete historical UMO
compatibility processing, Nexus registration/SSO/NXM handoff and live-account eligibility remain
outside this slice. These tests do not launch a game or change the public launcher/server.

## Server-pinned XML FOMOD candidate — 2026-10-22

- MSVC x64 Release build and launcher self-test pass. Existing native tests retain 153 assertions;
  a separate native FOMOD suite passes 79 assertions. The implementation uses Windows XmlLite
  in-process; it neither adds an installer runtime nor executes FOMOD scripts.
- Project-authored fixtures cover required files, wrapper roots, Unicode filenames, UTF-8/UTF-16LE
  XML, all five group types, required/unavailable choices, flag-based option types and step visibility,
  nested flag expressions, conditional files, alwaysInstall/installIfUsable and priority/tie ordering.
- Tests reject invalid cardinality, unknown/missing/hidden/duplicate selections, unsafe paths, launcher
  metadata targets, absent/unreceipted sources, malformed/deep XML, DTDs/entities, unknown XML and
  unsupported environment dependencies. Unselected branches are validated as well.
- `tests/test_fomod.py` passes 24 real-executable operations: reviewed/pinned choices, source hashes,
  plugin-master order, post-install file operations, separate modern/classic layouts from one source,
  same-size cache repair without redownload, stale-review rejection, failed-preparation preservation,
  previous-manifest rollback and the UMO per-download extension. The local HTTP fixture confirms that
  an external-entity URL is never requested. An XML mapping to an executable output is rejected.
- The existing native backend, 54-operation manifest and 23-operation archive/layout integration
  suites still pass. UI module syntax checking passes. No public assets or server settings change.
- Native WebView2 review of a local developer fixture displays the config path and canonical
  step/group/option choices before acceptance. No fixture profile or archive was installed.

These are synthetic installer fixtures, not live third-party FOMOD compatibility certification.
Installed-file/game/extender dependencies, a player-facing wizard, C#/ModScript and nested installers
remain unsupported. See PROFILE-FORMAT.md for the exact subset, bounds and deterministic choices.

## Published legacy-to-native upgrade acceptance — 2026-10-22

Read-only GitHub discovery identified the public tools package as source
093ff61a9d6e2605d1f65834b94714db5c6acd35. Downloaded its 1,920,141-byte ZIP and verified GitHub's
SHA-256 df70eea0f76d7a0a931bfbf68eca90d7ad5a844e932e56b22a306c8d48857d71, plus every inventory
record, before executing it. The candidate tested is the signed 972f558 native FOMOD tools package.

- The real legacy executable's updater entry point and pristine public scripts detected the tools
  update without downloading it during status checking, then installed the exact candidate files.
- Repeated with a live legacy GUI in an installation with spaces and a Unicode directory name.
  Confirmed that the old staged executable/UI remained running while installed files changed, then
  closed only that test window and reopened the new staged native executable with matching UI.
- Native self-test and backend initialization pass. Existing installation remains selected and
  personal config/settings/save/mod/synthetic-key/unrelated-UI markers remain byte-identical.
- Corrupt downloads and a digest-valid archive missing required ui/index.html both fail before old
  files are replaced. This is not a power-loss, mid-copy or whole-game rollback claim.
- Native WebView2 inspection of the reopened installation confirms its community server browser,
  Existing installation mode, original background and floating rockets. No Update or Play/Join
  button was clicked during UI inspection. Only test-owned processes were started/stopped;
  OpenMW was never launched.

Reports are retained in release-assets/launcher-upgrade-cli-test-3/upgrade-report.json and
release-assets/launcher-upgrade-gui-é/upgrade-report.json. The repeatable test supplies a test-only
adapter with local release endpoints and documented skip switches; the public launcher, updater and
bootstrap implementations remain unmodified. A child-only PS5.1 module path avoids this development
shell's PowerShell 7 module shadowing. Public GitHub assets/tags, servers and existing user game
installations are unchanged. See UPGRADE-ACCEPTANCE.md for scope and rerun instructions.
Both the download-current-public and retained-legacy-archive PowerShell wrapper paths pass; the
latter also repeats the full staged GUI check and cleans up only its own test windows.

## Live Nexus single-mod acceptance — 2026-10-22

The existing UMO configuration supplied the user's key in memory for an explicitly selected,
fresh test installation. Nexus account validation succeeded and reported a free account. The
native launcher then received HTTP 403 from the premium-style automatic file download endpoint,
offered manual import, accepted the cached original Hackle-Lo Pipes v4 ZIP and generated the
Morrowind/Tribunal/Bloodmoon/Hackle-Lo Pipe.esp order. Required pipe mesh, texture and sound exist.
The original config marker survived. The test-only DPAPI credential was removed afterward; no
credential value was written to request/result/report JSON or printed. The source UMO config is
unchanged. No game was launched and no server was changed by this preparation test.

The pinned Nexus file is morrowind mod 56939, file 1000064599 (now categorized OLD_VERSION).
The locally installed version's original 4,276,766-byte ZIP has SHA-256
a4211504a66587218c3ffd830a680ef33c0654110afbb503fb7d91b61e945970. The profile stores only its
source IDs/hash/layout, never mod payloads or credentials. Evidence:
release-assets/nexus-hackle-lo-live/nexus-live-report.json. The opt-in driver is tests/test_nexus_live.py.
This establishes real free-account refusal plus exact-file import, not a successful premium
download or browser-issued NXM entitlement. The user confirmed no known Fetcher application
registration; Nexus application approval/SSO and eligible-account trials are still pending.

## Native interrupted update recovery and operator restore — 2026-10-22

- Native transaction tests pass 59 assertions. Real child processes terminate after writes 1, 2
  and 3 for both client and tools updates. Backend startup restores executable/UI/resource/inventory
  bytes and removes newly added files. Repeated recovery is a no-op. Personal config/settings/save
  markers survive. Interrupted restore, corrupt-backup refusal, later-edit refusal and manual
  rollback are also tested. The fault helper is a test executable, absent from distributed tools.
- The native GUI recovery driver hard-kills an actual launcher/tools payload installation after
  replacing its executable and UI, then starts the partially updated installation. It restores all
  previous inventory files and restarts the previous native executable with the matching staged UI.
  It uses spaces/Unicode paths and preserves personal file markers. No OpenMW game is started.
- Native WebView2 inspection confirms the Updates restore panel lists the real completed snapshot.
  A separate CLI rollback of that real payload restores every previous package byte while the
  staged GUI remains open. The preview process is closed afterward. Evidence:
  release-assets/gui-recovery-acceptance/gui-recovery-report.json.
- The full build/self-test and existing native, server-manifest, archive/layout and FOMOD integration
  suites pass. Recovery preflights current/backup hashes, path containment and file ownership.
  Restoring an incompatible first installation reports that setup must be rerun. Public releases
  and game servers remain unchanged. Sudden-power-loss/storage-failure acceptance remains open.

## Native compatibility and full-preset source acceptance — 2026-10-22

- Native byte deltas validate exact source/output hashes and sizes, copy ranges, strict Base64,
  explicit creation, metadata containment and binary/BOM/null handling. The compatibility suite
  passes 16 assertions. Archive/layout integration now passes 36 real-executable operations,
  including derived-cache repair and preservation after wrong source/output hashes.
- A reviewed exact removal can discard an unused bundled helper; the entire final activated tree
  still rejects native programs/scripts. Merely selecting a data directory does not bypass it.
  Server-manifest integration passes 57 operations, including refusal without the removal and
  success with it. Original compressed/extracted cache bytes remain unchanged.
- Real Tamriel Data testing exposed a >8 MiB generated inventory and MAX_PATH failures after a
  longer hashed-cache move. Internal inventories now have a separate 128 MiB/100,000-file bound;
  external manifests retain 8 MiB. Extended paths/native separators support cache I/O/cleanup
  while ordinary paths remain in game configs. Reserved-name checks reuse a compiled expression.
- Source authoring resolves the operator's revised 90-mod list to 99 exact cached ZIP/7z/RAR
  archives. All pass native extraction/layout preparation: 5,065,202,678 source bytes. Mario Race
  and the Zelda beta mod are omitted only from the new preset, with explicit operator direction.
  No original game/config/list/database or public server is changed. Evidence:
  release-assets/umo-source-3/source-layout-report.json.
- Native application of 37 known fixes (19 Fetcher + 18 Bardcraft multiplayer 2.0.24) produces the
  exact installed output hashes. Engine resources remain excluded from server mod recipes.
  Evidence: release-assets/umo-source-3/compatibility-report.json. Read-only authoring tests cover
  exact-version binding, cache-name aliases, pin/hash mismatches, credential-URL exclusion,
  renamed plugin sources, new-file prerequisites and exclusion of client resources. The installed
  override audit compares 94,508 VFS paths; all 30 changed gameplay files are covered, with seven
  additional new outputs. Two shader-version differences remain distinct from compatibility fixes.

This is not full Simulation preset or game-join acceptance. Remaining installed overrides,
Starwind owned-game asset restoration and the full plugin/groundcover order still need curation
and testing. See PRESET-AUTHORING.md for repeatable commands and boundaries.

## Optional experimental Starwind isolation — 2026-10-22

The Starwind compatibility layer belongs only to the Fetcher experimental-server profile. It is
not injected by Nexus mod ID/name, not part of the built-in Vanilla profiles and not required for
the general launcher rollout. A reviewed profile must explicitly declare its separate package
and restoration actions before they can run.

- The x64 Release build passes all five CTest suites (165 native, 79 FOMOD, 59 transaction,
  16 compatibility and 25 owned-asset assertions), native HTTP/package tests, 57 manifest,
  36 archive/layout and 24 FOMOD integration operations, both authoring suites and self-test.
  Extended-path layout operations retain case-insensitive self-copy/overlap rejection.
- Native owned-game restoration reads uncompressed TES3 BSA headers/tables plus loose model,
  texture, animation and sound files. Sources are constrained to the selected owned Data Files
  and the three base-game BSA names. Paths, extensions, source/output hashes, byte ranges,
  replacement permission and byte/entry budgets are validated. No extractor or applier runs.
- The asset suite covers BSA/loose copying, hash-gated reconstruction, legacy filename decoding,
  case folding, idempotence, explicit replacement, unknown-source preservation, source/destination
  containment, disallowed files/fields and malformed/truncated/ambiguous BSA records.
- Authoring from the exact published Starwind compatibility 2.2.6 ZIP, SHA-256
  b796d213c4f8497907f77b1c2a09de33ae484f17023ced05d7f36e4ca73f3fd8, verifies 1,343 original/output
  fingerprints against the owned GOG installation: 1,037 copies and 306 NIF texture-name rewrites.
  Recipes contain byte ranges and changed length-prefixed texture names, without stock payload
  literals. Separate authoring tests reject incorrect occurrences, overlaps and unsafe BSA paths.
- A real-executable test first prepares plain Nexus Starwind and proves it has downloaded/installed
  no Fetcher compatibility package. It then explicitly accepts the experimental layer, removes
  that layer's PowerShell helper and selected overrides in derived layouts, and verifies all
  1,343 reconstructed assets. Switching back reproduces the original plain configuration; its
  raw music/UI files and the owned BSA hashes are unchanged. No game or server is launched.
  Evidence: release-assets/starwind-optional-acceptance-2/optional-starwind-report.json.
- The older release ZIP uses Windows backslashes in its member names. ZIP extraction now
  normalizes those before containment/duplicate checks, matching the other archive decoders.
  Forward/backslash traversal remains rejected. Explicit cleanup recipes support at most 512
  operations, accommodating the experimental UI exclusions; other extraction budgets remain.

This verifies one owned GOG installation and isolated profile preparation, not all game editions
or an experimental-server game join. Neither the optional layer nor any public launcher/server
configuration was published or changed. The complete experimental preset remains separate work.

## Native first installation and real local game join — 2026-10-22

- FetcherSetup.exe is a standalone x64 C++ setup GUI/headless entry point. It downloads the
  digest-backed Fetcher tools release and its declared client, verifies both inventories and
  required x64 executables/UI/resources, and uses the existing journaled installation/recovery.
  It requires neither WebView2 nor an existing game executable to open. The Microsoft runtime
  download remains an explicit link; automatic prerequisite installation is not claimed.
- tests/test_native_bootstrap.py passes 32 real-process operations: empty-folder setup, installed
  launcher profile preparation and process handoff, per-server identity persistence, character
  chooser arguments, cache reuse/corruption repair, running-client refusal (including the setup's
  extended-path alias), bad/missing/duplicate release metadata, corrupt archives/inventories,
  missing UI, wrong executable architecture, protected settings/setup and interrupted recovery.
  The recording client is a test-only executable, absent from packaged tools/client payloads.
- The live driver tests/test_bootstrap_game_live.py packages 510 explicit files from the local
  matching engine build, then installs via native setup into a new folder using loopback release
  metadata and verified archives. The installed launcher reviews/prepares the real 33,598,923-byte
  combined surf/truck/Studio bundle, saves the local Synth test identity and launches normally.
  The real client authenticated, selected Synth A, entered ToddTest and supplied the world actor
  roster to a matching unlisted local server on 25570. Computer-use inspection confirmed the
  character was in-world with the keyboard/stand. Owned ESM hashes and the read-only source test
  database are unchanged. The test closes only its own client/server; ports 25567/25568 are untouched.
  Evidence: release-assets/bga4/game-join-report.json and retained client/server logs.
- This exposed and fixed two actual first-launch failures: --mp-profile-root without the required
  account/character arguments, and absence of a base openmw.cfg anchor before following the
  generated --config directory. Existing configurations are preserved. A blank character now uses
  the in-game chooser; a named character receives isolated engine profiles and optional auto-entry.
- Native path aliases share installation locks/running-game checks. Windows filesystem exception
  messages with non-UTF-8 bytes are safely serialized in CLI/UI error responses instead of causing
  an exception while reporting the original failure.
- A deliberately deep real-engine fixture exceeded the engine's legacy path limit for a surf Lua
  file. A shorter dedicated installation succeeds. Native file installation supports extended paths;
  engine-wide long-path support remains open. See BOOTSTRAP.md for scope and repeatable tests.

These checks ran on the development Windows machine, with the owned GOG game and a local engine
build. They do not certify a clean Windows machine, every owned game edition, missing WebView2/
client prerequisites, the current public engine release, a public server join or final release
promotion. A standalone Fetcher SSO route would need staff registration; the selected UMO helper
now performs its own sign-in, as validated below. No public assets, tags or servers changed.

## External UMO helper — 2026-10-22

- The native launcher installs upstream UMO 0.11.2 from its fixed GitLab package URL, validates
  SHA-256 `18dcd4a33171b6ca5b8afd8286e4917906f4a108df52f60cb2e718914ec057e4`, and obtains the
  corresponding source archive for commit `2805e59ca4b627768fdd8e409a3d70a920225afc` with its own
  pinned hash. The AGPL adapter/license/source record are installed alongside the unchanged runtime.
- `tests/test_umo_helper.py` runs 18 native process operations for default provider selection,
  independent personal-key testing, malformed/duplicate/expired/mismatched NXM callbacks, private
  token-free callback results and actual Windows DPAPI encryption/decryption. It verifies the real
  user's NXM association is unchanged by automated tests.
- Optional upstream-runtime acceptance runs 23 launcher commands, compares all 3,848 upstream
  distribution files byte-for-byte, imports the actual modules, and exercises UMO's Premium and
  free-account downloader paths against a local archive fixture. The real native callback and UMO's
  legacy forwarding function both reach the waiting helper. An occupied private port exercises
  the fallback port. Tests never occupy the user's real UMO port. Mocked API responses exist only
  in the test harness; production has no fake authentication or alternate helper execution endpoint.
- Native preparation independently imports/verifies/extracts the returned archive; changed helper
  source is refused before execution. The registry fixture uses a process-private overridden hive
  to verify explicit registration, idempotence, exact prior-value restoration, unrelated-value/MOMW
  preservation and protection of an intervening handler owner.
- Live native WinHTTP installation from GitLab passes in `release-assets/umo-live-review`.
  The user authorised actual UMO SSO in Nexus. Its encrypted credential stayed in that isolated
  helper; no key was imported from the existing `_fetcher_umo` setup or exposed to the C++ API path.
- Live **free-account** Hackle-Lo Pipes v4 download and native profile preparation pass: mod 56939,
  file 1000064599, 4,276,766 bytes, SHA-256
  `a4211504a66587218c3ffd830a680ef33c0654110afbb503fb7d91b61e945970`.
  The first handoff exposed Chrome retaining the old UMO handler command. Supporting UMO's
  standard loopback forwarding port fixed the real test; automated legacy-forwarding coverage was
  added. The saved Windows NXM handler was restored exactly after the test, and all owned test
  helper/launcher processes exited. Evidence: `release-assets/umo-live-review/live-test-report.json`.
- No public server was joined or changed. Native launcher/tools promotion remains separate.
  Real Premium-account entitlement, clean Windows/WebView2 prerequisites and broader browser/
  simultaneous-manager acceptance remain open. Premium/free fixtures are not substitutes for
  those live environment checks.

## Fresh application install on PillowPC — 2026-10-22

- The repeatable inbox-PowerShell acceptance driver installs the candidate into a new directory
  on a second Windows device over authenticated SSH. PillowPC has Windows 11 Pro build 26100,
  an owned GOG game, WebView2 and the Visual C++ runtime, but had no Fetcher installation or
  usable system Python. See `WINDOWS-ACCEPTANCE.md` for payload, execution and evidence details.
- Native setup and source/inventory verification, first-run guidance, the actual desktop
  WebView2 page/native bridge, live public server retrieval, real HTTPS combined-content
  preparation, client runtime startup (`--version`), cached repair and UMO installation/import
  probe pass. Owned masters and NXM/MOMW handlers remain unchanged.
- OpenSSH uses service session 0, which cannot initialize WebView2 on this device. The UI check
  therefore runs as a temporary limited task in the existing signed-in desktop, then removes
  only its own task. The diagnostic writes structured results and closes without input automation.
- The retained real legacy updater installs the candidate and preserves personal markers;
  bad hashes and missing UI are rejected. All 32 bootstrap process operations pass remotely,
  including interrupted-install recovery and running-client/protected-file guards. These
  stdlib-only tests use the installed UMO runtime without installing a system Python.
- This certifies a fresh Fetcher directory on this device with existing prerequisites. In-world
  rendering/join on PillowPC, missing-prerequisite Windows installations, final production release
  URLs and coordinated public rollout remain separate. No public assets, tags or servers change.
