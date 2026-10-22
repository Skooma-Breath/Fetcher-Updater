[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$InstallRoot,
    [Parameter(Mandatory=$true)][string]$FixtureDir,
    [Parameter(Mandatory=$true)][string]$EvidenceRoot,
    [string]$GameData='C:\GOG Games\Morrowind\Data Files',
    [ValidateRange(1024,65535)][int]$Port=25570
)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
Set-StrictMode -Version Latest
if($Port -in @(25567,25568)) {throw 'Preserve the established Synth fixtures.'}
$install=(Resolve-Path -LiteralPath $InstallRoot).Path
$fixture=(Resolve-Path -LiteralPath $FixtureDir).Path
$evidence=[IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\')
if(Test-Path -LiteralPath $evidence) {throw 'Use a new dedicated evidence directory.'}
$launcher=Join-Path $install 'FetcherLauncher.exe'
$client=Join-Path $install 'openmw.exe'
if(@(Get-CimInstance Win32_Process -Filter "Name='openmw.exe'" | Where-Object ExecutablePath -eq $client).Count) {
    throw 'This test installation already has a running game.'
}
if(@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue).Count) {throw 'Test port is occupied.'}
New-Item -ItemType Directory -Path $evidence | Out-Null
function Json-File([string]$path,$value) {
    [IO.File]::WriteAllText($path,($value | ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
}
function Quote-Native([string]$value) {
    if($value.Contains('"') -or $value.Contains("`n") -or $value.EndsWith('\')) {throw 'Unsupported test argument.'}
    return '"'+$value+'"'
}
function Invoke-Launcher($request,[string]$name) {
    $requestFile=Join-Path $evidence "$name.request.json"
    $resultFile=Join-Path $evidence "$name.result.json"
    Json-File $requestFile $request
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$launcher
    $start.WorkingDirectory=$install
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.Arguments=(@('--install-root',$install,'--request-file',$requestFile,'--log-file',$resultFile) | ForEach-Object {Quote-Native $_}) -join ' '
    $process=[Diagnostics.Process]::Start($start)
    try {
        if(-not $process.WaitForExit(30000)) {$process.Kill();$process.WaitForExit();throw "Launcher $name timed out."}
        if($process.ExitCode -ne 0) {throw "Launcher $name failed; inspect $resultFile"}
    } finally {$process.Dispose()}
    return Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json
}
function Hash-Masters {
    $hashes=[ordered]@{}
    foreach($name in @('Morrowind.esm','Tribunal.esm','Bloodmoon.esm')) {
        $hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $GameData $name)).Hash
    }
    return $hashes
}
function Component([string]$value) {
    $slug=[regex]::Replace([regex]::Replace($value,'[^A-Za-z0-9_.-]','_'),'_+','_')
    if($slug -eq $value) {return $slug}
    $sha=[Security.Cryptography.SHA256]::Create()
    try {$hex=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($value)))).Replace('-','').ToLowerInvariant()}
    finally {$sha.Dispose()}
    return $slug+'-'+$hex.Substring(0,12)
}
$expected=Get-Content -LiteralPath (Join-Path $fixture 'expected.json') -Raw | ConvertFrom-Json
foreach($entry in $expected.files) {
    if($entry.name -notmatch '^[a-zA-Z0-9_./-]+$' -or $entry.name.Split('/') -contains '..') {throw 'Invalid fixture inventory path.'}
    if((Get-FileHash -LiteralPath (Join-Path $fixture $entry.name)).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Transferred fixture changed: $($entry.name)"
    }
}
$clientHash=(Get-FileHash -LiteralPath $client).Hash.ToLowerInvariant()
if($clientHash -ne $expected.clientSha256) {throw 'Server fixture does not match the installed client build.'}
$tools=Get-Content -LiteralPath (Join-Path $install 'fetcher-tester-tools.json') -Raw | ConvertFrom-Json
$beforeMasters=Hash-Masters
$baseHash=(Get-FileHash -LiteralPath (Join-Path $install 'openmw.cfg')).Hash
$report=[ordered]@{computer=$env:COMPUTERNAME;sourceCommit=$tools.sourceCommit
    clientSha256=$clientHash;serverSha256=(Get-FileHash -LiteralPath (Join-Path $fixture 'openmw-server.exe')).Hash.ToLowerInvariant()
    serverPort=$Port;publicDeployment=$false;inputAutomation=$false;checks=[ordered]@{}}
$server=$null
$identityRoot=$null
$taskName='Fetcher-World-Acceptance-'+[Guid]::NewGuid().ToString('N')
$registered=$false
try {
    $state=Invoke-Launcher @{op='init'} 'initial'
    if(-not $state.setup.profilePrepared -or -not $state.setup.gameDataReady) {throw 'Prepare the installed acceptance profile first.'}
    if($state.host -ne '127.0.0.1' -or $state.port -ne $Port) {throw 'Select the explicit loopback acceptance profile/port first.'}
    $profile=Join-Path $install ('_fetcher_launcher/profiles/'+$state.selectedProfile)
    $profileConfig=Join-Path $profile 'openmw.cfg'
    $configText=Get-Content -LiteralPath $profileConfig -Raw
    foreach($content in @('surf_mesa_mw.omwaddon','FetcherVehicles.omwaddon')) {
        if(-not $configText.Contains('content='+$content)) {throw "Missing prepared content: $content"}
    }
    # Only this previously-created acceptance installation is used. No public profile is edited.
    $null=Invoke-Launcher @{op='identity';account='ass';character='Synth A';autoEnter=$true} 'identity'
    $serverRoot=Join-Path $evidence 'server'
    New-Item -ItemType Directory -Path $serverRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $fixture 'server-scripts'),(Join-Path $fixture 'server-lua-packages') -Destination $serverRoot -Recurse
    Copy-Item -LiteralPath (Join-Path $fixture 'playerdata.db') -Destination $serverRoot
    # The server searches its working directory for internal Lua libraries. A developer
    # machine's compiled-in source fallback must not conceal a missing deployed runtime.
    $luaLibraries=Join-Path $install 'resources/lua_libs'
    if(-not(Test-Path -LiteralPath $luaLibraries -PathType Container)) {throw 'Installed runtime is missing internal Lua libraries.'}
    New-Item -ItemType Directory -Path (Join-Path $serverRoot 'resources') | Out-Null
    Copy-Item -LiteralPath $luaLibraries -Destination (Join-Path $serverRoot 'resources') -Recurse
    $logs=Join-Path $serverRoot 'logs'
    New-Item -ItemType Directory -Path $logs | Out-Null
    $cfg="[server]`nport=$Port`nmax_players=2`ndb_path=$($serverRoot.Replace('\','/'))/playerdata.db`n[content]`nopenmw_cfg=$($profileConfig.Replace('\','/'))`nresources=$($install.Replace('\','/'))/resources`nencoding=win1252`nverify_determinism=true`n[client_lua]`npackages=$($serverRoot.Replace('\','/'))/server-lua-packages`n[master]`npublic_listing=false`nserver_name=PillowPC isolated world acceptance`n"
    [IO.File]::WriteAllText((Join-Path $serverRoot 'server.cfg'),$cfg,[Text.UTF8Encoding]::new($false))
    $keys=Join-Path $install 'mp-keys'
    New-Item -ItemType Directory -Path $keys -Force | Out-Null
    $key=Join-Path $keys "127.0.0.1_$Port.key"
    if(Test-Path -LiteralPath $key) {
        if((Get-FileHash -LiteralPath $key).Hash -ne (Get-FileHash -LiteralPath (Join-Path $fixture 'identity.key')).Hash) {
            throw 'Existing test endpoint has a different identity key; it will not be replaced.'
        }
    } else {Copy-Item -LiteralPath (Join-Path $fixture 'identity.key') -Destination $key}
    $identityRoot=Join-Path $install ('profiles/'+(Component "127.0.0.1:$Port")+'/ass/characters/'+(Component 'Synth A'))
    $report.reusedTestIdentityState=Test-Path -LiteralPath $identityRoot
    if(-not $report.reusedTestIdentityState) {
        New-Item -ItemType Directory -Path (Join-Path $identityRoot 'userdata') -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $identityRoot 'userdata/settings.cfg'),"[Video]`nresolution x = 1024`nresolution y = 768`nfullscreen = false`n[Sound]`nmaster volume = 0`nmusic volume = 0`n",[Text.UTF8Encoding]::new($false))
    }
    $stdout=Join-Path $serverRoot 'stdout.log'
    $stderr=Join-Path $serverRoot 'stderr.log'
    # Child-only DLL search path: the installed client and this server are from the same runtime.
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=Join-Path $fixture 'openmw-server.exe'
    $start.WorkingDirectory=$serverRoot
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.EnvironmentVariables['PATH']=$install+';'+$env:PATH
    $start.Arguments=(@('--config',(Join-Path $serverRoot 'server.cfg'),'--log-dir',$logs) | ForEach-Object {Quote-Native $_}) -join ' '
    # Redirect through files using Start-Process so native server output cannot fill a pipe.
    $oldPath=$env:PATH
    try {
        $env:PATH=$start.EnvironmentVariables['PATH']
        $server=Start-Process -FilePath $start.FileName -ArgumentList $start.Arguments -WorkingDirectory $start.WorkingDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    } finally {$env:PATH=$oldPath}
    $deadline=[DateTime]::UtcNow.AddSeconds(90)
    while($true) {
        if($server.HasExited) {throw "Staged server exited $($server.ExitCode); inspect retained logs."}
        $text=Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue
        if($text -and $text.Contains('[LuaServerContext] Startup failed:')) {throw 'Staged server Lua failed to initialize; inspect retained logs.'}
        if($text -and $text.Contains('[Server] Listening on port') -and $text.Contains('Loaded 2 global server Lua script(s)')) {break}
        if([DateTime]::UtcNow -gt $deadline) {throw 'Staged server readiness timed out.'}
        Start-Sleep -Milliseconds 250
    }
    $report.checks.stagedServerReady=$true
    $report.checks.serverLuaLoaded=$true
    Write-Output 'Launching through the installed native launcher in the signed-in desktop...'
    $request=Join-Path $evidence 'launch.request.json'
    $result=Join-Path $evidence 'launch.result.json'
    Json-File $request @{op='launch'}
    $arguments=(@('--install-root',$install,'--request-file',$request,'--log-file',$result) | ForEach-Object {Quote-Native $_}) -join ' '
    $action=New-ScheduledTaskAction -Execute $launcher -Argument $arguments -WorkingDirectory $install
    $principal=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
    $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 5) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings | Out-Null
    $registered=$true
    Start-ScheduledTask -TaskName $taskName
    $deadline=[DateTime]::UtcNow.AddSeconds(150)
    while($true) {
        if($server.HasExited) {throw 'Staged server exited during game join.'}
        $text=Get-Content -LiteralPath $stdout -Raw
        $clientLog=Join-Path $identityRoot 'logs/openmw.log'
        $clientText=if(Test-Path -LiteralPath $clientLog) {Get-Content -LiteralPath $clientLog -Raw}else{''}
        if($text.Contains('[core] Connect: Synth A') -and
            $text.Contains('ActorList established complete cell roster from=Synth A') -and
            $clientText -and $clientText.Contains('[ServerLuaPackages] Staged generation=') -and
            $clientText.Contains('WorldObjectSync: placed refId=fetcher_procedural_synth_v1')) {break}
        if([DateTime]::UtcNow -gt $deadline) {throw 'Game join timed out; inspect retained server and client logs.'}
        Start-Sleep -Milliseconds 250
    }
    if(-not(Test-Path -LiteralPath $result)) {throw 'Native launch command did not report completion.'}
    $launchResult=Get-Content -LiteralPath $result -Raw | ConvertFrom-Json
    if($launchResult.PSObject.Properties['error']) {throw 'Native launch command reported an error.'}
    $clientLog=Join-Path $identityRoot 'logs/openmw.log'
    $clientText=Get-Content -LiteralPath $clientLog -Raw
    $renderer=[regex]::Match($clientText,'OpenGL Renderer: ([^\r\n]+)').Groups[1].Value
    if(-not $renderer) {throw 'Client did not report a rendering context.'}
    if(-not $clientText.Contains('Revision: '+$expected.runtimeRevision)) {throw 'Unexpected game runtime revision.'}
    foreach($content in @('surf_mesa_mw.omwaddon','FetcherVehicles.omwaddon')) {
        if(-not $clientText.Contains('Loading content file '+$content)) {throw "Client did not load $content"}
    }
    if($clientText -match 'Failed to load.*keyboard-stand|Failed to open.*(surf_|FetcherVehicles)') {throw 'Combined content failed to load.'}
    $report.renderer=$renderer
    $report.checks.nativeLauncherJoinedWorld=$true
    $report.checks.worldRosterReceived=$true
    $report.checks.rendererInitialized=$true
    $report.checks.combinedConfigUsed=$true
    $report.checks.serverPackagesStaged=$true
    $report.checks.studioObjectReceived=$true
    $afterMasters=Hash-Masters
    foreach($name in $beforeMasters.Keys) {if($beforeMasters[$name] -ne $afterMasters[$name]) {throw "Owned master changed: $name"}}
    if((Get-FileHash -LiteralPath (Join-Path $install 'openmw.cfg')).Hash -ne $baseHash) {throw 'Base configuration changed.'}
    $report.checks.ownedMastersPreserved=$true
    $report.checks.baseConfigPreserved=$true
    $report.ok=$true
} catch {
    $report.ok=$false
    $report.error=$_.Exception.Message
} finally {
    if($registered) {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    }
    $clients=@(Get-CimInstance Win32_Process -Filter "Name='openmw.exe'" | Where-Object ExecutablePath -eq $client)
    foreach($owned in $clients) {
        $process=Get-Process -Id $owned.ProcessId -ErrorAction SilentlyContinue
        if($process -and $process.Path -eq $client) {Stop-Process -Id $process.Id -Force}
    }
    if($server) {if(-not $server.HasExited) {$server.Kill();$server.WaitForExit()};$server.Dispose()}
    if($identityRoot) {
        $clientLog=Join-Path $identityRoot 'logs/openmw.log'
        if(Test-Path -LiteralPath $clientLog) {Copy-Item -LiteralPath $clientLog -Destination (Join-Path $evidence 'openmw.log')}
    }
    Json-File (Join-Path $evidence 'world-report.json') $report
    $report | ConvertTo-Json -Depth 8
}
if(-not $report.ok) {exit 1}
