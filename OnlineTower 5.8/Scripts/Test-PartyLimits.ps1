param([string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8', [int]$Port=17780)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$repository=Split-Path $projectRoot -Parent
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
$python=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
$project=Join-Path $projectRoot 'OnlineTower.uproject'
$run=Get-Date -Format 'MMddHHmmss'
$logs=Join-Path $projectRoot "Saved/Logs/PartyLimits-$run"
if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue){throw "UDP port $Port occupied"}
if(Get-NetTCPConnection -LocalPort 18080 -State Listen -ErrorAction SilentlyContinue){throw 'Stop your local backend before this isolated database test (18080 occupied).'}
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$owned=@()
try {
    $backend=Join-Path $repository 'Backend/server.py'
    $database=Join-Path $logs 'accounts.sqlite3'
    $owned+=Start-Process -FilePath $python -ArgumentList ('"{0}" --port 18080 --db "{1}"' -f $backend,$database) -WindowStyle Hidden -PassThru
    $ready=$false
    for($i=0;$i -lt 15;$i++){
        try{if((Invoke-RestMethod 'http://127.0.0.1:18080/health').ok){$ready=$true;break}}catch{}
        Start-Sleep -Seconds 1
    }
    if(-not $ready){throw 'Backend not ready'}
    $common='-nullrhi -unattended -nosound -NoSplash -ddc=InstalledNoZenLocalFallback -TowerPartyLimitsProbe -ExecCmds="t.MaxFPS 30"'
    $serverLog=Join-Path $logs 'server.log'
    $owned+=Start-Process -FilePath $editor -ArgumentList ('"{0}" /Game/Maps/Hub/L_Hub?game=/Script/OnlineTower.TowerHubGameMode -server -port={1} {2} -abslog="{3}"' -f $project,$Port,$common,$serverLog) -WindowStyle Hidden -PassThru
    $ready=$false
    for($i=0;$i -lt 60;$i++){
        if((Test-Path $serverLog) -and (Select-String -LiteralPath $serverLog -Pattern 'Bringing World .*L_Hub.*up for play' -Quiet)){$ready=$true;break}
        Start-Sleep -Seconds 1
    }
    if(-not $ready){throw "Server not ready: $serverLog"}
    $clientLogs=@()
    for($i=0;$i -lt 7;$i++){
        $log=Join-Path $logs "client-$i.log"
        $clientLogs+=$log
        $owned+=Start-Process -FilePath $editor -ArgumentList ('"{0}" 127.0.0.1:{1} -game {2} -TowerProbeUser=limits_{3}_{4} -TowerProbeIndex={4} -abslog="{5}"' -f $project,$Port,$common,$run,$i,$log) -WindowStyle Hidden -PassThru
        Start-Sleep -Milliseconds 400
    }
    $passed=$false
    for($i=0;$i -lt 180;$i++){
        Start-Sleep -Seconds 1
        $serverText=Get-Content -LiteralPath $serverLog -Raw
        if($serverText -match 'TOWER_LIMITS_PROBE FAIL'){throw "Server assertion failed: $logs"}
        $authenticated=0
        foreach($log in $clientLogs){if((Test-Path $log) -and (Select-String -LiteralPath $log -Pattern 'TOWER_LIMITS_PROBE AUTH_CLIENT_PASS' -Quiet)){$authenticated++}}
        $seventh=Select-String -LiteralPath $clientLogs[0] -Pattern 'TOWER_LIMITS_PROBE SEVENTH_REJECT_CLIENT_PASS' -Quiet -ErrorAction SilentlyContinue
        $cancel=Select-String -LiteralPath $clientLogs[1] -Pattern 'TOWER_LIMITS_PROBE CANCEL_CLIENT_PASS' -Quiet -ErrorAction SilentlyContinue
        if($serverText -match 'TOWER_LIMITS_PROBE SERVER_PASS' -and $authenticated -eq 7 -and $seventh -and $cancel){$passed=$true;break}
        if(@($owned|Where-Object {$_.HasExited}).Count){throw "A test process exited: $logs"}
    }
    if(-not $passed){throw "Party limits probe timed out: $logs"}
    Write-Output "PASS: seven real clients, max-six party, seventh rejected, oversized dungeon/duel rejected, member cancels entire ticket, no immediate requeue. Logs: $logs"
}finally{
    foreach($process in $owned){if(-not $process.HasExited){Stop-Process -Id $process.Id -ErrorAction SilentlyContinue}}
}
