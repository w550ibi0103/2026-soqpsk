# Regenerates src/sin_lut.inc and src/cos_lut.inc: linear-interpolated sin/cos
# lookup tables that replace hls::sin/hls::cos (see Note.md "CORDIC 發散問題" --
# hls::sin/cos showed a growing, non-deterministic RTL-vs-C divergence under this
# design's free-running/II=1 configuration; a LUT has no hidden pipeline state so
# it can't exhibit that failure mode).
#
# Table spans phase in [-pi, pi) in LutSize equal steps, matching current_phase's
# wrap range in top.cpp. Entry i = f(-pi + i * (2*pi/LutSize)).
#
# 512 entries + linear interpolation was sized so the table's own interpolation
# error stays under 1 LSB of dac_q15_t (ap_fixed<16,1>, Q1.15, 15 fractional
# bits, LSB ~= 3.05e-5): interpolation error ~= step^2/8, which for N=512 is
# ~1.9e-5 (~0.6 LSB). (Originally 256 entries sized against data_t's coarser
# 12-bit fraction -- bumped to 512 when COS_LUT/SIN_LUT storage moved from
# data_t/Q4.12 to dac_q15_t/Q1.15, see Note.md, so interpolation error doesn't
# become the dominant error term at the finer storage precision.)
#
# Usage: powershell -File scripts\gen_sincos_lut.ps1
param(
    [int]$LutSize = 512,
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
