param([string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8', [int[]]$Views=@(0,1,2,3))
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
$project=Join-Path $projectRoot 'OnlineTower.uproject'
foreach ($view in $Views) {
    if ($view -lt 0 -or $view -gt 3) { throw 'View must be 0 through 3' }
    $log=Join-Path $projectRoot "Saved/Logs/ArtView$view.log"
    $shot=Join-Path $projectRoot "Saved/Screenshots/ArtView$view.png"
    $started=Get-Date
    $maps=@('/Game/Maps/Hub/L_Hub','/Game/Maps/Trials/L_Dungeon','/Game/Maps/Trials/L_Duel','/Game/Maps/Trials/L_FloorFinal')
    $arguments='"{0}" {3} -game -unattended -nosound -NoSplash -RenderOffscreen -ResX=1440 -ResY=900 -ddc=InstalledNoZenLocalFallback -TowerArtView={1} -ExecCmds="t.MaxFPS 30" -abslog="{2}"' -f $project,$view,$log,$maps[$view]
    $owned=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    try {
        $captured=$false
        for ($attempt=0; $attempt -lt 150; $attempt++) {
            Start-Sleep -Seconds 1
            if ((Test-Path -LiteralPath $shot) -and (Get-Item -LiteralPath $shot).LastWriteTime -gt $started) { $captured=$true; break }
            if ($owned.HasExited) { throw "Capture process exited for view $view; inspect $log" }
        }
        if (-not $captured) { throw "Capture timed out for view $view; inspect $log" }
        Write-Output "CAPTURED $shot"
    } finally {
        if (-not $owned.HasExited) { Stop-Process -Id $owned.Id }
    }
}
