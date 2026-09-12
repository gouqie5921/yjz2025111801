# Rebuild the test1 schematic: clear+place -> planner autolayout (moderate gaps) -> NC flags.
# Wiring is done afterwards by wire_rest.ps1 (sequential, one CLI call per pin).
# ASCII only.
$ErrorActionPreference = 'Continue'
$w = 'D:\Projecttest\yingjian1\eda'
$exe = 'D:\Projecttest\easyeda-agent\easyeda.exe'
$proj = 'yingjian1'
$doc = 'P1'

Write-Output '=== 1/3 place (clear + 38 parts) ==='
& $exe apply "$w\place.playbook.json" --project $proj --doc $doc --yes --step-delay 0.4 2>&1 |
  Out-File -Encoding utf8 "$w\step1-place.log"
Get-Content "$w\step1-place.log" -Tail 2
& "$w\place_rest.ps1" 2>&1 | Select-Object -Last 1

Write-Output '=== 2/3 module autolayout (planner, moderate gaps) ==='
& $exe sch autolayout --project $proj --doc $doc --spec "$w\layout.json" --apply --zone-draw=false `
  --part-gap 40 --module-gap 150 --route-channel-gap 60 2>&1 |
  Out-File -Encoding utf8 "$w\step2-layout.log"
Get-Content "$w\step2-layout.log" -Tail 2

Write-Output '=== 3/3 no-connect flags ==='
& $exe sch no-connect --project $proj --doc $doc --designator U3 --pin 5 2>&1 | Out-Null
& $exe sch no-connect --project $proj --doc $doc --designator J1 --pin 'A6,B6,A7,B7,A8,B8' 2>&1 | Out-Null
Write-Output 'nc issued'
