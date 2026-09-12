# Re-place the markers I touched with the planner (fresh read per pin), instead of the
# explicit direction/offset I forced. Input: pins.txt style list built from the stagger plan.
# ASCII only.
$ErrorActionPreference = 'Continue'
$w = 'D:\Projecttest\yingjian1\eda'
$exe = 'D:\Projecttest\easyeda-agent\easyeda.exe'
$proj = 'yingjian1'
$doc = 'P1'

$pinList = @(
  'C13:2','C13:1','U1:6','U1:13','U1:22','C12:2','JP3:1','U1:47','U1:46','U1:37',
  'J4:2','J4:3','JP1:1','JP1:2','U2:1','U2:2','U2:3','J1:B5','C8:2','U3:8','U3:7',
  'C11:1','J3:1','J3:2','J3:3','J2:4'
)
$nl = [System.IO.File]::ReadAllText("$w\netlist.json", [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$map = @{}
foreach ($c in $nl.connections) { $map[$c.pin] = $c }

$ok = 0; $fail = 0
foreach ($pin in $pinList) {
  $c = $map[$pin]
  if (-not $c) { Write-Output "  skip $pin (not in netlist)"; continue }
  $null = (& $exe sch disconnect --project $proj --doc $doc --pin $pin 2>&1 | Out-String)
  $out = (& $exe sch autoconnect --project $proj --doc $doc --pin $pin --kind $c.kind --net $c.net `
      --offset-min 20 --offset-max 400 --json 2>&1 | Out-String)
  if ($out -match '"ok":\s*true') { $ok++ } else { $fail++; Write-Output "  FAIL $pin -> $($c.net)" }
}
Write-Output ("re-place done: ok={0} fail={1}" -f $ok, $fail)
