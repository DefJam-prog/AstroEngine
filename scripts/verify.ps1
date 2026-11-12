#requires -Version 7.0
[CmdletBinding()]
param([ValidateRange(1, 3600)][int]$TimeoutSeconds = 300)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'ProcessRunner.psm1') -Force
$root = Split-Path $PSScriptRoot -Parent
$run = Join-Path $root ('build/verification/' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($run)
$build = Join-Path $run 'build'
$steps = [Collections.Generic.List[object]]::new()
$report = [ordered]@{
    schemaVersion = 1; startedUtc = [DateTime]::UtcNow.ToString('o')
    source = $root; output = $run; os = [Runtime.InteropServices.RuntimeInformation]::OSDescription
    powershell = $PSVersionTable.PSVersion.ToString(); overall = 'incomplete'
    tools = [ordered]@{}; sourceHashes = @(); steps = $steps
}

function Add-Step([string]$Name, [string]$Tool, [string[]]$Arguments) {
    $r = Invoke-CheckedProcess -FilePath $Tool -Arguments $Arguments -WorkingDirectory $root -TimeoutSeconds $TimeoutSeconds
    $r | Add-Member -NotePropertyName name -NotePropertyValue $Name
    $steps.Add($r)
    [IO.File]::WriteAllText((Join-Path $run "$Name.stdout.log"), $r.stdout)
    [IO.File]::WriteAllText((Join-Path $run "$Name.stderr.log"), $r.stderr)
    Write-Host "$Name : $($r.status)"
    return $r
}
function Add-Skip([string]$Name, [string]$Status, [string]$Reason) {
    $steps.Add([pscustomobject]@{ name = $Name; status = $Status; reason = $Reason })
    Write-Host "$Name : $Status ($Reason)"
}

try {
    # Hash source/configuration files, including untracked work, without collecting secrets.
    $files = @('CMakeLists.txt', 'CMakePresets.json', '.clang-format', '.clang-tidy') |
        ForEach-Object { Get-Item -LiteralPath (Join-Path $root $_) }
    $files += Get-ChildItem (Join-Path $root 'engine'), (Join-Path $root 'tests'), $PSScriptRoot -Recurse -File |
        Where-Object { $_.Extension -in '.cpp', '.hpp', '.cmake', '.ps1', '.psm1' -or $_.Name -eq 'CMakeLists.txt' }
    $report.sourceHashes = @($files | Sort-Object FullName | ForEach-Object {
        @{ path = [IO.Path]::GetRelativePath($root, $_.FullName); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
    foreach ($name in @('git', 'cmake', 'ctest', 'ninja', 'clang-format', 'clang-tidy')) {
        $tool = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        $report.tools[$name] = if ($tool) { $tool.Source } else { $null }
        if ($tool) { $null = Add-Step "$name-version" $tool.Source @('--version') }
    }
    if ($report.tools['git']) {
        $null = Add-Step 'git-status' $report.tools['git'] @('status', '--short', '--branch')
        $null = Add-Step 'git-head' $report.tools['git'] @('rev-parse', 'HEAD')
    }
    $cpp = @($files | Where-Object { $_.Extension -in '.cpp', '.hpp' } | ForEach-Object { $_.FullName })
    if ($report.tools['clang-format']) {
        $null = Add-Step 'format' $report.tools['clang-format'] (@('--dry-run', '--Werror', '--style=file') + $cpp)
    } else { Add-Skip 'format' 'blocked' 'clang-format not found in PATH' }

    $pwsh = (Get-Process -Id $PID).Path
    $null = Add-Step 'script-tests' $pwsh @('-NoProfile', '-File', (Join-Path $root 'tests/scripts/process_runner.tests.ps1'))
    $configureArgs = @('-S', $root, '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Debug', '-DBUILD_TESTING=ON')
    if ($report.tools['clang-tidy']) { $configureArgs += '-DASTRO_ENABLE_CLANG_TIDY=ON' }
    else { Add-Skip 'static-analysis' 'skipped' 'Optional clang-tidy not found in PATH' }

    if ($report.tools['cmake'] -and $report.tools['ctest'] -and $report.tools['ninja']) {
        $configured = Add-Step 'configure' $report.tools['cmake'] $configureArgs
        if ($configured.status -eq 'passed') {
            $built = Add-Step 'build' $report.tools['cmake'] @('--build', $build)
            if ($report.tools['clang-tidy']) {
                $steps.Add([pscustomobject]@{ name = 'static-analysis'; status = $built.status; reason = 'clang-tidy runs during clean compilation; see build logs' })
            }
            if ($built.status -eq 'passed') {
                $null = Add-Step 'ctest' $report.tools['ctest'] @('--test-dir', $build, '--output-on-failure', '--no-tests=error')
            } else { Add-Skip 'ctest' 'blocked' 'Build did not pass' }
        } else {
            if ($report.tools['clang-tidy']) { Add-Skip 'static-analysis' 'blocked' 'Configuration did not pass' }
            Add-Skip 'build' 'blocked' 'Configuration did not pass'
            Add-Skip 'ctest' 'blocked' 'Configuration did not pass'
        }
    } else {
        if ($report.tools['clang-tidy']) { Add-Skip 'static-analysis' 'blocked' 'Build tools unavailable' }
        foreach ($stage in @('configure', 'build', 'ctest')) { Add-Skip $stage 'blocked' 'CMake, CTest and Ninja are required in PATH' }
    }
    $statuses = @($steps | ForEach-Object { $_.status })
    $report.overall = if (@($statuses | Where-Object { $_ -in 'failed', 'error', 'timeout' }).Count) { 'failed' }
        elseif ($statuses -contains 'blocked') { 'incomplete' } else { 'passed' }
} catch {
    $report.overall = 'failed'
    $report['error'] = $_.Exception.Message
} finally {
    $report['finishedUtc'] = [DateTime]::UtcNow.ToString('o')
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $run 'report.json') -Encoding utf8
    Write-Host "Report: $run/report.json"
}
switch ($report.overall) { 'passed' { exit 0 } 'incomplete' { exit 2 } default { exit 1 } }
