param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [int]$Port = 17777,
    [ValidateRange(1,16)][int]$Players = 5,
    [ValidateRange(0,2)][int]$TrialIndex = 0,
    [string]$ServerId = '',
    [string]$ServersFile = '',
    [switch]$Automated
)
$ErrorActionPreference = 'Stop'
# Current account/party integration probe owns an isolated backend and database.
if ($Automated) {
    if($TrialIndex -eq 2){throw 'Final gameplay is out of scope. Use manual 12-window entry validation.'}
    & (Join-Path $PSScriptRoot 'Test-Matchmaking.ps1') -EngineRoot $EngineRoot -Port $Port -TrialIndex $TrialIndex
    return
}
# Interactive clients require Start-LocalBackend.cmd for account login.
& (Join-Path $PSScriptRoot 'Ensure-LocalBackend.ps1')
$projectRoot = Split-Path $PSScriptRoot -Parent
$repository = Split-Path $projectRoot -Parent
$projectFile = Join-Path $projectRoot 'OnlineTower.uproject'
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
$logDir = Join-Path $projectRoot 'Saved/Logs/HubMultiplayer'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
if (-not (Test-Path -LiteralPath $editor)) { throw "Unreal executable not found: $editor" }
if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) { throw "Port $Port is already in use. Stop the previous test or choose -Port." }
if ([string]::IsNullOrWhiteSpace($ServersFile)) {
    $ServersFile = Join-Path $repository 'Backend/data/servers.json'
}
if ([string]::IsNullOrWhiteSpace($ServerId)) {
    $ServerId = if ($Port -eq 17778) { 'hub_b' } elseif ($Port -eq 17777) { 'hub_a' } else { '' }
}
$serverIdentity = ''
if (-not [string]::IsNullOrWhiteSpace($ServerId)) {
    $serverIdentity = '-TowerServerId={0} -TowerServersFile="{1}"' -f $ServerId, $ServersFile
}
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + "-$Port"
$serverLog = Join-Path $logDir "$runId-server.log"
$clientLogs = @()
$ownedProcesses = @()
$common = '-unattended -nosound -NoSplash -ddc=InstalledNoZenLocalFallback -ExecCmds="t.MaxFPS 60"'
try {
    $serverArgs = '"{0}" /Game/Maps/Hub/L_Hub?game=/Script/OnlineTower.TowerHubGameMode -server -nullrhi -port={1} {2} {3} -abslog="{4}"' -f $projectFile,$Port,$common,$serverIdentity,$serverLog
    $serverProcess = Start-Process -FilePath $editor -ArgumentList $serverArgs -WindowStyle Hidden -PassThru
    $ownedProcesses += $serverProcess
    $ready = $false
    for ($attempt=0; $attempt -lt 45; $attempt++) {
        if ($serverProcess.HasExited) { throw "Server exited. See $serverLog" }
        if ((Test-Path $serverLog) -and (Select-String -Path $serverLog -Pattern 'Bringing World .*L_Hub.*up for play' -Quiet)) { $ready=$true; break }
        Start-Sleep -Seconds 1
    }
    if (-not $ready) { throw "Server did not load the hub. See $serverLog" }
    for ($index=1; $index -le $Players; $index++) {
        $clientLog = Join-Path $logDir "$runId-client$index.log"
        $clientLogs += $clientLog
        $mode = if ($Automated) { '-nullrhi -TowerNetProbe -TowerProbeTrial=' + $TrialIndex } else { '-windowed -ResX=960 -ResY=640 -WinX=' + (($index-1)*960) + ' -WinY=50' }
        $clientArgs = '"{0}" /Game/Maps/Frontend/L_MainMenu -TowerServer=127.0.0.1:{1} -game {2} {3} -abslog="{4}"' -f $projectFile,$Port,$mode,$common,$clientLog
        $windowStyle = if ($Automated) { 'Hidden' } else { 'Normal' }
        $ownedProcesses += Start-Process -FilePath $editor -ArgumentList $clientArgs -WindowStyle $windowStyle -PassThru
    }
    if ($Automated) {
        $passed=$false
        for ($attempt=0; $attempt -lt 60; $attempt++) {
            Start-Sleep -Seconds 1
            $complete=0
            foreach ($clientLog in $clientLogs) {
                if ((Test-Path $clientLog) -and (Select-String -Path $clientLog -Pattern 'TOWER_NET_PROBE Characters=2 PlayerStates=2 Skills=4' -Quiet) -and (Select-String -Path $clientLog -Pattern 'TOWER_PORTAL_PROBE ReturnedToHub=1' -Quiet)) { $complete++ }
            }
            if ($complete -eq 2) { $passed=$true; break }
        }
        $serverText = Get-Content $serverLog -Raw
        $casts = ([regex]::Matches($serverText,'TOWER_CAST')).Count
        $rolls = ([regex]::Matches($serverText,'TOWER_ROLL')).Count
        if (-not $passed -or $casts -lt 2 -or $rolls -lt 2) { throw "Network probe incomplete: two clients=$passed, casts=$casts, rolls=$rolls. Logs: $logDir/$runId-*" }
        Write-Output "PASS: two clients see two characters and four abilities, enter a portal and return; server validated $casts casts and $rolls rolls."
    } else {
        $receipt = Join-Path $logDir "interactive-processes-$Port.json"
        $ownedProcesses | Select-Object Id,StartTime,Path | ConvertTo-Json | Set-Content -LiteralPath $receipt -Encoding UTF8
        $hubLabel = if ($ServerId) { $ServerId } else { "port $Port" }
        Write-Output "$Players clients opened at the main menu. Sign in to connect to localhost:$Port ($hubLabel). Server PID: $($serverProcess.Id)."
        Write-Output "Close the game windows when finished. Stop the test server with: Stop-Process -Id $($serverProcess.Id)"
        Write-Output "Logs: $logDir"
    }
} catch {
    foreach ($owned in $ownedProcesses) { if (-not $owned.HasExited) { Stop-Process -Id $owned.Id -ErrorAction SilentlyContinue } }
    throw
} finally {
    if ($Automated) { foreach ($owned in $ownedProcesses) { if (-not $owned.HasExited) { Stop-Process -Id $owned.Id -ErrorAction SilentlyContinue } } }
}
