param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$WorldId = "xatu_weiyang"
)

$ErrorActionPreference = "Stop"
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$meshRoot = Join-Path $ProjectRoot "simulation\models\$WorldId\meshes"
$outputRoot = Join-Path $ProjectRoot "resources\worldmaps"

function Read-ObjMesh([string]$Path) {
    $vertices = [System.Collections.Generic.List[object]]::new()
    $faces = [System.Collections.Generic.List[object]]::new()
    foreach ($line in [System.IO.File]::ReadLines($Path)) {
        if ($line.StartsWith("v ")) {
            $parts = $line.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
            $vertices.Add(@(
                [double]::Parse($parts[1], $culture),
                [double]::Parse($parts[2], $culture),
                [double]::Parse($parts[3], $culture)
            ))
        } elseif ($line.StartsWith("f ")) {
            $parts = $line.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
            $indices = [System.Collections.Generic.List[int]]::new()
            for ($index = 1; $index -lt $parts.Count; ++$index) {
                $vertexIndex = [int]($parts[$index].Split('/')[0])
                if ($vertexIndex -lt 0) {
                    $vertexIndex = $vertices.Count + $vertexIndex
                } else {
                    --$vertexIndex
                }
                $indices.Add($vertexIndex)
            }
            $faces.Add($indices.ToArray())
        }
    }
    return [pscustomobject]@{ Vertices = $vertices; Faces = $faces }
}

function Format-Number([double]$Value) {
    return $Value.ToString("0.###", $culture)
}

function Convert-FacesToSvgPath($Mesh, $Faces) {
    $builder = [System.Text.StringBuilder]::new()
    foreach ($face in $Faces) {
        if ($face.Count -lt 3) { continue }
        for ($pointIndex = 0; $pointIndex -lt $face.Count; ++$pointIndex) {
            $vertex = $Mesh.Vertices[$face[$pointIndex]]
            # Qt Quick's SVG image loader is stricter than QSvgRenderer when
            # many closed subpaths are concatenated. Keep commands and
            # subpaths explicitly separated instead of emitting "...ZM...".
            $command = if ($pointIndex -eq 0) { "M " } else { " L " }
            [void]$builder.Append($command)
            [void]$builder.Append((Format-Number $vertex[0]))
            [void]$builder.Append(" ")
            [void]$builder.Append((Format-Number (-$vertex[1])))
        }
        [void]$builder.Append(" Z ")
    }
    return $builder.ToString().Trim()
}

function Get-RoofFaces($Mesh) {
    $roofs = [System.Collections.Generic.List[object]]::new()
    foreach ($face in $Mesh.Faces) {
        $minimum = [double]::PositiveInfinity
        $maximum = [double]::NegativeInfinity
        foreach ($vertexIndex in $face) {
            $height = $Mesh.Vertices[$vertexIndex][2]
            $minimum = [Math]::Min($minimum, $height)
            $maximum = [Math]::Max($maximum, $height)
        }
        if ($minimum -gt 0.001 -and [Math]::Abs($maximum - $minimum) -lt 0.0001) {
            $roofs.Add([pscustomobject]@{ Face = $face; Height = $maximum })
        }
    }
    return $roofs
}

function New-WorldSvg([string]$Path, [hashtable]$Palette, $Meshes, $RoofFaces,
                      [double]$MinEast, [double]$MaxEast,
                      [double]$MinNorth, [double]$MaxNorth) {
    $width = $MaxEast - $MinEast
    $height = $MaxNorth - $MinNorth
    $viewBoxY = -$MaxNorth
    $greenPath = Convert-FacesToSvgPath $Meshes.Green $Meshes.Green.Faces
    $roadPath = Convert-FacesToSvgPath $Meshes.Road $Meshes.Road.Faces
    $waterPath = Convert-FacesToSvgPath $Meshes.Water $Meshes.Water.Faces
    $greenElement = if ([string]::IsNullOrWhiteSpace($greenPath)) { "" } else {
        "  <path d=`"$greenPath`" fill=`"$($Palette.Green)`" fill-opacity=`"0.88`"/>"
    }
    $roadElement = if ([string]::IsNullOrWhiteSpace($roadPath)) { "" } else {
        "  <path d=`"$roadPath`" fill=`"$($Palette.Road)`" fill-opacity=`"0.94`"/>"
    }
    $waterElement = if ([string]::IsNullOrWhiteSpace($waterPath)) { "" } else {
        "  <path d=`"$waterPath`" fill=`"$($Palette.Water)`" fill-opacity=`"0.94`" stroke=`"$($Palette.WaterEdge)`" stroke-width=`"0.8`"/>"
    }

    $heightGroups = $RoofFaces | Group-Object { Format-Number $_.Height } |
        Sort-Object { [double]::Parse($_.Name, $culture) }
    $buildingLayers = [System.Text.StringBuilder]::new()
    foreach ($group in $heightGroups) {
        $heightValue = [double]::Parse($group.Name, $culture)
        $normalized = [Math]::Min(1.0, $heightValue / 40.0)
        $opacity = 0.70 + 0.24 * $normalized
        # Keep every OBJ face as one array item. Without the unary comma,
        # PowerShell enumerates the vertex-index array and produces an empty
        # SVG building path even though the collision metadata is valid.
        $faces = @($group.Group | ForEach-Object { ,$_.Face })
        $pathData = Convert-FacesToSvgPath $Meshes.Building $faces
        if ([string]::IsNullOrWhiteSpace($pathData)) { continue }
        [void]$buildingLayers.AppendLine(
            "  <path d=`"$pathData`" fill=`"$($Palette.Building)`" fill-opacity=`"$($opacity.ToString('0.00', $culture))`" stroke=`"none`"/>")
    }

    $svg = @"
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="$(Format-Number $MinEast) $(Format-Number $viewBoxY) $(Format-Number $width) $(Format-Number $height)" preserveAspectRatio="none">
  <defs>
    <filter id="buildingShadow" x="-10%" y="-10%" width="120%" height="120%">
      <feDropShadow dx="1.4" dy="2.0" stdDeviation="1.2" flood-color="$($Palette.Shadow)" flood-opacity="$($Palette.ShadowOpacity)"/>
    </filter>
  </defs>
$greenElement
$roadElement
$waterElement
  <g filter="url(#buildingShadow)">
$buildingLayers  </g>
</svg>
"@
    [System.IO.File]::WriteAllText($Path, $svg, [System.Text.UTF8Encoding]::new($false))
}

$meshes = [pscustomobject]@{
    Building = Read-ObjMesh (Join-Path $meshRoot "buildings.obj")
    Road = Read-ObjMesh (Join-Path $meshRoot "roads.obj")
    Green = Read-ObjMesh (Join-Path $meshRoot "green_areas.obj")
    Water = Read-ObjMesh (Join-Path $meshRoot "water.obj")
}
$roofFaces = Get-RoofFaces $meshes.Building

# Derive bounds from the generated meshes so the same script works for every
# bounded city extract. They remain world coordinates: X east and Y north.
$allVertices = @(
    $meshes.Building.Vertices
    $meshes.Road.Vertices
    $meshes.Green.Vertices
    $meshes.Water.Vertices
)
if ($allVertices.Count -eq 0) { throw "World $WorldId has no mesh vertices." }
$rawMinEast = ($allVertices | ForEach-Object { $_[0] } | Measure-Object -Minimum).Minimum
$rawMaxEast = ($allVertices | ForEach-Object { $_[0] } | Measure-Object -Maximum).Maximum
$rawMinNorth = ($allVertices | ForEach-Object { $_[1] } | Measure-Object -Minimum).Minimum
$rawMaxNorth = ($allVertices | ForEach-Object { $_[1] } | Measure-Object -Maximum).Maximum
$extent = [Math]::Max($rawMaxEast - $rawMinEast, $rawMaxNorth - $rawMinNorth)
$margin = [Math]::Max(15.0, $extent * 0.025)
$minEast = [Math]::Floor($rawMinEast - $margin)
$maxEast = [Math]::Ceiling($rawMaxEast + $margin)
$minNorth = [Math]::Floor($rawMinNorth - $margin)
$maxNorth = [Math]::Ceiling($rawMaxNorth + $margin)

[System.IO.Directory]::CreateDirectory($outputRoot) | Out-Null

$lightPalette = @{
    Green = "#C8DCC8"; Road = "#D3D8DD"; Water = "#A9CDDE"
    WaterEdge = "#79AFC8"; Building = "#9CA8B3"; BuildingEdge = "#657381"
    Shadow = "#243140"; ShadowOpacity = "0.22"
}
$darkPalette = @{
    Green = "#294537"; Road = "#303B46"; Water = "#244B60"
    WaterEdge = "#3F7893"; Building = "#526171"; BuildingEdge = "#7E8D9D"
    Shadow = "#02060A"; ShadowOpacity = "0.55"
}

New-WorldSvg (Join-Path $outputRoot "$WorldId-light.svg") $lightPalette $meshes $roofFaces `
    $minEast $maxEast $minNorth $maxNorth
New-WorldSvg (Join-Path $outputRoot "$WorldId-dark.svg") $darkPalette $meshes $roofFaces `
    $minEast $maxEast $minNorth $maxNorth

$obstacles = [System.Collections.Generic.List[object]]::new()
foreach ($roof in $roofFaces) {
    $face = $roof.Face
    if ($face.Count -ne 3) { continue }
    $a = $meshes.Building.Vertices[$face[0]]
    $b = $meshes.Building.Vertices[$face[1]]
    $c = $meshes.Building.Vertices[$face[2]]
    $obstacles.Add(@($a[0], $a[1], $b[0], $b[1], $c[0], $c[1], $roof.Height))
}
$metadata = [ordered]@{
    format = "FlightBenchWorldMap"
    formatVersion = 1
    worldId = $WorldId
    coordinateFrame = "ENU"
    bounds = [ordered]@{
        minEast = $minEast; maxEast = $maxEast
        minNorth = $minNorth; maxNorth = $maxNorth
    }
    safetyMarginMeters = 3.0
    buildingRoofTriangles = $obstacles
}
$json = $metadata | ConvertTo-Json -Depth 6 -Compress
[System.IO.File]::WriteAllText(
    (Join-Path $outputRoot "$WorldId.json"), $json,
    [System.Text.UTF8Encoding]::new($false))

Write-Output "Generated $WorldId map: $($roofFaces.Count) roof triangles."
