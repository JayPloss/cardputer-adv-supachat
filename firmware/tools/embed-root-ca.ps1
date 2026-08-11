param(
    [Parameter(Mandatory)][string]$PemPath,
    [string]$SourcePath = (Join-Path $PSScriptRoot '..\src\main.cpp')
)

$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath $SourcePath -Raw
$pem = (Get-Content -LiteralPath $PemPath -Raw).Trim()
$replacement = "const char kRootCa[] PROGMEM = R`"EOF($pem)EOF`";"
$pattern = 'const char kRootCa\[\] PROGMEM = R"EOF\([\s\S]*?\)EOF";'
$updated = [regex]::Replace($source, $pattern, $replacement, 1)
if ($updated -eq $source) { throw 'Root CA marker was not found.' }
[IO.File]::WriteAllText((Resolve-Path -LiteralPath $SourcePath), $updated, [Text.UTF8Encoding]::new($false))
Write-Output "Embedded root certificate from $PemPath"
