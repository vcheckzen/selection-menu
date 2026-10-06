<#
  tools\make_icon.ps1 - renders res\app.ico
  Windows XP cannot read PNG-compressed icon frames, so every frame is written
  as a classic 32bpp BMP (BITMAPINFOHEADER + XOR bitmap + AND mask).
#>
param([string]$Out)

Add-Type -AssemblyName System.Drawing

if (-not $Out) { $Out = Join-Path $PSScriptRoot '..\res\app.ico' }

function Get-AndRowBytes([int]$w) {
    $bits = [Math]::Floor(($w + 31) / 32) * 32
    return [int]($bits / 8)
}

function New-BoltBitmap([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $d = [single]($s * 0.96)
    $x0 = [single](($s - $d) / 2)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($x0, $x0, $d, $d, 180, 90)
    $path.AddArc($x0 + $d - $d, $x0, $d, $d, 270, 90)
    $path.AddArc($x0, $x0 + $d - $d, $d, $d, 0, 90)
    $path.AddArc($x0, $x0, $d, $d, 90, 90)
    $path.CloseFigure()

    $b1 = New-Object System.Drawing.PointF(0, 0)
    $b2 = New-Object System.Drawing.PointF($s, $s)
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($b1, $b2,
        [System.Drawing.Color]::FromArgb(255, 0, 129, 224),
        [System.Drawing.Color]::FromArgb(255, 0, 86, 173))
    $g.FillPath($brush, $path)
    $brush.Dispose()

    $f = $s / 15.5
    $pts = @(
        @(5.6, -6.6), @(-3.4, 1.2), @(-0.8, 1.2),
        @(-2.2, 6.6), @(4.6, -1.4), @(0.9, -1.4)
    )
    $poly = New-Object System.Drawing.PointF[] 6
    for ($i = 0; $i -lt 6; $i++) {
        $boltDx = $s * 0.00
        $boltDy = -$s * 0.04
        $px = [single]($s / 2 + $boltDx + $pts[$i][0] * 1.3 * $f)
        $py = [single]($s / 2 + $boltDy + $pts[$i][1] * $f)
        $poly[$i] = New-Object System.Drawing.PointF -ArgumentList @($px, $py)
    }
    $gb = New-Object System.Drawing.SolidBrush -ArgumentList ([System.Drawing.Color]::White)
    $g.FillPolygon($gb, $poly)
    $gb.Dispose()
    $path.Dispose()
    $g.Dispose()
    return $bmp
}

$sizes = @(16, 24, 32, 48, 64, 128)
$frames = @()

foreach ($s in $sizes) {
    $bmp = New-BoltBitmap $s
    $data = New-Object byte[] ($s * $s * 4)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $s, $s)
    $lock = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                           [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $row = New-Object byte[] ($s * 4)
    for ($y = 0; $y -lt $s; $y++) {
        [System.Runtime.InteropServices.Marshal]::Copy($lock.Scan0.ToInt64() + $y * $lock.Stride, $row, 0, $s * 4)
        for ($x = 0; $x -lt $s; $x++) {
            $src = $x * 4
            $dst = (($s - 1 - $y) * $s + $x) * 4
            $data[$dst + 0] = $row[$src + 0]
            $data[$dst + 1] = $row[$src + 1]
            $data[$dst + 2] = $row[$src + 2]
            $data[$dst + 3] = $row[$src + 3]
        }
    }
    $bmp.UnlockBits($lock)
    $bmp.Dispose()
    $frames += ,@($s, $data)
}

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter -ArgumentList $ms

$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$sizes.Count)

$offset = 6 + 16 * $sizes.Count
foreach ($fr in $frames) {
    $s = $fr[0]
    $andRow = Get-AndRowBytes $s
    $imgSize = 40 + ($s * $s * 4) + ($andRow * $s)
    $w = $s
    if ($w -ge 256) { $w = 0 }
    $bw.Write([byte]$w)
    $bw.Write([byte]$s)
    $bw.Write([byte]0)
    $bw.Write([byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$imgSize)
    $bw.Write([UInt32]$offset)
    $offset += $imgSize
}

foreach ($fr in $frames) {
    $s = $fr[0]
    $data = $fr[1]
    $andRow = Get-AndRowBytes $s
    $bw.Write([UInt32]40)
    $bw.Write([int]$s)
    $bw.Write([int]($s * 2))
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]0)
    $bw.Write([UInt32](($s * $s * 4) + ($andRow * $s)))
    $bw.Write([int]0)
    $bw.Write([int]0)
    $bw.Write([UInt32]0)
    $bw.Write([UInt32]0)
    $bw.Write($data)
    $bw.Write((New-Object byte[] ($andRow * $s)))
}
$bw.Flush()

$dir = Split-Path -Parent $Out
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
[System.IO.File]::WriteAllBytes($Out, $ms.ToArray())
$bw.Dispose()
$ms.Dispose()
Write-Output ("wrote {0} ({1} bytes)" -f $Out, (Get-Item $Out).Length)
