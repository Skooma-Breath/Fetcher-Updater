# Server-owned mod manifests

Use Mods & load order → Review manifest with an HTTPS URL or a local JSON path. Review the
server endpoint, version, source URLs, archive hashes and plugin order, then Accept this manifest.
Accepting adds/selects a profile; Prepare, Play or Install updates can subsequently fetch its files.
Discovery and profile selection alone never install mods. The original openmw.cfg is not rewritten.

See examples/fetcher-vanilla-studio.json for the real pinned combined content candidate and
examples/community-umo.json for a wrapper referencing an exported UMO list.

```json
{
  "schemaVersion": 1,
  "id": "example-community",
  "name": "Example community",
  "version": "1.0",
  "host": "play.example.org",
  "port": 25565,
  "packages": [{
    "id": "community-assets",
    "url": "https://example.org/community-assets.zip",
    "sha256": "REPLACE_WITH_64_HEX_SHA256",
    "dataDirs": ["Data Files"],
    "archiveFormat": "zip"
  }],
  "content": ["Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm", "Example.esp"],
  "requiredFiles": ["meshes/example/model.nif"]
}
```

The profile's content array is the server-required load order. Package order, then dataDirs order,
determines data priority. A dataDirs entry of `.` exposes the archive root. Paths are relative to
that package and may not escape it. dataDirs replaces the need for a custom metadata file inside
ordinary mod archives. Older Fetcher ZIPs can still declare dataDirs in openmw-client-package.json.
Optional top-level groundcover and fallbackArchives arrays configure those separate OpenMW lists.
Omitting fallbackArchives keeps Morrowind.bsa, Tribunal.bsa and Bloodmoon.bsa.

## Nexus files

Replace url in a package with `"nexus": {"game":"morrowind", "modId":123, "fileId":456}`.
The SHA-256 and dataDirs requirements are the same. Pin exact files, not a latest-file search.

Players can save their own Nexus API key in the launcher. Windows DPAPI protects the saved key for
that Windows user; it is absent from profile/state JSON and is sent only to the Nexus API. The API
request identifies Fetcher Launcher. Eligible downloads resolve through Nexus v1 download_link;
credentials are never forwarded to the CDN. API redirects are refused. On authentication, eligibility
or rate-limit failure, the error leaves the previous setup intact and offers manual import.

Manual route: choose the package, Open Nexus file, download that exact file in the browser, paste
its local path and Import archive. SHA-256 must match. Then prepare the profile. No Nexus account
or credentials are needed by the launcher for importing a file already downloaded by the user.
There is no NXM protocol takeover, SSO integration or bypass of Nexus download requirements.
Public Nexus application registration and live authenticated account trials remain rollout tasks.

## UMO imports

Wrap an exported UMO JSON array using either `umoMods: [...]` or
`"umo": {"url":"https://example.org/list.json", "sha256":"..."}`. The latter hashes the raw
UTF-8 file bytes before parsing. A server wrapper still supplies id, name, version, host and port.

Each download_info entry needs pinned=true, sha256 and an exact nexus_file_id or a direct_download
HTTPS archive URL. Nexus mod identity comes from the entry's url. data_paths and extract_to are
normalized into per-package dataDirs. For multi-archive mods or ambiguous layouts, provide an
explicit dataDirs array on each download. With no wrapper content, vanilla masters and each mod's
plugins build the order; mod groundcover entries remain separate. Set wrapper content explicitly
when the server needs a different order. Native copy, rename/move and remove actions are supported
as described below. No arbitrary installer commands or compatibility scripts run. Unpinned files,
missing hashes/IDs and unsupported actions fail review.
Existing legacy lists often need owner curation before they satisfy these requirements.

ZIP, 7z and RAR/RAR5 archives are decoded in-process. Set archiveFormat to auto (the default), zip,
7z or rar. Actual bytes determine format; an explicit mismatched format fails preparation even when
the download filename has a different suffix. The pinned static libarchive/LZMA/zlib/bzip2 build
covers common LZMA/LZMA2, PPMd, Deflate and BZip2 7z codecs and stored/compressed/solid RAR cases.
Encrypted archives, multipart archives, self-extracting executables and other unsupported codec
variants fail without launching an external program. FOMOD choices, plugin-cleaning actions and
the historical compatibility-applier pipeline remain migration work. The legacy installer remains usable.

## Native file layout operations

Package dataDirs selects folders without moving files. When a mod requires actual layout changes,
add a bounded actions array. Direct package paths are archive-relative. UMO download_info.actions
uses paths beneath that download's extract_to prefix; normalization strips the prefix. Actions
cannot reach another package, the game installation, another profile or launcher metadata.

```json
"actions": [
  {"action":"copy", "src":"Options/Modern", "dst":"Data Files/textures"},
  {"action":"rename", "src":"Data Files/Old.esp", "dst":"Data Files/Selected.esp"},
  {"action":"remove", "paths":["Unused patch", "Optional/readme.txt"]}
]
```

copy recursively copies a file or directory. Copying a file into an existing directory retains
its filename. Directory merges and overwriting files require force=true. rename normalizes to
move; moves into an existing directory retain the source name. Replacing a move target requires
force=true. remove accepts exactly one path or a paths list; missing sources fail preparation.
Source/target paths cannot be equal, contain one another, use wildcards, escape the package, or
address .fetcher-* metadata. Up to 128 normalized operations are allowed. Known UMO tes3cmd=null
metadata is discarded; an executable/tool value is rejected. Unknown actions and fields fail review.
No shell, script interpreter, plugin cleaner, file association or downloaded executable is invoked.

Operations run in a fresh staging copy with entry/byte limits. The original SHA-addressed extracted
archive remains immutable. A separate derived layout cache is keyed by source SHA plus normalized
operations and recipe version; two servers can choose different variants without overwriting each
other. Verified derived layouts are reused and corrupted layouts rebuilt from the source cache.
Failure before activation leaves the previous config/layout usable. Review displays file operations,
including explicit replacements, before acceptance. Select a different dataDirs folder when moving
a directory into its ancestor would otherwise be necessary.

## Review, identity and updates

Manifest imports are namespaced by source location and owner ID. Distinct sources with the same ID
cannot overwrite each other. Reimport from the same URL to review a new version. The preview does
not change the selected profile or download archives; GitHub references are resolved/pinned during
review. Accept binds the exact reviewed fingerprint, not a later re-fetch of mutable remote content.
The previous accepted manifest is retained for Use previous manifest; Prepare then revalidates its
cached files and regenerates its config. This is manifest rollback, not power-loss transaction recovery.

GitHub packages may use repository/releaseTag/assetName instead of url/sha256. A release API asset
must supply a SHA-256 digest. Review pins its resolved URL and digest into the accepted profile.

Generated configurations live in _fetcher_launcher/profiles/<namespaced-id>/openmw.cfg. They replace
inherited data, content, groundcover and fallback-archive lists and keep a separate data-local path.
The launcher validates TES3 masters and required assets before replacing the generated config.
Optional local mods stay per profile; server-required plugins retain the server's order.

## Server browser and boundaries

The master URL is https://master.fetchers.org/v1/servers. If an entry supplies mod_profile_url
(or profile_url), the browser offers Review server mods. Its manifest must match the advertised
host/port. No master-service change was deployed here; manual URL import works independently.
Known built-in Fetcher endpoints continue to select their original profiles until explicitly changed.

Production requests require HTTPS and validate each redirect; local/private/reserved address
resolution and embedded URL credentials are rejected. Bounded extraction rejects path escapes,
symlinks and collisions. Server mod payloads cannot include Windows programs or installer scripts.
These controls do not certify game content as harmless: OpenMW Lua and assets retain the engine's
mod trust and sandbox limits. A broader untrusted-server review remains a public-rollout requirement.

Local HTTP and fake Nexus API fixtures require both --request-file and --allow-local-fixtures.
The optional --nexus-fixture-url is valid only in that mode. These flags never persist in profiles
and never enable local downloads in the GUI. Native-client/tools updates use their separate update
channel; a server manifest cannot replace the executable or launcher.

The public Vanilla server's content and authority configuration have not been changed. The combined
Vanilla manifest is a download/preparation candidate; game-join testing needs matching server content
and the required Studio native/Lua versions. Asset installation alone does not enable Studio gameplay.
