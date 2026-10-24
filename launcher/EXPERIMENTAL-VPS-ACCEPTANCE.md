# Experimental Fetcher server acceptance — 2026-10-24

UDP 25564 advertises its full owner profile automatically. The normal local installation
selected the server, prepared its isolated configuration and joined the real VPS world with
the existing Mario character and unchanged published game executable.

## Deployed requirements

- [presets/fetcher-simulation.json](presets/fetcher-simulation.json), version `2026.10.24.1`:
  103 packages, 136 configured content files and 12 groundcover plugins.
- Active owner file: `/srv/fetcher-simulator/launcher-profile.json`.
- Advertised SHA-256: `6e22a6ea38013d1ae871098fe22fb54cfef52a1fee203fa9a358fe7eed156e82`.
  Advisory size fields are excluded from the live profile so earlier launchers can still read it.
  A size-hint-only revision was reverted after finding older discovery validators reject those
  optional fields. The corrected launcher has explicit discovery regression coverage.
  Package hashes, recipes, content order and all 53 characters were preserved; both health
  checks pass and Vanilla's game process is unchanged.
- TR 26.08a and TD 26.08 Vanilla: actual official-mirror downloads, native hash verification,
  extraction and preparation pass. Upstream executable/JAR helpers are excluded, never run.
- Mario/Zelda: unchanged plugins/assets and original notices in the pinned combined GitHub ZIP.
  See [LEGACY-RACES.md](LEGACY-RACES.md). Starwind compatibility is explicit to this profile.
- Initial world-entry validation used signed launcher/tools `5f500bb78ecb951b837c44b3b139a9bc5c1124b6`.
  Subsequent Storage releases retain the same content preparation and add space/cleanup controls.
  Published tools inventories identify their exact signed source; all five release asset hashes
  are checked against the reviewed package.
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

Old TR/TD roots are excluded from the active generated configuration. The operator's original
configuration has now been migrated too, retaining its content order and settings. It passed a
scratch-data load into Balmora with old paths absent; the 2.39 GiB of verified retired files
were then removed. See [LEGACY-TRTD-MIGRATION.md](LEGACY-TRTD-MIGRATION.md).
The ping client, signed source `16e8f9bc3e`, subsequently joined this VPS with the same accepted
fingerprint, restored Mario and activated the server Lua package. The test client and its owned
crash-monitor companion were closed; both VPS health endpoints report healthy and zero players.
The initial process enumerator counted that companion as a second game; endpoint-filtered
validation of fresh logs confirmed the actual single session. The normal client update preserved
330 checked personal/configuration files. The independent local synth fixture and both VPS game
processes were not restarted for this slice.
Private logs, database
copies and licensed game assets remain outside the repository/release.
