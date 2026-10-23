# Native first installation

FetcherSetup.exe is a standalone Windows x64 C++ installer. It opens without WebView2 or an
existing OpenMW installation and defaults to %LOCALAPPDATA%\Fetcher. Choose a dedicated writable
folder, then Install / repair. Setup downloads the Fetcher launcher/tools release and its declared
client release using native WinHTTP. It does not run PowerShell, UMO or downloaded installer scripts.

The standalone setup executable is emitted beside fetcher-tester-tools.zip by the packaging script;
it is not added to the legacy tools ZIP. Existing launcher filenames and the legacy upgrade route
remain compatible. Publishing the new executable is a separate release action.

## Installation and recovery

The production tools endpoint is fixed to the Fetcher-Updater fetcher-tester-tools release.
Each archive must have one matching asset with a GitHub SHA-256 digest. Setup verifies the archive
and every installed inventory record. Required launcher UI/animation/notice files, the update channel,
x64 PE executables and the client's resources/vfs-mw directory must be present before installation.
The client release is chosen by the update channel inside the verified Fetcher tools package.
Server mod manifests cannot configure the setup executable's release endpoints.

Tools and client installations use the existing journaled transactions, backups and protected-file
rules. A failed client installation can leave a valid completed launcher installation; retry setup.
On a fresh install, setup creates a minimal openmw.cfg anchor required by the engine before it can
follow a managed --config directory. It never replaces an existing openmw.cfg. The anchor has no
game content list; first-run guidance still asks the player to select and prepare a server profile.
An interrupted transaction is recovered before another install. Personal game settings, saves,
keys, local mods and server profiles are preserved. Ordinary, extended-length and trailing-separator
paths share the same installation lock and running-game check. Setup refuses to update a running
client in its selected folder. Downloads are cached and checked again on retry.

Setup does not download Morrowind or any server mod list. After installation, open Fetcher Launcher:

1. Under Mods, set the owned Morrowind Data Files folder containing Morrowind, Tribunal and Bloodmoon.
2. Select a server and its matching built-in profile, or review/accept the operator's profile URL.
3. Prepare the profile. Downloads and generated configs remain separate from the original game.
4. Under Servers, enter the multiplayer account and optional character name, then Play.

Account/character choices are saved per server endpoint. They are game-server identities, separate
from Nexus credentials. No game password is stored by this form. A named character uses the engine's
isolated server/account/character settings and optionally auto-enters the world. A blank character
uses the in-game character chooser without passing the engine's incomplete --mp-profile-root option.
Launching without a selected server opens the client normally. The first-run panel reports missing
client/game/profile setup and links to the corresponding pages.

The launcher web interface requires Microsoft Edge WebView2 Runtime. Setup offers a link to the
official Microsoft download page; it does not silently install a third-party runtime.

## Validation

The normal build runs tests/test_native_bootstrap.py with real setup/launcher processes and an
argument-recording test client. Its loopback fixture verifies a fresh installation, profile readiness,
process arguments, per-server identities, character chooser mode, cache reuse/repair, bad metadata,
digests/inventories, missing UI, x64 checks, protected files, running-client refusal and crash recovery.
The recording executable is a test target and is never included in either distributed payload.

An opt-in real-engine/server acceptance driver is tests/test_bootstrap_game_live.py. It requires a
new root, explicit local runtime/content/tools inputs, an owned game, a read-only local Synth account
seed and a separate unused port. It installs through FetcherSetup.exe, prepares the combined content
through the installed launcher and launches through the launcher's normal saved-identity action.
Only its own client/server processes are closed afterward; it never uses ports 25567 or 25568.
This local fixture is not a clean Windows machine or public download/deployment test.
Fresh application installation also passes on a second Windows 11 device with existing WebView2/
VC runtimes and owned game files; see WINDOWS-ACCEPTANCE.md for its real UI bridge, HTTPS content,
runtime startup, repair and remote upgrade/recovery checks. Its installed client also joins a staged
local world on Intel HD Graphics 615, with server Lua and Studio keyboard delivery verified in logs.
Missing-prerequisite acceptance remains.
Use a short installation path for the current engine: native setup can write extended-length paths,
but this engine build failed to open a surf Lua script when a deeply nested acceptance fixture
exceeded the legacy Windows path limit. The launcher's default folder avoids that extra nesting;
engine-wide long-path support remains a separate acceptance item.

For unattended setup, --install --install-root <folder> --log-file <result.json> installs without
opening a launcher. The JSON result has a companion .progress.txt file. Endpoint/crash overrides
are accepted only with --install and --allow-local-fixtures and explicit 127.0.0.1 HTTP endpoints
for both releases; the normal GUI and server profiles cannot enable them.

The native public test was promoted on 2026-10-22. The default tools endpoint now serves the native
launcher/setup and selects the matching Studio client channel. Published HTTPS setup and a normal
installation joining the Vanilla VPS pass; see VANILLA-VPS-ACCEPTANCE.md. Local release fixtures
remain available for future candidate testing without changing public endpoints.
