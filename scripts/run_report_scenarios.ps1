param(
  [string]$CMakePath = "C:\Users\33625\cmake\cmake-4.3.2-windows-x86_64\bin\cmake.exe",
  [string]$BuildDir = "build-mingw",
  [string]$OutputDir = "build-local\report-scenarios",
  [string]$Requests = "data\normalized\requests.csv",
  [string]$Drivers = "data\normalized\drivers.csv",
  [int]$EndTime = 120,
  [string]$Radius = "0.03",
  [string]$K = "1",
  [int]$CellStatsGridCols = 100,
  [double]$DispatchOpportunityCostScale = 25.0,
  [double]$ColdDropoffPenalty = 1.0,
  [double]$HotDropoffDiscount = 1.0,
  [int]$CellNeighborRings = 0,
  [double]$CellNeighborWeight = 0.0,
  [int]$CellParentGridCols = 0,
  [double]$CellParentWeight = 0.0,
  [switch]$RunRouter,
  [string]$RouterUrl = "http://127.0.0.1:5000",
  [string]$RouteCacheCsv = "",
  [int]$RouteMaxFeatures = 0
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$PathText) {
  if ([System.IO.Path]::IsPathRooted($PathText)) {
    return $PathText
  }
  return Join-Path (Get-Location).Path $PathText
}

function Invoke-CheckedCommand {
  param(
    [string]$FilePath,
    [string[]]$Arguments,
    [string]$StdoutPath,
    [string]$StderrPath
  )

  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    & $FilePath @Arguments 1> $StdoutPath 2> $StderrPath
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
  if ($exitCode -ne 0) {
    throw "command failed ($exitCode): $FilePath $($Arguments -join ' ')"
  }
}

function Read-FirstDataRow([string]$CsvPath) {
  $rows = Import-Csv -Path $CsvPath
  if ($rows.Count -eq 0) {
    throw "CSV has no data rows: $CsvPath"
  }
  return $rows[0]
}

$repoRoot = (Get-Location).Path
$outputPath = Resolve-RepoPath $OutputDir
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$buildPath = Resolve-RepoPath $BuildDir
$requestsPath = Resolve-RepoPath $Requests
$driversPath = Resolve-RepoPath $Drivers

& $CMakePath -S $repoRoot -B $buildPath -G "MinGW Makefiles" | Out-Host
if ($LASTEXITCODE -ne 0) {
  throw "CMake configure failed"
}
& $CMakePath --build $buildPath --target replay_csv_demo k_sweep | Out-Host
if ($LASTEXITCODE -ne 0) {
  throw "CMake build failed"
}

$kSweepExe = Join-Path $buildPath "k_sweep.exe"
$replayExe = Join-Path $buildPath "replay_csv_demo.exe"

$baselineCsv = Join-Path $outputPath "baseline.csv"
$baselineErr = Join-Path $outputPath "baseline.stderr.txt"
$cellCsv = Join-Path $outputPath "cell_opportunity.csv"
$cellErr = Join-Path $outputPath "cell_opportunity.stderr.txt"
$candidateRoutesCsv = Join-Path $outputPath "candidate_routes.csv"
$candidateRoutesOut = Join-Path $outputPath "candidate_routes.stdout.txt"
$candidateRoutesErr = Join-Path $outputPath "candidate_routes.stderr.txt"
$routeCostsCsv = Join-Path $outputPath "route_costs.csv"
$routeCacheCsvPath = if ($RouteCacheCsv -ne "") { Resolve-RepoPath $RouteCacheCsv } else { Join-Path $outputPath "route_cache.csv" }
$routeCostsErr = Join-Path $outputPath "route_costs.stderr.txt"
$routeCostScenarioCsv = Join-Path $outputPath "route_cost_cell_opportunity.csv"
$routeCostScenarioErr = Join-Path $outputPath "route_cost_cell_opportunity.stderr.txt"
$summaryPath = Join-Path $outputPath "summary.md"

$commonSweepArgs = @(
  "--requests", $requestsPath,
  "--drivers", $driversPath,
  "--end-time", "$EndTime",
  "--radii", $Radius,
  "--k-values", $K
)

Invoke-CheckedCommand `
  -FilePath $kSweepExe `
  -Arguments $commonSweepArgs `
  -StdoutPath $baselineCsv `
  -StderrPath $baselineErr

$cellArgs = $commonSweepArgs + @(
  "--cell-stats-grid-cols", "$CellStatsGridCols",
  "--cell-neighbor-rings", "$CellNeighborRings",
  "--cell-neighbor-weight", "$CellNeighborWeight",
  "--cell-parent-grid-cols", "$CellParentGridCols",
  "--cell-parent-weight", "$CellParentWeight",
  "--dispatch-opportunity-cost-scale", "$DispatchOpportunityCostScale",
  "--cold-dropoff-penalty", "$ColdDropoffPenalty",
  "--hot-dropoff-discount", "$HotDropoffDiscount"
)
Invoke-CheckedCommand `
  -FilePath $kSweepExe `
  -Arguments $cellArgs `
  -StdoutPath $cellCsv `
  -StderrPath $cellErr

$candidateRouteArgs = @(
  "--requests", $requestsPath,
  "--drivers", $driversPath,
  "--end-time", "$EndTime",
  "--radius", $Radius,
  "--max-edges-per-request", $K,
  "--cell-stats-grid-cols", "$CellStatsGridCols",
  "--cell-neighbor-rings", "$CellNeighborRings",
  "--cell-neighbor-weight", "$CellNeighborWeight",
  "--cell-parent-grid-cols", "$CellParentGridCols",
  "--cell-parent-weight", "$CellParentWeight",
  "--dispatch-opportunity-cost-scale", "$DispatchOpportunityCostScale",
  "--cold-dropoff-penalty", "$ColdDropoffPenalty",
  "--hot-dropoff-discount", "$HotDropoffDiscount",
  "--candidate-routes-csv", $candidateRoutesCsv
)
Invoke-CheckedCommand `
  -FilePath $replayExe `
  -Arguments $candidateRouteArgs `
  -StdoutPath $candidateRoutesOut `
  -StderrPath $candidateRoutesErr

$routeStatus = "skipped"
if ($RunRouter) {
  $routeToolDir = Join-Path $repoRoot "tools\route_visual_export"
  $routeArgs = @(
    "run", ".",
    "--input-route-pairs-csv", $candidateRoutesCsv,
    "--route-cost-csv", $routeCostsCsv,
    "--route-cache-csv", $routeCacheCsvPath,
    "--router-url", $RouterUrl
  )
  if ($RouteMaxFeatures -gt 0) {
    $routeArgs += @("--max-features", "$RouteMaxFeatures")
  }
  Push-Location $routeToolDir
  try {
    Invoke-CheckedCommand `
      -FilePath "go" `
      -Arguments $routeArgs `
      -StdoutPath (Join-Path $outputPath "route_costs.stdout.txt") `
      -StderrPath $routeCostsErr
  } finally {
    Pop-Location
  }

  $routeScenarioArgs = $cellArgs + @("--route-cost-csv", $routeCostsCsv)
  Invoke-CheckedCommand `
    -FilePath $kSweepExe `
    -Arguments $routeScenarioArgs `
    -StdoutPath $routeCostScenarioCsv `
    -StderrPath $routeCostScenarioErr
  $routeStatus = "completed"
}

$baseline = Read-FirstDataRow $baselineCsv
$cell = Read-FirstDataRow $cellCsv
$routeScenario = $null
if (Test-Path $routeCostScenarioCsv) {
  $routeScenario = Read-FirstDataRow $routeCostScenarioCsv
}

$candidateRouteRows = @()
if (Test-Path $candidateRoutesCsv) {
  $candidateRouteRows = Import-Csv -Path $candidateRoutesCsv
}

$summary = @()
$summary += "# Dispatch Report Scenario Summary"
$summary += ""
$summary += "Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$summary += ""
$summary += "## Inputs"
$summary += ""
$summary += "- requests: ``$Requests``"
$summary += "- drivers: ``$Drivers``"
$summary += "- end_time: ``$EndTime``"
$summary += "- radius: ``$Radius``"
$summary += "- k: ``$K``"
$summary += "- cell_stats_grid_cols: ``$CellStatsGridCols``"
$summary += "- cell_neighbor_rings: ``$CellNeighborRings``"
$summary += "- cell_neighbor_weight: ``$CellNeighborWeight``"
$summary += "- cell_parent_grid_cols: ``$CellParentGridCols``"
$summary += "- cell_parent_weight: ``$CellParentWeight``"
$summary += "- dispatch_opportunity_cost_scale: ``$DispatchOpportunityCostScale``"
$summary += "- route precompute: ``$routeStatus``"
if ($RunRouter) {
  $summary += "- route cache CSV: ``$routeCacheCsvPath``"
}
$summary += ""
$summary += "## Metrics"
$summary += ""
$summary += "| scenario | assigned | completed | unserved | candidate_edges | mcmf_cost | applied_pickup_cost | avg_pickup_cost | opportunity_adjustment_avg |"
$summary += "|---|---:|---:|---:|---:|---:|---:|---:|---:|"
$summary += "| baseline | $($baseline.assigned) | $($baseline.completed) | $($baseline.unserved) | $($baseline.candidate_edges) | $($baseline.mcmf_cost) | $($baseline.applied_pickup_cost) | $($baseline.avg_pickup_cost) | $($baseline.opportunity_adjustment_avg) |"
$summary += "| cell_opportunity | $($cell.assigned) | $($cell.completed) | $($cell.unserved) | $($cell.candidate_edges) | $($cell.mcmf_cost) | $($cell.applied_pickup_cost) | $($cell.avg_pickup_cost) | $($cell.opportunity_adjustment_avg) |"
if ($routeScenario) {
  $summary += "| route_cost_cell_opportunity | $($routeScenario.assigned) | $($routeScenario.completed) | $($routeScenario.unserved) | $($routeScenario.candidate_edges) | $($routeScenario.mcmf_cost) | $($routeScenario.applied_pickup_cost) | $($routeScenario.avg_pickup_cost) | $($routeScenario.opportunity_adjustment_avg) |"
}
$summary += ""
$summary += "## Artifacts"
$summary += ""
$summary += "- baseline CSV: ``$baselineCsv``"
$summary += "- CellIndex opportunity CSV: ``$cellCsv``"
$summary += "- candidate route pairs CSV: ``$candidateRoutesCsv``"
$summary += "- candidate route pair rows: ``$($candidateRouteRows.Count)``"
if ($RunRouter) {
  $summary += "- route cost CSV: ``$routeCostsCsv``"
  $summary += "- route cache CSV: ``$routeCacheCsvPath``"
  $summary += "- route-cost replay CSV: ``$routeCostScenarioCsv``"
} else {
  $summary += "- route cost CSV: skipped; rerun with ``-RunRouter`` when OSRM is available"
}
$summary += ""
$summary += "## Interpretation Guardrails"
$summary += ""
$summary += "- `pickup_cost` remains the replay timeline fact."
$summary += "- `mcmf_cost` is `dispatch_cost`; it can include CellIndex opportunity adjustment and optional route-cost side table."
$summary += "- Route precompute is offline and file-based; the replay loop does not call OSRM."

Set-Content -Path $summaryPath -Encoding utf8 -Value ($summary -join [Environment]::NewLine)
Write-Output "summary=$summaryPath"
