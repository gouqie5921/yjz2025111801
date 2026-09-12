# Robust page readback with retry (connector occasionally drops a response).
# Usage: & read_page.ps1 [-OutFile <path>] [-Rounds 4] [-Doc P1] [-Project yingjian1]
# NOTE: keep this file ASCII-only. PowerShell 5.1 reads BOM-less UTF-8 as ANSI and
#       non-ASCII bytes can break parsing.
# NOTE: never name a parameter $Out -- PowerShell variables are case-insensitive and
#       the local $out assignment clobbers it.
param(
  [string]$OutFile = 'D:\Projecttest\yingjian1\eda\page1.json',
  [int]$Rounds = 4,
  [string]$Doc = 'P1',
  [string]$Project = 'yingjian1'
)
$exe = 'D:\Projecttest\easyeda-agent\easyeda.exe'
$enc = New-Object System.Text.UTF8Encoding($false)
for ($i = 1; $i -le $Rounds; $i++) {
  $resp = (& $exe sch list --project $Project --page $Doc --stay --include-pins --include-bbox 2>&1 | Out-String)
  $firstOk = [regex]::Match($resp, '"ok"\s*:\s*(true|false)')
  if ($firstOk.Success -and $firstOk.Groups[1].Value -eq 'true' -and $resp -match '"components"') {
    [System.IO.File]::WriteAllText($OutFile, $resp, $enc)
    Write-Output ("read ok on attempt {0} -> {1} ({2} chars)" -f $i, $OutFile, $resp.Length)
    exit 0
  }
  Write-Output ("attempt {0} failed; sleeping 5s" -f $i)
  Start-Sleep -Seconds 5
}
Write-Output ("READ FAILED after {0} attempts" -f $Rounds)
exit 1
