# Appearance editor rollout — 2026-10-25

Status: completed. Both public servers run the matching API-142 build and appearance
package v6. The published Experimental client and normal local installation are updated.

## Build inputs

- Engine source: signed `55839a79937ef81a916dfd1fb0dc29ebf1beda97` on
  `experimental/public-test`.
- [Experimental client workflow](https://github.com/Fetcher-Simulator/Fetcher-Simulator/actions/runs/40228906603).
  This existing workflow automatically publishes `fetcher-simulator.zip` on
  `Fetcher-Simulator-Experimental` when successful.
- The VPS push/build hook is compiling the same source into
  `/srv/fetcher-release-staging/20261025-appearance-editor/dist`.
  The temporary hook directs this build away from both live roots; the deployment
  script restores the tracked normal hook after successful staging.
- Existing public builds use OpenMW Lua API 141; the editor needs matching API 142
  client/server builds. Do not announce tester updates until server cutover passes.

## Staged state

Private VPS rollback snapshot: `/srv/fetcher-release-backups/20261025-appearance-editor`.
It retains both script/package trees, configuration, Lua storage, consistent SQLite
backups and links to the original binary/defaults. Integrity checks passed for all
53 experimental and 3 Vanilla characters. The original deployment hook and dirty
VPS source are retained. Comparing that source against the rollout branch confirmed
the existing native fixes are preserved; only the new appearance additions differ.

Staged runtime overlays are `experimental-runtime` and `vanilla-runtime` under the
staging root. Both contain appearance package v6, the server save/session controller,
`/appearance` command routing and its help entry. Vanilla explicitly allows the
command and loads its module. All existing package source files and unrelated
scripts remain byte-identical; existing package manifests only change their exact
Lua API pin from 141 to 142. Studio v30 and the experimental HUD overrides are retained.
Operator config, spawn/marks/timer settings, profiles, assets and authority are preserved.

The previous public client ZIP is retained locally under
`release-assets/appearance-vps-20261025/rollback` and verified as SHA-256
`bab8e3e5c0a55261b032780174154b5d977e3e93fd49b9d72f2cf49debf294a0`.
Other private scripts, source audits and logs are under the same ignored stage root.
The VPS push log is `vps-build.log`; the local command session was 34987.

Validation so far: 255 appearance checks, 39 Vanilla command/timer checks, and Lua
syntax checks for both staged script trees. The live editor previously passed local
preview/save/reconnect, mixed-part persistence and actual renderer-facing checks.

## Deployment procedure

1. Confirm both builds succeeded. A successful staged VPS deployment must contain
   its new executable/defaults and have restored the normal bare-repository hook.
   If the build fails, restore the original hook from the private snapshot before
   unrelated pushes; retain the staged build output for diagnosis.
2. Download and verify the published client inventory, native executables, embedded
   launcher/tools and API-142 package payload. Test a disposable staged VPS instance
   with the matching client/content before activation.
3. Recheck both player counts immediately before cutover. Coordinate a save/restart
   if anyone has connected. Snapshot the databases/storage again at shutdown so the
   initial backups cannot discard subsequent player progress.
4. Install the staged executable/defaults atomically at the shared experimental
   binary location, and activate each reviewed runtime overlay. Keep server.cfg,
   owned content/authority, public profiles, assets, keys, DB and storage in place.
5. Restart only the two relevant game processes through their existing supervisors.
   Verify ports 25564/25569 and health endpoints 8081/8082, all character data,
   old package preservation, `/help`, `/appearance`, save/cancel/reconnect and
   matching-client joins. Restore the runtime configuration if validation fails;
   never restore an older player DB over new player work without an explicit decision.
6. Update this status with final hashes, process/health evidence and tester instructions.

The launcher already resolves the Experimental asset digest and client inventory;
the appearance feature does not require another launcher binary/tools release.
No ARM server GitHub asset is required.

## Completed validation

The workflow succeeded and published `fetcher-simulator.zip`, SHA-256
`c3f4951ae0845d86923a42490ce25bcd816268045b20fd7a4d71f5cd1ebb4b82`.
All 677 managed files, setup wizard/importer and embedded launcher/tools verified.
The package is delivered by the servers, rather than requiring a separate client mod.

The VPS compiled successfully. Its staging guard initially stopped because the deployed
master image is `openmw-master-server:profile2m-20261023`, rather than the script's
`:local` default. Rerunning staging with `OPENMW_MASTER_IMAGE` set to that existing
image validated that protocols 1–24 were already admitted. The master container and
configuration were unchanged. Future scripted deployments on this host must retain
that image override. The normal tracked post-receive hook is restored and verified.

The published client passed private tests of both real content profiles using fresh
test databases and an SSH datagram bridge. `/appearance`, mixed head/hair preview,
cancel, native save, reopening, camera restoration and unchanged base stats passed.
These tests made no appearance saves on either public database.

Cutover used zero-player shutdowns and fresh consistent database/Lua-storage backups
under the private snapshot's `cutover` directory. Existing supervisors restarted the
servers as their original users. Experimental game PID: `1939576`; Vanilla: `1939593`.
Health endpoints 8081/8082 report healthy, and ports 25564/25569 remain available.
All 26 protected player-data tables on each server were identical immediately after
activation: 53 experimental and 3 Vanilla characters retained. Both DB integrity
checks pass. Server configuration, default spawn/marks, authority/content, Studio v30,
all ten experimental packages and the Vanilla Studio package are preserved. Unrelated
server script counts retained byte-for-byte: 39 experimental and 22 Vanilla.

Installed server SHA-256:
`9e9cedfbb73b02caf704fc4e6816268c633ac11536cf44ffc22b6e0c53f7c235`.
Matching defaults SHA-256:
`fb6a74897ab2672ab1383991338a0982b4796e08645fcc360fe8e208e92268b4`.

The normal installation updated through the existing native launcher. All 125 checked
personal settings/save/key files were unchanged and all 677 managed files verified.
It then joined both public servers through automatic server/profile discovery, accepted
the handshake, activated server Lua and restored its existing characters. The test
clients were closed and the original selected profile restored. Final health checks
show both servers healthy and empty; the private test port/servers are stopped.

Existing Inventory Extender/Inventory Camera localization-context errors remain in
the heavily modded profile. They are also present in the retained 2026-10-23 normal
join log, and the stock settings Lua is byte-identical between old/new client releases.
Their existing package sources were deliberately preserved; this rollout does not
claim to fix unrelated mod errors. Vanilla and appearance editor checks pass.

Evidence is retained in the ignored stage: published-client-verification.json,
normal-update-verification.json, live-join-report.json and both private client results.
The VPS stage retains cutover-report.json and final-audit.json. No temporary QA Lua
package or test character was installed in either public runtime/database.

Testers: close the game, run **Updates → Install updates** in Fetcher Launcher,
then join either server and type **`/appearance`**. F6 keeps its normal chat binding.
