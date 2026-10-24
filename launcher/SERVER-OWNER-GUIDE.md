# Server setup and launcher mod lists

Use the engine's [dedicated-server setup guide](https://github.com/Fetcher-Simulator/Fetcher-Simulator/blob/2fdbcfa5a76b7323e3b65f42d4de65939669f8d5/files/server/server-setup-guide.md)
for the server distribution, UDP firewall/NAT, `server.cfg`, server scripts and generating
`content-authority/` from a known-good client. This guide adds the launcher-facing mod list.
The complete schema and installation limits are in [PROFILE-FORMAT.md](PROFILE-FORMAT.md).

You need compatible client/server builds with launcher-profile support. That support first
shipped in engine commit `0f1aa3c5c5` / tag `Fetcher-Server-Profiles-1`. The current Fetcher
master and launcher support it. Older server binaries cannot advertise the new field.
Profiles over the original 256 KiB cap require the newer 2 MiB-capable launcher, server and
master together; updating the launcher alone does not raise an older server/master's limit.
Current Tamriel Data also needs the headless localization/default-settings fixes in
`Fetcher-Server-Profiles-4`.

When generating authority from a prepared launcher profile, include the matching client's
`fetcher-canonical-fallbacks.cfg` before preparation. Do not construct a reference fixture from
only its mod directories: missing fallbacks can change Lua-created game settings and reject
an otherwise byte-identical client. The deployed example is
[presets/fetcher-simulation.json](presets/fetcher-simulation.json), specific to the Fetcher server.

## 1. Decide exactly what players need

Start with a working reference client. Record its required plugins in their exact load order,
the original archive for each mod, the selected optional folders/installer choices and any
compatibility changes. Texture/sound replacements need downloads even if they have no plugin.

An installed `openmw.cfg` or server content fingerprint does not contain the Nexus download
identities. There is no general single-player-modlist-to-UMO exporter in the launcher yet.
Use an existing UMO JSON list if you have one; otherwise write the entries below by hand.
The same requirements can also be expressed directly with `packages`, without using UMO.

## 2. Create or curate the UMO JSON array

A UMO list is an array of mod entries. The server publishes that array inside a larger
launcher profile as `umoMods`. See the complete, pinned
[one-mod example](examples/community-umo.json), which uses the Hackle-Lo Pipes archive from
our earlier Nexus test. It is an example server, not an instruction to add the mod to Vanilla.

For each entry supply:

| Field | What to enter |
| --- | --- |
| `name` | A readable mod name. |
| `url` | The canonical Nexus mod page, such as `https://www.nexusmods.com/morrowind/mods/56939`. |
| `dir` | A stable relative name for the mod, such as `HackleLoPipes`. |
| `download_info` | One object per required original archive. |
| `plugins` | Enabled plugin/script-list filenames from this mod; use `[]` for asset-only mods. |

For **each download** inside `download_info`:

| Field | What to enter |
| --- | --- |
| `file_name` | A readable label for the selected file. |
| `extract_to` | A relative extraction prefix, such as `HackleLoPipes`. |
| `pinned` | `true`; an automatically changing "latest" file is not accepted. |
| `nexus_file_id` | The positive numeric ID of the exact Nexus file. This is different from the mod-page ID. |
| `sha256` | The SHA-256 of the original downloaded ZIP/7z/RAR bytes. |
| `dataDirs` | Archive-relative directories containing the game data, in priority order. |
| `archiveFormat` | Optional: `auto`, `zip`, `7z` or `rar`. |

Choose the exact version on the mod's Nexus Files page. Its download link identifies the
file with `file_id`; the mod page identifies the mod with `/mods/<modId>`. If you have an
existing pinned UMO list/cache, use its corroborated exact file identity. Do not guess the
file ID from the filename or reuse a temporary signed CDN download URL.

Download that original archive and calculate its hash:

```powershell
(Get-FileHash -LiteralPath 'C:\ModArchives\ChosenMod.zip' -Algorithm SHA256).Hash.ToLowerInvariant()
```

Inspect the archive before setting `dataDirs`: use `["."]` when it contains `Meshes`,
`Textures`, plugins, etc. at its root; use `["Data Files"]` when those files are inside a
`Data Files` folder. For a wrapper use its real path, e.g. `["Mod v2/Data Files"]`.
These paths are inside the archive, not paths on your PC. Multi-archive mods need explicit
`dataDirs` on every download. Include required patches and choose their priority deliberately.

For non-Nexus archives, put a permanent public HTTPS archive URL in `direct_download` on
the download object instead of `nexus_file_id`. Keep `pinned: true`, the exact SHA-256 and
the correct `dataDirs`. Use a host that permits distribution of the files. A webpage is
not an archive URL; executable installers and arbitrary shell commands are not supported.

Existing UMO exports may lack hashes, pin `latest`, contain obsolete file IDs or require
unsupported installer actions. They need curation before publication. Folder operations
and supported FOMOD selections are documented in [PROFILE-FORMAT.md](PROFILE-FORMAT.md).
The cache audit in [PRESET-AUTHORING.md](PRESET-AUTHORING.md) is a developer aid for the
existing Fetcher cache layout, not a generic one-click exporter or a ready server manifest.

## 3. Wrap the list in `launcher-profile.json`

Copy [examples/community-umo.json](examples/community-umo.json) and edit the server identity,
version, embedded mods and `content` order. Alternatively, if your curated array is saved as
`umo-mods.json`, create a profile with PowerShell:

```powershell
$serverMods = @(Get-Content -LiteralPath '.\umo-mods.json' -Raw |
    ConvertFrom-Json | ForEach-Object { $_ })
$serverProfile = [ordered]@{
    schemaVersion = 1
    id = 'my-community'
    name = 'My community server'
    version = '1.0'
    host = 'play.example.org'
    port = 25565
    umoMods = $serverMods
    content = @('Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm', 'Hackle-Lo Pipe.esp')
}
$profileJson = $serverProfile | ConvertTo-Json -Depth 32 -Compress
[IO.File]::WriteAllText(
    (Join-Path (Get-Location) 'launcher-profile.json'),
    $profileJson,
    [Text.UTF8Encoding]::new($false)
)
```

Replace the example `content` array with the exact required order from your reference client,
including required `.omwscripts` entries. Keep groundcover in a separate `groundcover` array.
Keep the owner `id` stable and change `version` when requirements change.

Automatic discovery requires an **embedded** `umoMods` array and/or `packages`. The older
`"umo": {"url": ..., "sha256": ...}` form is for manual imports only and is refused here.
Profiles are limited to **2 MiB of UTF-8** and depth 32. Minify large lists as above and
remove unused export metadata. Never include credentials, local account files or temporary
authorised download URLs: the profile is public.

## 4. Put it beside the server configuration

Example layout:

```text
my-server/
  server.cfg
  launcher-profile.json
  content-authority/
    openmw.cfg
```

Add this to the existing `[master]` section of `server.cfg`:

```ini
[master]
public_listing = true
server_name = My community server
game_mode = Co-op
launcher_profile = launcher-profile.json
```

The filename is resolved relative to **the actual `server.cfg` in use**, including when
you start the server with `--config`. An absolute profile path also works. Keep the
distribution's compatible `master_url`. Restart the server after changing the JSON/config.

The dedicated server uploads the local document during registration. The master stores it
and advertises its fingerprint; the launcher obtains it from the trusted master. You do
not need a separate website or to ask players to paste a URL. The observed server address
and registered port determine the connection, overriding the document's host/port.

## 5. Test downloads and the actual join

Prepare the same required content on the server using the engine setup guide. The launcher
profile describes **where clients obtain files**; it does not install server authority or
replace the server's content verification. Publishing JSON alone does not add a mod to the server.

After restart, check the server log for profile/registration errors. Its entry in
`https://master.fetchers.org/v1/servers` should include `mod_profile_sha256`. In a separate
test installation, select the server, set your owned Morrowind Data Files folder and press
Play. Confirm downloads, generated load order and a real join before announcing the setup.

Nexus users may need to connect the launcher's UMO helper and approve browser downloads for
their own accounts. Automatic discovery removes the mod-list handoff, not Nexus account or
download requirements. Exact-file manual import remains available. Nobody should send an
API key to the server owner or put one in a profile.

If a join fails with a content mismatch, compare the reference client's effective files,
archive versions, selected options, data-directory priority and the server authority.
Do not change hashes to match arbitrary downloaded or modified bytes. For updates, test the
new profile and matching authority together, retain the prior versions, then restart for
the coordinated cutover. For an actual rollback, republish the prior profile **and** restore
its matching server content; an old locally cached profile cannot override live requirements.
