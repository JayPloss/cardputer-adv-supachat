param(
    [Parameter(Mandatory)][string]$Repository,
    [string]$ExpectedRef = 'origin/main',
    [string]$ExpectedCommit = ''
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $Repository '.git'))) { throw "Not a Git worktree: $Repository" }
$status = & git.exe -C $Repository status --porcelain=v1 --untracked-files=all
if ($LASTEXITCODE -ne 0) { throw 'Unable to read Git status.' }
if ($status) { throw "Release worktree is dirty:`n$($status -join "`n")" }
$head = (& git.exe -C $Repository rev-parse HEAD).Trim()
$expected = (& git.exe -C $Repository rev-parse $ExpectedRef).Trim()
if ($LASTEXITCODE -ne 0) { throw "Unable to resolve expected ref: $ExpectedRef" }
if ($head -ne $expected) { throw "HEAD $head does not match $ExpectedRef $expected" }
if ($ExpectedCommit -and $head -ne $ExpectedCommit) { throw "HEAD $head does not match manifest commit $ExpectedCommit" }
Write-Output "release_source=PASS ref=$ExpectedRef commit=$head"

