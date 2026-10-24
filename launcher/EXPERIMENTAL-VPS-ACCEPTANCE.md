# Experimental Fetcher server acceptance — 2026-10-24

UDP 25564 advertises its full owner profile automatically. The normal local installation
selected the server, prepared its isolated configuration and joined the real VPS world with
the existing Mario character and unchanged published game executable.

## Deployed requirements

- [presets/fetcher-simulation.json](presets/fetcher-simulation.json), version `2026.10.24.2`:
  103 packages, 136 configured content files and 12 groundcover plugins.
- Active owner file: `/srv/fetcher-simulator/launcher-profile.json`.
- Advertised SHA-256: `3e6426d011b3fa93cf8d27c40c9d9e072058ccdfe9e82203a3c8a24ad23bc4cc`.
  Revision 2 adds measured size hints only. The world-entry evidence below used revision 1
  (`6e22a6ea38013d1ae871098fe22fb54cfef52a1fee203fa9a358fe7eed156e82`);
  package hashes, recipes and content order are identical. The metadata restart preserved
  all 53 characters, with both health checks passing and Vanilla's game process unchanged.
- TR 26.08a and TD 26.08 Vanilla: actual official-mirror downloads, native hash verification,
  extraction and preparation pass. Upstream executable/JAR helpers are excluded, never run.
- Mario/Zelda: unchanged plugins/assets and original notices in the pinned combined GitHub ZIP.
  See [LEGACY-RACES.md](LEGACY-RACES.md). Starwind compatibility is explicit to this profile.
- Published launcher/tools source: signed `5f500bb78ecb951b837c44b3b139a9bc5c1124b6`.
  All five public asset hashes match the reviewed package.
- VPS source includes the capacity update and signed engine fixes `bdd3ed7807` and `2fdbcfa5a7`,
  published as source tag `Fetcher-Server-Profiles-4`. No ARM GitHub asset was required.

## Evidence

New Tamriel Data exposed missing localization and inactive LOAD-script settings storage in
the headless runtime. The server now initializes localization and supplies empty, read-only
settings for content defaults. The authority builder copies effective localization YAML
byte-for-byte with VFS priority preserved. Its five tests, three Lua localization tests and
thirteen content-registry tests pass; Windows client/test and VPS server builds pass.

The initial reference fixture omitted canonical fallbacks. Record diagnostics found all
69,703 shared records identical, with 480 extra client GMSTs. Adding the canonical fallback
configuration fixed parity. The actual server and published client agree on resolved fingerprint
`01f835d221f4dea1de337f8821731e86fb4359ba843d3e130bce6d416bdffe3d`,
137 runtime content files including the builtin, and 330 Lua scripts. Validation was not bypassed.
Temporary record probes are excluded from the deployed authority.

A private VPS instance/database clone passed authentication, server-Lua activation and returning
character/full-state restoration before cutover. A private SSH datagram bridge bypassed an
inaccessible spare test port. Temporary firewall rules were removed and the private server and
bridge were stopped.

Cutover ran with no connected players. The previous authority, binaries, configuration,
consistent SQLite backup and Lua storage are retained privately. All 53 characters remain;
SQLite integrity checks pass. Vanilla's original game PID, authority, configuration and database
were preserved, and its health endpoint remains healthy.

The normal launcher/tools update preserved 331 checked game/configuration/profile files.
Actual native launcher entry used automatic discovery and the generated configuration. Fresh
logs confirm accepted handshake, server-Lua activation and restored-player synchronization.
The game executable, main `openmw.cfg` and main `settings.cfg` retained their hashes. The test
client was closed; both servers subsequently reported zero players. The first Python CLI
harness waited on an output pipe inherited by the game; final validation checked the completed
native result, fresh log and matching running process before closing it. Future harness commands
use file output.

## Performance and limits

Complete cached preparation improved from 629.4 seconds to a 53.3-second metadata upgrade and
24.83-second warm reopen on this machine. Changed files are rehashed; Repair verifies fully.
Native cache/corruption/containment tests, bounded full-profile HTTP tests and 47 master tests pass.

The full preset reused audited caches. Free-account UMO/NXM was tested separately with Hackle-Lo;
this was not a new network download trial of every Nexus archive. Broader device, long-session,
music/gameplay and worldspace exploration remain public-test work. The legacy UMO file is an older
installation snapshot; the advertised native profile is the current experimental requirement list.

Old TR/TD roots are excluded from the active generated configuration. Their folders remain referenced by the original
configuration/local setups. Updates → Storage now estimates space and previews/removes verified,
unreferenced managed caches; manually installed TR/TD folders are retained. See [STORAGE.md](STORAGE.md). Private logs, database
copies and licensed game assets remain outside the repository/release.
