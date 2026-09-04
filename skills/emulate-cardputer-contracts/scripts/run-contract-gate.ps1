param([string]$Repository = (Get-Location).Path)
$ErrorActionPreference = 'Stop'
$contract = Join-Path $Repository 'emulator\device-contract.mjs'
$flow = Join-Path $Repository 'emulator\test-flow.mjs'
if (-not (Test-Path -LiteralPath $contract) -or -not (Test-Path -LiteralPath $flow)) { throw 'Cardputer emulator gates are missing.' }
& node $contract
if ($LASTEXITCODE) { throw "Device-contract emulator failed with exit code $LASTEXITCODE." }
& node $flow
if ($LASTEXITCODE) { throw "Firmware flow emulator failed with exit code $LASTEXITCODE." }
$fox = Join-Path $Repository 'emulator\fox-contract.mjs'
if (Test-Path -LiteralPath $fox) {
  & node $fox
  if ($LASTEXITCODE) { throw "Fox Finding contract failed with exit code $LASTEXITCODE." }
}
