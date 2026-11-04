param([int]$Port=18080)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$repository=Split-Path $projectRoot -Parent
$url="http://127.0.0.1:$Port/health"
$health=$null
try {$health=Invoke-RestMethod $url -TimeoutSec 2 -NoProxy}catch{}
if($health){
    if($health.ok -and $health.social_version -eq 1){Write-Output 'Account and friends backend is ready.';return}
    throw "Port $Port responds but does not provide the current friends API. Stop the old backend and run Start-LocalBackend.cmd again."
}
if(Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue){throw "Port $Port belongs to another service."}
$python='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
if(-not(Test-Path -LiteralPath $python)){throw "Python not found: $python"}
$logs=Join-Path $projectRoot 'Saved/Logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$process=Start-Process -FilePath $python -ArgumentList ('"{0}" --port {1} --db "{2}" --servers-file "{3}"' -f
    (Join-Path $repository 'Backend/server.py'),$Port,(Join-Path $repository 'Backend/data/tower.sqlite3'),(Join-Path $repository 'Backend/data/servers.json')) `
    -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $logs 'LocalBackend.stdout.log') -RedirectStandardError (Join-Path $logs 'LocalBackend.stderr.log')
for($i=0;$i -lt 20;$i++){
    Start-Sleep -Milliseconds 300
    if($process.HasExited){throw "Backend exited. Inspect $logs/LocalBackend.stderr.log"}
    try {$health=Invoke-RestMethod $url -TimeoutSec 2 -NoProxy}catch{continue}
    if($health.ok -and $health.social_version -eq 1){
        Write-Output "Account and friends backend started. PID: $($process.Id). Persists after game windows close."
        return
    }
}
if(-not $process.HasExited){Stop-Process -Id $process.Id}
throw 'Backend did not become ready.'
