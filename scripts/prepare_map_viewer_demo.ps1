param(
  [string]$ArtifactSource = "",
  [switch]$SkipBuild,
  [switch]$Serve,
  [int]$Port = 5173
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ViewerDir = Join-Path $RepoRoot "web\map_viewer"
$PublicDataDir = Join-Path $ViewerDir "public\data"
$ReplayDir = Join-Path $PublicDataDir "replay"

$RequiredDataFiles = @(
  "tile_stats.geojson",
  "tile_corner_witnesses.geojson"
)

$RequiredReplayFiles = @(
  "replay_manifest.json",
  "replay_live_paths.geojson",
  "replay_live_points.geojson",
  "replay_live_routes.geojson",
  "replay_batches.json",
  "replay_batch_tiles.json",
  "sampled_order_explanations.json"
)

function Copy-IfPresent {
  param(
    [string]$Source,
    [string]$Destination
  )
  if (Test-Path -LiteralPath $Source) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    Write-Host "copied $Source -> $Destination"
  }
}

function Require-Command {
  param([string]$Name)
  if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
    throw "$Name was not found on PATH."
  }
}

if ($ArtifactSource -ne "") {
  $ArtifactRoot = Resolve-Path $ArtifactSource
  foreach ($file in $RequiredDataFiles) {
    Copy-IfPresent -Source (Join-Path $ArtifactRoot $file) -Destination (Join-Path $PublicDataDir $file)
  }
  foreach ($file in $RequiredReplayFiles) {
    Copy-IfPresent -Source (Join-Path $ArtifactRoot "replay\$file") -Destination (Join-Path $ReplayDir $file)
    Copy-IfPresent -Source (Join-Path $ArtifactRoot $file) -Destination (Join-Path $ReplayDir $file)
  }
}

$Missing = @()
foreach ($file in $RequiredDataFiles) {
  $path = Join-Path $PublicDataDir $file
  if (-not (Test-Path -LiteralPath $path)) {
    $Missing += "public\data\$file"
  }
}
foreach ($file in $RequiredReplayFiles) {
  $path = Join-Path $ReplayDir $file
  if (-not (Test-Path -LiteralPath $path)) {
    $Missing += "public\data\replay\$file"
  }
}

if ($Missing.Count -gt 0) {
  Write-Warning "Missing demo artifacts:"
  foreach ($file in $Missing) {
    Write-Warning "  $file"
  }
  Write-Warning "The viewer can still run, but missing layers or replay modes will be disabled."
}

if (-not $SkipBuild) {
  Require-Command "npm.cmd"
  Push-Location $ViewerDir
  try {
    if (-not (Test-Path -LiteralPath "node_modules")) {
      Write-Host "node_modules is missing; running npm.cmd install..."
      npm.cmd install
    }
    npm.cmd run build
  } finally {
    Pop-Location
  }
}

if ($Serve) {
  Push-Location $ViewerDir
  try {
    Require-Command "npm.cmd"
    Write-Host "Starting Map Viewer at http://127.0.0.1:$Port"
    npm.cmd run dev -- --host 127.0.0.1 --port $Port
  } finally {
    Pop-Location
  }
} else {
  Write-Host "Demo check complete."
  Write-Host "Run with -Serve to open http://127.0.0.1:$Port"
}
