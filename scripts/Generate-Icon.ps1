param(
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\res\ForceUnfreeze.ico')
)

Add-Type -AssemblyName System.Drawing

$sizes = @(16, 32, 48, 256)
$pngs = New-Object System.Collections.Generic.List[byte[]]

foreach ($size in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $accent = [System.Drawing.Color]::FromArgb(255, 0, 120, 215)
    $white = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)
    $penAccent = New-Object System.Drawing.Pen $accent, ([Math]::Max(2, [int]($size * 0.10)))
    $penWhite = New-Object System.Drawing.Pen $white, ([Math]::Max(2, [int]($size * 0.08)))
    $penAccent.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $penAccent.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $penWhite.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $penWhite.EndCap = [System.Drawing.Drawing2D.LineCap]::Round

    $pad = [Math]::Max(2, [int]($size * 0.12))
    $rect = New-Object System.Drawing.Rectangle $pad, $pad, ($size - 2 * $pad), ($size - 2 * $pad)
    $g.DrawArc($penAccent, $rect, 135, 300)
    $g.DrawLine($penWhite, [int]($size / 2), [int]($size * 0.18), [int]($size / 2), [int]($size * 0.48))

    $arrow = @(
        [System.Drawing.Point]::new([int]($size * 0.73), [int]($size * 0.18)),
        [System.Drawing.Point]::new([int]($size * 0.91), [int]($size * 0.22)),
        [System.Drawing.Point]::new([int]($size * 0.78), [int]($size * 0.35))
    )
    $brush = New-Object System.Drawing.SolidBrush $accent
    $g.FillPolygon($brush, $arrow)

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs.Add($ms.ToArray())
    $ms.Dispose()
    $brush.Dispose()
    $penAccent.Dispose()
    $penWhite.Dispose()
    $g.Dispose()
    $bmp.Dispose()
}

$dir = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$fs = [System.IO.File]::Create($OutputPath)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$pngs.Count)

$offset = 6 + (16 * $pngs.Count)
for ($i = 0; $i -lt $pngs.Count; $i++) {
    $size = $sizes[$i]
    $bytes = $pngs[$i]
    $bw.Write([byte]($(if ($size -eq 256) { 0 } else { $size })))
    $bw.Write([byte]($(if ($size -eq 256) { 0 } else { $size })))
    $bw.Write([byte]0)
    $bw.Write([byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$bytes.Length)
    $bw.Write([UInt32]$offset)
    $offset += $bytes.Length
}
foreach ($bytes in $pngs) {
    $bw.Write($bytes)
}
$bw.Dispose()
$fs.Dispose()
Write-Host "Generated $OutputPath"
