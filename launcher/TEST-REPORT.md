# Native launcher candidate validation — 2026-10-21

This is a local candidate, not a published release or public-server deployment.

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
