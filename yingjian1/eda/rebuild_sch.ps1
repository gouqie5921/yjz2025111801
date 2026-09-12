# Full schematic rebuild for test1 (STM32F103C8T6 minimum system board).
# ASCII only: PowerShell 5.1 mis-reads BOM-less UTF-8 as ANSI.
# Steps: clear+place 38 parts -> module autolayout -> pin-level autoconnect -> NC flags -> strict gate.
param(
  [switch]$SkipClear,
  [switch]$SkipGate
)
$ErrorActionPreference = 'Continue'
$w = 'D:\Projecttest\yingjian1\eda'
$exe = 'D:\Projecttest\easyeda-agent\easyeda.exe'
$proj = 'yingjian1'
$doc = 'P1'

function Show-Tail($path, $lines) {
  if (Test-Path $path) { Get-Content $path -Tail $lines }
}

Write-Output '=== 1/5 place (clear + 38 parts) ==='
& $exe apply "$w\place.playbook.json" --project $proj --doc $doc --yes --step-delay 0.4 2>&1 |
  Out-File -Encoding utf8 "$w\step1-place.log"
Write-Output ("place log tail:"); Show-Tail "$w\step1-place.log" 3

Write-Output '=== 1b verify all 38 present ==='
& "$w\place_rest.ps1" 2>&1 | Select-Object -Last 1

Write-Output '=== 2/5 module autolayout (apply, generous gaps) ==='
& $exe sch autolayout --project $proj --doc $doc --spec "$w\layout.json" --apply --zone-draw=false `
  --part-gap 80 --module-gap 200 --route-channel-gap 80 2>&1 |
  Out-File -Encoding utf8 "$w\step2-layout.log"
Show-Tail "$w\step2-layout.log" 3

Write-Output '=== 3/5 autoconnect (178 connections, wide stub range) ==='
& $exe sch autoconnect --project $proj --doc $doc --spec "$w\netlist.json" `
  --offset-min 25 --offset-max 400 --json 2>&1 |
  Out-File -Encoding utf8 "$w\netresult.json"
node "$w\report.mjs" "$w\netresult.json" 2>&1 | Select-Object -First 3

Write-Output '=== 4/5 no-connect flags ==='
& $exe sch no-connect --project $proj --doc $doc --designator U3 --pin 5 2>&1 | Out-Null
& $exe sch no-connect --project $proj --doc $doc --designator J1 --pin 'A6,B6,A7,B7,A8,B8' 2>&1 | Out-Null
Write-Output 'nc flags issued'

Write-Output '=== 4b readback pin->net reconciliation ==='
& "$w\read_page.ps1" 2>&1 | Select-Object -First 1
node "$w\pins.mjs" "$w\page1.json" --nets 2>&1 | Out-File -Encoding utf8 "$w\nets.txt"
Get-Content "$w\nets.txt" | Select-Object -First 2

if (-not $SkipGate) {
  Write-Output '=== 5/5 strict gate ==='
  & $exe sch gate --strict --json --project $proj --doc $doc 2>&1 | Out-File -Encoding utf8 "$w\gate.json"
  node "$w\gate-summary.mjs" "$w\gate.json" 2>&1 | Select-Object -First 20
}
