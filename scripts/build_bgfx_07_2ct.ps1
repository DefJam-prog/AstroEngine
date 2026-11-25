param(
    [string]$BgfxRoot = (Join-Path $PSScriptRoot '..\build\spike-deps\bgfx')
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BgfxRoot = (Resolve-Path $BgfxRoot).Path
$expectedRevision = 'eebf5686a8c5f1c77e1af2dad17b5746ba17da8f'
$patchPath = Join-Path $projectRoot 'third_party\patches\bgfx\07.2ct-vulkan-fixed-surface-extent.patch'

$actualRevision = (& git -C $BgfxRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $expectedRevision) {
    throw "BGFX revision mismatch. Expected $expectedRevision; found $actualRevision. No files changed."
}

$reverseCheck = & git -C $BgfxRoot apply --reverse --check $patchPath 2>$null
if ($LASTEXITCODE -ne 0) {
    $workingTree = @(& git -C $BgfxRoot status --porcelain)
    if ($workingTree.Count -gt 0) {
        throw "BGFX checkout has local changes; refusing to apply the project patch over them. Review: $BgfxRoot"
    }

    & git -C $BgfxRoot apply --check $patchPath
    if ($LASTEXITCODE -ne 0) {
        throw 'The BGFX resize patch does not apply cleanly; no source was changed.'
    }
    & git -C $BgfxRoot apply $patchPath
    if ($LASTEXITCODE -ne 0) {
        throw 'Applying the BGFX resize patch failed.'
    }
    Write-Output 'Applied the pinned BGFX Vulkan surface-extent patch.'
}
else {
    Write-Output 'The pinned BGFX Vulkan surface-extent patch is already applied.'
}

$projectFile = Join-Path $BgfxRoot '.build\projects\vs2022\bgfx.vcxproj'
$vsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $projectFile)) {
    throw "BGFX Visual Studio project is missing: $projectFile"
}
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio 2022 Developer command script is missing: $vsDevCmd"
}

$quotedProject = '"' + $projectFile + '"'
$command = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && msbuild.exe $quotedProject /p:Configuration=Release /p:Platform=x64 /m:8 /v:minimal"
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "BGFX Release build failed with exit code $LASTEXITCODE. The source patch remains applied for inspection."
}

Write-Output "Built patched BGFX Release library at $BgfxRoot\.build\win64_vs2022\bin\bgfxRelease.lib"
