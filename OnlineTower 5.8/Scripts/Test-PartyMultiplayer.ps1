param([string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8', [int]$Port=17779)
$ErrorActionPreference='Stop'
# Compatibility entry point: the dungeon now needs five players.
# Includes a two-person party, three solos and an uninvolved hub player.
& (Join-Path $PSScriptRoot 'Test-Matchmaking.ps1') -EngineRoot $EngineRoot -Port $Port -TrialIndex 0
