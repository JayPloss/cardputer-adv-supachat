param(
    [string]$Port = 'COM4',
    [ValidateSet('albie', 'juju', 'papa', 'emmanuelle')][string]$DeviceKey = 'albie',
    [string]$CredentialsFile = 'C:\Users\PC\OneDrive - Plossco\00_Jay-VSCode-Assets\Assets\projects\assets-cardputer-adv-supachat\private\supachat-credentials.json',
    [string]$Python = 'C:\dev\repos\personal-projects\milffinder-field-control\.venv\Scripts\python.exe'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$profileNames = @('COGECO-4BD000', 'Etang', 'Pianoface', '✨Starface✨', 'Porky Worky')
$esptool = 'C:\Users\PC\.platformio\packages\tool-esptoolpy\esptool.py'

function Get-SavedWifiKey {
    param([Parameter(Mandatory)][string]$Ssid)
    $profileOutput = & netsh.exe wlan show profile name="$Ssid" key=clear 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Windows could not read the saved Wi-Fi profile '$Ssid'." }
    foreach ($line in $profileOutput) {
        if ($line -match '^\s*Key Content\s*:\s*(.+?)\s*$') { return $Matches[1] }
    }
    throw "No saved key was found for Wi-Fi profile '$Ssid'."
}

function Invoke-CheckedProcess {
    param([string]$FilePath, [string[]]$Arguments, [string]$FailureMessage)
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$startInfo.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($startInfo)
    $standardOutput = $process.StandardOutput.ReadToEnd()
    $standardError = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "$FailureMessage $standardError$standardOutput" }
}

if (-not (Test-Path -LiteralPath $CredentialsFile)) { throw "Missing private credentials: $CredentialsFile" }
if (-not (Test-Path -LiteralPath $Python) -or -not (Test-Path -LiteralPath $esptool)) { throw 'Python or esptool is missing.' }

$credentials = Get-Content -LiteralPath $CredentialsFile -Raw | ConvertFrom-Json
$credentialProperty = if ($DeviceKey -eq 'papa') { 'papa_device' } else { $DeviceKey }
$deviceCredentials = $credentials.$credentialProperty
if ($null -eq $deviceCredentials -or $deviceCredentials.device_id -ne $DeviceKey -or [string]::IsNullOrWhiteSpace($deviceCredentials.device_token)) {
    throw "The private credential file does not contain $DeviceKey device credentials."
}
if ($null -eq $credentials.mesh -or $credentials.mesh.family_key -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'The private credential file does not contain a valid family mesh key.'
}

$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporaryDirectory = Join-Path $temporaryRoot ('supachat-nvs-' + [guid]::NewGuid().ToString('N'))
$csvPath = Join-Path $temporaryDirectory 'credentials.csv'
$binaryPath = Join-Path $temporaryDirectory 'nvs.bin'

try {
    [void](New-Item -ItemType Directory -Path $temporaryDirectory)
    $rows = [Collections.Generic.List[object]]::new()
    $rows.Add([pscustomobject]@{ key = 'supachat'; type = 'namespace'; encoding = ''; value = '' })
    $slot = 0
    foreach ($ssid in $profileNames) {
        $rows.Add([pscustomobject]@{ key = "ssid$slot"; type = 'data'; encoding = 'string'; value = $ssid })
        $rows.Add([pscustomobject]@{ key = "psk$slot"; type = 'data'; encoding = 'string'; value = (Get-SavedWifiKey -Ssid $ssid) })
        $slot++
    }
    $rows.Add([pscustomobject]@{ key = 'wifi_count'; type = 'data'; encoding = 'u8'; value = $slot })
    $rows.Add([pscustomobject]@{ key = 'device_token'; type = 'data'; encoding = 'string'; value = $deviceCredentials.device_token })
    $rows.Add([pscustomobject]@{ key = 'mesh_key'; type = 'data'; encoding = 'string'; value = $credentials.mesh.family_key.ToLowerInvariant() })
    $rows | ConvertTo-Csv -NoTypeInformation | Set-Content -LiteralPath $csvPath -Encoding utf8NoBOM

    Invoke-CheckedProcess -FilePath $Python -Arguments @(
        '-m', 'esp_idf_nvs_partition_gen', 'generate', '--version', '2',
        $csvPath, $binaryPath, '0x5000'
    ) -FailureMessage 'NVS generation failed.'
    if ((Get-Item -LiteralPath $binaryPath).Length -ne 0x5000) { throw 'Generated NVS partition has the wrong size.' }

    Invoke-CheckedProcess -FilePath $Python -Arguments @(
        $esptool, '--chip', 'esp32s3', '--port', $Port, '--baud', '921600',
        '--before', 'default_reset', '--after', 'hard_reset', 'write_flash', '0x9000', $binaryPath
    ) -FailureMessage "$DeviceKey NVS flash failed."

    Write-Output "$DeviceKey provisioned on $Port with $slot saved Wi-Fi networks. No secrets were printed or committed."
}
finally {
    if ((Test-Path -LiteralPath $temporaryDirectory) -and $temporaryDirectory.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
