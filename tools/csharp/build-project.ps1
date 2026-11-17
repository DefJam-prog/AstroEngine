[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $ProjectRoot,
    [Parameter(Mandatory = $true)][string] $DotnetRoot,
    [switch] $Check
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
$modulePath = Join-Path $PSScriptRoot 'AstroCSharpProjectBuild.psm1'
Import-Module -Name $modulePath -Force

if ($Check)
{
    $result = Test-AstroCSharpProjectBuild -ProjectRoot $ProjectRoot -DotnetRoot $DotnetRoot
    if ($result.Fresh)
    {
        Write-Output "C# project build is current."
        Write-Output (Join-Path $result.Snapshot.ProjectRoot $result.State.assemblyPath.Replace('/', '\'))
        exit 0
    }
    Write-Error "C# project build is stale: $($result.Reason)" -ErrorAction Continue
    if ($result.Invalid) { exit 2 }
    exit 3
}

$result = Invoke-AstroCSharpProjectBuild -ProjectRoot $ProjectRoot -DotnetRoot $DotnetRoot -RepositoryRoot (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
if ($result.Message) { Write-Output $result.Message }
if ($result.AssemblyPath) { Write-Output "Assembly: $($result.AssemblyPath)" }
exit $result.ExitCode
