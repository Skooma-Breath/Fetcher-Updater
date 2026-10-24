# Mario and Zelda direct bundle

The operator chose one GitHub ZIP instead of dropping the two legacy ModDB races from the
experimental server. The two original RARs were unavailable in the local archive cache;
this repack uses their installed files. It does not include the separate Nexus `Link_(Fixed)` mod.

Package definition: [presets/fetcher-legacy-races.json](presets/fetcher-legacy-races.json).
Original sources/credits and licence labels: [legacy-races.txt](../docs/content-attribution/legacy-races.txt).
This is a repack, not a new authorship or licensing claim. The source pages label both mods
Proprietary; the attribution record does not create a new licence for them.

Archive structure:

```text
Mario/Data Files/                 # Mario.ESP and all installed assets/readme
Zelda/Data Files/                 # The Legend of Zelda.ESP and all installed assets
FetcherLegacyRaces/ATTRIBUTION.txt
FetcherLegacyRaces/FILES.json
```

The deterministic ZIP is 414,192 bytes, contains 57 unchanged original files plus attribution
and an inventory, and has SHA-256
`65664591649f699eb84abbb4ca0f3650961f7ad0a7f1cfa198d887bb86e3c189`.
The native profile uses both `dataDirs` in the package definition. These remain separate roots
so same-named assets do not overwrite one another during extraction.

Both legacy UMO list entries reference the same direct ZIP/hash and select their own data root.
Their original ModDB page URLs remain for source identification. The legacy manual-download
helper skips ModDB prompts when the entry has an explicit direct source. Native preparation
caches the shared archive once by SHA-256. A full preset can use two logical definitions selecting
one root apiece to retain their original positions among other mods; both still share one ZIP/cache.

Rebuild from the named, known-good installation:

```powershell
python scripts/Build-FetcherLegacyRaceBundle.py `
    --install-root C:\serena_workspaces_directory\fetcher-simulator `
    --output-dir release-assets\legacy-races-rebuild
```

Compare the generated `package.json` hash before publishing; different installed source bytes
produce a different hash. The builder changes neither the installation nor the source plugins.

Validation on 2026-10-23:

- Independent rebuild produces the same ZIP hash.
- All packaged source files match the local installation; both plugin hashes also match the live
  experimental server authority.
- The native launcher downloads, validates and prepares both races with owned vanilla masters in
  an isolated fixture. Both plugins appear in the generated load order and the personal config
  marker is unchanged. This validation does not launch a game.

This small bundle removes the two manual ModDB steps. It does not itself complete the full
experimental profile, update server authority, or advertise that profile through the master.

## Official TR/TD replacement

[presets/fetcher-simulation-official-tr.json](presets/fetcher-simulation-official-tr.json) records
the direct Google Drive downloads linked from the official TR and Tamriel Data pages, checked
on 2026-10-23. The operator approved updating the candidate to TR **26.08a** and Tamriel Data
**26.08 Vanilla**. Their hashes pin exact bytes, not a floating latest version.

TR's data roots are now `00 Core/Data Files` and `01 Faction Integration/Data Files`.
TD uses `00 Data Files`; its optional normal maps are not selected. The two bundled desktop
tools are explicitly removed from the derived layout; the launcher does not run them.
These files are package fragments. The full [experimental profile](presets/fetcher-simulation.json)
and matching live authority completed their coordinated cutover on 2026-10-24; see
[EXPERIMENTAL-VPS-ACCEPTANCE.md](EXPERIMENTAL-VPS-ACCEPTANCE.md).

The real native WinHTTP download/hash/extraction/preparation test passes for both official
archives without Nexus: TR is 68,228,280 bytes and TD is 885,319,384 bytes. The test uses a fresh
profile and confirms all eight required vanilla/TD/TR content entries and personal-config
preservation. It does not launch a game or establish full-preset/server compatibility by itself.
