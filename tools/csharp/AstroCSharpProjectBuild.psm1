Set-StrictMode -Version Latest

function Get-AstroSha256([byte[]] $Bytes)
{
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([Convert]::ToHexString($sha.ComputeHash($Bytes))).ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Get-AstroFileSha256([string] $Path)
{
    $stream = [System.IO.File]::OpenRead($Path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([Convert]::ToHexString($sha.ComputeHash($stream))).ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Get-AstroRelativePath([string] $Root, [string] $Path)
{
    return [System.IO.Path]::GetRelativePath($Root, $Path).Replace('\', '/')
}

function ConvertTo-AstroQuoted([string] $Value)
{
    return '"' + $Value.Replace('\', '\\').Replace('"', '\"') + '"'
}

function Assert-AstroNoReparsePoint([string] $Path)
{
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
    {
        throw "Reparse points are not allowed in the C# build path: $Path"
    }
}

function Assert-AstroNoReparsePathSegments([string] $Root, [string] $Path)
{
    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    if (-not $pathFull.StartsWith($rootFull + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase))
    {
        throw "Path escapes the project root: $pathFull"
    }
    $relative = [System.IO.Path]::GetRelativePath($rootFull, $pathFull)
    $current = $rootFull
    foreach ($segment in $relative.Split([System.IO.Path]::DirectorySeparatorChar, [System.StringSplitOptions]::RemoveEmptyEntries))
    {
        $current = Join-Path $current $segment
        Assert-AstroNoReparsePoint $current
    }
}

function Get-AstroScriptBuildSnapshot([string] $ProjectRoot, [string] $DotnetRoot)
{
    $rootItem = Get-Item -LiteralPath $ProjectRoot -Force
    if (-not $rootItem.PSIsContainer) { throw "Project root is not a directory: $ProjectRoot" }
    Assert-AstroNoReparsePoint $rootItem.FullName
    $root = [System.IO.Path]::GetFullPath($rootItem.FullName)

    $manifestPath = Join-Path $root '.astro/csharp-build.json'
    $scriptsRoot = Join-Path $root 'Scripts'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf))
    {
        throw "Missing project C# build manifest: $manifestPath"
    }
    if (-not (Test-Path -LiteralPath $scriptsRoot -PathType Container))
    {
        throw "Missing project script directory: $scriptsRoot"
    }
    Assert-AstroNoReparsePoint (Join-Path $root '.astro')
    Assert-AstroNoReparsePoint $manifestPath
    Assert-AstroNoReparsePoint $scriptsRoot

    $manifestBytes = [System.IO.File]::ReadAllBytes($manifestPath)
    $manifestText = [System.Text.UTF8Encoding]::new($false, $true).GetString($manifestBytes)
    try { $manifest = ConvertFrom-Json -InputObject $manifestText -AsHashtable -ErrorAction Stop }
    catch { throw "Invalid UTF-8 JSON in C# build manifest: $($_.Exception.Message)" }
    $allowedKeys = @('schemaVersion', 'sdkVersion', 'targetFramework', 'assemblyName', 'scriptBindings')
    foreach ($key in $manifest.Keys)
    {
        if ($key -notin $allowedKeys) { throw "Unknown C# build manifest field: $key" }
    }
    foreach ($key in $allowedKeys)
    {
        if ($key -eq 'scriptBindings') { continue }
        if (-not $manifest.ContainsKey($key)) { throw "Missing C# build manifest field: $key" }
    }
    if ($manifest.schemaVersion -ne 1) { throw 'Only C# build manifest schemaVersion 1 is supported.' }
    if ($manifest.sdkVersion -notmatch '^10\.0\.\d+$') { throw 'sdkVersion must pin a .NET 10.0.x SDK version.' }
    if ($manifest.targetFramework -ne 'net10.0') { throw 'Only targetFramework net10.0 is supported by this prototype.' }
    if ($manifest.assemblyName -notmatch '^[A-Za-z_][A-Za-z0-9_.-]{0,99}$')
    {
        throw 'assemblyName must be a simple assembly name; path characters are not allowed.'
    }

    $dotnetPath = Join-Path $DotnetRoot 'dotnet.exe'
    if (-not (Test-Path -LiteralPath $dotnetPath -PathType Leaf)) { throw "dotnet.exe was not found under: $DotnetRoot" }
    Assert-AstroNoReparsePoint $dotnetPath
    $sdk = Invoke-AstroProcess $dotnetPath @('--version') $root
    if ($sdk.ExitCode -ne 0) { throw "Could not query .NET SDK version: $($sdk.StdErr)" }
    $sdkVersion = $sdk.StdOut.Trim()
    if ($sdkVersion -ne $manifest.sdkVersion)
    {
        throw "Pinned .NET SDK $($manifest.sdkVersion) is required; selected SDK is $sdkVersion."
    }

    $sourcesRoot = [System.IO.Path]::GetFullPath($scriptsRoot)
    $sourceItems = @(Get-ChildItem -LiteralPath $sourcesRoot -Recurse -File -Filter '*.cs' -Force | Sort-Object FullName)
    foreach ($directory in Get-ChildItem -LiteralPath $sourcesRoot -Directory -Recurse -Force)
    {
        Assert-AstroNoReparsePoint $directory.FullName
    }
    if ($sourceItems.Count -eq 0) { throw "No C# source files were found under $sourcesRoot" }
    if ($sourceItems.Count -gt 4096) { throw 'The project contains more than 4096 C# source files.' }
    $sources = [System.Collections.Generic.List[object]]::new()
    [long] $totalBytes = 0
    foreach ($item in $sourceItems)
    {
        Assert-AstroNoReparsePoint $item.FullName
        $relative = Get-AstroRelativePath $root $item.FullName
        if (-not $relative.StartsWith('Scripts/', [System.StringComparison]::OrdinalIgnoreCase))
        {
            throw "C# source escaped the project Scripts directory: $($item.FullName)"
        }
        if ($item.Length -gt 16777216) { throw "C# source exceeds the 16 MiB limit: $relative" }
        $totalBytes += $item.Length
        if ($totalBytes -gt 536870912) { throw 'C# sources exceed the 512 MiB total limit.' }
        try { [System.Text.UTF8Encoding]::new($false, $true).GetString([System.IO.File]::ReadAllBytes($item.FullName)) | Out-Null }
        catch { throw "C# source is not valid UTF-8: $relative" }
        $sources.Add([ordered]@{ path = $relative; sha256 = Get-AstroFileSha256 $item.FullName; length = $item.Length })
    }

    $scriptBindings = [System.Collections.Generic.List[object]]::new()
    $boundSources = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    if ($manifest.ContainsKey('scriptBindings'))
    {
        $configuredBindings = $manifest.scriptBindings
        $bindingsAreNotArray = $null -eq $configuredBindings -or $configuredBindings -is [string] -or
            $configuredBindings -is [System.Collections.IDictionary] -or $configuredBindings -isnot [System.Collections.IEnumerable]
        if ($bindingsAreNotArray)
        {
            throw 'scriptBindings must be an array of sourcePath/managedTypeName objects.'
        }
        foreach ($binding in $configuredBindings)
        {
            if ($binding -isnot [System.Collections.IDictionary])
                { throw 'Each scriptBindings entry must be a JSON object.' }
            $unknownBindingKeys = @($binding.Keys | Where-Object { $_ -notin @('sourcePath', 'managedTypeName') })
            if ($unknownBindingKeys.Count -gt 0) { throw "Unknown script binding field: $($unknownBindingKeys[0])" }
            if (-not $binding.ContainsKey('sourcePath') -or -not $binding.ContainsKey('managedTypeName'))
                { throw 'Each script binding requires sourcePath and managedTypeName.' }
            $sourcePath = [string]$binding.sourcePath
            $managedTypeName = [string]$binding.managedTypeName
            $sourceStartsUnderScripts = $sourcePath.StartsWith('Scripts/', [System.StringComparison]::Ordinal)
            $sourceHasBackslash = $sourcePath.IndexOf([char]92) -ge 0
            $sourceContainsParentMarker = $sourcePath.Contains('..')
            $sourceWasCompiled = @($sources | ForEach-Object { [string]$_.path }) -ccontains $sourcePath
            if (-not $sourceStartsUnderScripts -or $sourceHasBackslash -or $sourceContainsParentMarker -or -not $sourceWasCompiled)
            {
                throw "Managed type binding source must name a compiled source under Scripts/: $sourcePath"
            }
            if (-not $boundSources.Add($sourcePath)) { throw "A C# source has more than one selected managed type: $sourcePath" }
            $managedTypeIsInvalid = [string]::IsNullOrWhiteSpace($managedTypeName) -or $managedTypeName.Length -gt 512 -or
                $managedTypeName -match '[\x00-\x20\x7F]'
            if ($managedTypeIsInvalid)
            {
                throw "Managed type name must contain 1–512 UTF-8 bytes without whitespace/control characters: $sourcePath"
            }
            try { $typeNameBytes = [System.Text.UTF8Encoding]::new($false, $true).GetByteCount($managedTypeName) }
            catch { throw "Managed type name is not valid UTF-8: $sourcePath" }
            if ($typeNameBytes -gt 512) { throw "Managed type name exceeds 512 UTF-8 bytes: $sourcePath" }
            $scriptBindings.Add([ordered]@{ sourcePath = $sourcePath; managedTypeName = $managedTypeName })
        }
        if ($scriptBindings.Count -gt 4096) { throw 'The project contains more than 4096 selected C# types.' }
    }

    $snapshotData = [ordered]@{
        schemaVersion = 1
        sdkVersion = $sdkVersion
        targetFramework = [string]$manifest.targetFramework
        assemblyName = [string]$manifest.assemblyName
        manifestSha256 = Get-AstroSha256 $manifestBytes
        sources = @($sources)
    }
    $snapshotJson = ConvertTo-Json -InputObject $snapshotData -Depth 8 -Compress
    $fingerprint = Get-AstroSha256 ([System.Text.UTF8Encoding]::new($false).GetBytes($snapshotJson))
    return [pscustomobject]@{
        ProjectRoot = $root
        ManifestPath = $manifestPath
        ScriptsRoot = $sourcesRoot
        DotnetPath = [System.IO.Path]::GetFullPath($dotnetPath)
        SdkVersion = $sdkVersion
        TargetFramework = [string]$manifest.targetFramework
        AssemblyName = [string]$manifest.assemblyName
        Sources = @($sources | ForEach-Object { [pscustomobject]@{ FullName = (Join-Path $root $_.path.Replace('/', '\')); RelativePath = [string]$_.path; Sha256 = [string]$_.sha256 } })
        ScriptBindings = @($scriptBindings | ForEach-Object { [pscustomobject]@{ SourceRelativePath = [string]$_.sourcePath; ManagedTypeName = [string]$_.managedTypeName } })
        Fingerprint = $fingerprint
    }
}

function Invoke-AstroProcess([string] $Executable, [string[]] $Arguments, [string] $WorkingDirectory = '')
{
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    if ($WorkingDirectory) { $start.WorkingDirectory = $WorkingDirectory }
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $start.StandardErrorEncoding = [System.Text.Encoding]::UTF8
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw "Could not start $Executable" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    return [pscustomobject]@{ ExitCode = $process.ExitCode; StdOut = $stdoutTask.GetAwaiter().GetResult(); StdErr = $stderrTask.GetAwaiter().GetResult() }
}

function Get-AstroBuildState([string] $ProjectRoot)
{
    $path = Join-Path $ProjectRoot 'Library/Scripts/build-state.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    try
    {
        $text = [System.IO.File]::ReadAllText($path, [System.Text.UTF8Encoding]::new($false, $true))
        return ConvertFrom-Json -InputObject $text -AsHashtable -ErrorAction Stop
    }
    catch { return $null }
}

function Test-AstroCSharpProjectBuild([string] $ProjectRoot, [string] $DotnetRoot)
{
    try
    {
        $snapshot = Get-AstroScriptBuildSnapshot $ProjectRoot $DotnetRoot
        $state = Get-AstroBuildState $snapshot.ProjectRoot
        if ($null -eq $state) { return [pscustomobject]@{ Fresh = $false; Reason = 'No valid successful build state exists.'; Snapshot = $snapshot; State = $null; Invalid = $false } }
        if ($state.schemaVersion -ne 2 -or $state.fingerprint -ne $snapshot.Fingerprint)
        {
            return [pscustomobject]@{ Fresh = $false; Reason = 'Sources or build manifest changed since the last successful build.'; Snapshot = $snapshot; State = $state; Invalid = $false }
        }

        foreach ($field in @('assemblyPath', 'runtimeConfigPath', 'assemblySha256', 'runtimeConfigSha256', 'assetStatePath', 'assetStateSha256'))
        {
            if (-not $state.ContainsKey($field)) { return [pscustomobject]@{ Fresh = $false; Reason = "Build state is missing $field."; Snapshot = $snapshot; State = $state; Invalid = $false } }
        }
        $outputs = @(
            @{ Relative = [string]$state.assemblyPath; Hash = [string]$state.assemblySha256 },
            @{ Relative = [string]$state.runtimeConfigPath; Hash = [string]$state.runtimeConfigSha256 },
            @{ Relative = [string]$state.assetStatePath; Hash = [string]$state.assetStateSha256 }
        )
        foreach ($output in $outputs)
        {
            $path = [System.IO.Path]::GetFullPath((Join-Path $snapshot.ProjectRoot $output.Relative.Replace('/', '\')))
            $libraryRoot = [System.IO.Path]::GetFullPath((Join-Path $snapshot.ProjectRoot 'Library/Scripts'))
            if (-not $path.StartsWith($libraryRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase))
            {
                return [pscustomobject]@{ Fresh = $false; Reason = 'Build state points outside Library/Scripts.'; Snapshot = $snapshot; State = $state; Invalid = $false }
            }
            Assert-AstroNoReparsePathSegments $snapshot.ProjectRoot $path
            if (-not (Test-Path -LiteralPath $path -PathType Leaf))
            {
                return [pscustomobject]@{ Fresh = $false; Reason = "Build output is missing: $($output.Relative)"; Snapshot = $snapshot; State = $state; Invalid = $false }
            }
            Assert-AstroNoReparsePoint $path
            if ((Get-AstroFileSha256 $path) -ne $output.Hash)
            {
                return [pscustomobject]@{ Fresh = $false; Reason = "Build output hash changed: $($output.Relative)"; Snapshot = $snapshot; State = $state; Invalid = $false }
            }
        }
        return [pscustomobject]@{ Fresh = $true; Reason = 'Build inputs and outputs match the last successful build.'; Snapshot = $snapshot; State = $state; Invalid = $false }
    }
    catch { return [pscustomobject]@{ Fresh = $false; Reason = $_.Exception.Message; Snapshot = $null; State = $null; Invalid = $true } }
}

function Write-AstroAtomicUtf8([string] $Path, [string] $Text)
{
    $directory = Split-Path -Parent $Path
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    $temporaryPath = "$Path.tmp-$([guid]::NewGuid().ToString('N'))"
    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Text)
    [System.IO.File]::WriteAllBytes($temporaryPath, $bytes)
    if ([System.IO.File]::Exists($Path)) { [System.IO.File]::Move($temporaryPath, $Path, $true) }
    else { [System.IO.File]::Move($temporaryPath, $Path) }
}

function Invoke-AstroCSharpProjectBuild([string] $ProjectRoot, [string] $DotnetRoot, [string] $RepositoryRoot)
{
    $check = Test-AstroCSharpProjectBuild $ProjectRoot $DotnetRoot
    if ($check.Invalid) { return [pscustomobject]@{ ExitCode = 2; Status = 'invalid'; Message = $check.Reason; AssemblyPath = $null } }
    if ($check.Fresh)
    {
        $assembly = Join-Path $check.Snapshot.ProjectRoot $check.State.assemblyPath.Replace('/', '\')
        return [pscustomobject]@{ ExitCode = 0; Status = 'up-to-date'; Message = $check.Reason; AssemblyPath = $assembly }
    }

    try
    {
        $snapshot = Get-AstroScriptBuildSnapshot $ProjectRoot $DotnetRoot
        $libraryRoot = Join-Path $snapshot.ProjectRoot 'Library/Scripts'
        Assert-AstroNoReparsePathSegments $snapshot.ProjectRoot $libraryRoot
        Assert-AstroNoReparsePoint (Join-Path $snapshot.ProjectRoot 'Library')
        Assert-AstroNoReparsePoint $libraryRoot
        $stagingRoot = Join-Path $libraryRoot ('.staging-' + [guid]::NewGuid().ToString('N'))
        [System.IO.Directory]::CreateDirectory($stagingRoot) | Out-Null
        $sourceCopyRoot = Join-Path $stagingRoot 'src'
        [System.IO.Directory]::CreateDirectory($sourceCopyRoot) | Out-Null
        foreach ($source in $snapshot.Sources)
        {
            $relativeFromScripts = $source.RelativePath.Substring('Scripts/'.Length).Replace('/', '\')
            $destination = Join-Path $sourceCopyRoot $relativeFromScripts
            [System.IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
            [System.IO.File]::Copy($source.FullName, $destination, $false)
            if ((Get-AstroFileSha256 $destination) -ne $source.Sha256)
            {
                throw "C# source changed during build snapshot copy: $($source.RelativePath)"
            }
        }

        $project = @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>$($snapshot.TargetFramework)</TargetFramework>
    <AssemblyName>$($snapshot.AssemblyName)</AssemblyName>
    <OutputType>Library</OutputType>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <GenerateRuntimeConfigurationFiles>true</GenerateRuntimeConfigurationFiles>
    <Nullable>enable</Nullable>
    <ImplicitUsings>disable</ImplicitUsings>
    <Deterministic>true</Deterministic>
    <ContinuousIntegrationBuild>true</ContinuousIntegrationBuild>
    <DebugType>portable</DebugType>
    <PathMap>`$(MSBuildProjectDirectory)=/_/AstroEngine/Scripts</PathMap>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="src\**\*.cs" />
  </ItemGroup>
</Project>
"@
        $projectPath = Join-Path $stagingRoot 'AstroEngine.ProjectScripts.csproj'
        [System.IO.File]::WriteAllText($projectPath, $project, [System.Text.UTF8Encoding]::new($false))
        $nugetConfig = Join-Path $RepositoryRoot 'tools/csharp/NuGet.Config'
        $objPath = Join-Path $stagingRoot 'obj'
        $outputPath = Join-Path $stagingRoot 'output'
        $restore = Invoke-AstroProcess $snapshot.DotnetPath @('restore', $projectPath, '--configfile', $nugetConfig, "-p:BaseIntermediateOutputPath=$objPath/") $snapshot.ProjectRoot
        if ($restore.ExitCode -ne 0)
        {
            return [pscustomobject]@{ ExitCode = 1; Status = 'restore-failed'; Message = "Offline .NET restore failed; last successful build state was preserved.`n$($restore.StdOut)`n$($restore.StdErr)"; AssemblyPath = $null }
        }

        $build = Invoke-AstroProcess $snapshot.DotnetPath @('build', $projectPath, '--no-restore', '--configuration', 'Release', '--output', $outputPath, "-p:BaseIntermediateOutputPath=$objPath/") $snapshot.ProjectRoot
        if ($build.ExitCode -ne 0)
        {
            return [pscustomobject]@{ ExitCode = 1; Status = 'build-failed'; Message = "C# compilation failed; last successful build state was preserved.`n$($build.StdOut)`n$($build.StdErr)"; AssemblyPath = $null }
        }

        $assemblySource = Join-Path $outputPath ($snapshot.AssemblyName + '.dll')
        $runtimeConfigSource = Join-Path $outputPath ($snapshot.AssemblyName + '.runtimeconfig.json')
        foreach ($path in @($assemblySource, $runtimeConfigSource))
        {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return [pscustomobject]@{ ExitCode = 1; Status = 'missing-output'; Message = "Compiler did not produce required output: $path"; AssemblyPath = $null } }
        }

        $generationParent = Join-Path $libraryRoot $snapshot.AssemblyName
        Assert-AstroNoReparsePathSegments $snapshot.ProjectRoot $generationParent
        [System.IO.Directory]::CreateDirectory($generationParent) | Out-Null
        $generationName = $snapshot.Fingerprint + '-' + [guid]::NewGuid().ToString('N')
        $generation = Join-Path $generationParent $generationName
        Assert-AstroNoReparsePathSegments $snapshot.ProjectRoot $generation
        [System.IO.Directory]::CreateDirectory($generation) | Out-Null
        $assemblyName = $snapshot.AssemblyName + '.dll'
        $runtimeConfigName = $snapshot.AssemblyName + '.runtimeconfig.json'
        [System.IO.File]::Copy($assemblySource, (Join-Path $generation $assemblyName), $false)
        [System.IO.File]::Copy($runtimeConfigSource, (Join-Path $generation $runtimeConfigName), $false)
        $assemblyRelative = Get-AstroRelativePath $snapshot.ProjectRoot (Join-Path $generation $assemblyName)
        $runtimeConfigRelative = Get-AstroRelativePath $snapshot.ProjectRoot (Join-Path $generation $runtimeConfigName)
        $scriptStatePath = Join-Path $libraryRoot 'asset-bindings.state'
        $scriptStateLines = [System.Collections.Generic.List[string]]::new()
        $scriptStateLines.Add('ASTRO_CSHARP_PROJECT_BUILD 1')
        $scriptStateLines.Add("FINGERPRINT $($snapshot.Fingerprint)")
        $scriptStateLines.Add("MANIFEST_SHA256 $((Get-AstroFileSha256 $snapshot.ManifestPath))")
        $scriptStateLines.Add("SDK_VERSION $(ConvertTo-AstroQuoted $snapshot.SdkVersion)")
        $scriptStateLines.Add("TARGET_FRAMEWORK $(ConvertTo-AstroQuoted $snapshot.TargetFramework)")
        $scriptStateLines.Add("ASSEMBLY_NAME $(ConvertTo-AstroQuoted $snapshot.AssemblyName)")
        $scriptStateLines.Add("ASSEMBLY_PATH $(ConvertTo-AstroQuoted $assemblyRelative)")
        $scriptStateLines.Add("ASSEMBLY_SHA256 $((Get-AstroFileSha256 (Join-Path $generation $assemblyName)))")
        $scriptStateLines.Add("RUNTIME_CONFIG_PATH $(ConvertTo-AstroQuoted $runtimeConfigRelative)")
        $scriptStateLines.Add("RUNTIME_CONFIG_SHA256 $((Get-AstroFileSha256 (Join-Path $generation $runtimeConfigName)))")
        $scriptStateLines.Add("SOURCE_COUNT $($snapshot.Sources.Count)")
        foreach ($source in $snapshot.Sources)
            { $scriptStateLines.Add("SOURCE $(ConvertTo-AstroQuoted $source.RelativePath) $($source.Sha256)") }
        $scriptStateLines.Add("BINDING_COUNT $($snapshot.ScriptBindings.Count)")
        foreach ($binding in $snapshot.ScriptBindings)
            { $scriptStateLines.Add("BINDING $(ConvertTo-AstroQuoted $binding.SourceRelativePath) $(ConvertTo-AstroQuoted $binding.ManagedTypeName)") }
        $scriptStateLines.Add('END')
        $scriptStateText = ($scriptStateLines -join "`n") + "`n"
        $scriptStateBytes = [System.Text.UTF8Encoding]::new($false).GetBytes($scriptStateText)
        if ($scriptStateBytes.Length -gt 4194304)
            { return [pscustomobject]@{ ExitCode = 2; Status = 'state-too-large'; Message = 'C# build handoff state exceeds 4 MiB.'; AssemblyPath = $null } }
        $scriptStateHash = Get-AstroSha256 $scriptStateBytes

        $state = [ordered]@{
            schemaVersion = 2
            fingerprint = $snapshot.Fingerprint
            sdkVersion = $snapshot.SdkVersion
            targetFramework = $snapshot.TargetFramework
            assemblyName = $snapshot.AssemblyName
            scriptBindings = @($snapshot.ScriptBindings | ForEach-Object { [ordered]@{ sourcePath = $_.SourceRelativePath; managedTypeName = $_.ManagedTypeName } })
            assemblyPath = $assemblyRelative
            runtimeConfigPath = $runtimeConfigRelative
            assemblySha256 = Get-AstroFileSha256 (Join-Path $generation $assemblyName)
            runtimeConfigSha256 = Get-AstroFileSha256 (Join-Path $generation $runtimeConfigName)
            assetStatePath = (Get-AstroRelativePath $snapshot.ProjectRoot $scriptStatePath)
            assetStateSha256 = $scriptStateHash
        }
        Write-AstroAtomicUtf8 $scriptStatePath $scriptStateText
        $statePath = Join-Path $libraryRoot 'build-state.json'
        Write-AstroAtomicUtf8 $statePath (ConvertTo-Json -InputObject $state -Depth 6)

        # Preserve generated source/intermediate files under Library for diagnostics; outputs are versioned and never overwritten.
        return [pscustomobject]@{ ExitCode = 0; Status = 'built'; Message = 'C# project scripts compiled successfully.'; AssemblyPath = (Join-Path $generation $assemblyName) }
    }
    catch { return [pscustomobject]@{ ExitCode = 2; Status = 'invalid'; Message = $_.Exception.Message; AssemblyPath = $null } }
}

Export-ModuleMember -Function Test-AstroCSharpProjectBuild, Invoke-AstroCSharpProjectBuild
