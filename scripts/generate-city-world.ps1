[CmdletBinding(DefaultParameterSetName = 'OnlinePlace')]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-z0-9][a-z0-9_]{1,47}$')]
    [string]$WorldId,

    [Parameter(Mandatory = $true)]
    [ValidateLength(2, 80)]
    [string]$Name,

    [ValidateSet('campus', 'urban', 'residential', 'commercial', 'industrial', 'waterfront', 'park')]
    [string]$SceneType = 'urban',

    [Parameter(Mandatory = $true, ParameterSetName = 'OnlinePlace')]
    [string]$Place,

    [Parameter(Mandatory = $true, ParameterSetName = 'OnlineCoordinates')]
    [ValidateRange(-90, 90)]
    [double]$Latitude,

    [Parameter(Mandatory = $true, ParameterSetName = 'OnlineCoordinates')]
    [ValidateRange(-180, 180)]
    [double]$Longitude,

    [Parameter(ParameterSetName = 'OnlinePlace')]
    [Parameter(ParameterSetName = 'OnlineCoordinates')]
    [ValidateRange(100, 2000)]
    [double]$Radius = 700,

    [Parameter(Mandatory = $true, ParameterSetName = 'Offline')]
    [string]$SourceOsmJson,

    [Parameter(ParameterSetName = 'Offline')]
    [long]$BoundaryWayId,

    [Parameter(ParameterSetName = 'Offline')]
    [double[]]$Bounds,

    [string]$Description,
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$python = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command python -ErrorAction SilentlyContinue }
if (-not $python) { throw 'Python 3 was not found.' }

$sourceRoot = Join-Path $ProjectRoot "city-sources\$WorldId"
$rawPath = Join-Path $sourceRoot 'source\osm.json'
$scenePath = Join-Path $sourceRoot "$WorldId.scene.json"
$previewPath = Join-Path $sourceRoot "$WorldId.preview.svg"
[IO.Directory]::CreateDirectory((Split-Path -Parent $rawPath)) | Out-Null

function Invoke-PythonChecked {
    param([string[]]$Arguments)
    Write-Host ('> python ' + ($Arguments -join ' ')) -ForegroundColor DarkGray
    & $python.Source @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Python command failed with exit code $LASTEXITCODE." }
}

if ($PSCmdlet.ParameterSetName -ne 'Offline') {
    $fetchArguments = @(
        (Join-Path $PSScriptRoot 'cityworld\fetch_osm.py'),
        '--radius', $Radius.ToString([Globalization.CultureInfo]::InvariantCulture),
        '--output', $rawPath
    )
    if ($PSCmdlet.ParameterSetName -eq 'OnlinePlace') {
        $fetchArguments += @('--place', $Place)
    } else {
        $fetchArguments += @(
            '--latitude', $Latitude.ToString([Globalization.CultureInfo]::InvariantCulture),
            '--longitude', $Longitude.ToString([Globalization.CultureInfo]::InvariantCulture)
        )
    }
    Invoke-PythonChecked $fetchArguments
    # Windows PowerShell 5.1 otherwise decodes BOM-less UTF-8 JSON through the
    # active ANSI code page. A multibyte Chinese character can then consume the
    # following ASCII quote and make valid JSON appear malformed.
    $download = Get-Content -Raw -Encoding UTF8 -LiteralPath $rawPath | ConvertFrom-Json
    $Bounds = @($download._flightbench.bounds | ForEach-Object { [double]$_ })
} else {
    $SourceOsmJson = (Resolve-Path -LiteralPath $SourceOsmJson).Path
    Copy-Item -LiteralPath $SourceOsmJson -Destination $rawPath -Force
    $sourceQuery = Join-Path (Split-Path -Parent $SourceOsmJson) 'overpass.query'
    if (Test-Path -LiteralPath $sourceQuery) {
        Copy-Item -LiteralPath $sourceQuery -Destination (Join-Path (Split-Path -Parent $rawPath) 'overpass.query') -Force
    }
    if ($BoundaryWayId -le 0 -and (!$Bounds -or $Bounds.Count -ne 4)) {
        $metadata = Get-Content -Raw -Encoding UTF8 -LiteralPath $rawPath | ConvertFrom-Json
        if ($metadata._flightbench.bounds) {
            $Bounds = @($metadata._flightbench.bounds | ForEach-Object { [double]$_ })
        } else {
            throw 'Offline input needs -BoundaryWayId or four -Bounds values: south, west, north, east.'
        }
    }
}

$convertArguments = @(
    (Join-Path $PSScriptRoot 'cityworld\convert_osm_scene.py'),
    $rawPath, $scenePath,
    '--preview', $previewPath,
    '--world-id', $WorldId,
    '--name', $Name,
    '--scene-type', $SceneType
)
if ($BoundaryWayId -gt 0) {
    $convertArguments += @('--boundary-way', [string]$BoundaryWayId)
} else {
    $convertArguments += '--bounds'
    $convertArguments += @($Bounds | ForEach-Object {
        $_.ToString([Globalization.CultureInfo]::InvariantCulture)
    })
}
Invoke-PythonChecked $convertArguments

Invoke-PythonChecked @(
    (Join-Path $PSScriptRoot 'cityworld\generate_scene_assets.py'),
    $scenePath,
    '--project-root', $ProjectRoot
)

& (Join-Path $PSScriptRoot 'generate-world-map.ps1') -ProjectRoot $ProjectRoot -WorldId $WorldId
if (-not $?) { throw 'The two-dimensional world map could not be generated.' }

$descriptions = @{
    # Keep this script ASCII-only so Windows PowerShell 5.1 does not decode an
    # UTF-8 file without BOM through the active ANSI code page. Callers can
    # still pass a localized -Description from the command line.
    campus = 'Campus model for building and road inspection'
    urban = 'Urban model for route and building-avoidance tests'
    residential = 'Residential model for inspection between buildings'
    commercial = 'Commercial model for mixed-height building missions'
    industrial = 'Industrial model for factory and wide-area inspection'
    waterfront = 'Waterfront model for river and shoreline inspection'
    park = 'Urban park model for road and open-area missions'
}
if ([string]::IsNullOrWhiteSpace($Description)) { $Description = $descriptions[$SceneType] }

$catalogPath = Join-Path $ProjectRoot 'simulation\worlds\catalog.json'
$catalog = if (Test-Path -LiteralPath $catalogPath) {
    @(Get-Content -Raw -Encoding UTF8 -LiteralPath $catalogPath | ConvertFrom-Json)
} else { @() }
$catalog = @($catalog | Where-Object { $_.id -ne $WorldId })
$catalog += [pscustomobject][ordered]@{
    id = $WorldId
    name = $Name
    description = $Description
    previewModelDirectory = "$WorldId/meshes"
    sceneType = $SceneType
    attribution = ([string][char]0x00A9) + ' OpenStreetMap contributors'
}
$catalogJson = ConvertTo-Json -InputObject @($catalog | Sort-Object id) -Depth 5
[System.IO.File]::WriteAllText(
    $catalogPath,
    $catalogJson + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))

Invoke-PythonChecked @(
    (Join-Path $PSScriptRoot 'cityworld\validate_assets.py'),
    '--project-root', $ProjectRoot,
    '--world-id', $WorldId,
    '--scene', $scenePath
)

Write-Host "City world ready: $Name ($WorldId)" -ForegroundColor Green
Write-Host "Source: $sourceRoot"
Write-Host 'Reconfigure and rebuild FlightBench so the new map resources are embedded.'
