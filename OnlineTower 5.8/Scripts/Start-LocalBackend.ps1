param(
    [string]$Python = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe',
    [int]$Port = 18080
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$server = Join-Path $repository 'Backend/server.py'
$database = Join-Path $repository 'Backend/data/tower.sqlite3'
if (-not (Test-Path -LiteralPath $Python)) { throw "Python not found: $Python. Supply -Python with your Python executable." }
try {
    $health=Invoke-RestMethod -Uri "http://127.0.0.1:$Port/health" -TimeoutSec 2
    if ($health.ok) { Write-Output "Local backend is already running on port $Port."; exit 0 }
} catch { }
Write-Output "Local account backend: http://127.0.0.1:$Port; keep this window open while playing. Ctrl+C stops it."
& $Python $server --port $Port --db $database
exit $LASTEXITCODE
