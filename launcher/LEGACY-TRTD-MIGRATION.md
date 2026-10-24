# Automatic legacy TR / Tamriel Data cleanup

Existing testers use the normal **Install updates** or **Play** flow for the experimental
server. Once its current replacement content is prepared and verified, the launcher updates
the old data paths and deletes the verified obsolete folders automatically. There are no
migration, preview, restore or final-removal buttons.

After an older launcher updates and restarts, startup also finishes cleanup when the selected
experimental profile is fully cached/current and the game is closed. It does not download the
experimental mod set for Vanilla players or unrelated profiles. Fresh installs have no legacy
folders to migrate. Completed migrations are no-ops on later launches.

Original configuration files are backed up under `_fetcher_launcher/migrations/trtd-v1`.
Their plugin order, fallbacks and unrelated settings are retained. The old files are verified,
internally moved aside and removed in the same operation, without a user testing step.
Interrupted staging is recovered before retrying; interrupted deletion resumes automatically.
The original archives remain outside this cleanup. Restoring a historical configuration alone
does not restore deleted old content.

Unknown or modified files and external configuration dependencies are protected. If cleanup
cannot safely finish, the launcher keeps those files, records a short status message and still
allows the prepared current profile to be used. It retries during later preparation. A running
game defers cleanup; independent local fixtures remain untouched.

## Scope and integrity

Only TD 25.05 / TR 25.08.12 in the two fixed legacy Fetcher directories are eligible, with the
known TD 26.08 / TR 26.08a replacements. Server manifests cannot provide deletion paths or
migration instructions. Other profiles, manual mods, saves, account keys and characters are
outside this migration.

The launcher carries 47,039 original file paths/sizes/hashes from the audited pinned archives.
This metadata is inert JSON in the existing interface payload, preserving older updater path
allowlists. A compiled-in digest verifies canonical LF bytes across Windows/source checkouts.
No original game assets are embedded in the interface. All legacy content is checked before
removal; extra files, changed data and links prevent deletion.

## Validation

The operator's main configuration and existing world entry passed migration acceptance before
the old extracted folders were removed: 2,569,307,256 payload bytes (about 2.39 GiB). That
installation is already clean and is not repopulated for testing.

Automatic-flow acceptance recreates those same legacy files in an isolated prepared fixture.
An ordinary prepare migrates and removes them without additional commands or buttons. A
custom-file trial retains the old content while still preparing the current profile. Repeated
preparation leaves the migrated configuration unchanged. Unit tests cover automatic recovery
of staging, staged and partially removed journals, as well as hashes, includes and preservation.
