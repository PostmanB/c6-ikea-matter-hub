param([int]$Port = 8765, [string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$projectPath = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Write-Host "Open http://localhost:$Port/setup/ in desktop Chrome or Edge."
Write-Host 'Keep this window open. Press Ctrl+C here to stop the local page server.'
& $Python -m http.server $Port --bind 127.0.0.1 --directory $projectPath
if ($LASTEXITCODE -ne 0) { throw 'Could not start the local page server. Install Python 3 or use the hosted setup page.' }
