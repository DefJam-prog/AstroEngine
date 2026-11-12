#requires -Version 7.0
Set-StrictMode -Version Latest

function Invoke-CheckedProcess {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory)][string]$WorkingDirectory,
        [ValidateRange(1, 3600)][int]$TimeoutSeconds = 300
    )
    $result = [ordered]@{
        executable = $FilePath; arguments = $Arguments; workingDirectory = $WorkingDirectory
        status = 'error'; exitCode = $null; stdout = ''; stderr = ''; durationMs = 0
    }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::new()
    try {
        $process.StartInfo.FileName = $FilePath
        $process.StartInfo.WorkingDirectory = $WorkingDirectory
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.CreateNoWindow = $true
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true
        $process.StartInfo.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
        $process.StartInfo.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
        foreach ($argument in $Arguments) { $process.StartInfo.ArgumentList.Add($argument) }
        [void]$process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill($true)
            $process.WaitForExit()
            $result.status = 'timeout'
        } else {
            $result.exitCode = $process.ExitCode
            $result.status = if ($process.ExitCode -eq 0) { 'passed' } else { 'failed' }
        }
        $result.stdout = $stdout.GetAwaiter().GetResult()
        $result.stderr = $stderr.GetAwaiter().GetResult()
    } catch {
        $result.stderr = $_.Exception.Message
    } finally {
        $timer.Stop()
        $result.durationMs = $timer.ElapsedMilliseconds
        $process.Dispose()
    }
    [pscustomobject]$result
}

Export-ModuleMember -Function Invoke-CheckedProcess
