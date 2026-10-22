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
