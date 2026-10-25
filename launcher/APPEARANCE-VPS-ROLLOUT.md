# Appearance editor rollout — 2026-10-25

Status: builds running; public server cutover pending. Neither public game process
has been restarted for this rollout. The operator will report back when builds finish.

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

## Finish after builds

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
