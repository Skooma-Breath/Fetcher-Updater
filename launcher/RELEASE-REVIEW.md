# Launcher release and Studio cutover

The reviewed cutover was completed on 2026-10-22. VANILLA-VPS-ACCEPTANCE.md records the deployed
versions, public assets, actual normal-installation VPS checks and retained rollback state.
The sections below retain the review/deployment procedure; their candidate descriptions are historical.

The launcher can ship against the current public client and servers. A separate ARM server GitHub
release is unnecessary: `git push vps` already invokes the server build/deployment hook. Keep the
launcher release and Studio world/content activation as separately reviewable steps.

## Local review

`scripts/Stage-FetcherStudioReview.py` assembles the five launcher release assets, a Windows Studio
client archive with a verified file inventory, the existing attributed content bundle, a proposed
Vanilla profile, clean committed server/deployment source, checksums and a readable `REVIEW.md`.
Its inputs are explicit; it neither publishes assets nor runs deployment scripts. Put output under
ignored `release-assets/`. Keep private server backups, game data, keys and databases out of `publish/`.

Retain the current public tools-release metadata and all four asset files in `rollback/`, verifying
each against GitHub's digest. Keep public-client metadata and its verified archive as acceptance
inputs. Build tools from a signed updater commit using `Build-FetcherTesterTools.ps1` and the tested
native build. The package manifest identifies the packaging source; retain the original native
binary build/test hash separately. The Studio client/server build must identify the same engine
commit, excluding unrelated worktree Lua overrides from the reviewed server source.

`--vanilla-snapshot` optionally generates a concrete private Vanilla overlay and server.cfg diff from
the retained live config, manifest and health script. It enables only Studio, includes its required
Lua modules/package, keeps the current health behavior and references the existing owned Vanilla
data plus staged combined content. It does not import the experimental runtime or auto-place test
objects. Review the generated overlay inventory and choose keyboard placement before activation.

## Launcher-only publication

1. Validate the package, retained legacy-to-native upgrade and native setup with the current public
   Vehicles client. Verify UI/server discovery and preservation of owned data/settings.
2. Review the exact five files listed in `release-review.json` under `launcherReleaseAssets`.
   They are the tools ZIP, native FetcherSetup.exe and the three legacy bootstrap/list assets.
3. Once publication is authorized, use the existing updater release workflow/publisher with exactly
   those five assets. Retain the older target and files. The workflow now includes native setup.
   The stable tools tag must match the candidate package's sourceCommit.
4. Verify published digests and setup/upgrade download behavior. Tell testers to update, wait for
   success, close the old window and reopen. Keep the legacy bootstrap available.

This leaves the existing client channel, current-public Vanilla profile, server database and content
unchanged. A Studio preview still requires a matching server; the launcher update alone does not
activate it. Publishing the whole review folder with the stable publisher would be incorrect because
that publisher removes assets not in its explicit list.

## Studio server/client/content test

The operator has authorized VPS updates when appropriate for testing. No ARM binary needs uploading
to GitHub. Use the existing push/build workflow when the matching client and server inputs are ready.

1. Snapshot both VPS server installations privately before pushing: binary/resources, server.cfg,
   server-scripts, client Lua packages, content authority, player database and server Lua storage.
   Use SQLite's online backup API or stop the relevant instance for a consistent database copy;
   copying only a live .db while omitting its WAL is not a valid backup. Retain ownership/modes.
2. The live hook at `/home/ubuntu/openmw-bare.git/hooks/post-receive` deploys every branch pushed to
   it. A review branch is therefore not isolated. `/root/openmw-server-dist` resolves to
   `/srv/fetcher-simulator`; the hook replaces its binary/scripts/packages and can update master
   protocol admission. Existing running processes do not automatically adopt the new executable.
3. Preserve operator `config.lua`, `mod_manifest.lua`, `server.omwscripts`, custom overrides and
   credentials across the deployment. The current deploy script copies generic script defaults;
   do not silently replace the experimental server's configuration. Back up the hook/source ref too.
4. Push the pinned engine commit through `vps`, monitor the build to completion, restore/preserve
   the reviewed operator configuration, then smoke-test the built binary. Start a separate unlisted
   test instance with a disposable seeded database before restarting the public instances.
5. Vanilla currently uses only `GLOBAL: vanilla_runtime.lua`. Preserve that health/admin behavior;
   add the Studio runtime and its dependencies explicitly. Do not import the experimental server's
   entire mod/admin setup. Review keyboard placement; never copy the disposable test's mpNum=1 or
   its ToddTest spawn rules into an existing public database.
6. Build the new content authority from the matching client config using the engine's
   `files/server/Build-OpenMWServerAuthority.py`. Keep owned masters/BSAs private. Verify the ordered
   plugins, resolved Lua fingerprint and required keyboard/vehicle assets before server activation.
7. Test the matching Windows client, server Lua/package delivery, Studio interactions and persistence
   against that staged VPS instance. Then coordinate the Windows client channel, Vanilla manifest/
   built-in profile and server content switch. New-client assets must exist and pass digest checks
   before tools that select them are promoted. A current-public stock profile must not silently
   point at a server already switched to the combined content set.

## Rollback

For launcher rollback, restore the retained tools-release assets and old tag target together, then
verify public metadata. Testers close/reopen and use retained legacy bootstrap if needed. Preserve
their game settings, saves, local mods, keys and profiles; tools rollback is not a game-data restore.

For server/content rollback, stop only the affected instances, restore the matching prior binary,
resources, scripts/packages, authority and configuration, then restart and verify health/join. Keep
new database/storage changes separately; restoring a database discards subsequent player work and
requires an explicit decision. Revert client/profile admission coherently rather than mixing old
content authority with new required files. Retain every snapshot until the public test is accepted.
