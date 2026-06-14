param(
  [int]$MaxPlanLines = 320
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
  $failures.Add($Message) | Out-Null
}

function Test-RepoPath([string]$RelativePath) {
  $fullPath = Join-Path $repoRoot $RelativePath
  if (-not (Test-Path -LiteralPath $fullPath)) {
    Add-Failure "missing required path: $RelativePath"
  }
}

function Resolve-DocTokenPath([string]$Token, [string]$SourceFile) {
  $trimmed = $Token.Trim().TrimEnd(".,;:")
  if ($trimmed.Length -eq 0) {
    return $null
  }

  if ($trimmed -match "^(https?://|#|--|-)" -or $trimmed.Contains("*")) {
    return $null
  }

  $firstWord = ($trimmed -split "\s+")[0]
  if ($firstWord -match "^(build-local|build-|\.\\build|tools\\route_visual_export\\go run)") {
    return $null
  }

  $hasSeparator = $firstWord.Contains("/") -or $firstWord.Contains("\")
  $looksLikePath = $hasSeparator -or
    $firstWord -match "\.(md|h|hpp|cpp|c|go|ps1|txt|json|csv|tsx|ts|css|html|exe|py)$"
  if (-not $looksLikePath) {
    return $null
  }

  if (-not $hasSeparator -and $firstWord -match "\.csv$") {
    return $null
  }

  $knownTopLevel = @(
    "data", "docs", "include", "plan", "scripts", "src", "tests", "tools", "web",
    "README.md", "PROJECT_STATUS.md", "INDEX.md", "AGENTS.md", "CMakeLists.txt",
    "index_.md", "main.cpp", "Dockerfile"
  )

  $normalized = $firstWord.Replace("/", "\")
  if ([System.IO.Path]::IsPathRooted($normalized)) {
    return $normalized
  }

  $baseDir = Split-Path -Parent (Join-Path $repoRoot $SourceFile)
  if ($normalized.StartsWith("..\") -or $normalized.StartsWith(".\")) {
    return [System.IO.Path]::GetFullPath((Join-Path $baseDir $normalized))
  }

  if ($hasSeparator) {
    $firstSegment = ($normalized -split "\\")[0]
    if ($knownTopLevel -notcontains $firstSegment) {
      return $null
    }
  }

  if (-not $hasSeparator -and $baseDir -ne $repoRoot) {
    $docRelativeCandidate = Join-Path $baseDir $normalized
    if (Test-Path -LiteralPath $docRelativeCandidate) {
      return $docRelativeCandidate
    }
  }

  return Join-Path $repoRoot $normalized
}

function Test-DocInlinePaths([string]$SourceFile) {
  $fullPath = Join-Path $repoRoot $SourceFile
  $content = Get-Content -Raw -LiteralPath $fullPath
  $matches = [regex]::Matches($content, '`([^`]+)`')
  foreach ($match in $matches) {
    $candidate = Resolve-DocTokenPath $match.Groups[1].Value $SourceFile
    if ($null -eq $candidate) {
      continue
    }
    if (-not (Test-Path -LiteralPath $candidate)) {
      $relative = $candidate
      $repoPrefix = $repoRoot.TrimEnd("\") + "\"
      if ($candidate.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        $relative = $candidate.Substring($repoPrefix.Length)
      }
      Add-Failure "$SourceFile references missing path: $relative"
    }
  }
}

$requiredPaths = @(
  "README.md",
  "PROJECT_STATUS.md",
  "INDEX.md",
  "docs\README.md",
  "plan\README.md",
  "plan\dispatch_next_steps.md",
  "index_.md",
  "scripts\run_report_scenarios.ps1"
)
foreach ($path in $requiredPaths) {
  Test-RepoPath $path
}

$stableDocs = @(
  "docs\system_modeling.md",
  "docs\timeline_model.md",
  "docs\algorithm_and_strategy.md",
  "docs\region_design.md",
  "docs\ppt_prompt.md"
)
foreach ($path in $stableDocs) {
  Test-RepoPath $path
}

$indexShim = Get-Content -Raw -LiteralPath (Join-Path $repoRoot "index_.md")
if ($indexShim -notmatch "Legacy Index Shim" -or $indexShim -notmatch "progressive-disclosure") {
  Add-Failure "index_.md should remain a progressive-disclosure legacy shim"
}

$indexShimLines = (Get-Content -LiteralPath (Join-Path $repoRoot "index_.md")).Count
if ($indexShimLines -gt 40) {
  Add-Failure "index_.md is too long for a legacy shim: $indexShimLines lines"
}

$planLines = (Get-Content -LiteralPath (Join-Path $repoRoot "plan\dispatch_next_steps.md")).Count
if ($planLines -gt $MaxPlanLines) {
  Add-Failure "plan/dispatch_next_steps.md is over $MaxPlanLines lines: $planLines"
}

$entryDocs = @(
  "README.md",
  "PROJECT_STATUS.md",
  "INDEX.md",
  "docs\README.md",
  "plan\README.md",
  "plan\dispatch_next_steps.md",
  "index_.md"
)
foreach ($doc in $entryDocs) {
  Test-DocInlinePaths $doc
}

if ($failures.Count -gt 0) {
  Write-Host "project doctor failed:"
  foreach ($failure in $failures) {
    Write-Host "- $failure"
  }
  exit 1
}

Write-Host "project doctor passed"
Write-Host "checked entry docs, stable docs, legacy shim, and inline repo paths"
