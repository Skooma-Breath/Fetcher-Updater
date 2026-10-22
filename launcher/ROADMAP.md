# Native launcher migration and public rollout

## Implemented candidate

Native client/tools updater, verified package cache/install, exception rollback, public server browser,
favorites/direct connect, imported server profiles, isolated mod configurations, plugin master ordering,
optional local mod controls, and visible Sketchfab credits. A combined surf/truck/Studio bundle is built
locally and passes native integration. The web interface is retained; its backend uses C++.

## Remaining work before replacing the public launcher

1. Generalize server-owned mod lists before completing the Fetcher-specific migration. Add reviewed,
   versioned server manifests with UMO import, pinned Nexus file identities and hashes, authenticated
   Nexus downloads/manual fallback, and HTTPS archives for non-Nexus content. Keep each server's
   configuration isolated. Implement only validated declarative install operations, never arbitrary
   UMO actions or owner-supplied commands. See `SERVER-MODLIST-DESIGN.md` for the planned contract.
   Then finish parity with the separate legacy workflow: third-party tool/bootstrap handling,
   current UMO fixes, hash-gated compatibility appliers, and the complete Fetcher Simulation preset.
   Existing installations remain usable meanwhile.
2. Validate a real game launch/join from the generated config with a staged matching server and clean
   Windows bootstrap. Exercise update interruption/recovery and provide an operator rollback UI.
3. Stage the vanilla-server content/authority and Studio Lua/native-client release together. Publish
   the combined mod archive with its credits, then promote the matching profile. Keep rollback copies.
4. Release the native launcher candidate to a small public test group before replacing the stable tag.

## Subsequent mod-manager/browser improvements

- Server-advertised profile URLs, manifest identity/version display and protocol/build compatibility.
- Rich dependency/conflict view, optional-mod presets, package removal/cache management and disk-space UI.
- More server operators/profile formats, connection diagnostics and large-list UX checks.

This roadmap does not authorize an automatic public deployment. The candidate build and content ZIP
are reviewable local artifacts; the public releases and servers remain unchanged.
