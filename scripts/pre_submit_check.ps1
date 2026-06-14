param(
  [string]$BuildDir = "build-local",
  [string]$ReportOutputDir = "build-local\pre-submit-report-scenarios",
  [int]$EndTime = 120,
  [double]$Radius = 0.03,
  [int]$K = 1,
  [switch]$SkipBuild,
  [switch]$SkipTests,
  [switch]$SkipReportScenarios
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

function Invoke-Step([string]$Name, [scriptblock]$Body) {
  Write-Host ""
  Write-Host "==> $Name"
  & $Body
}

function Invoke-CppTests([string]$ResolvedBuildDir) {
  $ctest = Get-Command ctest -ErrorAction SilentlyContinue
  if ($null -ne $ctest) {
    ctest --test-dir $ResolvedBuildDir --output-on-failure
    return
  }

  Write-Host "ctest not found; running *_test.exe files directly"
  $testExes = Get-ChildItem -LiteralPath $ResolvedBuildDir -Recurse -Filter "*_test.exe" |
    Sort-Object FullName
  if ($testExes.Count -eq 0) {
    throw "No *_test.exe files found under $ResolvedBuildDir"
  }

  foreach ($testExe in $testExes) {
    Write-Host "running $($testExe.FullName)"
    & $testExe.FullName
    if ($LASTEXITCODE -ne 0) {
      throw "test failed: $($testExe.FullName)"
    }
  }
}

function Test-RequiredArtifact([string]$PathText) {
  if (-not (Test-Path -LiteralPath $PathText)) {
    throw "missing required artifact: $PathText"
  }
  $item = Get-Item -LiteralPath $PathText
  if ($item.Length -eq 0) {
    throw "empty required artifact: $PathText"
  }
}

Invoke-Step "Project doctor" {
  powershell -ExecutionPolicy Bypass -File scripts\project_doctor.ps1
}

if (-not $SkipBuild) {
  $cmake = Get-Command cmake -ErrorAction SilentlyContinue
  if ($null -ne $cmake) {
    Invoke-Step "Configure C++ build" {
      cmake -S . -B $BuildDir -G "MinGW Makefiles"
    }

    Invoke-Step "Build C++ targets" {
      cmake --build $BuildDir
    }
  } elseif (Test-Path -LiteralPath $BuildDir) {
    Write-Host "cmake not found; reusing existing build directory: $BuildDir"
  } else {
    throw "cmake not found and build directory does not exist: $BuildDir"
  }
} else {
  Write-Host "Skipping build"
}

if (-not $SkipTests) {
  Invoke-Step "Run C++ tests" {
    Invoke-CppTests $BuildDir
  }
} else {
  Write-Host "Skipping tests"
}

if (-not $SkipReportScenarios) {
  Invoke-Step "Generate report scenario evidence" {
    powershell -ExecutionPolicy Bypass `
      -File scripts\run_report_scenarios.ps1 `
      -BuildDir $BuildDir `
      -OutputDir $ReportOutputDir `
      -EndTime $EndTime `
      -Radius $Radius `
      -K $K
    Test-RequiredArtifact (Join-Path $ReportOutputDir "summary.md")
    Test-RequiredArtifact (Join-Path $ReportOutputDir "baseline.csv")
    Test-RequiredArtifact (Join-Path $ReportOutputDir "cell_opportunity.csv")
    Test-RequiredArtifact (Join-Path $ReportOutputDir "candidate_routes.csv")
  }
} else {
  Write-Host "Skipping report scenario evidence"
}

Write-Host ""
Write-Host "pre-submit check passed"
