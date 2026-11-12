#requires -Version 7.0
param([Parameter(Mandatory)][string]$ReportPath)
$ErrorActionPreference = 'Stop'
$report = Get-Content -LiteralPath $ReportPath -Raw -Encoding utf8 | ConvertFrom-Json
$count = 0
function Check($Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:count++
    Write-Output "PASS: $Message"
}
Check ($report.schemaVersion -eq 1 -and $report.sourceHashes.Count -gt 0) 'report identifies schema and source inputs'
foreach ($name in @('format', 'script-tests', 'configure', 'build', 'ctest', 'static-analysis')) {
    Check (@($report.steps | Where-Object name -eq $name).Count -eq 1) "stage recorded exactly once: $name"
}
$bad = @($report.steps | Where-Object { $_.status -in 'failed', 'error', 'timeout' })
$blocked = @($report.steps | Where-Object status -eq 'blocked')
$expected = if ($bad.Count) { 'failed' } elseif ($blocked.Count) { 'incomplete' } else { 'passed' }
Check ($report.overall -eq $expected) 'overall result does not hide failures or blockers'
$logsPresent = $true
foreach ($step in $report.steps) {
    if ($step.PSObject.Properties.Name -contains 'executable') {
        foreach ($stream in @('stdout', 'stderr')) {
            if (-not (Test-Path -LiteralPath (Join-Path $report.output "$($step.name).$stream.log"))) { $logsPresent = $false }
        }
    }
}
Check $logsPresent 'executed processes have separate stdout/stderr logs'
Write-Output "Passed: $count"
