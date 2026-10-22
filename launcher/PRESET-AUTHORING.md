# Fetcher Simulation native preset authoring

The new preset keeps 90 mods / 99 source archives. The operator requested that Mario Race and
The Legend of Zelda beta mod be omitted on 2026-10-22 because their original ModDB archives are
unavailable locally. This changes only the new preset plan. The original 92-mod legacy JSON and
installed game are unchanged.

Developer authoring uses Python and a local 7-Zip executable to inventory original archives;
players do not need either tool for native profile preparation. These scripts read the UMO
JSON/SQLite data formats independently, without importing UMO implementation code or executing
its actions. API credentials and cached temporary download URLs are not copied into reports.

## Bind exact sources

```powershell
python scripts/Audit-FetcherUmoPreset.py `
  --umo-list release-root/fetcher-simulator-umo.json `
  --install-root C:\serena_workspaces_directory\fetcher-simulator `
  --curation launcher/presets/fetcher-simulation-curation.json `
  --output release-assets/full-preset-curated-audit.json
```

The audit opens cache.db read-only, binds each stored download name/extract_to identity to one
archive/version, checks size/hash metadata and pins its exact SHA-256. It never chooses a newer
filename. Nexus file identities must agree with list pins and cached mod metadata, or explicit
operator curation backed by source verification. Source archives with programs/scripts require
exact reviewed removal actions. The curation excludes two unused helpers, not ordinary game Lua.

Nexus's authenticated MD5-search responses on 2026-10-22 corroborated the FaceTheMoon and
Daggerfall Skeleton Sounds file IDs, plus the Doors of Oblivion 1.4 archive whose local SHA record
was stale. The curation records their public source identities and stronger installation SHA-256
pins. Other source bindings use the existing installed metadata; this is not a claim that every
Nexus file has been freshly downloaded or that every archived file remains publicly available.

## Prepare originals in an isolated fixture

```powershell
python tests/test_umo_cache_live.py --exe <candidate.exe> `
  --audit release-assets/full-preset-curated-audit.json `
  --root <new-isolated-fixture> --game-data <owned-Morrowind-Data-Files>
```

The driver hardlinks the original compressed archives into the new fixture, verifies their hashes,
then uses native Review/Accept/Prepare. It writes a vanilla-master-only source test profile, not
a full game/server preset. The 99-archive acceptance passes with 5,065,202,678 source bytes; its
report is release-assets/umo-source-3/source-layout-report.json. Resume is explicit and only
accepts the exact test manifest previously created by this driver.

Large internal inventories support up to 128 MiB and 100,000 entries. External profile JSON stays
limited to 8 MiB. Internal cache paths use Windows extended-length paths with native separators;
generated game config paths remain ordinary paths. No machine registry setting is changed.

## Port owned compatibility recipes

```powershell
python scripts/Build-FetcherNativeCompatibility.py `
  --audit release-assets/full-preset-curated-audit.json --cache-root <source-fixture> `
  --installed-root C:\serena_workspaces_directory\fetcher-simulator `
  --legacy-patches release-root/fetcher-mod-compatibility-patches.json `
  --additional-patches launcher/presets/fetcher-additional-compatibility.json `
  --bardcraft-archive <exact-cached-published-patch.zip> `
  --bardcraft-sha256 8af146edd0d8330f6f21e111ec17d434ac2f9cc815c6f6a8c794b8fc83b1f1f3 `
  --output release-assets/native-compatibility-recipes.json
python tests/test_native_compatibility_live.py --exe <candidate.exe> `
  --root <source-fixture> --recipes release-assets/native-compatibility-recipes.json
```

The converter verifies raw source bytes and exact output against the installed authoritative
setup. Renamed cleaned plugins can derive from a uniquely matching original; new files require
explicit creation and an existing/planned prerequisite. Unknown source/output/prerequisites are
reported rather than force-applied. Client/engine resources are excluded from server mod recipes.

Native application of all 37 fixes passes: 13 existing Fetcher compatibility files, six UI/reconnect
fixes from the legacy Hookshot/ZHI/follower/TakeControl pipeline, and 18 files from the published
Bardcraft multiplayer patch 2.0.24. The native implementation does not invoke
either PowerShell applier. Evidence: release-assets/umo-source-3/compatibility-report.json.

## Reconcile the complete preset

```powershell
python scripts/Audit-FetcherInstalledOverrides.py `
  --audit release-assets/full-preset-curated-audit.json --cache-root <source-fixture> `
  --install-root C:\serena_workspaces_directory\fetcher-simulator `
  --output release-assets/full-preset-installed-overrides.json
```

This compares the merged VFS source inventory with installed mod bytes and lists changed, absent
and added files. It writes no payload bytes and makes no installation changes. Review those
differences before adding the remaining compatibility/cleaned-plugin recipes and Starwind's
owned-game asset restoration. Personal MIDI files and updater receipts are not automatically
promoted into a general preset. The audit found 32 changed, 176 absent and 82 added files among
94,508 VFS source paths. All 30 changed gameplay/compatibility files are covered by the 37-fix
recipe (seven outputs are new files). The two remaining changed files are Rafael shader 2.0e
updates over the cached 2.0c source, not multiplayer fixes. Most added files are old backups.
The complete plugin/groundcover order, matching server and
native-client release still need end-to-end acceptance before publication. Source-layout and
37-file compatibility acceptance alone do not certify the complete historical preset.
