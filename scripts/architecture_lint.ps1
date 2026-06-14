param(
  [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"

if ($RepoRoot -eq "") {
  $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
} else {
  $RepoRoot = (Resolve-Path $RepoRoot).Path
}

$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
  $failures.Add($Message) | Out-Null
}

function Get-TrackedFiles {
  $files = git -C $RepoRoot ls-files
  if ($LASTEXITCODE -ne 0) {
    throw "git ls-files failed"
  }
  return $files
}

function Resolve-RepoFile([string]$RelativePath) {
  return Join-Path $RepoRoot ($RelativePath -replace "/", "\")
}

function Test-FilePattern {
  param(
    [string[]]$Files,
    [string]$Pattern,
    [string]$Message
  )

  foreach ($file in $Files) {
    $fullPath = Resolve-RepoFile $file
    if (-not (Test-Path -LiteralPath $fullPath)) {
      continue
    }
    $matches = Select-String -Path $fullPath -Pattern $Pattern -CaseSensitive:$false
    foreach ($match in $matches) {
      Add-Failure "${Message}: ${file}:$($match.LineNumber): $($match.Line.Trim())"
    }
  }
}

function Test-FileContains {
  param(
    [string]$File,
    [string]$Pattern,
    [string]$Message
  )

  $fullPath = Resolve-RepoFile $File
  if (-not (Test-Path -LiteralPath $fullPath)) {
    Add-Failure "missing required architecture file: $File"
    return
  }
  $match = Select-String -Path $fullPath -Pattern $Pattern -CaseSensitive:$false -Quiet
  if (-not $match) {
    Add-Failure $Message
  }
}

$tracked = @(Get-TrackedFiles)

# UI can read static artifacts, but must not run tools or write dispatch/replay state.
$uiCode = @($tracked | Where-Object {
  $_ -match "^web/map_viewer/src/.*\.(ts|tsx|js|jsx)$"
})
Test-FilePattern $uiCode "(child_process|spawn\s*\(|exec\s*\(|go run|\.exe|replay_csv_demo|k_sweep|run_report_scenarios|run_cost_grid_search|TaxiSystem::|apply_assignment)" `
  "UI must not invoke local C++/Go tools or dispatch write APIs"
Test-FilePattern $uiCode "fetch\s*\(\s*['""]https?://" `
  "UI fetch must not call remote services; use static artifacts"
Test-FilePattern $uiCode "method\s*:\s*['""](POST|PUT|PATCH|DELETE)['""]" `
  "UI must not write dispatch/replay state"

# C++ core must not depend on tools, web, scripts, generated build dirs, or network routing.
$cppCore = @($tracked | Where-Object {
  $_ -match "^(include|src)/.*\.(h|hpp|hh|c|cc|cpp|cxx)$"
})
Test-FilePattern $cppCore "(tools[\\/]|web[\\/]|scripts[\\/]|build-local|build-[A-Za-z0-9_-]+)" `
  "C++ core must not depend on repo outer layers or generated outputs"
Test-FilePattern $cppCore "(https?://|\bosrm\b|\bcurl\b|httplib|WinHttp|router-url)" `
  "C++ core must not call live routing or HTTP services"

# Replay loop cannot call routers. Route-cost input belongs outside the loop.
$replayLoop = @("src/dispatch_replay.cpp", "include/dispatch_replay.h")
Test-FilePattern $replayLoop "(https?://|\bosrm\b|\bcurl\b|httplib|WinHttp|router-url|route_duration_s|load_route_dispatch_costs_csv)" `
  "Replay loop must not perform live routing or load route-cost side tables directly"

# Cost semantics must stay explicit.
Test-FileContains "docs/design-docs/glossary.md" "pickup_cost.*Replay timeline fact" `
  "glossary must define pickup_cost as a replay timeline fact"
Test-FileContains "docs/design-docs/glossary.md" "dispatch_cost.*Matching objective" `
  "glossary must define dispatch_cost as the matching objective"
Test-FileContains "src/dispatch_replay.cpp" "assignment\.pickup_cost" `
  "replay timing must still use assignment.pickup_cost"
Test-FileContains "src/mcmf_batch_strategy.cpp" "edge\.dispatch_cost" `
  "MCMF must still optimize edge.dispatch_cost"

# Docs structure: root docs stay shallow; stable design and plans live under docs/.
$allowedRootMarkdown = @("AGENTS.md", "ARCHITECTURE.md", "README.md")
$rootMarkdown = @($tracked | Where-Object { $_ -match "^[^/]+\.md$" })
foreach ($file in $rootMarkdown) {
  if ($allowedRootMarkdown -notcontains $file) {
    Add-Failure "root markdown must be one of AGENTS.md, ARCHITECTURE.md, README.md: $file"
  }
}

foreach ($file in $tracked) {
  if ($file -match "^plan/") {
    Add-Failure "execution plans must live under docs/exec-plans/: $file"
  }
  if ($file -match "^docs/(system_modeling|timeline_model|algorithm_and_strategy|region_design|glossary)\.md$") {
    Add-Failure "design docs must live under docs/design-docs/: $file"
  }
  if ($file -match "^(INDEX|PROJECT_STATUS)\.md$") {
    Add-Failure "root status/index docs must live under docs/: $file"
  }
}

if ($failures.Count -gt 0) {
  Write-Host "architecture lint failed:"
  foreach ($failure in $failures) {
    Write-Host "- $failure"
  }
  exit 1
}

Write-Host "architecture lint passed"
Write-Host "checked UI/static boundary, C++ dependency boundary, route-cost boundary, cost semantics, and docs structure"
