# Server-owned mod manifests

For an operator walkthrough, including how to create the UMO list, see
[Server setup and launcher mod lists](SERVER-OWNER-GUIDE.md).

## Automatic discovery

Save the JSON beside `server.cfg` and add `launcher_profile = launcher-profile.json` to its
existing `[master]` section (`public_listing = true`). Restart after changing the file.
The server uploads the bounded UTF-8 document in its HTTPS registration; the master stores it
only while the server is listed and advertises its SHA-256. No separate website is needed.
Players select the server and press Play: its requirements and isolated load order are prepared
automatically. Nexus may still require account authorisation/browser approval for downloads.
Players do not paste JSON/URLs or approve a separate manifest review.

Selecting a server reads requirements only. Play/Prepare re-check the live advertisement and
digest; missing or invalid advertisements fail closed. The master's observed source address and
registered port determine the connection, overriding any host/port inside the document.
Server-browser/direct selection needs the live master. Existing local/manual profiles remain.

Embed `umoMods` or `packages`. Every archive, including a GitHub reference, needs a pinned SHA-256.
External `umo` list URLs, unknown top-level fields and arbitrary instructions are refused.
Existing path, declarative layout, FOMOD, archive and executable-payload checks still apply.
No website opens automatically, no shell installers run, and writes stay in managed directories.
The main configuration, account keys and player saves remain intact. Previous profile revisions
are kept for inspection/rollback; Play still requires the currently advertised revision.

Manifest requests use a launcher-constructed master HTTPS path and refuse redirects. Archive
redirects must use HTTPS and pass the public-address check at every hop. Connections pin the
checked IP while retaining TLS hostname validation; cookies and automatic Windows authentication
are disabled. Windows 10 21H1 or newer and direct internet access are required (system HTTP proxies
are not used). Local HTTP fixtures require an explicit test-only mode.

The master caps bodies at 600 KiB, profiles at 2 MiB/depth 32, active servers at 2048, servers
per source address at 16 and stored profile bytes at 64 MiB. Profiles are public: never include
passwords/API keys. Registration ties a listing to its source IP; it does not certify an operator
or prove assets safe. Hashes verify the owner-pinned bytes, and joining still entails trusting the
server's game assets and sandboxed game Lua. Manual imports use the review route below.

## Manual profile import

Use Mods & load order → Review manifest with an HTTPS URL or a local JSON path. Review the
server endpoint, version, source URLs, archive hashes and plugin order, then Accept this manifest.
Accepting adds/selects a profile; Prepare, Play or Install updates can subsequently fetch its files.
Discovery and profile selection alone never install mods. The original openmw.cfg is not rewritten.

See examples/fetcher-vanilla-studio.json for the real pinned combined content candidate and
examples/community-umo.json for an embedded, pinned UMO list suitable for automatic discovery.

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

The normal public route uses the external UMO helper for the player's own Nexus sign-in and
download approvals. UMO sign-in and the free-account Hackle-Lo NXM handoff have passed live
acceptance. NXM handling is opt-in; the launcher does not silently replace another mod manager.
Premium entitlement and broader-browser acceptance remain. See UMO-HELPER.md.

The advanced personal-testing route lets players save their own Nexus API key. Windows DPAPI protects the saved key for
that Windows user; it is absent from profile/state JSON and is sent only to the Nexus API. The API
request identifies Fetcher Launcher. Eligible downloads resolve through Nexus v1 download_link;
credentials are never forwarded to the CDN. API redirects are refused. On authentication, eligibility
or rate-limit failure, the error leaves the previous setup intact and offers manual import.

Manual route: choose the package, Open Nexus file, download that exact file in the browser, paste
its local path and Import archive. SHA-256 must match. Then prepare the profile. No Nexus account
or credentials are needed by the launcher for importing a file already downloaded by the user.
There is no bypass of Nexus download requirements. A separate Fetcher-owned SSO application is
future work; the supported public sign-in route currently uses UMO's authorisation.

## UMO imports

Wrap an exported UMO JSON array using either `umoMods: [...]` or
`"umo": {"url":"https://example.org/list.json", "sha256":"..."}`. The latter hashes the raw
UTF-8 file bytes before parsing. A server wrapper still supplies id, name, version, host and port.
External-list references are accepted only by manual imports. Automatic server discovery requires
the embedded `umoMods` form; it will not follow an external mod-list website.

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
variants fail without launching an external program. Server-pinned XML FOMOD choices are supported
below. Plugin-cleaning actions and the historical compatibility-applier pipeline remain migration
work. The legacy installer remains usable.

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
address .fetcher-* metadata. Up to 512 normalized operations are allowed. Known UMO tes3cmd=null
metadata is discarded; an executable/tool value is rejected. Unknown actions and fields fail review.
No shell, script interpreter, plugin cleaner, file association or downloaded executable is invoked.

Operations run in a fresh staging copy with entry/byte limits. The original SHA-addressed extracted
archive remains immutable. A separate derived layout cache is keyed by source SHA plus normalized
operations and recipe version; two servers can choose different variants without overwriting each
other. Verified derived layouts are reused and corrupted layouts rebuilt from the source cache.
Failure before activation leaves the previous config/layout usable. Review displays file operations,
including explicit replacements, before acceptance. Select a different dataDirs folder when moving
a directory into its ancestor would otherwise be necessary.

### Hash-gated compatibility patches

For a Fetcher-owned compatibility change, an action can reconstruct a file from bounded source
ranges and Base64 literals. This ports known byte deltas without invoking a script or plugin cleaner:

```json
{"action":"patch", "path":"scripts/example.lua",
 "sourceSha256":"<64 hex source digest>", "sourceSize":15,
 "outputSha256":"<64 hex output digest>", "outputSize":15,
 "operations":[{"copyOffset":0,"copyLength":6}, {"data":"bmV3"},
               {"copyOffset":9,"copyLength":6}]}
```

Both exact file sizes and SHA-256 digests are required. Unknown source bytes fail before that file
changes; the complete reconstructed output is verified before its atomic write. Already matching
output is a no-op. A missing file requires create=true, sourceSize=0 and the empty-source SHA-256;
an existing unexpected file is still refused. The 64 MiB source/output limit, 100,000-operation limit,
strict Base64 and copy bounds apply during review. Unknown fields/commands are rejected. Binary
files, null bytes and UTF-8 BOMs are handled as raw bytes. Patch actions share the ordinary 512-action
limit and package containment rules; server patches cannot address engine resources or another mod.
The recipe participates in the derived-layout cache key and is shown before acceptance.

An upstream archive can contain an optional helper tool only when the reviewed layout explicitly
removes it, or a FOMOD selects an output that excludes it. The complete final tree is checked for
blocked programs/scripts before activation, including files outside dataDirs. Folder selection
alone cannot bypass this check. Original archive/cache bytes can retain such inert files; the
launcher never invokes them. For example, the Simulation authoring curation discards Tamriel Data's
Java file patcher and Ashlander Architect's Python build helper by their exact paths.

### Optional owned-game asset restoration

A server profile can explicitly declare restoreGameAssets after its normal archive layout actions.
It reads only from the player's selected owned Morrowind Data Files. A file entry contains archive
(Morrowind.bsa, Tribunal.bsa, Bloodmoon.bsa, or an empty string for a loose file), sourcePath, and a
patch using the exact hash/size/range schema above. The patch path is the destination within this
package's derived layout. Both source and destination are restricted to .dds/.nif/.kf/.tga/.wav;
relative paths and launcher-metadata restrictions still apply. No arbitrary source root, other
archive filename, script, executable or installed game file can be written by the action.

Up to 4,096 explicit files share the layout byte/entry budget. Original source fingerprints must
match before a destination is written. Existing different destinations require force=true on
the action. Already matching outputs are a no-op. The native reader supports uncompressed TES3
BSA format 0x100 with bounded header/tables/names/offsets and legacy filename decoding; no BSA
extractor process is invoked. Loose originals and BSA archives stay read-only. Verified outputs
use the same isolated derived-cache mechanism as other actions. The reviewed UI lists the owned
sources, destinations and hashes. Changing game editions/original assets can require a newly
reviewed recipe; an unknown source is refused.

Fetcher uses this only for its optional experimental-server Starwind compatibility layer. Merely
installing the same Nexus Starwind mod through another profile never adds this action or downloads
Fetcher's compatibility package. No mod-ID detection injects compatibility fixes. The built-in
Vanilla profiles do not include Starwind or this layer. The developer authoring tool
scripts/Build-FetcherOwnedAssetRecipe.py converts the pinned Fetcher release's own asset manifest
to native operations. Copied stock asset bytes remain local; NIF changes encode only the changed
length-prefixed texture names, with all other bytes read from the player's originals.

## Server-pinned XML FOMOD choices

For a supported XML installer, add fomod to the package (or the UMO download_info entry):

```json
"dataDirs": ["."],
"fomod": {
  "config": "Wrapper/fomod/ModuleConfig.xml",
  "selections": [
    {"step":"Textures", "group":"Resolution", "plugins":["2K"]},
    {"step":"Optional patches", "group":"Patches", "plugins":[]}
  ]
}
```

config is archive-relative, including any wrapper directory. XML source paths are relative to the
directory containing fomod/. Selection names match the XML step/group/plugin names exactly. Every
visible group needs an explicit selection, including an empty plugins list for no optional choices.
Required and SelectAll options must be included explicitly. Unknown, hidden, unavailable, duplicate
or missing selections and group-count violations fail preparation. There are no inferred defaults
or player-specific choices. Change the manifest version and review it again to change the selection.
The review panel shows the pinned choices without downloading the archive. Full XML/choice validation
occurs during preparation, after its SHA-256 is verified.

This candidate supports required files/folders, all five selection-group types, static and flag-based
option types, step visibility, condition flags, nested And/Or flag dependencies, conditional file
installs, alwaysInstall, installIfUsable and signed 32-bit file priorities. Step/group/option ordering
obeys Explicit/Ascending/Descending (default Ascending). Higher priority installs last; equal-priority
ties follow the resulting directive order (required files, ordered selected steps, conditional files).
Flags from selected options become available to later steps. Conflicting flag values within one
step are rejected. alwaysInstall/installIfUsable apply to options in visible steps. Display metadata
is accepted without fetching images or schemas.

The installer builds a new output tree containing only the chosen data files. dataDirs must be ["."];
ordinary actions, if supplied, run afterward relative to that output. The source archive stays intact.
The config path, canonical selections and interpreter version participate in the derived-cache key.
An optional folder cannot leak into the game merely because it was present in the downloaded archive.
FOMOD file paths accept either slash style, but cannot escape their package or use launcher metadata.
Sources must be nonempty relative paths; destination="" installs a folder's contents at the data root
or a file by its basename. An omitted destination retains the source path. Source files must belong
to the verified archive receipt. Expansion is bounded by 100,000 output entries and 8 GiB, including
overwritten copies. XML is limited to 4 MiB, 32 nesting levels and 100,000 reader nodes; DTDs, external
entities, processing instructions and namespace extensions are rejected.

This is a supported subset, not a general FOMOD wizard. Installed-file/plugin state, game/manager/
script-extender version dependencies, C#/ModScript installers, nested archive installers and other
unknown XML constructs are rejected, even in unselected branches. Use curated dataDirs/actions for
those packages until support is added. No installer script, external XML resource or downloaded
program runs. XML semantics were checked against the
[Nexus XML installer schema](https://github.com/Nexus-Mods/Nexus-Mod-Manager/blob/master/Script%20Types/XmlScript/Schemas/XmlScript5.0.xsd)
and [Nexus's FOMOD interface documentation](https://nexus-mods.github.io/NexusMods.App/developers/misc/AboutFomod/).

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

The master URL is https://master.fetchers.org/v1/servers. An advertised `mod_profile_sha256`
selects automatic discovery using the launcher's constructed master retrieval URL; server-supplied
manifest URLs are not followed in this route. Selection and Play/Prepare revalidate requirements
and bind the connection to the registered endpoint. Legacy entries with only `mod_profile_url`
(or `profile_url`) can offer Review server mods; that manual manifest must match the listed endpoint.
Built-in profile settings are retained when the matching server adopts automatic discovery.

Production requests require HTTPS and validate each redirect; local/private/reserved address
resolution and embedded URL credentials are rejected. Bounded extraction rejects path escapes,
symlinks and collisions. Activated server mod payloads cannot include Windows programs or installer scripts.
These controls do not certify game content as harmless: OpenMW Lua and assets retain the engine's
mod trust and sandbox limits. A broader untrusted-server review remains a public-rollout requirement.

Local HTTP and fake Nexus API fixtures require both --request-file and --allow-local-fixtures.
The optional --nexus-fixture-url is valid only in that mode. These flags never persist in profiles
and never enable local downloads in the GUI. Native-client/tools updates use their separate update
channel; a server manifest cannot replace the executable or launcher.

Public Vanilla now uses the combined surf/vehicle/Studio content and automatically advertised
owner profile. The older candidate examples retain their original identities; they are not a
promise that every example matches the live server. A new server still needs matching authority
and compatible Studio native/Lua versions. Asset installation alone does not enable Studio gameplay.
