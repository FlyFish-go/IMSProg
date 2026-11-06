param(
    [Parameter(Mandatory = $true)][string]$Bundle,
    [int]$TimeoutSeconds = 20
)
$ErrorActionPreference = 'Stop'
$bundlePath = (Resolve-Path $Bundle).Path

# These are child-only environment changes; the runner environment is untouched.
foreach ($executable in @('IMSProg.exe', 'IMSProg_database_update.exe', 'IMSProg_editor.exe')) {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $bundlePath $executable
    $start.Arguments = '--portable-smoke-test'
    $start.WorkingDirectory = $bundlePath
    $start.UseShellExecute = $false
    $start.EnvironmentVariables['PATH'] = "$bundlePath;$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($name in @($start.EnvironmentVariables.Keys)) {
        if ($name -like 'QT_*' -or $name -like 'QML*') {
            $start.EnvironmentVariables.Remove($name)
        }
    }
    $start.EnvironmentVariables['QT_QPA_PLATFORM'] = 'windows'

    $process = [System.Diagnostics.Process]::Start($start)
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        $ready = $false
        do {
            Start-Sleep -Milliseconds 250
            $process.Refresh()
            if ($process.HasExited) {
                throw "$executable exited during smoke start with code $($process.ExitCode)"
            }
            # A Windows loader error dialog can keep a process alive. Require an
            # actual Qt window and the Windows platform plugin from this bundle.
            $modules = @($process.Modules)
            $widgets = @($modules | Where-Object ModuleName -eq 'Qt6Widgets.dll')
            $platform = @($modules | Where-Object ModuleName -eq 'qwindows.dll')
            $ready = $process.MainWindowHandle -ne 0 -and $process.Responding -and
                     $widgets.Count -eq 1 -and $platform.Count -eq 1 -and
                     $process.MainWindowTitle -like 'IMSProg *'
            if ($ready) {
                foreach ($module in @($widgets[0], $platform[0])) {
                    if (-not $module.FileName.StartsWith($bundlePath + '\', [StringComparison]::OrdinalIgnoreCase)) {
                        throw "$executable loaded a Qt module outside its bundle: $($module.FileName)"
                    }
                }
            }
        } while (-not $ready -and (Get-Date) -lt $deadline)
        if (-not $ready) {
            throw "$executable did not open a Qt window using the bundled Windows platform plugin"
        }
        # Also reject a startup crash just after window creation.
        Start-Sleep -Seconds 2
        $process.Refresh()
        if ($process.HasExited -or -not $process.Responding) {
            throw "$executable failed after window creation"
        }
        Write-Output "PASS: $executable opened '$($process.MainWindowTitle)' with isolated PATH"
    } finally {
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        $process.Dispose()
    }
}
