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
The four themes added in signed source `02c73c8` remain: Graphite, Obsidian, Deep Space and
Ember, with persistent selection and floating skooma rockets. Published source `5f500bb`
also avoids rehashing unchanged large package caches on every Play.

Owner documentation now includes a manual UMO-list authoring walkthrough, embedded Nexus
example, archive hashing, folder/load-order selection and master publication. Start with
[SERVER-OWNER-GUIDE.md](SERVER-OWNER-GUIDE.md) and [PROFILE-FORMAT.md](PROFILE-FORMAT.md).

## Experimental Fetcher server: deployed and joined

Port 25564 now advertises its full 103-package profile automatically. The normal local install
prepared it and joined the real VPS world with its existing character and unchanged game client.
TR 26.08a / TD 26.08 use the official mirrors; Mario/Zelda use the credited, pinned combined ZIP.
The explicit Starwind compatibility layer belongs only to this server profile. Vanilla's running
game process, authority and database were preserved. See [EXPERIMENTAL-VPS-ACCEPTANCE.md](EXPERIMENTAL-VPS-ACCEPTANCE.md)
for evidence and limits, including reuse of the audited archive cache rather than a new free-account
download of every Nexus file.

## Next usability priorities

1. **Disk space and cleanup: basic workflow implemented.** Updates → Storage shows actual-volume
   free space, cached/queued sizes, partial layout estimates and an explicit preview/removal flow
   for verified unused managed caches. Saved/current/previous profiles and rollback references
   protect their files; runtime writes check available space. See [STORAGE.md](STORAGE.md).
   Known legacy TR/TD retirement is implemented with a staged migration, verified files,
   configuration backups and restoration before deletion. The operator's installation passed
   cell entry with old paths absent, then removed about 2.39 GiB of files. See
   [LEGACY-TRTD-MIGRATION.md](LEGACY-TRTD-MIGRATION.md). Remaining: fuller client/backup/temporary
   peak estimates and migration support for other manually installed versions/custom files.
2. **Ping in both browsers: implemented.** Locally measured ICMP/TCP host RTT, sorting, bounded
   background probes, unavailable values and in-game refresh-generation/selection handling.
   The TCP fallback identifies its method and measures no HTTP processing. See [PING.md](PING.md).
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
   removal and richer cache management, after the browser priorities above.
5. **Content attribution display.** Pick an appropriate content-specific location, such as Studio
   Help or server-profile content details. Model/source/license records and packaged notices
   already exist under docs/content-attribution and in the content bundles. There is no general
   launcher model Credits tab.

The current public test does not need to wait for every advanced installer or UI improvement.
Keep the current supported manifest subset explicit and gather acceptance evidence as it expands.

## Experimental preset implementation notes

The full profile is deployed. Starwind compatibility is an explicit
server-profile layer and is never injected into another owner's ordinary Starwind setup.
The original two ModDB archives were excluded from the source audit; the new combined direct
package supplies their unchanged installed content instead. See LEGACY-RACES.md.

The archive audit, 37 native compatibility fixes and explicit Starwind owned-asset isolation
path are combined with updated TR/TD sources and the full plugin/groundcover order. Real
client/server game-join acceptance passes. Broader clean-download, long-session and device
coverage remains. Preserve the original installation and rollback sources; cache cleanup
retains their references and does not retire manual installations. See PRESET-AUTHORING.md and the acceptance report.

## Deferred owner-list authoring tools

After the manual JSON workflow is fully accepted, add an assistant for bringing existing
single-player mod lists online: inventory the installed setup, match exact archives and source
identities, collect missing Nexus links and choices, then export a curated pinned owner profile.
An `openmw.cfg` or server fingerprint cannot supply missing Nexus links by itself. This remains
future work; the existing Fetcher-specific cache audit is not a general exporter.
