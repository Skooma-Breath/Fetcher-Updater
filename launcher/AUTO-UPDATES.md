# Startup checks and automatic restart

Opening the GUI renders the saved setup, then refreshes the master list and checks the published
launcher/tools, client and selected profile. It performs metadata/receipt checks only: it does not
install updates, launch the game, authorise Nexus or install UMO. Available updates appear in the
global notice and Updates page. Network failures are logged and offer retry; a failed check alone
does not block an otherwise usable saved installation. Known client/content updates still require
installation before Play, and advertised profile validation on Play remains mandatory.

After successful Install updates, Repair or Restore, the GUI compares its loaded executable/UI
snapshot with installed files. Client/content-only updates and updater-script-only changes do not
restart it. Changed executable or UI files trigger a fresh staged GUI with the same installation
root, saved identity/profile selection and debug setting. No game client is restarted or launched.

Restart stages only files matching the installed tools inventory, under the installation lock.
Missing files, mismatched hashes, extra UI files and duplicate runtime entries fail before handoff.
Staged copies are hashed again. The new GUI confirms its bridge and initial render through a
one-use local Windows event before the original window closes (15-second limit). Older rollback
targets without this capability use Windows GUI input-idle/process checks. A failed handoff leaves
the original window open with an error and Restart launcher retry button. An incomplete overall
update stays open with its original error rather than silently discarding the failure.

`launcherRestartHandshake: 1` in newly built tools inventories advertises readiness support.
The marker is supplied by the tools packager, never by a server-owner profile. Headless request,
updater/status and bootstrap operations retain their CLI behavior and never reopen a GUI just
because a runtime changed. An already-running older build needs one manual reopen to gain the
new capability; it cannot acquire restart logic from files it has not loaded.

## Validation — 2026-10-23

- All five native suites pass. New checks cover read-only startup discovery/release checks,
  offline retry and launch behavior, unchanged runtime, executable/UI-only differences, verified
  staging, incomplete/duplicate inventories and legacy capability selection.
- Real staged WebView2 testing uses the disposable fixture prepared by
  `tests/prepare_launcher_restart_fixture.py`. Public metadata receipts mark its fake client and
  tools current; the real transaction helper installs replacement test runtime snapshots. This
  isolates the handoff from downloading an older public build during development.
- Startup automatically completes its update check. An unchanged update leaves PID 16524 running.
  A verified executable/UI replacement followed by update completion opens PID 19532 and closes
  16524 after readiness acknowledgement. The visible interface changes from V1 to V2.
- An unverified UI edit rejects the handoff and keeps PID 19532 usable with a retry button.
  Repairing the fixture and retrying opens PID 42552 and closes 19532. Exactly one fixture GUI
  remains, and personal configuration/settings/fake key markers are unchanged throughout.
- Native package integration and all 32 native bootstrap process operations pass. No real client
  or server is launched/restarted by these checks. Fixture data/receipts are retained under ignored
  `release-assets/launcher-auto-restart-20261023`.

Prepare a new fixture root using `seed --root ... --build launcher/build/bin`; open its executable
with Computer Use. Use `stage --variant V2` and press Install updates to test handoff, or `corrupt`
to test rejection. Repair with a fresh `stage --variant V3` and use Restart launcher. `verify`
checks personal markers. This helper performs file/process setup only; Windows inputs remain
explicit Computer Use actions.
