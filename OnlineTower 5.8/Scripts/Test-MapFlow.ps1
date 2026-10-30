param([string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
$project=Join-Path $projectRoot 'OnlineTower.uproject'
$runId=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$log=Join-Path $projectRoot "Saved/Logs/MapFlow-$runId.log"
$slot="TowerMenuProbe_MapFlow_$runId"
$arguments='"{0}" /Game/Maps/Frontend/L_MainMenu -game -nullrhi -unattended -nosound -NoSplash -ddc=InstalledNoZenLocalFallback -TowerMapProbe -TowerProfileSlot={1} -ExecCmds="t.MaxFPS 60" -abslog="{2}"' -f $project,$slot,$log
$owned=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if(-not $owned.WaitForExit(180000)){throw "Map flow timed out: $log"}
    $result=Get-Content -LiteralPath $log -Raw
    if($owned.ExitCode -ne 0 -or $result -notmatch 'TOWER_MENU_PROBE PASS') {throw "Map flow failed: $log"}
    foreach($trial in 0..2){
        if($result -notmatch "TOWER_MAP_PROBE Trial=$trial LoadoutPreserved=1 ReturnPortal=1"){throw "Trial $trial missing: $log"}
    }
    Write-Output "PASS: menu, hub, three trial roundtrips, profile and loadout. Log: $log"
} finally {
    if(-not $owned.HasExited){Stop-Process -Id $owned.Id}
}
