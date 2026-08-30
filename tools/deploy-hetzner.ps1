param(
    [string]$Server = 'root@65.108.148.87',
    [string]$IdentityFile = 'C:\Users\PC\.ssh\id_ed25519',
    [string]$PrivateEnvironmentFile = 'C:\Users\PC\OneDrive - Plossco\00_Jay-VSCode-Assets\Assets\projects\assets-cardputer-adv-supachat\private\supachat.env'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$serverRoot = Join-Path $projectRoot 'server'
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporaryDirectory = Join-Path $temporaryRoot ('supachat-deploy-' + [guid]::NewGuid().ToString('N'))
$archive = Join-Path $temporaryDirectory 'supachat-server.tar.gz'
$installer = Join-Path $temporaryDirectory 'install-supachat.sh'

if (-not (Test-Path -LiteralPath $PrivateEnvironmentFile)) { throw "Missing private environment file: $PrivateEnvironmentFile" }

try {
    New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
    & tar.exe -czf $archive -C $serverRoot package.json package-lock.json src web deploy
    if ($LASTEXITCODE -ne 0) { throw 'Failed to create deployment archive.' }
    $installerSource = Get-Content -LiteralPath (Join-Path $serverRoot 'deploy\install-remote.sh') -Raw
    [IO.File]::WriteAllText($installer, $installerSource.Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
    & scp.exe -i $IdentityFile -o BatchMode=yes $archive "${Server}:/tmp/supachat-server.tar.gz"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to upload the server archive.' }
    & scp.exe -i $IdentityFile -o BatchMode=yes $PrivateEnvironmentFile "${Server}:/tmp/supachat.env"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to upload the private environment file.' }
    & scp.exe -i $IdentityFile -o BatchMode=yes $installer "${Server}:/tmp/install-supachat.sh"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to upload the installer.' }
    & ssh.exe -i $IdentityFile -o BatchMode=yes $Server 'bash /tmp/install-supachat.sh'
    if ($LASTEXITCODE -ne 0) { throw 'Remote installation failed.' }
    Write-Output 'SupaChat deployment completed and local health check passed.'
}
finally {
    if ((Test-Path -LiteralPath $temporaryDirectory) -and $temporaryDirectory.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
