param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Baud = 115200,
    [string]$Python = 'python',
    [switch]$InitializeMatterStorage,
    [switch]$FirstInstall
)
$ErrorActionPreference = 'Stop'
$projectPath = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$firmwarePath = Join-Path $projectPath 'firmware'
# Pin the backup helper to the esptool API against which it was tested.
& $Python -c "import esptool; assert esptool.__version__ == '4.10.0', 'Install esptool==4.10.0 in this Python environment'"
if ($LASTEXITCODE -ne 0) { throw 'Python/esptool check failed; nothing was written' }
$manifest = Get-Content -Raw -LiteralPath (Join-Path $firmwarePath 'flash-manifest.json') | ConvertFrom-Json
$flashArgs = @()
foreach ($entry in $manifest.files) {
    $path = (Resolve-Path -LiteralPath (Join-Path $firmwarePath $entry.file)).Path
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Firmware checksum mismatch: $($entry.file)"
    }
    $flashArgs += @($entry.address, $path)
}
# Before every flash, save a private full backup beside the project.
$backupPath = Join-Path $projectPath ('private-backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $backupPath | Out-Null
& $Python -u (Join-Path $PSScriptRoot 'backup-flash.py') --chip esp32c6 --port $Port --baud $Baud read_flash --no-progress 0 0x800000 (Join-Path $backupPath 'flash.bin')
if ($LASTEXITCODE -ne 0) { throw 'Backup failed; flash cancelled' }
if ($FirstInstall) {
    # An explicit first-install request resets both Thread and Matter storage.
    & $Python -m esptool --chip esp32c6 --port $Port erase_flash
    if ($LASTEXITCODE -ne 0) { throw 'First-install erase failed' }
} elseif ($InitializeMatterStorage) {
    # First installation only: this region held part of the diagnostic app.
    # The original Thread NVS at 0x9000 is preserved.
    & $Python -m esptool --chip esp32c6 --port $Port erase_region 0x10000 0x20000
    if ($LASTEXITCODE -ne 0) { throw 'Matter storage initialization failed' }
}
$settings = $manifest.flash_settings
& $Python -m esptool --chip esp32c6 --port $Port --baud $Baud write_flash --no-progress --flash_mode $settings.flash_mode --flash_freq $settings.flash_freq --flash_size 8MB @flashArgs
if ($LASTEXITCODE -ne 0) { throw 'Firmware flashing failed' }
Write-Host 'Firmware flashed. Open the COM port at 115200 baud.'
