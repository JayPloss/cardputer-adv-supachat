param(
    [string]$Port = 'COM4',
    [int]$Baud = 230400,
    [string]$DestinationDirectory = 'C:\Users\PC\OneDrive - Plossco\00_Jay-VSCode-Assets\Assets\projects\assets-cardputer-adv-supachat\stock-backup-albie',
    [string]$Python = 'C:\dev\repos\personal-projects\milffinder-field-control\.venv\Scripts\python.exe',
    [string]$EspTool = 'C:\Users\PC\.platformio\packages\tool-esptoolpy\esptool.py'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$chunkSize = 0x100000
$flashSize = 0x800000
$outputPath = Join-Path $DestinationDirectory 'albie-cardputer-adv-stock-full-8mb-2026-08-08.bin'
$chunkDirectory = Join-Path $DestinationDirectory 'chunks'
New-Item -ItemType Directory -Path $chunkDirectory -Force | Out-Null

for ($offset = 0; $offset -lt $flashSize; $offset += $chunkSize) {
    $chunkNumber = [int]($offset / $chunkSize)
    $chunkPath = Join-Path $chunkDirectory ('chunk-{0:D2}.bin' -f $chunkNumber)
    $logPath = Join-Path $chunkDirectory ('chunk-{0:D2}.log' -f $chunkNumber)
    if ((Test-Path -LiteralPath $chunkPath) -and (Get-Item -LiteralPath $chunkPath).Length -eq $chunkSize) {
        Write-Output "Chunk $chunkNumber already complete."
        continue
    }
    Write-Output ("Reading chunk {0}/7 at 0x{1:X6}..." -f $chunkNumber, $offset)
    & $Python $EspTool --chip esp32s3 --port $Port --baud $Baud read_flash $offset $chunkSize $chunkPath *> $logPath
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $logPath -Tail 20
        throw "Flash read failed for chunk $chunkNumber."
    }
    if ((Get-Item -LiteralPath $chunkPath).Length -ne $chunkSize) { throw "Chunk $chunkNumber has the wrong size." }
    $chunkHash = Get-FileHash -LiteralPath $chunkPath -Algorithm SHA256
    Write-Output "Chunk $chunkNumber verified: $($chunkHash.Hash)"
}

$output = [IO.File]::Open($outputPath, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    for ($chunkNumber = 0; $chunkNumber -lt 8; $chunkNumber++) {
        $chunkPath = Join-Path $chunkDirectory ('chunk-{0:D2}.bin' -f $chunkNumber)
        $input = [IO.File]::OpenRead($chunkPath)
        try { $input.CopyTo($output) } finally { $input.Dispose() }
    }
}
finally { $output.Dispose() }

$backup = Get-Item -LiteralPath $outputPath
if ($backup.Length -ne $flashSize) { throw "Combined backup has the wrong size: $($backup.Length)" }
$hash = Get-FileHash -LiteralPath $outputPath -Algorithm SHA256
Write-Output "Backup complete: $outputPath"
Write-Output "Size: $($backup.Length)"
Write-Output "SHA256: $($hash.Hash)"
