[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $RepositoryRoot,
    [Parameter(Mandatory = $true)][string] $DotnetRoot,
    [Parameter(Mandatory = $true)][string] $WorkParent
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $RepositoryRoot 'tools/csharp/AstroCSharpProjectBuild.psm1') -Force

function Assert([bool] $Condition, [string] $Message)
{
    if (-not $Condition) { throw $Message }
}

function Invoke-BuilderCli([string[]] $Arguments)
{
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $PSHOME 'pwsh.exe'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $start.StandardErrorEncoding = [System.Text.Encoding]::UTF8
    $start.ArgumentList.Add('-NoProfile')
    $start.ArgumentList.Add('-File')
    $start.ArgumentList.Add((Join-Path $RepositoryRoot 'tools/csharp/build-project.ps1'))
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start
    Assert $process.Start() 'Unable to start the C# build CLI.'
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    return [pscustomobject]@{ ExitCode = $process.ExitCode; StdOut = $stdoutTask.GetAwaiter().GetResult(); StdErr = $stderrTask.GetAwaiter().GetResult() }
}

function New-Fixture([string] $Root, [string] $Name, [string] $SdkVersion)
{
    $project = Join-Path $Root $Name
    [System.IO.Directory]::CreateDirectory((Join-Path $project '.astro')) | Out-Null
    [System.IO.Directory]::CreateDirectory((Join-Path $project 'Scripts')) | Out-Null
    $manifest = @{
        schemaVersion = 1
        sdkVersion = $SdkVersion
        targetFramework = 'net10.0'
        assemblyName = 'FixtureScripts'
        scriptBindings = @(@{ sourcePath = 'Scripts/Player.cs'; managedTypeName = 'Fixture.Actor' })
    } | ConvertTo-Json
    [System.IO.File]::WriteAllText((Join-Path $project '.astro/csharp-build.json'), $manifest, [System.Text.UTF8Encoding]::new($false))
    $source = 'namespace Fixture; public static class Actor { public static int Value => 1; }'
    [System.IO.File]::WriteAllText((Join-Path $project 'Scripts/Player.cs'), $source, [System.Text.UTF8Encoding]::new($false))
    return $project
}

$parent = [System.IO.Path]::GetFullPath($WorkParent)
[System.IO.Directory]::CreateDirectory($parent) | Out-Null
$testRoot = Join-Path $parent ('csharp-project-build-' + [guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($testRoot) | Out-Null
try
{
    $dotnetExe = Join-Path $DotnetRoot 'dotnet.exe'
    $sdkVersion = (& $dotnetExe --version).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Unable to query the local .NET SDK version.' }
    $firstProject = New-Fixture $testRoot 'Fixture-A - проект' $sdkVersion
    $firstBuild = Invoke-BuilderCli @('-ProjectRoot', $firstProject, '-DotnetRoot', $DotnetRoot)
    Assert ($firstBuild.ExitCode -eq 0) "Initial CLI build failed: $($firstBuild.StdErr)`n$($firstBuild.StdOut)"
    $assemblyMatch = [regex]::Match($firstBuild.StdOut, '(?m)^Assembly: (.+)$')
    Assert $assemblyMatch.Success 'The build CLI did not report the assembly path.'
    $firstBuildAssemblyPath = $assemblyMatch.Groups[1].Value.Trim()
    Assert (Test-Path -LiteralPath $firstBuildAssemblyPath -PathType Leaf) "The build did not produce its assembly at '$firstBuildAssemblyPath'. CLI output: $($firstBuild.StdOut)"
    $statePath = Join-Path $firstProject 'Library/Scripts/build-state.json'
    $firstStateBytes = [System.IO.File]::ReadAllBytes($statePath)
    $fresh = Test-AstroCSharpProjectBuild $firstProject $DotnetRoot
    Assert $fresh.Fresh "Fresh build was not recognized: $($fresh.Reason)"
    $freshCli = Invoke-BuilderCli @('-ProjectRoot', $firstProject, '-DotnetRoot', $DotnetRoot, '-Check')
    Assert ($freshCli.ExitCode -eq 0) "Fresh CLI check returned $($freshCli.ExitCode): $($freshCli.StdErr)"

    $secondProject = New-Fixture $testRoot 'Fixture-B' $sdkVersion
    $secondBuild = Invoke-AstroCSharpProjectBuild $secondProject $DotnetRoot $RepositoryRoot
    Assert ($secondBuild.ExitCode -eq 0) "Repeat build failed: $($secondBuild.Message)"
    $firstHash = (Get-FileHash -LiteralPath $firstBuildAssemblyPath -Algorithm SHA256).Hash
    $secondHash = (Get-FileHash -LiteralPath $secondBuild.AssemblyPath -Algorithm SHA256).Hash
    Assert ($firstHash -eq $secondHash) 'Identical sources/configuration produced different assembly bytes.'

    $sourcePath = Join-Path $firstProject 'Scripts/Player.cs'
    [System.IO.File]::AppendAllText($sourcePath, "`n// changed source`n", [System.Text.UTF8Encoding]::new($false))
    $stale = Test-AstroCSharpProjectBuild $firstProject $DotnetRoot
    Assert (-not $stale.Fresh) 'A changed source file was incorrectly reported as current.'
    $staleCli = Invoke-BuilderCli @('-ProjectRoot', $firstProject, '-DotnetRoot', $DotnetRoot, '-Check')
    Assert ($staleCli.ExitCode -eq 3) "Stale CLI check returned $($staleCli.ExitCode), expected 3."
    $updatedBuild = Invoke-AstroCSharpProjectBuild $firstProject $DotnetRoot $RepositoryRoot
    Assert ($updatedBuild.ExitCode -eq 0) "Rebuild after source change failed: $($updatedBuild.Message)"
    Assert (Test-Path -LiteralPath $firstBuildAssemblyPath -PathType Leaf) 'Rebuild overwrote or removed the prior versioned output.'
    Assert ($updatedBuild.AssemblyPath -ne $firstBuildAssemblyPath) 'Rebuild reused an immutable generation path.'

    $updatedStateBytes = [System.IO.File]::ReadAllBytes($statePath)
    [System.IO.File]::WriteAllText($sourcePath, 'this is not valid C#', [System.Text.UTF8Encoding]::new($false))
    $failedBuild = Invoke-AstroCSharpProjectBuild $firstProject $DotnetRoot $RepositoryRoot
    Assert ($failedBuild.ExitCode -eq 1 -and $failedBuild.Status -eq 'build-failed') 'Invalid C# source did not return a compile failure.'
    $stateAfterFailure = [System.IO.File]::ReadAllBytes($statePath)
    Assert ([Convert]::ToBase64String($stateAfterFailure) -eq [Convert]::ToBase64String($updatedStateBytes)) 'A failed build replaced the last successful state.'

    [System.IO.File]::WriteAllText($sourcePath, 'namespace Fixture; public static class Actor { public static int Value => 2; }', [System.Text.UTF8Encoding]::new($false))
    $recoveredBuild = Invoke-AstroCSharpProjectBuild $firstProject $DotnetRoot $RepositoryRoot
    Assert ($recoveredBuild.ExitCode -eq 0) "Build did not recover after fixing source: $($recoveredBuild.Message)"
    $runtimeConfig = Join-Path (Split-Path -Parent $recoveredBuild.AssemblyPath) 'FixtureScripts.runtimeconfig.json'
    [System.IO.File]::AppendAllText($runtimeConfig, ' ', [System.Text.UTF8Encoding]::new($false))
    $tampered = Test-AstroCSharpProjectBuild $firstProject $DotnetRoot
    Assert (-not $tampered.Fresh -and $tampered.Reason -like '*hash changed*') 'Tampered build output was not detected.'
    [System.IO.File]::WriteAllBytes($sourcePath, [byte[]]@(0xFF, 0xFE, 0xFD))
    $invalidUtf8 = Test-AstroCSharpProjectBuild $firstProject $DotnetRoot
    Assert ($invalidUtf8.Invalid -and $invalidUtf8.Reason -like '*valid UTF-8*') 'Invalid UTF-8 source was not rejected.'

    Write-Output 'C# project build integration passed: offline build, deterministic output, stale detection, UTF-8 validation, immutable generations, failure preservation and output integrity.'
}
finally
{
    $resolvedParent = [System.IO.Path]::GetFullPath($parent).TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    $resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
    if (-not $resolvedTestRoot.StartsWith($resolvedParent, [System.StringComparison]::OrdinalIgnoreCase))
    {
        throw "Refusing to remove test directory outside its designated build parent: $resolvedTestRoot"
    }
    if ([System.IO.Directory]::Exists($resolvedTestRoot)) { [System.IO.Directory]::Delete($resolvedTestRoot, $true) }
}
