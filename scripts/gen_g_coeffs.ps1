# Regenerates src/g_coeffs_sps{16,8,4}.inc for the dynamic-SPS tfm_modulator IP.
#
# Mirrors Block 4 (Frequency Pulse g(t) Generation) of the reference model at
# C:\Users\eddiehppc\Documents\igps-rasp-receiver-iq-to-toa\iGPS-PlutoSDR\tests\soqpsk-tg\soqpsk-tg.py
# using the same rho/B/T1/T2/Tb/L parameters (Table 2-4), just re-evaluated at a
# different sps. Written in PowerShell/.NET because this machine has no Python.
#
# SPS=2 was dropped from the supported set (2026-07-09): not enough oversampling
# margin for the receiver's symbol timing recovery, and it was also the worst
# case for free-running throughput efficiency before the loop-flatten fix.
#
# Usage: powershell -File scripts\gen_g_coeffs.ps1
param(
    [int[]]$SpsList = @(16, 8, 4),
    [string]$OutDir = "$PSScriptRoot\..\src"
)

$rho = 0.70
$B   = 1.25
$T1  = 1.5
$T2  = 0.5
$Tb  = 1.0
$L   = 8

function Get-Sinc([double]$x) {
    if ([Math]::Abs($x) -lt 1e-12) { return 1.0 }
    return [Math]::Sin([Math]::PI * $x) / ([Math]::PI * $x)
}

foreach ($sps in $SpsList) {
    $N = $L * $sps
    $start = -$L * $Tb / 2.0
    $end   =  $L * $Tb / 2.0
    $step  = ($end - $start) / ($N - 1)

    $g = New-Object double[] $N
    $sum = 0.0

    for ($k = 0; $k -lt $N; $k++) {
        $t = $start + $k * $step
        $absNorm = [Math]::Abs($t / (2.0 * $Tb))

        if ($absNorm -le $T1) {
            $w = 1.0
        } elseif ($absNorm -le ($T1 + $T2)) {
            $w = 0.5 + 0.5 * [Math]::Cos([Math]::PI * ($absNorm - $T1) / $T2)
        } else {
            $w = 0.0
        }

        $x1 = $rho * $B * $t / (2.0 * $Tb)
        $term1 = [Math]::Cos([Math]::PI * $x1) / (1.0 - 4.0 * $x1 * $x1)
        $x2 = $B * $t / (2.0 * $Tb)
        $term2 = Get-Sinc $x2

        $val = $term1 * $term2 * $w
        $g[$k] = $val
        $sum += $val
    }

    $scale = 0.5 / ($sum * ($Tb / $sps))
    for ($k = 0; $k -lt $N; $k++) {
        $g[$k] = $g[$k] * $scale
    }

    $outFile = Join-Path $OutDir "g_coeffs_sps$sps.inc"
    $sw = New-Object System.IO.StreamWriter($outFile, $false)
    foreach ($v in $g) {
        $sw.WriteLine("$($v.ToString('R')),")
    }
    $sw.Close()

    Write-Output "Wrote $outFile ($N taps)"
}
