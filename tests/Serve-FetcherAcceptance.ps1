[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PayloadDir,
    [Parameter(Mandatory=$true)][string]$PortFile
)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$payload=(Resolve-Path -LiteralPath $PayloadDir).Path
$listener=New-Object Net.Sockets.TcpListener -ArgumentList ([Net.IPAddress]::Loopback),0
$listener.Start()
$port=$listener.LocalEndpoint.Port
$origin="http://127.0.0.1:$port"
$routes=@{}
foreach($name in @('fetcher-tester-tools.zip','fetcher-simulator.zip','profile.json')) {
    $routes["/$name"]=Join-Path $payload $name
}
$releases=@{}
foreach($item in @(@('tools','fetcher-tester-tools.zip'),@('client','fetcher-simulator.zip'))) {
    $path=Join-Path $payload $item[1]
    $releases["/release/$($item[0])"]=[Text.Encoding]::UTF8.GetBytes((@{
        assets=@(@{name=$item[1];size=(Get-Item -LiteralPath $path).Length
            digest='sha256:'+(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            browser_download_url="$origin/$($item[1])"})
    } | ConvertTo-Json -Depth 5 -Compress))
}
[IO.File]::WriteAllText($PortFile,[string]$port)
try {
    while($true) {
        $client=$listener.AcceptTcpClient()
        try {
            $client.ReceiveTimeout=10000
            $client.SendTimeout=60000
            $stream=$client.GetStream()
            $reader=New-Object IO.StreamReader -ArgumentList $stream,([Text.Encoding]::ASCII),$false,4096,$true
            $request=$reader.ReadLine()
            while($reader.ReadLine()) {}
            $reader.Dispose()
            $path=if($request -match '^GET (/[^ ]*) HTTP/1\.[01]$'){$Matches[1]}else{''}
            if($releases.ContainsKey($path)) {
                $body=$releases[$path]
                $header="HTTP/1.1 200 OK`r`nContent-Type: application/json`r`nContent-Length: $($body.Length)`r`nConnection: close`r`n`r`n"
                $bytes=[Text.Encoding]::ASCII.GetBytes($header)
                $stream.Write($bytes,0,$bytes.Length)
                $stream.Write($body,0,$body.Length)
            } elseif($routes.ContainsKey($path)) {
                $file=[IO.File]::OpenRead($routes[$path])
                try {
                    $header="HTTP/1.1 200 OK`r`nContent-Type: application/octet-stream`r`nContent-Length: $($file.Length)`r`nConnection: close`r`n`r`n"
                    $bytes=[Text.Encoding]::ASCII.GetBytes($header)
                    $stream.Write($bytes,0,$bytes.Length)
                    $file.CopyTo($stream)
                } finally {$file.Dispose()}
            } else {
                $bytes=[Text.Encoding]::ASCII.GetBytes("HTTP/1.1 404 Not Found`r`nContent-Length: 0`r`nConnection: close`r`n`r`n")
                $stream.Write($bytes,0,$bytes.Length)
            }
            $stream.Flush()
            Write-Output $path
        } catch {Write-Output 'Fixture request ended before completion.'}
        finally {$client.Dispose()}
    }
} finally {$listener.Stop()}
