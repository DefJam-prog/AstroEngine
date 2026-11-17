param(
    [Parameter(Mandatory = $true)][string]$DotnetRoot,
    [Parameter(Mandatory = $true)][string]$ValidatorAssembly,
    [Parameter(Mandatory = $true)][string]$WorkParent,
    [int[]]$TypeCounts = @(1, 16, 64, 128, 256),
    [ValidateRange(1, 10)][int]$Repetitions = 3
)

$ErrorActionPreference = 'Stop'
$DotnetRoot = [IO.Path]::GetFullPath($DotnetRoot)
$ValidatorAssembly = [IO.Path]::GetFullPath($ValidatorAssembly)
$WorkParent = [IO.Path]::GetFullPath($WorkParent)
$dotnet = Join-Path $DotnetRoot 'dotnet.exe'
$workspace = Join-Path $WorkParent ('metadata-validator-benchmark-' + [guid]::NewGuid().ToString('N'))
$outputDirectory = Join-Path $workspace 'out'
$intermediateDirectory = Join-Path $workspace 'obj'
New-Item -ItemType Directory -Path $workspace | Out-Null

$project = Join-Path $workspace 'RepresentativeScripts.csproj'
$source = Join-Path $workspace 'Bindings.cs'
$nugetConfig = Join-Path $workspace 'NuGet.Config'
Set-Content -LiteralPath $project -Encoding utf8 -Value '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework><AssemblyName>RepresentativeScripts</AssemblyName><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup></Project>'
Set-Content -LiteralPath $nugetConfig -Encoding utf8 -Value '<?xml version="1.0" encoding="utf-8"?><configuration><packageSources><clear /></packageSources></configuration>'
$sourceLines = [Collections.Generic.List[string]]::new()
$sourceLines.Add('using System.Runtime.InteropServices;')
$sourceLines.Add('namespace AstroEngine.Benchmark;')
for ($index = 1; $index -le 256; $index++) {
    $name = 'Binding{0:D3}' -f $index
    $sourceLines.Add("public static class $name { [UnmanagedCallersOnly] public static int OnCreate(IntPtr p, int n) => 0; [UnmanagedCallersOnly] public static int FixedUpdate(IntPtr p, int n) => 0; [UnmanagedCallersOnly] public static int OnDestroy(IntPtr p, int n) => 0; }")
}
Set-Content -LiteralPath $source -Encoding utf8 -Value $sourceLines

& $dotnet restore $project --configfile $nugetConfig "-p:BaseIntermediateOutputPath=$intermediateDirectory/"
if ($LASTEXITCODE -ne 0) { throw "Offline benchmark restore failed with code $LASTEXITCODE" }
& $dotnet build $project --no-restore --configuration Release --output $outputDirectory "-p:BaseIntermediateOutputPath=$intermediateDirectory/"
if ($LASTEXITCODE -ne 0) { throw "Benchmark fixture build failed with code $LASTEXITCODE" }
$assembly = Join-Path $outputDirectory 'RepresentativeScripts.dll'

function Invoke-MetadataBatch([int]$FirstIndex, [int]$Count) {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $dotnet
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    [void]$startInfo.ArgumentList.Add($ValidatorAssembly)
    [void]$startInfo.ArgumentList.Add('inspect-many')
    [void]$startInfo.ArgumentList.Add('--dotnet-root')
    [void]$startInfo.ArgumentList.Add($DotnetRoot)
    [void]$startInfo.ArgumentList.Add('--assembly')
    [void]$startInfo.ArgumentList.Add($assembly)
    for ($index = $FirstIndex; $index -lt ($FirstIndex + $Count); $index++) {
        [void]$startInfo.ArgumentList.Add('--type')
        [void]$startInfo.ArgumentList.Add(('AstroEngine.Benchmark.Binding{0:D3}' -f $index))
    }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $timer = [Diagnostics.Stopwatch]::StartNew()
    [void]$process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(90000)) {
        $process.Kill($true)
        throw "Validator worker exceeded 90 seconds for a $Count-type batch."
    }
    $timer.Stop()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    try { $response = $stdout | ConvertFrom-Json -Depth 16 }
    catch { throw "Validator returned invalid JSON: $stdout $stderr" }
    $invalidBindingCount = @($response.bindings | Where-Object status -ne 'ok').Count
    if ($process.ExitCode -ne 0 -or $response.bindings.Count -ne $Count -or $invalidBindingCount -gt 0) {
        throw "Metadata validation failed for a $Count-type batch: $stdout $stderr"
    }
    return [pscustomobject]@{
        TypeCount = $Count
        ElapsedMilliseconds = $timer.Elapsed.TotalMilliseconds
        ResponseBytes = [Text.Encoding]::UTF8.GetByteCount($stdout)
    }
}

$samples = [Collections.Generic.List[object]]::new()
$summary = [Collections.Generic.List[object]]::new()
foreach ($typeCount in ($TypeCounts | Sort-Object -Unique)) {
    if ($typeCount -lt 1 -or $typeCount -gt 256) { throw "TypeCounts must be between 1 and 256: $typeCount" }
    $scenarioTotals = [Collections.Generic.List[double]]::new()
    for ($repetition = 1; $repetition -le $Repetitions; $repetition++) {
        $scenarioTimer = [Diagnostics.Stopwatch]::StartNew()
        $remaining = $typeCount
        $first = 1
        while ($remaining -gt 0) {
            $count = [Math]::Min(64, $remaining)
            $sample = Invoke-MetadataBatch $first $count
            $samples.Add([pscustomobject]@{
                RequestedTypes = $typeCount
                Repetition = $repetition
                FirstTypeIndex = $first
                BatchTypes = $count
                WorkerMilliseconds = [Math]::Round($sample.ElapsedMilliseconds, 1)
                ResponseBytes = $sample.ResponseBytes
            })
            $first += $count
            $remaining -= $count
        }
        $scenarioTimer.Stop()
        $scenarioTotals.Add($scenarioTimer.Elapsed.TotalMilliseconds)
    }
    $sorted = @($scenarioTotals | Sort-Object)
    $median = $sorted[[int][Math]::Floor($sorted.Count / 2)]
    $summary.Add([pscustomobject]@{
        RequestedTypes = $typeCount
        Repetitions = $Repetitions
        MedianTotalMilliseconds = [Math]::Round($median, 1)
        MinTotalMilliseconds = [Math]::Round($sorted[0], 1)
        MaxTotalMilliseconds = [Math]::Round($sorted[-1], 1)
        MedianWorkerBatches = [Math]::Ceiling($typeCount / 64.0)
    })
}

"Benchmark workspace: $workspace"
'Samples:'
$samples | Format-Table -AutoSize | Out-String | Write-Output
'Summary:'
$summary | Format-Table -AutoSize | Out-String | Write-Output
