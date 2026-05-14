param(
    [ValidateSet("all", "functional", "core", "systems", "performance")]
    [string]$Target = "all",

    [string]$Filter = "",
    [string]$BuildDir = "build-tests",
    [string]$FunctionalConfig = "Debug",
    [string]$PerformanceConfig = "Release"
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildPath = Join-Path $root $BuildDir
$reportDir = Join-Path $root "reports"
$tempReportDir = Join-Path $buildPath "_test_report_tmp"
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
New-Item -ItemType Directory -Force -Path $tempReportDir | Out-Null

$functionalGroups = @(
    @{
        Name = "core"
        Title = "Core Functional Test Report"
        Target = "ecs_tests"
        Source = "tests/ecs_test.cpp"
        Regex = "^(EntityTest|ComponentIdTest|SparseSetTest|EntityLifecycleTest|ViewTest|SystemDeterminismTest)\."
        Description = "Core/ECS behavior: Entity, ComponentID, SparseSet, Registry, View, deterministic ECS update."
    },
    @{
        Name = "systems"
        Title = "Systems Functional Test Report"
        Target = "systems_tests"
        Source = "tests/systems_test.cpp"
        Regex = "^SystemsTest\."
        Description = "Existing Systems behavior: physics, collision, emotion decay, event bus, naming, decision, action execution, appraisal, line output."
    }
)

$performanceGroups = @(
    @{
        Name = "cache_benchmark"
        Target = "cache_benchmark"
        Source = "tests/cache_benchmark.cpp"
        Description = "Sparse/Dense cache access and pointer-chase latency benchmark."
    },
    @{
        Name = "false_sharing_bench"
        Target = "false_sharing_bench"
        Source = "tests/false_sharing_bench.cpp"
        Description = "AoS false sharing, padded layout, and SoA split comparison."
    },
    @{
        Name = "pipeline_benchmark"
        Target = "pipeline_benchmark"
        Source = "tests/pipeline_benchmark.cpp"
        Description = "Current ECS PhysicSystem pipeline versus SoA scalar and SoA AVX2."
    }
)

function Write-Section([string]$Text) {
    Write-Host ""
    Write-Host "== $Text =="
}

function Invoke-AndCapture {
    param(
        [string]$Command,
        [string[]]$Arguments,
        [string]$OutputPath
    )

    $output = & $Command @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $output | Tee-Object -FilePath $OutputPath
    if ($exitCode -ne 0) {
        throw "$Command failed with exit code $exitCode"
    }
}

function Clear-ReportOutputs {
    Write-Section "Clear old reports"
    if (Test-Path $reportDir) {
        Get-ChildItem -LiteralPath $reportDir -Force | Remove-Item -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
    if (Test-Path $tempReportDir) {
        Get-ChildItem -LiteralPath $tempReportDir -Force | Remove-Item -Recurse -Force
    }
}

function Configure-Tests {
    Write-Section "Configure"
    cmake -S $root -B $buildPath -DBUILD_TESTS=ON
}

function Build-Target([string]$TargetName, [string]$Config) {
    cmake --build $buildPath --target $TargetName --config $Config
    if ($LASTEXITCODE -ne 0) {
        throw "cmake build failed for $TargetName ($Config) with exit code $LASTEXITCODE"
    }
}

function Get-ExePath([string]$TargetName, [string]$Config) {
    $candidates = @(
        (Join-Path $buildPath "$Config\$TargetName.exe"),
        (Join-Path $buildPath "$TargetName.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    throw "Cannot find executable for $TargetName ($Config)"
}

function Convert-CtestLogToMarkdown {
    param(
        [hashtable]$Group,
        [string]$EffectiveRegex,
        [string]$LogPath,
        [string]$MarkdownPath
    )

    $testRows = @()
    foreach ($line in Get-Content $LogPath) {
        if ($line -match '^\s*\d+/\d+\s+Test\s+#\d+:\s+(.+?)\s+\.*\s+(\*\*\*Failed|Passed|\*\*\*Not Run|Not Run)\s+([\d.]+)\s+sec') {
            $testRows += [pscustomobject]@{
                Name = $matches[1].Trim()
                Status = $matches[2].Replace("*", "")
                Seconds = [double]$matches[3]
            }
        }
    }

    $passed = ($testRows | Where-Object { $_.Status -eq "Passed" }).Count
    $failed = ($testRows | Where-Object { $_.Status -ne "Passed" }).Count
    $total = $testRows.Count
    $totalSeconds = ($testRows | Measure-Object -Property Seconds -Sum).Sum
    if ($null -eq $totalSeconds) { $totalSeconds = 0.0 }
    $summaryLine = (Select-String -Path $LogPath -Pattern 'tests passed|No tests were found|failed out of' | Select-Object -Last 1).Line
    if ($null -eq $summaryLine) { $summaryLine = "No summary line found." }

    $lines = @(
        "# $($Group.Title)",
        "",
        "- Source: $($Group.Source)",
        "- CMake target: $($Group.Target)",
        "- Category: Functional",
        "- Config: $FunctionalConfig",
        "- Filter: $EffectiveRegex",
        "- Total: $total",
        "- Passed: $passed",
        "- Failed: $failed",
        "- Sum of listed test time: $([string]::Format('{0:F2}', $totalSeconds)) sec",
        "",
        "## Scope",
        "",
        $Group.Description,
        "",
        "## Summary",
        "",
        '```text',
        $summaryLine,
        '```',
        "",
        "## Per-Test Results",
        "",
        "| # | Test | Status | Time (sec) |",
        "|---:|---|---|---:|"
    )

    for ($i = 0; $i -lt $testRows.Count; ++$i) {
        $row = $testRows[$i]
        $lines += "| $($i + 1) | $($row.Name) | $($row.Status) | $([string]::Format('{0:F2}', $row.Seconds)) |"
    }

    if ($testRows.Count -eq 0) {
        $lines += "| 1 | <none> | Not Run | 0.00 |"
    }

    $lines | Set-Content -Path $MarkdownPath -Encoding UTF8
}

function Run-FunctionalGroup([hashtable]$Group) {
    Write-Section "Build $($Group.Name)"
    Build-Target $Group.Target $FunctionalConfig

    Write-Section "Run $($Group.Name)"
    $logPath = Join-Path $reportDir "$($Group.Name).log"
    $logPath = Join-Path $tempReportDir "$($Group.Name).log"
    $markdownPath = Join-Path $reportDir "$($Group.Name).md"
    $effectiveRegex = if ($Filter -ne "") { $Filter } else { $Group.Regex }

    Invoke-AndCapture -Command "ctest" -Arguments @(
        "--test-dir", $buildPath,
        "-C", $FunctionalConfig,
        "--output-on-failure",
        "-R", $effectiveRegex
    ) -OutputPath $logPath

    Convert-CtestLogToMarkdown $Group $effectiveRegex $logPath $markdownPath
}

function Run-PerformanceGroup([hashtable]$Group) {
    Write-Section "Build $($Group.Name)"
    Build-Target $Group.Target $PerformanceConfig

    Write-Section "Run $($Group.Name)"
    $exePath = Get-ExePath $Group.Target $PerformanceConfig
    $logPath = Join-Path $tempReportDir "$($Group.Name).log"
    Invoke-AndCapture -Command $exePath -Arguments @() -OutputPath $logPath
}

function Write-PerformanceMarkdown {
    $markdownPath = Join-Path $reportDir "performance.md"
    $lines = @(
        "# Performance Test Report",
        "",
        "- Category: Performance",
        "- Config: $PerformanceConfig",
        "- Total benchmark files: $($performanceGroups.Count)",
        "",
        "## Benchmark Files",
        "",
        "| Source | CMake target | Description |",
        "|---|---|---|"
    )

    foreach ($group in $performanceGroups) {
        $lines += "| $($group.Source) | $($group.Target) | $($group.Description) |"
    }

    foreach ($group in $performanceGroups) {
        $logPath = Join-Path $tempReportDir "$($group.Name).log"
        $lines += @(
            "",
            "## $($group.Source)",
            "",
            $group.Description,
            "",
            '```text',
            (Get-Content $logPath),
            '```'
        )
    }

    $lines | Set-Content -Path $markdownPath -Encoding UTF8
}

function Write-IndexMarkdown {
    $markdownPath = Join-Path $reportDir "index.md"
    $lines = @(
        "# Test Report Index",
        "",
        'This report covers every existing `tests/*.cpp` file.',
        "",
        "## Functional Tests",
        "",
        "| Source | CMake target | Report |",
        "|---|---|---|"
    )

    foreach ($group in $functionalGroups) {
        $lines += "| $($group.Source) | $($group.Target) | $(Join-Path $reportDir "$($group.Name).md") |"
    }

    $lines += @(
        "",
        "## Performance Tests",
        "",
        "| Source | CMake target | Report |",
        "|---|---|---|"
    )

    foreach ($group in $performanceGroups) {
        $lines += "| $($group.Source) | $($group.Target) | $(Join-Path $reportDir 'performance.md') |"
    }

    $lines | Set-Content -Path $markdownPath -Encoding UTF8
}

Clear-ReportOutputs
Configure-Tests

if ($Target -eq "all" -or $Target -eq "functional" -or $Target -eq "core") {
    Run-FunctionalGroup $functionalGroups[0]
}
if ($Target -eq "all" -or $Target -eq "functional" -or $Target -eq "systems") {
    Run-FunctionalGroup $functionalGroups[1]
}
if ($Target -eq "all" -or $Target -eq "performance") {
    foreach ($group in $performanceGroups) {
        Run-PerformanceGroup $group
    }
    Write-PerformanceMarkdown
}

Write-IndexMarkdown

Write-Section "Reports"
Write-Host "Index:       $(Join-Path $reportDir 'index.md')"
Write-Host "Core:        $(Join-Path $reportDir 'core.md')"
Write-Host "Systems:     $(Join-Path $reportDir 'systems.md')"
Write-Host "Performance: $(Join-Path $reportDir 'performance.md')"

if (Test-Path $tempReportDir) {
    Get-ChildItem -LiteralPath $tempReportDir -Force | Remove-Item -Recurse -Force
}
