# Server-owned mod lists: planned design

Status: design for the next implementation slice, not a claim of native Nexus/UMO support today.
The current launcher already imports HTTPS profiles, installs verified ZIP packages, and generates
isolated load orders. Its native backend does not yet import arbitrary UMO lists or authenticate to Nexus.

## Operator and player workflow

Any operator publishes an HTTPS server manifest and shares its URL. An import must preview the server
endpoint, manifest identity/version, mod names/authors, exact files, download origins and total known
size before installation. Selecting a server must not itself install its mods. Later, the master/server
protocol can advertise the same manifest URL; importing a URL remains available without that protocol
change. A server name is display text and cannot establish ownership of an endpoint or profile.

The launcher resolves the manifest into a pinned installation plan, fetches missing archives, verifies
hashes, installs in its managed cache, generates that profile's configuration, and joins the selected
endpoint. Switching servers selects their own configuration. Existing Fetcher presets become ordinary
profiles rather than privileged paths through the installer. Shared archives can reuse verified cache
content. Distinct profiles must remain isolated even when operators choose identical friendly IDs.

## One manifest, two download sources

- Nexus entries identify the game domain, mod ID and exact file ID, expected archive SHA-256,
  display name/version, author/source page, selected data directories and enabled plugin files.
  Resolve download URLs through the user's own authenticated Nexus access at install time; never
  publish API keys, NXM access keys, or expiring CDN links in the shared manifest.
- External entries identify an HTTPS archive URL, SHA-256 and the same data/layout information.
  Allow operator-hosted archives, GitHub releases and author-hosted files. Preserve attribution and
  distribution permissions; Nexus files need not be rehosted by server owners.
- The manifest explicitly controls data-directory priority and plugin load order. Validate masters
  against that order. Native game binaries and launcher updates have a separate trusted update path;
  a server manifest cannot replace them.

UMO is an import format, not an execution engine. Normalize its `url`, `download_info`,
`nexus_file_id`, `data_paths` and `plugins` fields into the native plan. Legacy unpinned/missing file
IDs and hashes need operator resolution before publication, rather than silently choosing the latest
version. Show unsupported actions/layouts with actionable errors. Only specifically implemented,
validated declarative transformations may be supported; never run arbitrary commands or installers.
Ordinary mod archives must not need Fetcher's custom package manifest inside them: layout belongs
in the server manifest. Initially reject unsupported archive formats explicitly; add bounded native
extraction for the formats actually used by representative UMO lists before claiming general parity.

## Download and trust boundaries

Nexus download eligibility is controlled by Nexus. Support an authenticated automatic route where
allowed and a guided website/NXM or manual archive import route where required. Keep user credentials
in Windows-protected local storage, redact them from logs and never send them to server operators or
external archive hosts. Follow Nexus registration and application-identification requirements before
a broad public release. Do not impersonate UMO or share a hoster's credentials among players.

Review downloads once per installation plan and again when sources, content permissions or pinned
requirements change. Display source domains and changed files. Hashes pin expected bytes; they do
not prove that an author or operator is trustworthy. A signature can establish manifest continuity,
but cannot establish that a mod is harmless.

Enforce HTTPS including redirects, reject credentials in URLs, and restrict remote profiles from
using loopback/private/local-service download destinations. Keep loopback fixtures explicitly scoped
to test use. Bound downloads, expanded sizes and entry counts; reject path escapes, links and collisions.
Never load server-provided HTML descriptions as trusted markup. Install only allowed game content
into managed paths, with no executable/DLL/shortcut/script-installer launch support. OpenMW Lua is
intentional game content and still carries the engine's mod trust and sandbox limits; an archive hash
is not a security audit of its Lua, meshes or plugins.

Stage and validate before activation. Keep the previous working profile/version available for
rollback, and detect partial installs. Server manifests cannot change the user's main configuration,
security settings, native runtime or launcher. Broad claims of safe untrusted-server installation
must wait for a dedicated review of extraction, network redirection and game-content boundaries.

## Acceptance targets for the implementation

1. Two unrelated operators can import UMO-based lists, each with Nexus and external content, without
   editing Fetcher source or overwriting one another's profiles/settings.
2. Exact pinned files/layouts reproduce the same validated data and plugin order; missing/changed
   archives and unsupported UMO actions fail before activation, preserving the previous setup.
3. Exercise authenticated Nexus behavior with eligible accounts and the guided/manual fallback;
   use fixtures for automated tests without embedding real credentials.
4. Test hostile profiles/archives, redirect downgrade/private destinations, credential redaction,
   interrupted downloads, cache repair and rollback. Review trust UI with a changed manifest.
5. Demonstrate a clean client joining a matching staged server from its published manifest before
   general rollout. Native UMO/Nexus support remains marked incomplete until this works end to end.
