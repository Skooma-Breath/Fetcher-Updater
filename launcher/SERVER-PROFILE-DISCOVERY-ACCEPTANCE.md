# Automatic server profile discovery — 2026-10-23

Dedicated-server/master source: signed engine commit `0f1aa3c5c5`.
Vanilla now reads `/srv/openmw-vanilla-25569/launcher-profile.json` through its
`[master] launcher_profile` setting and advertises the requirements through the live HTTPS master.
Its manifest SHA-256 is `9b9180a7319d3cf4af378b3b4cbccb3185cff6f2454d45a3329afe58af56092d`.
The ARM runtime SHA-256 is `3d61444a4767a5ce45ce677d8882dd6eca26a1aa2e6d84346895f01d7214157d`;
it was built on the VPS and no ARM GitHub asset is required.

Validation:

- 46 master tests pass, including profile lifetime/digests, legacy registrations, duplicate JSON
  keys, nesting, unsupported instructions, UTF-8 byte limits, POST limits and registry capacity.
- All five native CTest suites pass. Discovery tests cover endpoint binding, derived master URLs,
  refused redirects, pinned digests, external-reference rejection, changed/removed advertisements,
  persisted profiles and preservation of existing endpoint settings/data directories.
- 57 real executable manifest operations pass, including manual/automatic Nexus paths, credential
  isolation, UMO imports, rollback and hostile payloads. Native package integration also passes.
- A fresh install root uses the real public master and `selectServer`, then `prepare`, with no
  review/import action or supplied manifest URL. It downloads the real 33,598,923-byte bundle,
  verifies SHA-256 `8ad68f4741d08560cb5e6cb7887fc834ca7ad6408847fcf182499e638a822fdb`,
  extracts 354 files and generates all ten expected plugins. The original configuration is intact.
- The master Docker image passes a separate registration/discovery/profile HTTP test before its
  live replacement. Original container and nginx configuration are retained for rollback.
- Vanilla is restarted only after two zero-player checks. Its eleven Studio storage records are
  unchanged and SQLite quick_check passes. The experimental process keeps its previous executable;
  its scripts/configuration and the local Synth fixtures are not replaced.

Reproduce fresh public discovery with `tests/test_server_discovery_live.py --exe ... --root ...
--game-data ... --host 163.192.106.225 --port 25569`. Use a new, disposable root. It does not launch
a game. This slice verifies discovery/preparation rather than another full gameplay join.

Private VPS backups: `/srv/fetcher-release-backups/20261023-profile-discovery`. Local receipts and
release inputs: ignored `release-assets/server-profile-discovery-20261023`. Never publish private
operator snapshots or restore their older database over newer player progress.

Security and owner setup are documented in PROFILE-FORMAT.md. This test uses direct GitHub content;
Nexus can still require its own account/download approval. The master distributes declarative
requirements and verifies registration source addresses; it does not certify a server operator
or make arbitrary game assets intrinsically safe.
