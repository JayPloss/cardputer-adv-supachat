$ErrorActionPreference = 'Stop'
$output = (& platformio device list | Out-String)
$known = @{
  '28:84:85:75:75:A0' = @{ Name='Albie'; Environment='cardputer-adv' }
  '28:84:85:75:5E:FC' = @{ Name='Julien'; Environment='juju' }
  '28:84:85:76:A4:94' = @{ Name='Papa'; Environment='papa' }
  '28:84:85:75:CA:70' = @{ Name='Emmanuelle'; Environment='emmanuelle' }
  '50:78:7D:CD:EC:38' = @{ Name='Naomie'; Environment='naomie' }
  '28:84:85:76:8A:EC' = @{ Name='Andrew'; Environment='andrew' }
}
$port = [regex]::Match($output, '(?m)^\s*(COM\d+)\s*$').Groups[1].Value
$mac = [regex]::Match($output, 'SER=([0-9A-F:]{17})').Groups[1].Value.ToUpperInvariant()
if (-not $port -or -not $known.ContainsKey($mac)) { throw "Unknown or missing Cardputer. port='$port' mac='$mac'" }
[pscustomobject]@{ Port=$port; Mac=$mac; Name=$known[$mac].Name; Environment=$known[$mac].Environment }
