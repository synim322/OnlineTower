param(
    [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8',
    [ValidateSet('Main','Create','Settings','HUD','Pause','PauseSettings')][string]$View='Main'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
$project=Join-Path $projectRoot 'OnlineTower.uproject'
$log=Join-Path $projectRoot "Saved/Logs/Menu-$View.log"
$isHub=$View -in @('HUD','Pause','PauseSettings')
$map=if($isHub){'/Game/Maps/Hub/L_Hub'}else{'/Game/Maps/Frontend/L_MainMenu'}
$flags=switch($View){
    'Main' {'-TowerMenuScreenshot -TowerMenuPage=0'}
    'Create' {'-TowerMenuScreenshot -TowerMenuPage=2'}
    'Settings' {'-TowerMenuScreenshot -TowerMenuPage=3'}
    'HUD' {'-TowerScreenshot'}
    'Pause' {'-TowerScreenshot -TowerHUDPage=1'}
    'PauseSettings' {'-TowerScreenshot -TowerHUDPage=2'}
}
$source=Join-Path $projectRoot $(if($isHub){'Saved/Screenshots/HubDungeon.png'}else{'Saved/Screenshots/MainMenu.png'})
$destination=Join-Path $projectRoot "Saved/Screenshots/Menu-$View.png"
$started=Get-Date
$arguments='"{0}" {1} -game -unattended -nosound -NoSplash -RenderOffscreen -windowed -ResX=1440 -ResY=900 -ddc=InstalledNoZenLocalFallback {2} -ExecCmds="t.MaxFPS 30" -abslog="{3}"' -f $project,$map,$flags,$log
$owned=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    $captured=$false
    for($attempt=0;$attempt -lt 120;$attempt++){
        Start-Sleep -Seconds 1
        if((Test-Path -LiteralPath $source) -and (Get-Item -LiteralPath $source).LastWriteTime -gt $started){$captured=$true;break}
        if($owned.HasExited){throw "Capture exited; inspect $log"}
    }
    if(-not $captured){throw "Capture timed out; inspect $log"}
    Copy-Item -LiteralPath $source -Destination $destination
    Write-Output "CAPTURED $destination"
} finally {
    if(-not $owned.HasExited){Stop-Process -Id $owned.Id}
}
