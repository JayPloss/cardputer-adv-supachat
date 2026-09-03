param(
    [ValidateSet('papa', 'albie', 'julien', 'josee', 'vero', 'theo')]
    [string]$Username = 'papa',
    [string]$Server = 'root@65.108.148.87',
    [string]$IdentityFile = 'C:\Users\PC\.ssh\id_ed25519'
)

$ErrorActionPreference = 'Stop'
Write-Host "Setting the Authentik password for SupaChat user '$Username'."
Write-Host 'The password is entered directly into Authentik and is not stored by this script.'
$remoteCommand = "container=`$(docker ps --filter label=com.docker.compose.service=authentik-server --format '{{.Names}}' | head -n 1); test -n `"`$container`"; docker exec -it `"`$container`" ak changepassword $Username"
& ssh.exe -t -i $IdentityFile $Server $remoteCommand
if ($LASTEXITCODE -ne 0) { throw 'Authentik password change failed.' }
