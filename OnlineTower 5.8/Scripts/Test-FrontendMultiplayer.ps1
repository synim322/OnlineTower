param(
    [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8',
    [int]$Port=17831,
    [int]$BackendPort=18083,
    [ValidateSet('host','a')][string]$Initiator='host'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$repository=Split-Path $projectRoot -Parent
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
$python=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
$project=Join-Path $projectRoot 'OnlineTower.uproject'
if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue){throw "UDP port $Port occupied"}
if(Get-NetTCPConnection -LocalPort $BackendPort -State Listen -ErrorAction SilentlyContinue){throw "TCP port $BackendPort occupied"}
$run=Get-Date -Format 'MMddHHmmss'
$logs=Join-Path $projectRoot "Saved/Logs/Frontend-$run-$Initiator"
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$owned=@()
try {
    $owned+=Start-Process -FilePath $python -ArgumentList ('"{0}" --port {1} --db "{2}"' -f (Join-Path $repository 'Backend/server.py'),$BackendPort,(Join-Path $logs 'accounts.sqlite3')) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $logs 'backend.stderr.log')
    $ready=$false
    for($i=0;$i -lt 20;$i++){
        try {if((Invoke-RestMethod "http://127.0.0.1:$BackendPort/health").ok){$ready=$true;break}}catch{}
        Start-Sleep -Seconds 1
    }
    if(-not $ready){throw "Backend not ready: $logs"}
    foreach($role in @('host','a','b')){
        $destination=if($role -eq 'host'){'/Game/Maps/Frontend/L_MainMenu?listen?game=/Script/OnlineTower.TowerFrontendGameMode'}else{"127.0.0.1:$Port"}
        $trigger=if($role -eq $Initiator){'-TowerEntryInitiator'}else{''}
        $log=Join-Path $logs "$role.log"
        $launchArgs='"{0}" {1} -game -port={2} -nullrhi -unattended -nosound -NoSplash -ddc=InstalledNoZenLocalFallback -TowerFrontendNetworkProbe -TowerEntryRole={3} -TowerProbeUser=entry_{4}_{3} -TowerBackend=http://127.0.0.1:{5} {6} -ExecCmds="t.MaxFPS 30" -abslog="{7}"' -f $project,$destination,$Port,$role,$run,$BackendPort,$trigger,$log
        $owned+=Start-Process -FilePath $editor -ArgumentList $launchArgs -WindowStyle Hidden -PassThru
        if($role -eq 'host'){
            $ready=$false
            for($i=0;$i -lt 90;$i++){
                if((Test-Path $log) -and (Select-String -LiteralPath $log -Pattern "listening on port $Port" -Quiet)){$ready=$true;break}
                if(@($owned|Where-Object {$_.HasExited}).Count){throw "Process exited: $logs"}
                Start-Sleep -Seconds 1
            }
            if(-not $ready){throw "Listen host not ready: $logs"}
        }
    }
    $passed=$false
    for($i=0;$i -lt 150;$i++){
        Start-Sleep -Seconds 1
        $count=0
        foreach($role in @('host','a','b')){
            $log=Join-Path $logs "$role.log"
            if((Test-Path $log) -and (Select-String -LiteralPath $log -Pattern "TOWER_ENTRY_PROBE CLIENT_PASS Role=$role" -Quiet)){$count++}
        }
        if($count -eq 3 -and (Select-String -LiteralPath (Join-Path $logs 'host.log') -Pattern "TOWER_ENTRY_PROBE SERVER_PASS Players=3 Port=$Port" -Quiet)){$passed=$true;break}
        if(@($owned|Where-Object {$_.HasExited}).Count){throw "Process exited: $logs"}
    }
    if(-not $passed){throw "Frontend multiplayer timed out: $logs"}
    foreach($role in @('host','a','b')){
        $text=Get-Content -LiteralPath (Join-Path $logs "$role.log") -Raw
        if($text -notmatch 'SeamlessTravel finished' -or $text -match 'TravelFailure|NetworkFailure|ConnectionTimeout'){
            throw "Travel did not preserve the connection for ${role}: $logs"
        }
    }
    Write-Output "PASS: $Initiator initiated menu -> hub; host and two clients share three authenticated pawns; delayed login succeeds; port preserved. Logs: $logs"
} finally {
    foreach($process in $owned){if(-not $process.HasExited){Stop-Process -Id $process.Id -ErrorAction SilentlyContinue}}
}
