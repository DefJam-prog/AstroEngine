#requires -Version 7.0
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../../scripts/ProcessRunner.psm1') -Force
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$pwsh = (Get-Process -Id $PID).Path
$count = 0
function Check($Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:count++
    Write-Output "PASS: $Message"
}
$r = Invoke-CheckedProcess $pwsh @('-NoProfile', '-Command', '[Console]::Write("hello"); [Console]::Error.Write("diagnostic"); exit 0') $root
Check ($r.status -eq 'passed' -and $r.stdout -eq 'hello' -and $r.stderr -eq 'diagnostic') 'stdout and stderr captured independently'
$r = Invoke-CheckedProcess $pwsh @('-NoProfile', '-Command', 'exit 7') $root
Check ($r.status -eq 'failed' -and $r.exitCode -eq 7) 'nonzero exit preserved'
$r = Invoke-CheckedProcess (Join-Path $root 'nonexistent-tool-00.2.exe') @() $root
Check ($r.status -eq 'error' -and $r.stderr.Length -gt 0) 'missing executable reported as error'
$r = Invoke-CheckedProcess $pwsh @('-NoProfile', '-Command', 'Start-Sleep -Seconds 10') $root -TimeoutSeconds 1
Check ($r.status -eq 'timeout' -and $null -eq $r.exitCode) 'timeout never reported as success'
$r = Invoke-CheckedProcess $pwsh @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'fixtures/echo_argument.ps1'), 'path with spaces Юникод') $root
Check ($r.status -eq 'passed' -and $r.stdout -eq 'path with spaces Юникод') 'Unicode argument boundaries preserved'
Write-Output "Passed: $count"
