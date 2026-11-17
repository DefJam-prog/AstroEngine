param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][string]$DotnetRoot,
    [Parameter(Mandatory = $true)][string]$ValidatorProject,
    [Parameter(Mandatory = $true)][string]$WorkParent
)

$ErrorActionPreference = 'Stop'
$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$DotnetRoot = [IO.Path]::GetFullPath($DotnetRoot)
$ValidatorProject = [IO.Path]::GetFullPath($ValidatorProject)
$WorkParent = [IO.Path]::GetFullPath($WorkParent)
$dotnet = Join-Path $DotnetRoot 'dotnet.exe'
$fixtureProject = Join-Path $RepositoryRoot 'tests/fixtures/csharp_metadata_validator/Fixture.csproj'
$fixtureConfig = Join-Path $RepositoryRoot 'tests/fixtures/csharp_metadata_validator/NuGet.Config'
$validatorConfig = Join-Path $RepositoryRoot 'tools/csharp/metadata-validator/NuGet.Config'
$testRoot = Join-Path $WorkParent ('metadata validator проверка ' + [guid]::NewGuid().ToString('N'))

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Invoke-JsonProcess([string]$FilePath, [string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $FilePath
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "Could not start $FilePath" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(60000)) {
        $process.Kill($true)
        throw "Process exceeded 60 seconds: $FilePath"
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $result = $null
    if (-not [string]::IsNullOrWhiteSpace($stdout)) {
        try { $result = $stdout | ConvertFrom-Json -Depth 16 }
        catch { throw "Invalid JSON from $FilePath. stdout=$stdout stderr=$stderr" }
    }
    return @{ ExitCode = $process.ExitCode; Json = $result; Stdout = $stdout; Stderr = $stderr }
}

try {
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
    $fixtureOut = Join-Path $testRoot 'fixture-out'
    $fixtureObj = Join-Path $testRoot 'fixture-obj'
    $validatorOut = Join-Path $testRoot 'validator-out'
    $validatorObj = Join-Path $testRoot 'validator-obj'

    & $dotnet restore $fixtureProject --configfile $fixtureConfig "-p:BaseIntermediateOutputPath=$fixtureObj/"
    if ($LASTEXITCODE -ne 0) { throw "Fixture offline restore failed with code $LASTEXITCODE" }
    & $dotnet build $fixtureProject --no-restore --configuration Release --output $fixtureOut "-p:BaseIntermediateOutputPath=$fixtureObj/"
    if ($LASTEXITCODE -ne 0) { throw "Fixture build failed with code $LASTEXITCODE" }
    & $dotnet restore $ValidatorProject --configfile $validatorConfig "-p:BaseIntermediateOutputPath=$validatorObj/"
    if ($LASTEXITCODE -ne 0) { throw "Validator offline restore failed with code $LASTEXITCODE" }
    & $dotnet build $ValidatorProject --no-restore --configuration Release --output $validatorOut "-p:BaseIntermediateOutputPath=$validatorObj/"
    if ($LASTEXITCODE -ne 0) { throw "Validator build failed with code $LASTEXITCODE" }

    $validator = Join-Path $validatorOut 'AstroEngine.CSharpMetadataValidator.dll'
    $fixtureDll = Join-Path $fixtureOut 'ValidatorFixture.dll'
    Assert-True (Test-Path $validator) 'Validator apphost was not produced.'
    Assert-True (Test-Path $fixtureDll) 'Fixture assembly was not produced.'

    $valid = Invoke-JsonProcess $dotnet (@($validator) + @('inspect', '--dotnet-root', $DotnetRoot, '--assembly', $fixtureDll, '--type', 'AstroEngine.Tests.Инспектор.ValidBinding'))
    Assert-True ($valid.ExitCode -eq 0 -and $valid.Json.status -eq 'ok') "Valid managed binding failed: $($valid.Stdout) $($valid.Stderr)"
    Assert-True ($valid.Json.methods.Count -eq 3) 'Valid binding response omitted required methods.'

    $invalid = Invoke-JsonProcess $dotnet (@($validator) + @('inspect', '--dotnet-root', $DotnetRoot, '--assembly', $fixtureDll, '--type', 'AstroEngine.Tests.Инспектор.InvalidBinding'))
    Assert-True ($invalid.Json.status -eq 'invalid-binding') 'Wrong method signatures were not diagnosed.'
    Assert-True (($invalid.Json.diagnostics -join ' ') -match 'signature') 'Signature diagnostic is missing.'

    $missing = Invoke-JsonProcess $dotnet (@($validator) + @('inspect', '--dotnet-root', $DotnetRoot, '--assembly', $fixtureDll, '--type', 'AstroEngine.Tests.Инспектор.DoesNotExist'))
    Assert-True ($missing.Json.status -eq 'missing-type') 'Unknown managed type was not diagnosed.'

    $batch = Invoke-JsonProcess $dotnet (@($validator) + @('inspect-many', '--dotnet-root', $DotnetRoot,
        '--assembly', $fixtureDll, '--type', 'AstroEngine.Tests.Инспектор.ValidBinding',
        '--type', 'AstroEngine.Tests.Инспектор.InvalidBinding', '--type', 'AstroEngine.Tests.Инспектор.DoesNotExist'))
    Assert-True ($batch.ExitCode -ne 0 -and $batch.Json.status -eq 'invalid' -and $batch.Json.bindings.Count -eq 3) 'Batch inspection did not return all selected type results.'
    $batchStatusesValid = $batch.Json.bindings[0].status -eq 'ok' -and $batch.Json.bindings[1].status -eq 'invalid-binding' -and $batch.Json.bindings[2].status -eq 'missing-type'
    Assert-True $batchStatusesValid 'Batch inspection statuses were not preserved.'

    $manyTypesRoot = Join-Path $testRoot 'sixty-four registered types'
    New-Item -ItemType Directory -Path $manyTypesRoot | Out-Null
    $manyProject = Join-Path $manyTypesRoot 'ManyBindings.csproj'
    $manySource = Join-Path $manyTypesRoot 'Bindings.cs'
    $manyOutput = Join-Path $manyTypesRoot 'out'
    $manyObj = Join-Path $manyTypesRoot 'obj'
    Set-Content -LiteralPath $manyProject -Encoding utf8 -Value '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework><AssemblyName>ManyBindings</AssemblyName><ImplicitUsings>enable</ImplicitUsings></PropertyGroup></Project>'
    $bindingLines = [Collections.Generic.List[string]]::new()
    $bindingLines.Add('using System.Runtime.InteropServices;')
    $bindingLines.Add('namespace AstroEngine.Тесты.Batch;')
    for ($index = 1; $index -le 64; $index++) {
        $type = 'Binding{0:D2}' -f $index
        $bindingLines.Add("public static class $type { [UnmanagedCallersOnly] public static int OnCreate(IntPtr p, int n) => 0; [UnmanagedCallersOnly] public static int FixedUpdate(IntPtr p, int n) => 0; [UnmanagedCallersOnly] public static int OnDestroy(IntPtr p, int n) => 0; }")
    }
    Set-Content -LiteralPath $manySource -Encoding utf8 -Value $bindingLines
    & $dotnet restore $manyProject --configfile $fixtureConfig "-p:BaseIntermediateOutputPath=$manyObj/"
    if ($LASTEXITCODE -ne 0) { throw "64-type fixture offline restore failed with code $LASTEXITCODE" }
    & $dotnet build $manyProject --no-restore --configuration Release --output $manyOutput "-p:BaseIntermediateOutputPath=$manyObj/"
    if ($LASTEXITCODE -ne 0) { throw "64-type fixture build failed with code $LASTEXITCODE" }
    $manyAssembly = Join-Path $manyOutput 'ManyBindings.dll'
    $manyArguments = [Collections.Generic.List[string]]::new()
    $manyArguments.Add('inspect-many'); $manyArguments.Add('--dotnet-root'); $manyArguments.Add($DotnetRoot)
    $manyArguments.Add('--assembly'); $manyArguments.Add($manyAssembly)
    for ($index = 1; $index -le 64; $index++) {
        $manyArguments.Add('--type')
        $manyArguments.Add(('AstroEngine.Тесты.Batch.Binding{0:D2}' -f $index))
    }
    $manyTypes = Invoke-JsonProcess $dotnet (@($validator) + $manyArguments.ToArray())
    Assert-True ($manyTypes.ExitCode -eq 0 -and $manyTypes.Json.status -eq 'ok' -and $manyTypes.Json.bindings.Count -eq 64) 'Maximum 64-type isolated batch did not return all bindings.'
    Assert-True (@($manyTypes.Json.bindings | Where-Object status -ne 'ok').Count -eq 0) 'One or more generated 64-type bindings failed metadata validation.'
    Assert-True ($manyTypes.Stdout.Length -lt 32768) '64-type worker response exceeded its 32 KiB output bound.'

    $tooManyArguments = [Collections.Generic.List[string]]::new()
    $tooManyArguments.Add('inspect-many'); $tooManyArguments.Add('--dotnet-root'); $tooManyArguments.Add($DotnetRoot)
    $tooManyArguments.Add('--assembly'); $tooManyArguments.Add($manyAssembly)
    for ($index = 1; $index -le 65; $index++) {
        $tooManyArguments.Add('--type')
        $tooManyArguments.Add(('AstroEngine.Тесты.Batch.Binding{0:D2}' -f $index))
    }
    $tooMany = Invoke-JsonProcess $dotnet (@($validator) + $tooManyArguments.ToArray())
    Assert-True ($tooMany.ExitCode -eq 2 -and $null -eq $tooMany.Json) 'Validator did not reject a 65-type worker request before staging.'

    $malformedPath = Join-Path $testRoot 'not a managed image.bin'
    [IO.File]::WriteAllBytes($malformedPath, [byte[]](0x4d, 0x5a, 0x00, 0x01, 0x02))
    $malformed = Invoke-JsonProcess $dotnet (@($validator) + @('inspect', '--dotnet-root', $DotnetRoot, '--assembly', $malformedPath, '--type', 'x'))
    Assert-True ($malformed.ExitCode -ne 0 -and $malformed.Json.status -eq 'error') 'Malformed PE did not yield a bounded diagnostic.'
    Assert-True ($malformed.Stdout.Length -lt 32768) 'Malformed PE diagnostic exceeded the response bound.'

    $oversizedPath = Join-Path $testRoot 'oversized assembly.bin'
    $oversizedStream = [IO.File]::Create($oversizedPath)
    try { $oversizedStream.SetLength(64MB + 1) } finally { $oversizedStream.Dispose() }
    $oversized = Invoke-JsonProcess $dotnet (@($validator) + @('inspect', '--dotnet-root', $DotnetRoot, '--assembly', $oversizedPath, '--type', 'x'))
    Assert-True ($oversized.ExitCode -ne 0 -and $oversized.Json.status -eq 'error') 'Assembly larger than 64 MiB was not rejected.'

    $outsidePath = Join-Path $testRoot 'секретный sentinel.txt'
    [IO.File]::WriteAllText($outsidePath, 'must remain inaccessible', [Text.UTF8Encoding]::new($false))
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try {
        $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
        $probe = Invoke-JsonProcess $dotnet (@($validator) + @('--probe', '--dotnet-root', $DotnetRoot, '--outside-file', $outsidePath, '--port', "$port"))
        Assert-True ($probe.ExitCode -eq 0 -and $probe.Json.outsideFileDenied -and $probe.Json.networkDenied) "AppContainer did not deny file and loopback access: $($probe.Stdout) $($probe.Stderr)"
        Assert-True (-not $listener.Pending()) 'AppContainer reached the local TCP listener.'
    }
    finally { $listener.Stop() }

    $timeout = Invoke-JsonProcess $dotnet (@($validator) + @('--timeout-probe', '--dotnet-root', $DotnetRoot))
    Assert-True ($timeout.ExitCode -eq 0 -and $timeout.Json.status -eq 'terminated') "Timed worker was not terminated as expected: $($timeout.Stdout) $($timeout.Stderr)"

    Write-Output 'PASS: single and batch binding inspection, Unicode type name, wrong signature, missing type, malformed/oversized PE, no initializer execution, AppContainer file/network denial, and timeout termination.'
}
finally {
    if (Test-Path $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
