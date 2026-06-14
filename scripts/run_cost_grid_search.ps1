param(
  [string]$CMakePath = "C:\Users\33625\cmake\cmake-4.3.2-windows-x86_64\bin\cmake.exe",
  [string]$BuildDir = "build-mingw",
  [string]$OutputDir = "build-local\cost-grid-search",
  [string]$Requests = "data\normalized\requests.csv",
  [string]$Drivers = "data\normalized\drivers.csv",
  [int]$EndTime = 120,
  [string]$Radius = "0.03",
  [string]$K = "1",
  [int]$CellStatsGridCols = 100,
  [int]$CellNeighborRings = 0,
  [string]$CellNeighborWeight = "0",
  [int]$CellParentGridCols = 0,
  [string]$CellParentWeight = "0",
  [string]$RouteCostCsv = "",
  [string[]]$OpportunityScales = @("0", "10", "25", "50"),
  [string[]]$ColdPenalties = @("0.5", "1", "2"),
  [string[]]$HotDiscounts = @("0", "0.5", "1")
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

  $process = Start-Process `
    -FilePath $FilePath `
    -ArgumentList $Arguments `
    -RedirectStandardOutput $StdoutPath `
    -RedirectStandardError $StderrPath `
    -Wait `
    -PassThru `
    -NoNewWindow
  if ($process.ExitCode -ne 0) {
    throw "command failed ($($process.ExitCode)): $FilePath $($Arguments -join ' ')"
  }
}

function Read-FirstDataRow([string]$CsvPath) {
  $rows = Import-Csv -Path $CsvPath
  if ($rows.Count -eq 0) {
    throw "CSV has no data rows: $CsvPath"
  }
  return $rows[0]
}

function To-Double([object]$Value) {
  return [double]::Parse([string]$Value, [System.Globalization.CultureInfo]::InvariantCulture)
}

function Expand-List([string[]]$Values) {
  $expanded = New-Object System.Collections.Generic.List[string]
  foreach ($value in $Values) {
    foreach ($part in ([string]$value).Split(",")) {
      $trimmed = $part.Trim()
      if ($trimmed.Length -gt 0) {
        $expanded.Add($trimmed) | Out-Null
      }
    }
  }
  return $expanded
}

$repoRoot = (Get-Location).Path
$outputPath = Resolve-RepoPath $OutputDir
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$buildPath = Resolve-RepoPath $BuildDir
$requestsPath = Resolve-RepoPath $Requests
$driversPath = Resolve-RepoPath $Drivers
$routeCostPath = if ($RouteCostCsv.Length -gt 0) { Resolve-RepoPath $RouteCostCsv } else { "" }

& $CMakePath -S $repoRoot -B $buildPath -G "MinGW Makefiles" | Out-Host
if ($LASTEXITCODE -ne 0) {
  throw "CMake configure failed"
}
& $CMakePath --build $buildPath --target k_sweep | Out-Host
if ($LASTEXITCODE -ne 0) {
  throw "CMake build failed"
}

$kSweepExe = Join-Path $buildPath "k_sweep.exe"
$rows = New-Object System.Collections.Generic.List[object]
$runIndex = 0
$expandedOpportunityScales = Expand-List $OpportunityScales
$expandedColdPenalties = Expand-List $ColdPenalties
$expandedHotDiscounts = Expand-List $HotDiscounts

foreach ($scale in $expandedOpportunityScales) {
  foreach ($cold in $expandedColdPenalties) {
    foreach ($hot in $expandedHotDiscounts) {
      ++$runIndex
      $stdout = Join-Path $outputPath ("run_{0:D3}.csv" -f $runIndex)
      $stderr = Join-Path $outputPath ("run_{0:D3}.stderr.txt" -f $runIndex)
      $arguments = @(
        "--requests", $requestsPath,
        "--drivers", $driversPath,
        "--end-time", ([string]$EndTime),
        "--radii", $Radius,
        "--k-values", $K,
        "--cell-stats-grid-cols", ([string]$CellStatsGridCols),
        "--cell-neighbor-rings", ([string]$CellNeighborRings),
        "--cell-neighbor-weight", $CellNeighborWeight,
        "--cell-parent-grid-cols", ([string]$CellParentGridCols),
        "--cell-parent-weight", $CellParentWeight,
        "--dispatch-opportunity-cost-scale", $scale,
        "--cold-dropoff-penalty", $cold,
        "--hot-dropoff-discount", $hot
      )
      if ($routeCostPath.Length -gt 0) {
        $arguments += @("--route-cost-csv", $routeCostPath)
      }

      Invoke-CheckedCommand -FilePath $kSweepExe -Arguments $arguments `
        -StdoutPath $stdout -StderrPath $stderr

      $row = Read-FirstDataRow $stdout
      $completed = To-Double $row.completed
      $assigned = To-Double $row.assigned
      $unserved = To-Double $row.unserved
      $mcmfCost = To-Double $row.mcmf_cost
      $appliedPickupCost = To-Double $row.applied_pickup_cost
      $coldAssignmentRate = To-Double $row.cold_dropoff_assignment_rate
      $hotAssignmentRate = To-Double $row.hot_dropoff_assignment_rate

      $servicePenalty = ($unserved * 1000000.0) + (($assigned - $completed) * 100000.0)
      $score = $servicePenalty + $mcmfCost + (0.1 * $appliedPickupCost) - (1000.0 * $coldAssignmentRate) - (500.0 * $hotAssignmentRate)

      $rows.Add([pscustomobject]@{
        rank = 0
        score = [math]::Round($score, 4)
        dispatch_opportunity_cost_scale = $scale
        cold_dropoff_penalty = $cold
        hot_dropoff_discount = $hot
        assigned = $row.assigned
        completed = $row.completed
        unserved = $row.unserved
        mcmf_cost = $row.mcmf_cost
        applied_pickup_cost = $row.applied_pickup_cost
        avg_pickup_cost = $row.avg_pickup_cost
        opportunity_adjustment_avg = $row.opportunity_adjustment_avg
        hot_dropoff_assignment_rate = $row.hot_dropoff_assignment_rate
        cold_dropoff_assignment_rate = $row.cold_dropoff_assignment_rate
        source_csv = $stdout
      }) | Out-Null
    }
  }
}

$ranked = $rows | Sort-Object score, mcmf_cost
$rank = 0
foreach ($row in $ranked) {
  ++$rank
  $row.rank = $rank
}

$resultCsv = Join-Path $outputPath "cost_grid_search.csv"
$ranked | Export-Csv -NoTypeInformation -Path $resultCsv

$best = $ranked | Select-Object -First 1
$summaryPath = Join-Path $outputPath "summary.md"
@(
  "# Cost Grid Search Summary",
  "",
  "- requests: ``$requestsPath``",
  "- drivers: ``$driversPath``",
  "- route_cost_csv: ``$(if ($routeCostPath.Length -gt 0) { $routeCostPath } else { 'none' })``",
  "- end_time: ``$EndTime``",
  "- radius: ``$Radius``",
  "- k: ``$K``",
  "- cell_stats_grid_cols: ``$CellStatsGridCols``",
  "- cell_neighbor_rings: ``$CellNeighborRings``",
  "- cell_neighbor_weight: ``$CellNeighborWeight``",
  "- cell_parent_grid_cols: ``$CellParentGridCols``",
  "- cell_parent_weight: ``$CellParentWeight``",
  "- runs: ``$($rows.Count)``",
  "",
  "## Best Row",
  "",
  "| score | scale | cold_penalty | hot_discount | assigned | completed | mcmf_cost | applied_pickup_cost |",
  "|---:|---:|---:|---:|---:|---:|---:|---:|",
  "| $($best.score) | $($best.dispatch_opportunity_cost_scale) | $($best.cold_dropoff_penalty) | $($best.hot_dropoff_discount) | $($best.assigned) | $($best.completed) | $($best.mcmf_cost) | $($best.applied_pickup_cost) |",
  "",
  "## Scoring",
  "",
  "Lower is better. The score prioritizes service coverage first, then MCMF dispatch cost, with small tie-breakers for applied pickup cost and hot/cold assignment rates.",
  "",
  "CSV: ``$resultCsv``"
) | Set-Content -Path $summaryPath

Write-Host "summary=$summaryPath"
Write-Host "csv=$resultCsv"
