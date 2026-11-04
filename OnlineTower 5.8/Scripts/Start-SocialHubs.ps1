param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference = 'Stop'
# Two independent hubs share one account/friends backend and the trusted servers.json registry.
$projectRoot = Split-Path $PSScriptRoot -Parent
$repository = Split-Path $projectRoot -Parent
$serversFile = Join-Path $repository 'Backend/data/servers.json'
& (Join-Path $PSScriptRoot 'Ensure-LocalBackend.ps1')
& (Join-Path $PSScriptRoot 'Start-HubMultiplayer.ps1') -EngineRoot $EngineRoot -Port 17777 -Players 1 -ServerId hub_a -ServersFile $serversFile
& (Join-Path $PSScriptRoot 'Start-HubMultiplayer.ps1') -EngineRoot $EngineRoot -Port 17778 -Players 1 -ServerId hub_b -ServersFile $serversFile
Write-Output 'Two menu windows are ready: first connects to hub_a (17777), second to hub_b (17778).'
Write-Output 'Use different accounts → найти по нику → добавить в друзья → пригласить в группу → принять. Friend travel joins the inviter hub.'
