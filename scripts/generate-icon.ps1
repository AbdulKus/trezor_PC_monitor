param(
    [string]$Output = "app/windows/pcmonitor.ico"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $Output))
$outputDirectory = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

function New-RoundedPath([System.Drawing.RectangleF]$rect, [float]$radius) {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $radius * 2
    $path.AddArc($rect.X, $rect.Y, $diameter, $diameter, 180, 90)
    $path.AddArc($rect.Right - $diameter, $rect.Y, $diameter, $diameter, 270, 90)
    $path.AddArc($rect.Right - $diameter, $rect.Bottom - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($rect.X, $rect.Bottom - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

$bitmap = [System.Drawing.Bitmap]::new(256, 256,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.Clear([System.Drawing.Color]::Transparent)

$tilePath = New-RoundedPath ([System.Drawing.RectangleF]::new(8, 8, 240, 240)) 52
$tileBrush = [System.Drawing.SolidBrush]::new(
    [System.Drawing.ColorTranslator]::FromHtml("#245edb"))
$graphics.FillPath($tileBrush, $tilePath)

$screenPath = New-RoundedPath ([System.Drawing.RectangleF]::new(35, 42, 186, 137)) 20
$screenBrush = [System.Drawing.SolidBrush]::new(
    [System.Drawing.ColorTranslator]::FromHtml("#07111f"))
$screenPen = [System.Drawing.Pen]::new(
    [System.Drawing.ColorTranslator]::FromHtml("#dbeafe"), 10)
$graphics.FillPath($screenBrush, $screenPath)
$graphics.DrawPath($screenPen, $screenPath)

$graphPen = [System.Drawing.Pen]::new(
    [System.Drawing.ColorTranslator]::FromHtml("#38d6ff"), 12)
$graphPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$graphPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$graphPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
$points = @(
    [System.Drawing.Point]::new(51, 145), [System.Drawing.Point]::new(78, 120),
    [System.Drawing.Point]::new(101, 132), [System.Drawing.Point]::new(131, 85),
    [System.Drawing.Point]::new(158, 110), [System.Drawing.Point]::new(181, 73),
    [System.Drawing.Point]::new(205, 92))
$graphics.DrawLines($graphPen, $points)

$buttonBrush = [System.Drawing.SolidBrush]::new(
    [System.Drawing.ColorTranslator]::FromHtml("#f8fafc"))
$graphics.FillEllipse($buttonBrush, 67, 190, 38, 38)
$graphics.FillEllipse($buttonBrush, 151, 190, 38, 38)

$pngStream = [IO.MemoryStream]::new()
$bitmap.Save($pngStream, [System.Drawing.Imaging.ImageFormat]::Png)
$png = $pngStream.ToArray()
$pngStream.Dispose()

# ICO directory with one 256x256 PNG-compressed 32-bit image. Windows uses
# the PNG directly, preserving alpha and colour accuracy unlike GDI's
# palette-based Icon.Save conversion.
$stream = [IO.File]::Open($outputPath, [IO.FileMode]::Create)
$writer = [IO.BinaryWriter]::new($stream)
$writer.Write([uint16]0)       # reserved
$writer.Write([uint16]1)       # icon
$writer.Write([uint16]1)       # image count
$writer.Write([byte]0)         # 256 px width
$writer.Write([byte]0)         # 256 px height
$writer.Write([byte]0)         # no palette
$writer.Write([byte]0)         # reserved
$writer.Write([uint16]1)       # colour planes
$writer.Write([uint16]32)      # bits per pixel
$writer.Write([uint32]$png.Length)
$writer.Write([uint32]22)      # ICO header + one directory entry
$writer.Write($png)
$writer.Dispose()
$stream.Dispose()
$buttonBrush.Dispose()
$graphPen.Dispose()
$screenPen.Dispose()
$screenBrush.Dispose()
$screenPath.Dispose()
$tileBrush.Dispose()
$tilePath.Dispose()
$graphics.Dispose()
$bitmap.Dispose()

Write-Host "Windows icon generated: $outputPath"
