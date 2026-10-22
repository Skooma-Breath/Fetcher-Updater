# Fresh Fetcher installation on another Windows device

The repeatable test uses `tests/Test-FetcherFreshWindows.ps1` and its loopback release fixture
`tests/Serve-FetcherAcceptance.ps1`. These are acceptance tools, not launcher dependencies or
production updater scripts. The launcher and setup perform their own native installation work.
The fixture uses inbox Windows PowerShell/.NET; no system Python or development toolchain is needed.

## Payload and execution

Use a dedicated payload directory containing:

- `FetcherSetup.exe` from the candidate build.
- `fetcher-tester-tools.zip` built from the same source commit.
- `fetcher-simulator.zip` containing a real supported client and `fetcher-client-files.json`.
- `profile.json`, an explicitly selected test manifest whose archives use real HTTPS sources.
- `expected.json`: `sourceCommit`, `workingTreeCandidate` (false for the signed final candidate),
  and `files`, with each payload file's `name` and independently recorded `sha256`.

For the separate upgrade/recovery tests, also transfer the retained legacy tools archive, its
recorded SHA-256, `fetcher-launch-fixture.exe`, `tests/test_launcher_upgrade.py` and
`tests/test_native_bootstrap.py`. The launch fixture is test-only and never enters a release package.

Transfer over an already authenticated SSH connection, verify transferred hashes, then run:

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass `
  -File C:/FetcherAcceptance/Test-FetcherFreshWindows.ps1 `
  -PayloadDir C:/FetcherAcceptance/payload `
  -InstallRoot C:/Users/PLAYER/AppData/Local/Fetcher-Candidate `
  -EvidenceRoot C:/FetcherAcceptance/evidence `
  -GameData 'C:/GOG Games/Morrowind/Data Files'
```

Use new installation/evidence directories. The driver never resets an existing installation.
`-ExecutionPolicy Bypass` applies only to the acceptance process; it does not change machine/user
execution policy. The test serves only known payload routes on IPv4 loopback with an ephemeral
port. No HTTP URL reservation, firewall exception or remote-access policy change is required.
Setup endpoint overrides require its existing explicit CLI fixture mode; normal setup/server
profiles cannot use them.

## What it checks

1. Hash-verified native setup into a fresh directory and exact installed source commit.
2. First-run client/game/profile readiness.
3. Actual WebView2 loading, native bridge calls and rendering of the launcher controls.
4. Native retrieval of the live public server listing.
5. Review/acceptance and real HTTPS preparation of the combined surf/truck/Studio content.
6. Real client `--version` startup, exercising its loader/runtime dependencies.
7. Cached setup repair while preserving the existing base configuration.
8. Optional upstream UMO installation/import probe, without signing in or changing protocols.
9. Byte-identical owned Morrowind/Tribunal/Bloodmoon masters and unchanged NXM/MOMW associations.

The UI diagnostic is `FetcherLauncher.exe --smoke-test-ui --install-root <folder> --log-file <json>`.
It loads the actual shipped page and native bridge, writes checks and exits. Its test mode does not
auto-refresh servers or execute an installer action. Production startup retains normal behavior.
Use an external timeout when running it; a page that cannot execute its startup script cannot report
completion. Missing UI/runtime initialization errors return structured failures instead of a modal.

An OpenSSH command runs in service session 0, where WebView2 initialization failed on the tested
machine. The driver therefore uses a temporary limited scheduled task in the account's existing
signed-in desktop session. No password is stored or new logon created. The UI closes itself, and the
driver removes only that uniquely named task afterward. The account must have a signed-in desktop.

After the optional UMO install, its pinned embedded Python can run the stdlib-only upgrade/bootstrap
acceptance drivers, without installing or adding a system Python to PATH. These are test harness
processes; Python is not needed by native setup/update/profile preparation.

## PillowPC acceptance, 2026-10-22

PillowPC runs Windows 11 Pro x64 build 26100 with Intel HD Graphics 615. It had an owned GOG
Morrowind installation, WebView2 155.0.4283.62 and the Visual C++ runtime, but no Fetcher installation.
Its `python.exe` entry was only the Windows Store alias. This is a fresh application install on a
second Windows device, not a freshly imaged operating system or a missing-prerequisite test.

The native setup, desktop WebView2/bridge, public server browser, real combined-content HTTPS
preparation, real client runtime startup, cached repair and UMO runtime probe pass. Owned masters
and NXM/MOMW handlers remain unchanged. The real retained legacy updater also installs the candidate
and preserves personal markers, while rejecting bad hashes/missing UI. All 32 bootstrap operations
pass on this device, including interrupted-process recovery and running-client/protected-file guards.

Evidence is retained under `release-assets/pillowpc-evidence-*` locally and the dedicated
`%LOCALAPPDATA%/FetcherAcceptance/20261022-pillowpc` directory on the test device. Reports distinguish
development worktree trials from the final signed-source package.

## In-world acceptance on PillowPC

`tests/New-FetcherWorldFixture.ps1` builds a private input directory from a matching client/server
runtime, server scripts, client Lua packages, a closed freshly seeded test database, and its matching
local test key. It records every file hash and the expected client hash/revision. Seed the database
with the engine's `scripts/seed-procedural-synth-test.py`, which reads its source database without
changing it; do not copy a live database. The fixture contains an identity key and is not a release
asset. Keep it in an ignored private test directory and transfer it over authenticated SSH.

```powershell
./tests/New-FetcherWorldFixture.ps1 `
  -ServerRuntime C:/AcceptanceInputs/runtime `
  -ServerScripts C:/AcceptanceInputs/server-scripts `
  -LuaPackages C:/AcceptanceInputs/server-lua-packages `
  -SeedDatabase C:/AcceptanceInputs/seeded-playerdata.db `
  -IdentityKey C:/AcceptanceInputs/local-test.key `
  -RuntimeRevision 13e46dce90 `
  -OutputDir ./release-assets/private-world-fixture
```

On the test device, use the existing dedicated acceptance installation with its prepared loopback
profile selected, then run:

```powershell
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass `
  -File C:/FetcherAcceptance/Test-FetcherWorldWindows.ps1 `
  -InstallRoot C:/Users/PLAYER/AppData/Local/Fetcher-Candidate `
  -FixtureDir C:/FetcherAcceptance/private-world-fixture `
  -EvidenceRoot C:/FetcherAcceptance/world-evidence
```

The driver requires `127.0.0.1` and the explicit test port (default 25570); it refuses ports
25567/25568 and occupied ports/running clients. It verifies fixture/client hashes, copies the seed
into a new server directory, and starts an unlisted server against the generated client config.
Internal Lua libraries are copied from the installed client to the server's working directory:
developer source-path fallbacks must not mask a missing deployed runtime. Server Lua must load
successfully before a temporary limited desktop task invokes the installed launcher's saved-identity
launch action. This uses the known `ass` / `Synth A` fixture, without entering or automating input.
It seeds quiet windowed settings only for a new test identity; existing test settings are preserved
and an existing different endpoint key is refused.

PillowPC's installed candidate from signed source `5630808` passes with engine revision
`13e46dce90`, Intel HD Graphics 615 and OpenGL 4.6. The real character enters ToddTest, supplies the
complete actor roster, stages the server's Studio Lua package and receives the placed keyboard.
Client logs confirm that surf and vehicle plugins load. Owned masters and the base configuration
remain byte-identical. The runner closes only its own game/server and removes its temporary task.
Evidence: `release-assets/pillowpc-world-evidence` and remote `world-3` under the acceptance root.

The first remote trial exposed missing internal server Lua libraries in the test fixture; startup
reported `module not found: util` even though the game connected. The fixture now supplies those
libraries and rejects Lua startup failures before launching. The earlier local game-join harness
also supplies them explicitly. This was a test-server staging correction, not a client change.

This is log-based world/render-context acceptance, not visual inspection, music listening or an
interactive Studio editing/band test. Still separate: fresh/missing runtime prerequisites, real
Premium entitlement and other browsers, storage hardware/power-loss failures, final production
release URLs and the coordinated public client/server/content rollout. Earlier local in-world
acceptance remains documented in `BOOTSTRAP.md` and `TEST-REPORT.md`.
