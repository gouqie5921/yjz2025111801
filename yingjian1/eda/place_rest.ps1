# 逐件放置剩余器件：先回读页面已有位号，缺哪个补哪个；每次放置后校验是否真的落地。
# 用法: pwsh -File place_rest.ps1
param(
  [int]$MaxRounds = 4,
  [double]$DelaySec = 1.5
)

$ErrorActionPreference = 'Continue'
$w = 'D:\Projecttest\yingjian1\eda'
$exe = 'D:\Projecttest\easyeda-agent\easyeda.exe'
$lib = '0819f05c4eef4c71ace90d822a990e87'
$proj = 'yingjian1'
$doc = 'P1'
$COLS = 7; $X0 = 70; $Y0 = 70; $DX = 175; $DY = 145

$bom = [System.IO.File]::ReadAllText("$w\bom.json", [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$parts = @($bom.parts)
"BOM total: $($parts.Count)"

function Get-Refs {
  $out = (& $exe sch list --project $proj --page $doc --stay 2>$null | Out-String)
  $names = [regex]::Matches($out, '"designator"\s*:\s*"([^"]*)"') | ForEach-Object { $_.Groups[1].Value }
  return @($names | Where-Object { $_ -ne '' })
}

for ($round = 1; $round -le $MaxRounds; $round++) {
  $have = Get-Refs
  $missing = @($parts | Where-Object { $have -notcontains $_.ref })
  "--- round $round : have=$($have.Count) missing=$($missing.Count) : $($missing.ref -join ',')"
  if ($missing.Count -eq 0) { break }

  foreach ($p in $missing) {
    $i = [array]::IndexOf($parts, $p)
    $col = $i % $COLS
    $row = [math]::Floor($i / $COLS)
    $x = $X0 + $col * $DX
    $y = $Y0 + $row * $DY
    "  place $($p.ref) @($x,$y)"
    & $exe sch place --project $proj --doc $doc --lib $lib --uuid $p.uuid --x $x --y $y --designator $p.ref 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { "    (command failed, will verify by read-back)" }
    Start-Sleep -Seconds $DelaySec
  }
  Start-Sleep -Seconds 2
}

$final = Get-Refs
$still = @($parts | Where-Object { $final -notcontains $_.ref })
"=== FINAL: page has $($final.Count) designators; still missing: $($still.ref -join ',')"
