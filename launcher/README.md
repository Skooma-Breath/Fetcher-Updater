# Fetcher Simulator Launcher

This is the first Windows-only launcher frontend for Fetcher Simulator.

It uses:

- C++17
- [`webview/webview`](https://github.com/webview/webview) 0.12.0
- Microsoft Edge WebView2 on Windows
- The existing `Update-Fetcher-Simulator.ps1` as the update backend

The launcher does not reimplement updater behavior. It stages the installed PowerShell updater into the user's temporary directory, runs it with the current Fetcher installation as `-InstallRoot`, and streams combined standard output and error text into the HTML interface.

## Current functionality

- Automatically performs a read-only startup release check for the client, launcher/tester tools, client-mod bundle, and installed multiplayer compatibility patches.
- Shows **Up to date**, **Update required**, or **Unable to verify** before enabling Play.
- Blocks normal Play while a known update is available and exposes **Play Anyway** only when release verification fails.
- Displays the selected Fetcher release channel and installed client commit.
- Uses a steampunk/dystopian HTML interface with periodically spawned transparent background animations derived from the supplied Fetcher Simulator recording.
- Runs `Update-Fetcher-Simulator.ps1` without opening a console window.
- Provides a fast **Check for Updates** action that reuses a successful UMO scan for up to 24 hours when the modlist is unchanged.
- Provides **Full Mod Check / Repair** to force the complete UMO scan and reinstall/repair workflow.
- Avoids reinstalling an unchanged client-mod bundle during quick checks by validating its digest-backed receipt.
- Starts its interactive window from a per-user LocalAppData staging copy so the installed executable can be replaced during an update.
- Streams updater and UMO output into the launcher window.
- Launches `openmw.exe`.
- Launches `openmw-wizard.exe`.
- Opens the Fetcher installation folder.
- Supports `--install-root <path>` for development and diagnostics.
- Supports `--self-test` for build validation.

## Build

From the repository root in PowerShell:

```powershell
.\scripts\Build-FetcherLauncher.ps1 -Clean
```

The packaged prototype is written to:

```text
release-assets\fetcher-launcher
```

For normal use, place `FetcherLauncher.exe` and its `ui` directory beside `Update-Fetcher-Simulator.ps1`, `openmw.exe`, and `openmw-wizard.exe`.

## Development launch

```powershell
.\release-assets\fetcher-launcher\FetcherLauncher.exe `
    --install-root "C:\Games\Fetcher-Simulator-Test"
```

Pass `--debug` to enable the WebView developer tools context.

## Runtime requirement

The Windows build uses the installed Microsoft Edge WebView2 Runtime. The launcher catches WebView initialization errors and displays a native error message when the runtime is unavailable.
