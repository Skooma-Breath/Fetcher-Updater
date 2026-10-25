Fetcher Simulator Experimental
===============================

New installation
----------------

Download the complete client ZIP (includes FetcherLauncher.exe):

   https://github.com/Fetcher-Simulator/Fetcher-Simulator/releases/download/Fetcher-Simulator-Experimental/fetcher-simulator.zip

1. Extract it to a folder such as C:\Games\FetcherSimulator.
2. Open FetcherLauncher.exe.
3. Under Mods, select your owned Morrowind Data Files folder and click Save folder.
4. Select Fetcher Vanilla or Fetcher Simulator in the server browser.
5. Enter your server account name and click Play. Required mods and load order are prepared automatically.
6. Follow any UMO/Nexus sign-in prompts. Free accounts may need Slow download approval.

Morrowind with Tribunal and Bloodmoon is required. The Setup wizard is optional.
FetcherSetup.exe is an alternative downloader for the same client and tools.

Updating an existing install
----------------------------

Close the game and choose Updates > Install updates in FetcherLauncher.exe.
Launcher/tools updates switch older Vehicles/Studio installations to the
Fetcher-Simulator-Experimental client release before checking the client.
Settings, accounts and character files are preserved.

Update-Fetcher-Simulator.bat remains available as a legacy fallback.
Client releases belong to Fetcher-Simulator/Fetcher-Simulator; launcher/tools
releases belong to Skooma-Breath/Fetcher-Updater.

Client files are staged, hash-verified, and installed with rollback. The updater
does not overwrite openmw.cfg, settings.cfg, userdata, saves, screenshots, logs,
mp-keys, UMO downloads/configuration, databases, custom server MIDI files, or
other files that are not owned by the Fetcher release inventory.

Do not move only the updater BAT into another OpenMW install. Run it from the
root of the Fetcher Simulator release beside openmw.exe.

Character-isolated multiplayer launch
-------------------------------------

To keep settings, Lua storage, logs, and saves separate for each multiplayer
character, double-click:

   Launch-Fetcher-Character.bat

Enter the linked account and character names when prompted. Mutable files are
stored under profiles\<server>\<account>\characters\<character>. Account keys
are shared only by characters on the same account and server. This allows
multiple clients from one install when each process uses a different character.
Character-bound Lua state is stored under multiplayer-characters so it remains
the same if the client is later opened directly. The updater preserves both
directories.

UMO install path
----------------

UMO can help download and extract Nexus mods for OpenMW.

UMO page: https://modding-openmw.com/mods/umo/
UMO source: https://gitlab.com/modding-openmw/umo

Basic UMO steps:

Fast path:

1. Double-click:

   Install-Fetcher-Bardcraft-With-UMO.bat

   Or run this one-liner from this folder:

   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install-Fetcher-Bardcraft-With-UMO.ps1

The helper downloads umo.exe and tes3cmd.exe if they are not already present.
It also downloads a portable copy of the official 7-Zip command-line tools when
7-Zip is unavailable. Nothing is installed system-wide. The helper then uses the
included fetcher-simulator-umo.json modlist. If that file is missing, it tries to
download it from the Fetcher Simulator GitHub prerelease.

The updater also applies its own hash-gated Lua compatibility deltas after UMO
finishes. It preserves verified upstream backups under _fetcher_update and
refuses unknown versions or locally edited files instead of overwriting them.

After UMO installs the Nexus version of Bardcraft, the helper downloads the
small Fetcher Bardcraft multiplayer compatibility patch from its own GitHub
prerelease. It reads the current asset checksum from GitHub, then verifies the
download and the installed Bardcraft scripts before applying anything.
Unsupported or locally modified Bardcraft versions are left unchanged and
reported as an error.

The helper tells UMO to install the mods inside this package under:

   Data Files\fetcher-simulator

On first run, UMO may open a Nexus login page in your browser. Finish that login
and return to the console. The helper also registers its portable umo.exe as the
current user's nxm:// handler so Nexus "Slow Download" buttons can return files
to the waiting installer. After UMO finishes, the helper rewrites openmw.cfg with
the needed data= and content= lines.

Large Nexus downloads can take several minutes and the console may look quiet
while UMO is still downloading. Leave the window open until it reports that the
Fetcher public test load order was applied or prints an error.

Manual UMO steps:

1. Download UMO for Windows.
2. Open Windows Terminal in the folder that contains umo.exe.
3. Run:

   .\umo.exe setup
   .\umo.exe check

4. Use the Nexus "Mod Manager Download" button for Bardcraft and each
   dependency. UMO should catch the nxm:// links after setup.
5. Let UMO download and extract the mods.

For manual UMO usage, set UMO's mod install path to this package's Data Files
folder if you want the install to stay self-contained. Then run
Apply-Fetcher-Public-Test-Config.bat after the mods are installed.

Manual install path
-------------------

If you do not use UMO:

1. Create a folder such as C:\OpenMWMods\Fetcher.
2. Download Tamriel Data, Skill Framework, Stats Window Extender, and Bardcraft
   from Nexus.
3. Extract each mod into its own folder.
4. Add data= lines in this package's openmw.cfg for:

   - your Morrowind Data Files folder
   - Tamriel Data
   - Skill Framework
   - Stats Window Extender
   - Bardcraft

OpenMW must be pointed at the folder that directly contains the mod files, not
at a parent folder. For Bardcraft, the correct folder contains:

   Bardcraft.ESP
   Bardcraft.omwscripts
   scripts\Bardcraft
   meshes\Bardcraft
   sound\Bardcraft
   midi\Bardcraft

Apply the public test load order
--------------------------------

After the mods are installed and their data folders are listed in openmw.cfg,
double-click:

   Apply-Fetcher-Public-Test-Config.bat

The BAT file rewrites the content= lines in this package's openmw.cfg to match
the public test server load order. It also creates a backup of the previous
openmw.cfg next to the original file.

If the BAT reports missing files, install that mod or add the correct data=
folder to openmw.cfg, then run the BAT again.

OpenMW animation settings
-------------------------

This package enables the required Bardcraft settings automatically in both
settings.cfg files:

   shield sheathing = true
   smooth animation transitions = true
   use additional anim sources = true
   weapon sheathing = true

Community songs
---------------

The public server does not distribute custom MIDI files through Bardcraft by
default. Each player must install the same local song pack. Bardcraft matches
the local song content hash when synchronizing multiplayer playback, so a file
with the same title but different notes will not be substituted.

If Bardcraft reports a missing local song, obtain the pack from the server
community and install its loose .mid/.midi files under:

   midi\Bardcraft\custom

OpenMW cannot discover songs that remain inside a ZIP/7Z archive. Do not rename
another MIDI file to match; Bardcraft validates the actual song content hash.

Hosting your own multiplayer server
----------------------------------

The Fetcher Simulator client/server release itself includes:

   Build-OpenMWServerAuthority.py
   server-setup-guide.md

These files ship with the OpenMW release and do not depend on the Fetcher updater.
The authority builder is generic to this OpenMW multiplayer server branch; it
does not contain a hard-coded Fetcher mod list. It reads a known-good client's
openmw.cfg, resolves that client's effective data= stack, and creates the
portable content-authority directory consumed by openmw-server.

Before building server authority, fully update the client that will be used as
the reference and make sure its load order is correct. Then, from this folder,
for example:

   python .\Build-OpenMWServerAuthority.py --client-root . --output C:\OpenMW-Server\content-authority

To inspect the reference client without creating files:

   python .\Build-OpenMWServerAuthority.py --client-root . --plan-only

The builder copies authoritative files byte-for-byte. It does not normalize Lua
encoding, UTF-8 BOMs, or CRLF/LF line endings. The server hashes those exact VFS
bytes at startup, so byte-identical client/server content is required.

The generated bundle contains content-authority\openmw.cfg,
content-authority\content-data, and bundle-manifest.json. The JSON file is an
audit report; openmw-server does not load it as the handshake manifest. Instead,
the server reads [content] openmw_cfg from server.cfg, loads that OpenMW content
configuration, and generates its real content/Lua SHA-256 manifest in memory.

For the complete setup process, including UDP port forwarding, Windows/Linux
firewall rules, server.cfg, public listing, content-authority generation,
production deployment, updates, and mismatch troubleshooting, read:

   server-setup-guide.md

Troubleshooting
---------------

If Bardcraft's menu does not open with B:

   - Bardcraft.omwscripts is not enabled, or
   - the Bardcraft data folder is not listed in openmw.cfg.

If instruments or outfits are missing:

   - Bardcraft.ESP is not enabled, or
   - Tamriel_Data.esm is not enabled, or
   - the wrong data folder was added.

If animations do not play:

   - Use Additional Animation Sources is not enabled, or
   - Bardcraft.omwscripts is not loaded.
