[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ServerRuntime,
    [Parameter(Mandatory=$true)][string]$ServerScripts,
    [Parameter(Mandatory=$true)][string]$LuaPackages,
    [Parameter(Mandatory=$true)][string]$SeedDatabase,
    [Parameter(Mandatory=$true)][string]$IdentityKey,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{10,40}$')][string]$RuntimeRevision,
    [Parameter(Mandatory=$true)][string]$OutputDir
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$runtime=(Resolve-Path -LiteralPath $ServerRuntime).Path
$scripts=(Resolve-Path -LiteralPath $ServerScripts).Path
$packages=(Resolve-Path -LiteralPath $LuaPackages).Path
$seed=(Resolve-Path -LiteralPath $SeedDatabase).Path
$key=(Resolve-Path -LiteralPath $IdentityKey).Path
$outputPath=[IO.Path]::GetFullPath($OutputDir).TrimEnd('\')
if(Test-Path -LiteralPath $outputPath) {throw 'Use a new private fixture directory; existing files are never replaced.'}
foreach($name in @('openmw.exe','openmw-server.exe','defaults.bin')) {
    if(-not(Test-Path -LiteralPath (Join-Path $runtime $name) -PathType Leaf)) {throw "Incomplete matching runtime: $name"}
}
if(-not(Test-Path -LiteralPath (Join-Path $scripts 'server.omwscripts') -PathType Leaf)) {throw 'Server Lua manifest is missing.'}
if((Test-Path -LiteralPath ($seed+'-wal')) -or (Test-Path -LiteralPath ($seed+'-journal'))) {
    throw 'Use a closed, freshly seeded test database, not a live database.'
}
$seedHash=(Get-FileHash -LiteralPath $seed).Hash
New-Item -ItemType Directory -Path $outputPath | Out-Null
Copy-Item -LiteralPath (Join-Path $runtime 'openmw-server.exe'),(Join-Path $runtime 'defaults.bin') -Destination $outputPath
New-Item -ItemType Directory -Path (Join-Path $outputPath 'server-scripts'),(Join-Path $outputPath 'server-lua-packages') | Out-Null
Get-ChildItem -LiteralPath $scripts -Force | Copy-Item -Destination (Join-Path $outputPath 'server-scripts') -Recurse
Get-ChildItem -LiteralPath $packages -Force | Copy-Item -Destination (Join-Path $outputPath 'server-lua-packages') -Recurse
Copy-Item -LiteralPath $seed -Destination (Join-Path $outputPath 'playerdata.db')
Copy-Item -LiteralPath $key -Destination (Join-Path $outputPath 'identity.key')
$files=@(Get-ChildItem -LiteralPath $outputPath -File -Recurse | ForEach-Object {
    @{name=$_.FullName.Substring($outputPath.Length+1).Replace('\','/')
      sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
})
if((Get-FileHash -LiteralPath $seed).Hash -ne $seedHash) {throw 'Source seed changed while building the fixture.'}
$expected=@{clientSha256=(Get-FileHash -LiteralPath (Join-Path $runtime 'openmw.exe')).Hash.ToLowerInvariant()
    runtimeRevision=$RuntimeRevision;sourceDatabasePreserved=$true;files=$files}
[IO.File]::WriteAllText((Join-Path $outputPath 'expected.json'),($expected|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
Write-Output "Created private world fixture with $($files.Count) verified files. It contains a test identity key; keep it out of release packages."
