# Legacy-to-native launcher upgrade

The native launcher uses the existing `fetcher-tester-tools` release and `fetcher-tester-tools.zip`
asset, with the same installed `FetcherLauncher.exe` and `ui/` filenames. The old launcher checks
the tools release tag against the installed tools manifest's sourceCommit. Its normal Update action
uses the existing PowerShell updater/bootstrap to download and validate the replacement tools.

The running legacy GUI stages its executable and UI in LocalAppData. Updating replaces the files in
the game installation while the staged old window continues to run. The player must close it and
reopen the installed launcher to see the new interface. Detection alone does not install an update.
During rollout, tell players: **Update, wait for success, then close and reopen the launcher.**

## Repeatable pre-release check

Build a candidate tools ZIP, then run:

```powershell
.\scripts\Test-FetcherLauncherUpgrade.ps1 `
  -CandidateArchivePath .\release-assets\fomod-candidate-tools\fetcher-tester-tools.zip `
  -OutputDir .\release-assets\launcher-upgrade-acceptance `
  -Gui
```

Use a new output directory each time. The script fetches the current public tools ZIP read-only,
verifies GitHub's SHA-256 and runs an isolated local GitHub release fixture. It does not publish,
touch a user's game directory or launch OpenMW. GUI mode verifies real old/new staged processes;
without it, the test exercises the legacy executable's updater CLI. LeavePreview is an optional
GUI-only switch for inspection. Outputs are retained for review; no existing test root is deleted.

After the public tag has moved to the native launcher, use a retained trusted legacy ZIP instead:

```powershell
.\scripts\Test-FetcherLauncherUpgrade.ps1 `
  -CandidateArchivePath <candidate-tools.zip> -OutputDir <new-repository-test-directory> `
  -LegacyArchivePath <retained-legacy-tools.zip> -LegacySha256 <recorded-sha256> -Gui
```

The baseline must contain the actual legacy launcher. Run this harness only with trusted release
packages: it executes their launcher and PowerShell scripts. The test checks every manifest record
and the pinned legacy outer digest before executing anything from the baseline.

## What the test verifies

- The real legacy executable and pristine public updater detect a changed tools commit without
  downloading the new archive during the status check.
- The pristine legacy bootstrap downloads the candidate, verifies its digest and installs the exact
  executable, UI, assets, notices and scripts from the candidate's inventory.
- Personal configuration, settings, a save, mod data, a synthetic key marker and an unrelated UI file
  remain byte-identical. The new native backend starts in Existing installation mode.
- A real staged legacy GUI can remain open during installation. Reopening starts the new executable
  with the matching new staged UI, including the server browser and floating rockets.
- A bad outer hash and a hash-valid archive missing a required UI file fail before replacing the old
  installed files. These are validation-failure checks, not power-loss or mid-copy rollback proof.

The harness installs a small test-only entry-point adapter to supply documented loopback endpoint
and content/client-skip switches. It retains and invokes the unmodified public updater script; it
does not patch the legacy executable or bootstrap. Thus this verifies tools migration independently
of a client/mod rollout. No real game join, third-party mod migration, clean Windows bootstrap or
full compatibility pass is implied. Only test child processes receive a Windows PowerShell inbox
module path, preventing PowerShell 7 development modules from shadowing PS5.1 modules. User/machine
environment settings stay unchanged.

## Native interrupted updates and rollback

From version 0.6, native updates hash/verify the complete inventory and back up every affected
existing file before writing a transaction journal. Individual replacements use a temporary file
inside the managed backup directory and an atomic Windows move. A journal without a completion
marker is restored on restart. Recovery verifies backup hashes and refuses to replace a target
whose bytes match neither the old nor planned new version. If recovery itself was interrupted,
it resumes idempotently. Native GUI startup restarts the restored executable so its staged UI
matches it. The old PowerShell-to-native migration does not gain this new transaction mechanism.

Updates → Restore a previous update lists completed native tools/client snapshots. It preserves
user configuration/data and refuses later edits to update-owned files. Close the game first and
reopen after restoring launcher files. A failed/corrupt snapshot remains available for inspection;
the launcher does not force an overwrite. Old pre-0.6 backup formats are retained but not offered
as transactional snapshots. A client/tools rollback does not restore a project's saves, database,
server configuration or mod-manifest version (use the separate manifest rollback for that).

CTest runs actual child processes killed at file-write boundaries for both client and tools
updates, then verifies startup recovery, idempotence, edit/corruption guards and manual rollback.
For a real executable/UI restart check, use a fresh directory:

```powershell
python tests/test_launcher_gui_recovery.py --old-tools <previous-native-tools.zip> `
  --new-launcher <new-launcher-build-directory> `
  --transaction-helper launcher/build/bin/fetcher-transaction-tests.exe `
  --root <new-test-directory>
```

These tests cover terminated processes and simulated interrupted restores, not sudden power loss,
failed storage hardware or recovery from missing/corrupt backups. Retain an external stable package.

## Release requirements

Keep the stable tools tag, release asset names and installed filenames compatible. The package's
sourceCommit must match the commit used for the release tag. The existing publishing script replaces
assets before advancing that tag. Retain the verified legacy ZIP and its hash, the candidate archive,
upgrade-report.json, and a rollback copy of stable assets before promotion. Changing these local files
or passing this test does not authorize publication. Test the launcher with a small group against
the existing public client/server. The separate Studio client/server/content cutover does not require
an ARM GitHub server asset; see RELEASE-REVIEW.md.
