# Installation storage and cache cleanup

Open **Updates → Storage** to see available space on the actual installation/cache volume.
**Estimate selected profile** shows queued downloads, raw extraction and estimated derived
layouts separately from cached files. Missing size information is reported explicitly.
Client updates, retained backups and some temporary installer/layout peaks need additional space.
These figures are planning estimates, not a guarantee that an installation will fit.

Owners may provide optional `downloadBytes` (up to 4 GiB) and `expandedBytes` (up to 8 GiB)
per package. They are advisory and never replace archive hashes, extraction limits or runtime
free-space checks. Cached receipts supply measured sizes when available. Download/extraction
writes and ordinary layout copies check available space; existing transaction recovery still
applies if a write fails.

A measured experimental profile trial totals about 4.9 GiB of downloads and 11.8 GiB of
extraction for a fresh cache. Its derived layouts add an estimated 5.2 GiB, with nine incomplete
intermediate estimates. The live experimental profile omits size hints for compatibility with
older launchers; fresh installs consequently show unknown sizes, while cached receipts provide
measured figures. Existing caches reduce the remaining requirements.

## Preview and remove unused cache

1. Choose **Preview unused cache**. This does not delete files.
2. Inspect the complete list and the logical size shown.
3. Close the game, then choose **Remove listed unused cache** if desired.

Only content-addressed archives, extracted packages and derived layouts inside
`_fetcher_launcher` can be candidates. The scan checks current, saved and previous profiles,
rollback records, legacy updater records, game profiles, configuration includes and configured
local mod paths. A saved reference protects the source archive/package as well as its layout.
Unreadable or unresolved references disable cleanup instead of guessing.

Before deletion the launcher checks that the preview is current, verifies every candidate's
full file hashes and checks saved references again. Unknown/extra files, modified content,
symbolic links and junctions prevent removal. Files are removed through checked cache paths;
the feature never recursively removes an arbitrary mod folder. A changed preview requires a
new scan. The game must be closed.

The displayed amount is logical file size. Hard links, compression and other filesystem
sharing mean the actual increase in available space may be smaller.

## Older Tamriel Rebuilt / Tamriel Data folders

The managed experimental configuration selects fresh TR/TD directories. Ordinary unused-cache
cleanup retains arbitrary manual/UMO folders. Known legacy Fetcher TR/TD paths are now migrated
and their verified obsolete folders removed automatically after the current experimental profile
is prepared. No separate migration steps or buttons are required. See [LEGACY-TRTD-MIGRATION.md](LEGACY-TRTD-MIGRATION.md).
Unknown or modified files are protected. Saved characters, unrelated configurations and ordinary
rollback data are retained. The operator's normal installation passed migration and game startup;
its old and retired TR/TD folders were then removed.

## Validation and upgrades

Native tests cover protected references, include cycles, path aliases, stale previews, full-hash
corruption detection, extra personal files, running-game refusal and symlink containment.
The WebView2 smoke test checks the Storage controls and actual-volume display. A read-only
preview on the normal installation found 21 candidates (about 5.4 GiB logical), retaining
216 referenced entries; none were removed. An empty-cache estimate used a manually imported size-hint candidate
without downloading game content or requesting Nexus authentication.

Launcher updates can now install verified tools even when the selected server profile cannot
be read; stale requirements are not prepared. Very old launchers with the former 256 KiB
manifest limit may need the current Setup executable or selection of Vanilla for their first
upgrade, because the fallback cannot repair code that has not been installed yet.
