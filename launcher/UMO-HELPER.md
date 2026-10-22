# UMO Nexus helper

Fetcher uses UMO as its default Nexus provider. The launcher/backend remains C++; a separate
AGPL adapter runs the downloaded UMO Python runtime. It uses UMO's own sign-in, Nexus client,
browser download handler and downloader modules. UMO's upstream files are not modified.

## Player flow

1. Under **Mods**, choose **Install / repair UMO helper**. This downloads about 50 MB plus source
   from the pinned upstream release. Nothing is downloaded or authenticated on launcher startup.
2. Choose **Connect Nexus through UMO** and authorise UMO in your browser. The separate console
   is for UMO's fallback key entry if browser sign-in fails. Never send a key to a server operator.
3. For free-account browser downloads, choose **Use Fetcher for Nexus browser download links**.
   This is an explicit opt-in Windows-user NXM association. It replaces the current link handler;
   **Restore previous Nexus link handler** restores its prior values. Premium automatic downloads
   do not need this association. Manual exact-file archive import also remains available.
4. Review/accept a server profile and choose **Prepare profile**. Free accounts choose Nexus's
   **Mod manager download / Slow download** for each requested file and allow the link to open Fetcher.
   Nexus determines eligibility, limits and availability. Locked/removed files may need manual import
   or an updated server manifest; the launcher does not bypass these restrictions.
   If the browser does not open the manager automatically, choose Nexus's **Start download manually**
   fallback link on that same page; for this flow it is an NXM manager link.

During a UMO handoff, **Cancel Nexus operation** stops its supervised process. Closing the launcher
also cancels that process. This button does not cancel unrelated native update/download work.

## Boundaries and local state

- State is isolated under `<install>/_fetcher_launcher/umo/`. Existing legacy `_fetcher_umo` files,
  configuration, keys, mods and caches are neither read nor modified.
- The adapter keeps the UMO key encrypted with Windows DPAPI for the current Windows user. The
  C++ backend never reads/decrypts that key or uses it for its own API requests. **Disconnect Nexus**
  removes the helper credential. It does not revoke Nexus's authorisation on the user's account.
- The helper receives only a validated game/mod/file tuple, pinned SHA-256 and browser-link opt-in.
  It constructs a minimal descriptor with no actions. It does not extract archives, execute mod
  scripts, clean plugins, run tools, install mods or consume a remote UMO script/configuration.
- Fetcher independently verifies the completed archive, copies it into its ordinary download cache,
  and performs native extraction/layout/plugin-order checks. No game is launched by preparation.
- Browser callbacks bypass the update mutex and open no additional UI. They accept only the pending
  game/mod/file, reject malformed/duplicate/expired parameters, and forward the token on loopback to
  the waiting UMO process. Tokens, credentials, signed URLs and raw upstream exceptions are not
  written to Fetcher logs or profile metadata. UMO's private cache can contain its download metadata.
- When available, the helper listens on UMO's standard loopback port 6666 as well as supporting
  Fetcher's pending-file dispatcher. This accommodates browsers retaining an old UMO link command:
  that dispatcher forwards to the waiting process before loading its own account or cache. If an
  existing UMO process owns the port, Fetcher uses an ephemeral port for its own dispatcher.
- Enabling the NXM association retains the exact prior values, preserves unrelated values/MOMW links,
  and restores only while Fetcher still owns the handler. Another application's later change is kept.
- The advanced **Personal API key testing** controls retain the independent native API test path.
  That path identifies itself as Fetcher and never borrows UMO's identity or keys. **Use UMO downloads**
  returns to the normal external-helper flow. A future standalone Fetcher SSO implementation would
  require its own registration/review; this integration does not implement that route.

## Pinned distribution, license and corresponding source

| Item | Pin |
| --- | --- |
| Upstream | https://gitlab.com/modding-openmw/umo |
| UMO | 0.11.2 |
| Source commit | `2805e59ca4b627768fdd8e409a3d70a920225afc` |
| Windows package | https://gitlab.com/modding-openmw/umo/-/package_files/366287976/download |
| Package SHA-256 | `18dcd4a33171b6ca5b8afd8286e4917906f4a108df52f60cb2e718914ec057e4` |
| Source ZIP SHA-256 | `7420931c2748b55f2425867764e116487ef1d3b2f3b632f59ca3042ccd87f30f` |

UMO is AGPL-3.0-or-later. Fetcher's separate adapter in `launcher/umo/adapter.py` is also provided
under AGPL-3.0-or-later, with the complete license in `launcher/umo/LICENSE`. The installed helper
includes that adapter source, full license, attribution/source record and the exact complete UMO
source archive. The full upstream Windows distribution (3,848 files, including dependency notices)
is retained under `tool-0.11.2/upstream/`. The launcher verifies installed tool files before execution.
Server manifests cannot select helper binaries, versions, executable URLs or command-line arguments.

The tools release contains the adapter/license as embedded data; the optional UMO distribution and
source are downloaded from upstream when requested. Public release must retain this source/license
delivery and verify the final packaged payload. There is no claim that process separation alone
decides license obligations or that Nexus has endorsed Fetcher.

## Repeatable tests

```powershell
python tests/test_umo_helper.py --exe launcher/build/bin/FetcherLauncher.exe
python tests/test_umo_helper.py --exe launcher/build/bin/FetcherLauncher.exe `
  --upstream-archive release-assets/umo-research/umo-win.zip `
  --upstream-source release-assets/umo-research/source.zip
```

The first command runs native provider/callback/DPAPI tests. The optional second command installs
the real pinned runtime into a temporary root, compares every upstream file, and exercises actual
UMO download code against local Premium/free-account fixtures. Account/API responses are mocked
only in the test harness; archive HTTP, browser callback and verification paths run normally.
The registry fixture uses a process-private overridden hive, never the real NXM association.
These automated fixtures do not constitute a live Nexus entitlement test.
