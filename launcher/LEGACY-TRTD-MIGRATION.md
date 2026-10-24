# Retiring an older TR / Tamriel Data installation

New native installations receive the current server-pinned content directly. Existing Fetcher
installations may also retain TD 25.05 and TR 25.08.12 in the older UMO folders. Updates now
provides a separate migration for these known versions; ordinary unused-cache removal still
does not delete manual mod folders.

Select the experimental Fetcher server, close this installation's clients/local servers and open
**Updates → Older TR / Tamriel Data installation**:

1. **Prepare / preview migration** prepares the current profile, checks replacement masters and
   lists supported old folders/configurations. It does not remove old files.
2. **Migrate configurations** verifies the old file contents, backs up affected configurations,
   replaces their three TR/TD data paths and moves the old folders into the managed retirement
   area. Content order, fallbacks and unrelated settings remain. Test the game while those old
   paths are absent.
3. Close the game and choose **Remove retired TR / TD** to verify the retired files again and
   reclaim their space. Before removal begins, **Restore migration** restores the original
   configuration bytes and moves the folders back.

An interrupted staging operation can be restored. Once removal begins it can be resumed;
restore is disabled because some old files may already be gone. Original configuration backups
and the migration journal remain under `_fetcher_launcher/migrations/trtd-v1`. They document
the previous setup; after final removal, restoring an old configuration alone cannot restore
the deleted game content. Original cached archives are outside this cleanup.

## Boundaries

The launcher ships an inventory of 47,039 original file paths/sizes/hashes derived from the
audited, pinned TD/TR archives. It is embedded as inert JSON in the existing interface bundle,
preserving older launchers' update allowlists, and has a compiled-in SHA-256. It contains
metadata, not game assets. Unknown/modified files, links, stale previews and edited configurations
block the operation. Only the two fixed legacy Fetcher folders and current known replacement
versions are supported; server JSON cannot authorize deletion paths or define migrations.

The scan covers installation configurations, saved profiles and explicit configuration includes.
An included external configuration referencing the old content blocks automatic mutation.
Unrelated external installations, historical backups and manually rerun legacy installers are
outside this workflow. Legacy installers may reinstall their historical versions; use the native
server profile for ongoing updates. Running clients and servers from the installation must close
before migration; independent fixtures are left running.

## Normal installation acceptance — 2026-10-24

The operator's main configuration was migrated, preserving its content/fallback lists and
personal settings. Both old directory paths were absent during a real single-player load into
Balmora, Guild of Mages, using scratch configuration/userdata. The client was then closed and
47,039 verified files totalling 2,569,307,256 bytes (about 2.39 GiB) were removed. The retired
copies were also gone; the original configuration backup remains. Drive free space increased
by about 2.5 GiB during the test, including any concurrent background filesystem activity.

Seven launcher CTest suites cover the migration, including same-size edits, unknown files,
external includes, links, stale previews, restoration and protected settings. The existing
procedural-synth fixture uses its own configuration and was not restarted.
