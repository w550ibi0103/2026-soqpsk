# Regenerates src/sin_lut.inc and src/cos_lut.inc: linear-interpolated sin/cos
# lookup tables that replace hls::sin/hls::cos (see Note.md "CORDIC 發散問題" --
# hls::sin/cos showed a growing, non-deterministic RTL-vs-C divergence under this
# design's free-running/II=1 configuration; a LUT has no hidden pipeline state so
# it can't exhibit that failure mode).
#
# Table spans phase in [-pi, pi) in LutSize equal steps, matching current_phase's
# wrap range in top.cpp. Entry i = f(-pi + i * (2*pi/LutSize)).
#
# 256 entries + linear interpolation was sized so the table's own quantization
# error stays well under 1 LSB of data_t (ap_fixed<16,4>, 12 fractional bits,
# LSB ~= 2.44e-4 rad): interpolation error ~= step^2/8, which for N=256 is
# ~7.5e-5 (~0.3 LSB).
#
# Usage: powershell -File scripts\gen_sincos_lut.ps1
param(
    [int]$LutSize = 256,
    [string]$OutDir = "$PSScriptRoot\..\src"
)

$pi = [Math]::PI
$step = (2.0 * $pi) / $LutSize

$sinFile = Join-Path $OutDir "sin_lut.inc"
$cosFile = Join-Path $OutDir "cos_lut.inc"
$sinWriter = New-Object System.IO.StreamWriter($sinFile, $false)
$cosWriter = New-Object System.IO.StreamWriter($cosFile, $false)

for ($i = 0; $i -lt $LutSize; $i++) {
    $theta = -$pi + $i * $step
    $sinWriter.WriteLine("$([Math]::Sin($theta).ToString('R')),")
    $cosWriter.WriteLine("$([Math]::Cos($theta).ToString('R')),")
}

$sinWriter.Close()
$cosWriter.Close()

Write-Output "Wrote $sinFile and $cosFile ($LutSize entries each)"
