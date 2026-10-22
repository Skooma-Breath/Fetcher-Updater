# Native launcher migration and public rollout

## Implemented candidate

Native client/tools updater, verified package cache/install, exception rollback, public server browser,
favorites/direct connect, imported server profiles, isolated mod configurations, plugin master ordering,
optional local mod controls, reviewed/namespaced server manifests and previous-manifest rollback,
UMO ZIP-list normalization, Nexus API-key/manual archive routes, external archives and visible
Sketchfab credits. A combined surf/truck/Studio bundle is built
locally and passes native integration. The web interface is retained; its backend uses C++.

## Remaining work before replacing the public launcher

1. Finish general server-list parity beyond the implemented reviewed manifests, pinned declarative
   UMO ZIP imports, Nexus API-key download path and exact-file manual import. Add 7z/RAR extraction,
   required declarative UMO transforms/FOMOD choices, Nexus app registration/SSO/NXM handoff and live
   eligible/free-account trials. Then port the legacy tool/bootstrap, hash-gated compatibility fixes
   and complete Fetcher Simulation preset. The separate legacy installer remains available.
2. Validate a real game launch/join from the generated config with a staged matching server and clean
   Windows bootstrap. Exercise update interruption/recovery and provide an operator rollback UI.
3. Stage the vanilla-server content/authority and Studio Lua/native-client release together. The
   combined archive/credits and server manifest are published as separate candidate assets and pass
   real HTTPS preparation. Promote the matching profile only with the coordinated server/client
   rollout. Keep the original archive and rollback copies.
4. Release the native launcher candidate to a small public test group before replacing the stable tag.

## Subsequent mod-manager/browser improvements

- Server-advertised profile URLs, manifest identity/version display and protocol/build compatibility.
- Rich dependency/conflict view, optional-mod presets, package removal/cache management and disk-space UI.
- More server operators/profile formats, connection diagnostics and large-list UX checks.

The launcher build remains local. Two additional content-test assets were published with explicit
approval; the original release assets and public server configuration are unchanged. This roadmap
does not authorize an automatic launcher or public-server deployment.
