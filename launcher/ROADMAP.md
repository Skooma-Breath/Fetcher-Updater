# Fetcher launcher roadmap

## Published and working

The public native launcher includes automatic master-hosted server mod profiles, a server
browser with search/favourites/direct connect, isolated per-server configurations and load
orders, verified client/tools/content updates, cached repair, journaled recovery and rollback.
Players select a server and press Play; an owner JSON/URL paste is no longer needed.

Pinned embedded UMO lists, ZIP/7z/RAR extraction, contained layout actions, supported XML FOMOD
selections and hash-gated native compatibility patches are implemented. UMO is the normal
external Nexus sign-in/download helper. Real UMO authorisation and free-account Hackle-Lo
browser/NXM download preparation pass; personal API keys and exact-file manual import remain.

The Vanilla VPS runs the coordinated surf/truck/Studio content and advertises its owner
profile. Its 600-song Studio MIDI library is delivered over the game connection. A matching
Windows client is published; the VPS builds its own ARM server, without an ARM GitHub asset.
Normal local installation and real Vanilla world/Studio entry pass. Second-device setup and
an isolated world join pass on PillowPC. See VANILLA-VPS-ACCEPTANCE.md and WINDOWS-ACCEPTANCE.md.

Startup checks for updates without installing them. Successful changes to the launcher's own
executable/interface restart it after a verified readiness handoff; content/client-only updates
do not. Older builds need one manual reopen on their first upgrade. See AUTO-UPDATES.md.
The published theme release is signed source `02c73c8`: Graphite, Obsidian, Deep Space and
Ember, with persistent selection and floating skooma rockets.

Owner documentation now includes a manual UMO-list authoring walkthrough, embedded Nexus
example, archive hashing, folder/load-order selection and master publication. Start with
[SERVER-OWNER-GUIDE.md](SERVER-OWNER-GUIDE.md) and [PROFILE-FORMAT.md](PROFILE-FORMAT.md).

## Current priority: experimental Fetcher server

1. Finish the full port-25564 server profile and a fresh launcher-based join using a **free**
   Nexus account. Reconcile the complete installed layout, required compatibility layers,
   plugin/groundcover order and matching server authority before advertising it automatically.
   This server remains separate from Vanilla and from other owners' ordinary Starwind installs.
2. Keep Mario Race and the Zelda beta through one small, hash-pinned GitHub ZIP, retaining
   source credits and the unchanged plugins. The original ModDB archive entries are replaced
   by a direct package; do not require users to find the two original RARs manually.
3. Use the official TR/TD mirrors for the experimental candidate. The operator approved updating
   from TR 25.08.12 / TD 25.05 to TR 26.08a / TD 26.08. Validate the new layouts, dependencies,
   compatibility and authority together before the live cutover. Download speed still depends
   on the mirror; the purpose is to avoid the Nexus free-download approval steps for these files.

## Next usability priorities

1. **Disk space.** Show available space on the actual installation/cache volume, queued download
   size and estimated extraction/derived-layout needs before preparation. Distinguish cached files,
   unknown sizes and estimates; catch insufficient space before changing the active configuration.
   After the new setup passes, offer explicit cleanup of obsolete TR/TD folders and unused cache
   versions. Check current and saved configuration references first; preserve active installs and
   retained rollback versions. Never merge a new TR release over the old extracted folder.
2. **Ping in both browsers.** Add client-measured latency to the launcher and the in-game server
   browser, with bounded background probes, refresh/sorting and a clear unavailable/timeout state.
   A master's HTTP response time is not a player's latency to the game server.
3. **Browser polish.** Improve protocol/build compatibility display, connection diagnostics,
   independently authored server lists and large-list presentation.

## Later acceptance and broader mod-manager work

1. **Nexus and Windows coverage.** Expand free-account/browser/NXM and missing-prerequisite trials.
   Premium-account entitlement testing is deferred until a willing Premium tester is available;
   the owner does not need to purchase Premium or wait for it to launch the experimental server.
   Native bootstrap, legacy upgrade and second-device acceptance already pass.
2. **Failure and soak testing.** Collect small-group public-test results on the current payload.
   Broaden device/storage-failure acceptance and long-running download/update trials.
   Interrupted-process recovery, retained backups and native restore are already implemented.
3. **Advanced mod installer support.** Add FOMOD installed-file/plugin/game/manager/version
   dependencies and an interactive option wizard. Encrypted/multipart archives, unsupported
   codecs, nested/scripted installers, plugin cleaning and third-party tool bootstrap remain
   unsupported; decide which are worth supporting without executing arbitrary server commands.
4. **Mod management UI.** Add dependency/conflict explanations, optional-mod presets, package
   removal and cache management, after the disk-space/browser priorities above.
5. **Content attribution display.** Pick an appropriate content-specific location, such as Studio
   Help or server-profile content details. Model/source/license records and packaged notices
   already exist under docs/content-attribution and in the content bundles. There is no general
   launcher model Credits tab.

The current public test does not need to wait for every advanced installer or UI improvement.
Keep the current supported manifest subset explicit and gather acceptance evidence as it expands.

## Experimental preset implementation notes

This is now the first deployment priority. Starwind compatibility is an explicit
server-profile layer and is never injected into another owner's ordinary Starwind setup.
The original two ModDB archives were excluded from the source audit; the new combined direct
package supplies their unchanged installed content instead. See LEGACY-RACES.md.

All 99 original archives for the remaining 90 mods, 37 native compatibility fixes and the
explicit Starwind owned-asset isolation path pass their respective tests. Finish remaining
installed-layout/version differences (including the separately installed shader version),
plugin/groundcover order, and matching full-preset client/server game-join acceptance before
publishing that complete preset. The TR/TD update needs new acceptance; the old source audit
does not validate those replacement versions. Component checks alone do not establish full parity.
See PRESET-AUTHORING.md. Preserve the original installation and rollback sources.

## Deferred owner-list authoring tools

After the manual JSON workflow is fully accepted, add an assistant for bringing existing
single-player mod lists online: inventory the installed setup, match exact archives and source
identities, collect missing Nexus links and choices, then export a curated pinned owner profile.
An `openmw.cfg` or server fingerprint cannot supply missing Nexus links by itself. This remains
future work; the existing Fetcher-specific cache audit is not a general exporter.
