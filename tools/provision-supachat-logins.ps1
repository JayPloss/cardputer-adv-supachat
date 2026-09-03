param(
    [string]$Server = 'root@65.108.148.87',
    [string]$IdentityFile = 'C:\Users\PC\.ssh\id_ed25519',
    [string]$CredentialFile = 'C:\Users\PC\OneDrive - Plossco\00_Jay-VSCode-Assets\Assets\projects\assets-cardputer-adv-supachat\private\supachat-credentials.json'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$credentials = Get-Content -LiteralPath $CredentialFile -Raw | ConvertFrom-Json
if (-not $credentials.PSObject.Properties['logins']) {
    $credentials | Add-Member -NotePropertyName logins -NotePropertyValue ([pscustomobject]@{})
}

$accounts = @(
    [pscustomobject]@{ username = 'josee'; name = 'Josée' },
    [pscustomobject]@{ username = 'vero'; name = 'Véro' },
    [pscustomobject]@{ username = 'theo'; name = 'Théo' }
)

foreach ($account in $accounts) {
    $saved = $credentials.logins.PSObject.Properties[$account.username]
    if ($saved -and $saved.Value.password) {
        $account | Add-Member -NotePropertyName password -NotePropertyValue ([string]$saved.Value.password)
        continue
    }
    $bytes = [Security.Cryptography.RandomNumberGenerator]::GetBytes(18)
    $password = [Convert]::ToBase64String($bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_')
    $account | Add-Member -NotePropertyName password -NotePropertyValue $password
}

$payload = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($accounts | ConvertTo-Json -Compress)))
$python = @"
import base64, json
from authentik.core.models import User
accounts = json.loads(base64.b64decode('$payload').decode('utf-8'))
for account in accounts:
    user = User.objects.get(username=account['username'])
    user.set_password(account['password'])
    user.save(update_fields=['password'])
    if not user.check_password(account['password']):
        raise RuntimeError('Password verification failed for ' + account['username'])
    print('Password set for ' + account['username'])
"@

$remote = 'container=$(docker ps --filter label=com.docker.compose.service=authentik-server --format ''{{.Names}}'' | head -n 1); test -n "$container"; docker exec -i "$container" ak shell'
$python | & ssh.exe -i $IdentityFile -o BatchMode=yes $Server $remote
if ($LASTEXITCODE -ne 0) { throw 'Authentik login provisioning failed; the credential file was not changed.' }

foreach ($account in $accounts) {
    $record = [pscustomobject]@{ username = $account.username; password = $account.password }
    $existing = $credentials.logins.PSObject.Properties[$account.username]
    if ($existing) { $existing.Value = $record } else { $credentials.logins | Add-Member -NotePropertyName $account.username -NotePropertyValue $record }
}

$backup = "$CredentialFile.before-logins-$(Get-Date -Format 'yyyyMMddHHmmss')"
Copy-Item -LiteralPath $CredentialFile -Destination $backup
$credentials | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $CredentialFile -Encoding UTF8
Write-Output "Provisioned josee, vero, and theo; credentials saved under the 'logins' object."
Write-Output "Credential backup: $backup"
