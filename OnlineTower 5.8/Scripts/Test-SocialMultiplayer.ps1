param(
    [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8',
    [int]$BackendPort=18081,
    [int]$HubAPort=17781,
    [int]$HubBPort=17782
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$repository=Split-Path $projectRoot -Parent
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
$python=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
$project=Join-Path $projectRoot 'OnlineTower.uproject'
if($HubAPort -eq $HubBPort){throw 'The two game servers require different ports.'}
foreach($port in @($HubAPort,$HubBPort)){
    if(Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue){throw "UDP port $port occupied"}
}
if(Get-NetTCPConnection -LocalPort $BackendPort -State Listen -ErrorAction SilentlyContinue){throw "Backend port $BackendPort occupied"}
$run=Get-Date -Format 'MMddHHmmss'
$logs=Join-Path $projectRoot "Saved/Logs/Social-$run"
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$registry=Join-Path $logs 'servers.json'
$servers=@(
    @{id='hub_a';address="127.0.0.1:$HubAPort";key=([guid]::NewGuid().ToString('N')+[guid]::NewGuid().ToString('N'))},
    @{id='hub_b';address="127.0.0.1:$HubBPort";key=([guid]::NewGuid().ToString('N')+[guid]::NewGuid().ToString('N'))}
)
[System.IO.File]::WriteAllText($registry,(@{servers=$servers}|ConvertTo-Json -Depth 4),[System.Text.UTF8Encoding]::new($false))
$owned=@()
try {
    $backend=Join-Path $repository 'Backend/server.py'
    $database=Join-Path $logs 'accounts.sqlite3'
    $owned+=Start-Process -FilePath $python -ArgumentList ('"{0}" --port {1} --db "{2}" --servers-file "{3}"' -f $backend,$BackendPort,$database,$registry) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $logs 'backend.stderr.log')
    $ready=$false
    for($i=0;$i -lt 15;$i++){
        try{if((Invoke-RestMethod "http://127.0.0.1:$BackendPort/health").ok){$ready=$true;break}}catch{}
        Start-Sleep -Seconds 1
    }
    if(-not $ready){throw "Backend not ready: $logs"}
    $common='-nullrhi -unattended -nosound -NoSplash -ddc=InstalledNoZenLocalFallback -TowerSocialProbe -TowerBackend=http://127.0.0.1:{0} -ExecCmds="t.MaxFPS 30"' -f $BackendPort
    $serverLogs=@()
    for($i=0;$i -lt 2;$i++){
        $serverId=$servers[$i].id
        $port=if($i -eq 0){$HubAPort}else{$HubBPort}
        $log=Join-Path $logs "$serverId.log"
        $serverLogs+=$log
        $launchArguments='"{0}" /Game/Maps/Hub/L_Hub?game=/Script/OnlineTower.TowerHubGameMode -server -port={1} -TowerServerId={2} -TowerServersFile="{3}" {4} -abslog="{5}"' -f $project,$port,$serverId,$registry,$common,$log
        $owned+=Start-Process -FilePath $editor -ArgumentList $launchArguments -WindowStyle Hidden -PassThru
    }
    $ready=$false
    for($i=0;$i -lt 90;$i++){
        $readyCount=0
        foreach($log in $serverLogs){if((Test-Path $log) -and (Select-String -LiteralPath $log -Pattern 'Bringing World .*L_Hub.*up for play' -Quiet)){$readyCount++}}
        if($readyCount -eq 2){$ready=$true;break}
        if(@($owned|Where-Object {$_.HasExited}).Count){throw "A server process exited: $logs"}
        Start-Sleep -Seconds 1
    }
    if(-not $ready){throw "Both hubs were not ready: $logs"}
    $clientLogs=@()
    for($i=0;$i -lt 2;$i++){
        $role=if($i -eq 0){'a'}else{'b'}
        $peer=if($i -eq 0){'b'}else{'a'}
        $port=if($i -eq 0){$HubAPort}else{$HubBPort}
        $log=Join-Path $logs "client-$role.log"
        $clientLogs+=$log
        $launchArguments='"{0}" 127.0.0.1:{1} -game {2} -TowerProbeUser=social_{3}_{4} -TowerProbePeer=social_{3}_{5} -TowerSocialRole={4} -abslog="{6}"' -f $project,$port,$common,$run,$role,$peer,$log
        $owned+=Start-Process -FilePath $editor -ArgumentList $launchArguments -WindowStyle Hidden -PassThru
    }
    $passed=$false
    for($i=0;$i -lt 180;$i++){
        Start-Sleep -Seconds 1
        $texts=@{}
        foreach($log in @($serverLogs)+@($clientLogs)){if(Test-Path $log){$texts[$log]=Get-Content -LiteralPath $log -Raw}else{$texts[$log]=''}}
        if(@($texts.Values|Where-Object {$_ -match 'TOWER_SOCIAL_PROBE FAIL'}).Count){throw "Social probe assertion failed: $logs"}
        $a=$texts[$serverLogs[0]];$b=$texts[$serverLogs[1]]
        $ca=$texts[$clientLogs[0]];$cb=$texts[$clientLogs[1]]
        if($a -match 'INITIAL_SERVER_PASS Hub=hub_a' -and $b -match 'INITIAL_SERVER_PASS Hub=hub_b' -and
           $a -match 'DESTINATION_SERVER_PASS' -and $b -match 'SOURCE_EMPTY_PASS' -and
           $ca -match 'INITIAL_CLIENT_PASS Role=a Hub=hub_a' -and $cb -match 'INITIAL_CLIENT_PASS Role=b Hub=hub_b' -and
           $ca -match 'CLIENT_PASS Role=a Friends=1' -and $cb -match 'CLIENT_PASS Role=b Friends=1' -and
           $cb -match 'CROSS_HUB_INVITE_ACCEPTED'){$passed=$true;break}
        if(@($owned|Where-Object {$_.HasExited}).Count){throw "A social test process exited: $logs"}
    }
    if(-not $passed){throw "Social probe timed out: $logs"}
    Write-Output "PASS: accounts started in different hubs, backend search and accepted friendship, trusted cross-hub invitation, real travel, destination party of two and preserved identity. Logs: $logs"
}finally{
    foreach($process in $owned){if(-not $process.HasExited){Stop-Process -Id $process.Id -ErrorAction SilentlyContinue}}
}

