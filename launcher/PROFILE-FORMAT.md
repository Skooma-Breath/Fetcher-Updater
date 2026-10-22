# Server mod profiles

The native launcher discovers public servers from `https://master.fetchers.org/v1/servers`.
The current master response supplies server addresses/player counts/protocols, not mod download URLs.
Selecting a known endpoint selects its supplied profile. Other servers use the existing installation
until the player imports a profile URL supplied by the server owner. The launcher does not guess
an authoritative plugin order from the server's display name.

Operators can provide one profile over HTTPS:

```json
{
  "id": "community-example",
  "name": "Example community",
  "mode": "managed",
  "description": "Content required by our server",
  "host": "play.example.org",
  "port": 25565,
  "packages": [
    {
      "id": "community-content",
      "repository": "example/community-content",
      "releaseTag": "server-content-v1",
      "assetName": "community-content.zip"
    }
  ],
  "content": ["Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm", "Example.omwaddon"],
  "requiredFiles": ["meshes/example/instrument.nif"]
}
```

GitHub assets must have a `sha256:` digest in the release API. A direct package source can instead
use `url` and a required 64-character `sha256` value. HTTPS is mandatory; loopback HTTP is allowed
only for package integration fixtures. Importing a remote profile requires HTTPS.

Each content ZIP contains `openmw-client-package.json`, for example:

```json
{"dataDirs":["./Data Files"],"content":["Example.omwaddon"]}
```

Payloads live beneath the declared relative data directories. The **profile's** `content` is the
required server load order; the package manifest can describe content for several profiles.
No installer commands, PowerShell scripts or arbitrary command-line arguments are accepted in profiles.
Downloaded packages are stored in a content-addressed `_fetcher_launcher/packages/<sha256>` cache.

The launcher reads each TES3 plugin's MAST records and validates dependency order. Missing masters,
cycles, duplicate plugin names, missing required assets, archive path escapes and symlinks are refused.
Optional local plugins can be added, disabled and reordered per profile. Required server plugins
remain enabled and retain the operator's order. Extra local plugins still need to satisfy the
server's content policy; adding one does not grant permission to bypass the game handshake.

Generated configs live in `_fetcher_launcher/profiles/<id>/openmw.cfg`. They explicitly replace
inherited data, content, groundcover and fallback-archive lists, use an isolated data-local directory,
and never rewrite the user's main openmw.cfg. Launch uses the generated config and selected endpoint.
Character profiles remain under the installation's existing profiles directory.

An optional local `fetcher-launcher-profiles.json` catalog has `schemaVersion: 1` and a `profiles` array.
Imported profiles persist in this catalog; selection, favorites, game-data directory and optional-mod
choices persist separately in `_fetcher_launcher/settings.json`.

## Rollout boundaries

The built-in current-public Fetcher Vanilla profile keeps its three vanilla master files.
The separate surf/truck/Studio preview profile is for the matching staged server. Publish the
combined content package and update the server authority/content configuration together before
promoting that profile. Studio gameplay also requires the matching native client and server Lua;
the asset ZIP alone does not add Studio gameplay or update public servers.

The full existing UMO/Nexus installer and its compatibility-applier migration remain separate legacy
tools. The new launcher does not call those PowerShell scripts or claim to reinstall the entire
legacy mod list. Existing-installation mode continues to launch that already configured installation.
