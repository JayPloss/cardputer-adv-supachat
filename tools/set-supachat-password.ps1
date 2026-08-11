param(
    [ValidateSet('papa', 'albie', 'julien')]
    [string]$Username = 'papa',
    [string]$Server = 'root@65.108.148.87',
    [string]$IdentityFile = 'C:\Users\PC\.ssh\id_ed25519'
)

$ErrorActionPreference = 'Stop'
Write-Host "Setting the Authentik password for SupaChat user '$Username'."
Write-Host 'The password is entered directly into Authentik and is not stored by this script.'
& ssh.exe -t -i $IdentityFile $Server "docker exec -it le954-authentik-server ak changepassword $Username"
if ($LASTEXITCODE -ne 0) { throw 'Authentik password change failed.' }
