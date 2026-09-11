# ============================================================================
#  measure_step.ps1 - capture the VOFA+ JustFloat stream from the fan project
#  and evaluate the PID step response automatically.
#
#  Channels (see Core/Src/app.c): 0=target 1=actual 2=pwm 3=error 4=angle
#
#  Usage:
#    powershell -ExecutionPolicy Bypass -File .\tools\measure_step.ps1
#    powershell -ExecutionPolicy Bypass -File .\tools\measure_step.ps1 -Port COM7 -Seconds 15 -Csv out.csv
#
#  IMPORTANT: VOFA+ (or any serial terminal) must be CLOSED - a COM port can
#  only be owned by one program at a time.
#
#  NOTE: keep this file pure ASCII. Windows PowerShell 5.1 reads .ps1 as
#  ANSI/GBK when there is no BOM, so non-ASCII comments break parsing.
# ============================================================================
param(
    [string]$Port    = "COM7",
    [int]   $Baud    = 115200,
    [int]   $Seconds = 15,
    [string]$Csv     = ""
)

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------- 1) capture
if (-not [System.IO.Ports.SerialPort]::GetPortNames().Contains($Port)) {
    Write-Host ("Port " + $Port + " not found. Available: " + ([System.IO.Ports.SerialPort]::GetPortNames() -join ", ")) -ForegroundColor Red
    exit 2
}

$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, "None", 8, "One")
$sp.ReadTimeout = 500
try { $sp.Open() } catch {
    Write-Host ("Cannot open " + $Port + " : " + $_.Exception.Message) -ForegroundColor Red
    Write-Host "Close VOFA+ / other serial tools first." -ForegroundColor Yellow
    exit 3
}

Write-Host ("Capturing " + $Seconds + " s from " + $Port + " ...") -ForegroundColor Cyan
$ms = New-Object System.IO.MemoryStream
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$buf = New-Object byte[] 4096
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    try {
        $n = $sp.Read($buf, 0, $buf.Length)
        if ($n -gt 0) { $ms.Write($buf, 0, $n) }
    } catch { }
}
$sp.Close(); $sp.Dispose()
$data = $ms.ToArray()
$ms.Dispose()
Write-Host ("Captured " + $data.Length + " bytes") -ForegroundColor Cyan

# ------------------------------------------- 2) parse JustFloat frames (24 B)
$tg = New-Object System.Collections.Generic.List[double]
$ac = New-Object System.Collections.Generic.List[double]
$pw = New-Object System.Collections.Generic.List[double]
$er = New-Object System.Collections.Generic.List[double]
$an = New-Object System.Collections.Generic.List[double]

for ($i = 23; $i -lt $data.Length; $i++) {
    if (($data[$i] -eq 0x7F) -and ($data[$i-1] -eq 0x80) -and ($data[$i-2] -eq 0x00) -and ($data[$i-3] -eq 0x00)) {
        $s = $i - 23
        $v0 = [BitConverter]::ToSingle($data, $s)
        $v1 = [BitConverter]::ToSingle($data, $s + 4)
        $v2 = [BitConverter]::ToSingle($data, $s + 8)
        $v3 = [BitConverter]::ToSingle($data, $s + 12)
        $v4 = [BitConverter]::ToSingle($data, $s + 16)
        $ok = $true
        foreach ($v in @($v0,$v1,$v2,$v3,$v4)) {
            if ([double]::IsNaN($v) -or [double]::IsInfinity($v) -or [Math]::Abs($v) -gt 1.0e6) { $ok = $false }
        }
        if ($ok) {
            $tg.Add($v0); $ac.Add($v1); $pw.Add($v2); $er.Add($v3); $an.Add($v4)
            $i += 3
        }
    }
}

$n = $tg.Count
if ($n -lt 50) {
    Write-Host ("Too few valid frames (" + $n + "). Is the board running / baud 115200 / JustFloat?") -ForegroundColor Red
    exit 4
}
Write-Host ("Frames: " + $n + "   (approx " + [math]::Round($n / $Seconds, 1) + " fps)") -ForegroundColor Cyan

if ($Csv -ne "") {
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("idx,target,actual,pwm,error,angle")
    for ($k = 0; $k -lt $n; $k++) {
        [void]$sb.AppendLine(($k.ToString() + "," + $tg[$k] + "," + $ac[$k] + "," + $pw[$k] + "," + $er[$k] + "," + $an[$k]))
    }
    Set-Content -Path $Csv -Value $sb.ToString() -Encoding ASCII
    Write-Host ("CSV written: " + $Csv) -ForegroundColor Cyan
}

# --------------------------------------------------- 3) find upward target steps
$lvl = 0.0
for ($k = 0; $k -lt $n; $k++) { if ($tg[$k] -gt $lvl) { $lvl = $tg[$k] } }
if ($lvl -le 1.0) { Write-Host "Target never rises (is APP_AUTO_STEP on?)." -ForegroundColor Yellow; exit 0 }
$thr = $lvl * 0.5

$edges = New-Object System.Collections.Generic.List[int]
for ($k = 1; $k -lt $n; $k++) {
    if (($tg[$k] -gt $thr) -and ($tg[$k-1] -le $thr)) { $edges.Add($k) }
}
if ($edges.Count -eq 0) { Write-Host "No target step found." -ForegroundColor Yellow; exit 0 }

# ------------------------------ 3b) light smoothing of the actual (quantisation)
$acs = New-Object 'double[]' $n
for ($k = 0; $k -lt $n; $k++) {
    $a = [Math]::Max(0, $k - 2)
    $b = [Math]::Min($n - 1, $k + 2)
    $s = 0.0
    for ($j = $a; $j -le $b; $j++) { $s += $ac[$j] }
    $acs[$k] = $s / ($b - $a + 1)
}

# ------------------------------------------------ 4) evaluate each step response
$rows = New-Object System.Collections.Generic.List[object]
for ($e = 0; $e -lt $edges.Count; $e++) {
    $k = $edges[$e]
    # evaluation window = from the rising edge until the target drops back below
    # the threshold (i.e. only the "high" phase, not the following rest phase).
    $end = $n - 1
    for ($j = $k + 1; $j -lt $n; $j++) {
        if ($tg[$j] -le $thr) { $end = $j - 1; break }
    }
    if (($end - $k) -lt 40) { continue }

    $b0 = [Math]::Max(0, $k - 10)
    $base = 0.0
    for ($j = $b0; $j -lt $k; $j++) { $base += $ac[$j] }
    $base = $base / ($k - $b0)

    $f0 = [Math]::Max($k, $end - 20)
    $fin = 0.0
    for ($j = $f0; $j -le $end; $j++) { $fin += $ac[$j] }
    $fin = $fin / ($end - $f0 + 1)

    $pk = -1.0e9
    for ($j = $k; $j -le $end; $j++) { if ($ac[$j] -gt $pk) { $pk = $ac[$j] } }

    $amp = $fin - $base
    if ($amp -lt 1.0) { continue }

    $p10 = $base + 0.10 * $amp
    $p90 = $base + 0.90 * $amp
    $t10 = -1; $t90 = -1
    for ($j = $k; $j -le $end; $j++) {
        if (($t10 -lt 0) -and ($ac[$j] -ge $p10)) { $t10 = $j }
        if (($t90 -lt 0) -and ($ac[$j] -ge $p90)) { $t90 = $j; break }
    }

    $band = 0.05 * [Math]::Abs($amp)
    $ts = $k
    for ($j = $end; $j -ge $k; $j--) {
        if ([Math]::Abs($acs[$j] - $fin) -gt $band) { $ts = $j + 1; break }
    }

    $riseMs = -1
    if (($t10 -ge 0) -and ($t90 -ge 0)) { $riseMs = ($t90 - $t10) * 20 }

    $rows.Add([pscustomobject]@{
        Step      = $e + 1
        Target    = [math]::Round($tg[$end], 1)
        Base      = [math]::Round($base, 1)
        Final     = [math]::Round($fin, 1)
        Peak      = [math]::Round($pk, 1)
        Overshoot = [math]::Round((($pk - $fin) / $amp) * 100.0, 1)
        RiseMs    = $riseMs
        SettleMs  = ($ts - $k) * 20
        SSErr     = [math]::Round($fin - $tg[$end], 1)
        MaxPwm    = [math]::Round((($pw[($k)..($end)] | Measure-Object -Maximum).Maximum), 0)
    })
}

if ($rows.Count -eq 0) { Write-Host "Steps found but none usable (motor not moving?)." -ForegroundColor Yellow; exit 0 }

Write-Host ""
Write-Host "================= STEP RESPONSE =================" -ForegroundColor Green
$rows | Format-Table -AutoSize

$avgOs = ($rows | Measure-Object -Property Overshoot -Average).Average
$avgTs = ($rows | Measure-Object -Property SettleMs  -Average).Average
$avgSe = ($rows | Measure-Object -Property SSErr     -Average).Average
$maxPw = ($rows | Measure-Object -Property MaxPwm    -Maximum).Maximum

Write-Host ("AVG  overshoot = " + [math]::Round($avgOs,1) + " %    settling = " + [math]::Round($avgTs,0) + " ms    SS error = " + [math]::Round($avgSe,1) + "    peak PWM = " + $maxPw) -ForegroundColor Green
Write-Host ""
Write-Host "How to read it:" -ForegroundColor Cyan
Write-Host "  overshoot big (>15%)  -> Kp too big, or Ki too big        -> lower Kp / Ki"
Write-Host "  settling slow         -> Kp too small (or Ki too small)   -> raise Kp"
Write-Host "  SS error not ~0       -> Ki too small                     -> raise Ki"
Write-Host "  peak PWM saturating at 1000 -> not enough headroom/load   -> check VM / deadzone"
