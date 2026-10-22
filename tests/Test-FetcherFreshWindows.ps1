[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PayloadDir,
    [Parameter(Mandatory=$true)][string]$InstallRoot,
    [Parameter(Mandatory=$true)][string]$EvidenceRoot,
    [string]$GameData='C:\GOG Games\Morrowind\Data Files',
    [switch]$SkipUmo
)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
Set-StrictMode -Version Latest
$payload=(Resolve-Path -LiteralPath $PayloadDir).Path
$install=[IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
$evidence=[IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\')
if((Test-Path -LiteralPath $install) -or (Test-Path -LiteralPath $evidence)) {
    throw 'Use new dedicated installation/evidence folders; existing folders are never reset.'
}
if($install -eq [IO.Path]::GetPathRoot($install).TrimEnd('\')) {throw 'A drive root is not a test installation.'}
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$expected=Get-Content -LiteralPath (Join-Path $payload 'expected.json') -Raw | ConvertFrom-Json
foreach($entry in $expected.files) {
    if($entry.name -notmatch '^[a-zA-Z0-9_.-]+$') {throw 'Invalid payload filename.'}
    $actual=(Get-FileHash -LiteralPath (Join-Path $payload $entry.name) -Algorithm SHA256).Hash.ToLowerInvariant()
    if($actual -ne $entry.sha256) {throw "Transferred payload changed: $($entry.name)"}
}
function Quote-NativeArgument([string]$value) {
    if($value.Contains('"') -or $value.Contains("`n") -or $value.EndsWith('\')) {throw 'Unsupported diagnostic argument.'}
    return '"'+$value+'"'
}
function Invoke-Native([string]$exe,[string[]]$values,[int]$seconds=180,[string]$label='native') {
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$exe
    $start.Arguments=($values | ForEach-Object {Quote-NativeArgument $_}) -join ' '
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.WorkingDirectory=Split-Path -Parent $exe
    $process=[Diagnostics.Process]::Start($start)
    try {
        if(-not $process.WaitForExit($seconds*1000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "Timed out: $label"
        }
        if($process.ExitCode -ne 0) {throw "$label exited $($process.ExitCode)"}
    } finally {$process.Dispose()}
}
function Json-File([string]$path,$value) {
    [IO.File]::WriteAllText($path,($value | ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
}
function Invoke-DesktopSmoke([string]$result) {
    # WebView2 requires an interactive desktop, not OpenSSH's service session 0.
    # Run a temporary, limited task as this already logged-in account; no stored password.
    $name='Fetcher-UI-Acceptance-'+[Guid]::NewGuid().ToString('N')
    $exe=Join-Path $install 'FetcherLauncher.exe'
    $arguments=(@('--smoke-test-ui','--install-root',$install,'--log-file',$result) | ForEach-Object {Quote-NativeArgument $_}) -join ' '
    $action=New-ScheduledTaskAction -Execute $exe -Argument $arguments -WorkingDirectory $install
    $principal=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
    $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 1) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    $registered=$false
    try {
        Register-ScheduledTask -TaskName $name -Action $action -Principal $principal -Settings $settings | Out-Null
        $registered=$true
        Start-ScheduledTask -TaskName $name
        $deadline=[DateTime]::UtcNow.AddSeconds(40)
        while(-not(Test-Path -LiteralPath $result)) {
            if([DateTime]::UtcNow -gt $deadline) {throw 'Desktop UI smoke test timed out; ensure the account has a logged-in desktop.'}
            Start-Sleep -Milliseconds 200
        }
        $deadline=[DateTime]::UtcNow.AddSeconds(10)
        while((Get-ScheduledTask -TaskName $name).State -eq 'Running' -and [DateTime]::UtcNow -lt $deadline) {Start-Sleep -Milliseconds 100}
        $info=Get-ScheduledTaskInfo -TaskName $name
        if($info.LastTaskResult -ne 0) {throw "Desktop UI task exited $($info.LastTaskResult)"}
    } finally {
        if($registered) {
            Stop-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue
            Unregister-ScheduledTask -TaskName $name -Confirm:$false -ErrorAction SilentlyContinue
        }
    }
}
function Invoke-Launcher($value,[string]$label,[int]$seconds=180) {
    $request=Join-Path $evidence "$label.request.json"
    $result=Join-Path $evidence "$label.result.json"
    Json-File $request $value
    Invoke-Native (Join-Path $install 'FetcherLauncher.exe') @('--install-root',$install,'--allow-local-fixtures','--request-file',$request,'--log-file',$result) $seconds $label
    return Get-Content -LiteralPath $result -Raw | ConvertFrom-Json
}
function Master-Hashes {
    $result=[ordered]@{}
    foreach($name in @('Morrowind.esm','Tribunal.esm','Bloodmoon.esm')) {
        $result[$name]=(Get-FileHash -LiteralPath (Join-Path $GameData $name) -Algorithm SHA256).Hash
    }
    return $result
}
function Nxm-Handler {
    return @('HKCU:\Software\Classes\nxm\shell\open\command','HKCU:\Software\Classes\momw\shell\open\command') | ForEach-Object {
        if(Test-Path -LiteralPath $_) {(Get-Item -LiteralPath $_).GetValue('')}else{'<not registered>'}
    }
}
$beforeMasters=Master-Hashes
$beforeHandler=@(Nxm-Handler)
$os=Get-CimInstance Win32_OperatingSystem
$report=[ordered]@{computer=$env:COMPUTERNAME;windows=$os.Caption;build=$os.BuildNumber
    account=[Security.Principal.WindowsIdentity]::GetCurrent().Name
    sourceCommit=$expected.sourceCommit;freshFetcherRoot=$true;freshOperatingSystem=$false
    workingTreeCandidate=[bool]$expected.workingTreeCandidate
    systemPythonRequired=$false;publicDeployment=$false;checks=[ordered]@{}}
$fixture=$null
try {
    $portFile=Join-Path $evidence 'fixture-port.txt'
    $serverScript=Join-Path $PSScriptRoot 'Serve-FetcherAcceptance.ps1'
    $serverArgs=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$serverScript,'-PayloadDir',$payload,'-PortFile',$portFile)
    $fixture=Start-Process -FilePath "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList (($serverArgs | ForEach-Object {Quote-NativeArgument $_}) -join ' ') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $evidence 'fixture.log') -RedirectStandardError (Join-Path $evidence 'fixture.error.log')
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while(-not (Test-Path -LiteralPath $portFile)) {
        if($fixture.HasExited -or [DateTime]::UtcNow -gt $deadline) {throw 'Loopback release fixture did not start.'}
        Start-Sleep -Milliseconds 100
    }
    $origin='http://127.0.0.1:'+(Get-Content -LiteralPath $portFile -Raw)
    Write-Output 'Installing the verified native candidate into a fresh folder...'
    $setupResult=Join-Path $evidence 'setup-result.json'
    $setupArgs=@('--install','--install-root',$install,'--log-file',$setupResult,'--allow-local-fixtures','--tools-release-url',"$origin/release/tools",'--client-release-url',"$origin/release/client")
    Invoke-Native (Join-Path $payload 'FetcherSetup.exe') $setupArgs 240 'native setup'
    $setup=Get-Content -LiteralPath $setupResult -Raw | ConvertFrom-Json
    if(-not $setup.installed) {throw 'Setup did not confirm installation.'}
    $report.checks.nativeSetup=$true
    $report.toolsSha256=(Get-FileHash -LiteralPath (Join-Path $payload 'fetcher-tester-tools.zip')).Hash.ToLowerInvariant()
    $report.launcherSha256=(Get-FileHash -LiteralPath (Join-Path $install 'FetcherLauncher.exe')).Hash.ToLowerInvariant()
    $tools=Get-Content -LiteralPath (Join-Path $install 'fetcher-tester-tools.json') -Raw | ConvertFrom-Json
    if($tools.sourceCommit -ne $expected.sourceCommit) {throw 'Installed source commit differs from the candidate.'}
    $initial=Invoke-Launcher @{op='init'} 'initial'
    if(-not $initial.setup.clientInstalled -or $initial.setup.gameDataReady -or $initial.setup.profilePrepared) {throw 'Unexpected first-run readiness state.'}
    $report.checks.firstRunGuidance=$true
    $report.checks.nativeSourceCommit=$true
    Write-Output 'Loading the actual WebView2 page and native UI bridge...'
    $uiResult=Join-Path $evidence 'ui-smoke.json'
    Invoke-DesktopSmoke $uiResult
    $ui=Get-Content -LiteralPath $uiResult -Raw | ConvertFrom-Json
    if(-not $ui.ok -or -not $ui.webView2Loaded -or -not $ui.bridgeReady) {throw 'WebView2 UI bridge check failed.'}
    $report.checks.webView2Ui=$true
    $report.uiSession='existing signed-in desktop via temporary limited scheduled task'
    $browser=Invoke-Launcher @{op='refreshServers'} 'server-browser'
    $report.checks.publicServerBrowser=(@($browser.servers).Count -gt 0)
    if(-not $report.checks.publicServerBrowser) {throw 'No live public server records were received.'}
    $null=Invoke-Launcher @{op='gameData';path=$GameData} 'owned-game'
    $review=Invoke-Launcher @{op='reviewProfile';url="$origin/profile.json"} 'review-profile'
    $null=Invoke-Launcher @{op='confirmProfile';token=$review.profileReview.token} 'accept-profile'
    Write-Output 'Downloading and preparing the real combined content over HTTPS...'
    $prepared=Invoke-Launcher @{op='prepare'} 'prepare-profile' 240
    if(-not $prepared.setup.gameDataReady -or -not $prepared.setup.profilePrepared) {throw 'Prepared profile is not ready.'}
    $report.checks.realHttpsContentPreparation=$true
    Invoke-Native (Join-Path $install 'openmw.exe') @('--version') 30 'real client runtime startup'
    $report.checks.realClientRuntimeStartup=$true
    $beforeRepair=(Get-FileHash -LiteralPath (Join-Path $install 'openmw.cfg')).Hash
    Invoke-Native (Join-Path $payload 'FetcherSetup.exe') $setupArgs 180 'cached setup repair'
    if((Get-FileHash -LiteralPath (Join-Path $install 'openmw.cfg')).Hash -ne $beforeRepair) {throw 'Setup repair replaced the existing game configuration.'}
    $report.checks.cachedRepairPreservesConfiguration=$true
    if(-not $SkipUmo) {
        Write-Output 'Installing and probing the optional upstream UMO runtime without sign-in...'
        $umo=Invoke-Launcher @{op='umoInstall'} 'umo-install' 240
        if(-not $umo.umo.installed -or $umo.umo.connected) {throw 'Unexpected UMO helper state.'}
        $report.checks.umoRuntime=$true
    }
    $afterMasters=Master-Hashes
    foreach($name in $beforeMasters.Keys) {
        if($beforeMasters[$name] -ne $afterMasters[$name]) {throw "Owned game file changed: $name"}
    }
    if((@($beforeHandler) | ConvertTo-Json -Compress) -ne (@(Nxm-Handler) | ConvertTo-Json -Compress)) {throw 'Acceptance changed existing NXM/MOMW handlers.'}
    $report.checks.ownedMastersPreserved=$true
    $report.checks.protocolHandlersPreserved=$true
    $report.ok=$true
} catch {
    $report.ok=$false
    $report.error=$_.Exception.Message
    Write-Output "Acceptance failed: $($report.error)"
} finally {
    if($fixture -and -not $fixture.HasExited) {$fixture.Kill();$fixture.WaitForExit()}
    if($fixture) {$fixture.Dispose()}
    Json-File (Join-Path $evidence 'acceptance-report.json') $report
    $report | ConvertTo-Json -Depth 8
}
if(-not $report.ok) {exit 1}
