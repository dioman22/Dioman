# Runs a command and tees its output to a log file, then:
#   * annotates the tail of that log as ::error:: when the command fails, because a
#     workflow's own log stream can only be read with an authenticated request while
#     check-run annotations can be read by anyone; and
#   * leaves the log in $env:RUNNER_TEMP, where the workflow's last step publishes every
#     step-*.log to the ci-logs branch. That is the full-log channel: a step that is
#     killed by its timeout still leaves its partial log behind.
#
# Usage (dot-source it, so `exit` ends the step with the command's status):
#   ./.github/scripts/run-step.ps1 -- cmake -S . -B build
#
# Arguments go after `--`: PowerShell would otherwise bind -DCRATE_*=... as a parameter of
# this script.
#
# Note: do not reach for Start-Process here. Redirecting a child's streams through it and
# polling the file looked like a way to add a timeout, and it made the SDK fetch step fail
# in one second with no output captured.
param(
    # Kill the command after this many seconds. GitHub's own step timeout does not reliably
    # end a wedged child process tree, and a step that never ends publishes no evidence.
    [int] $TimeoutSeconds = 0,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Command
)

if ($Command -and $Command[0] -eq '--') {
    $Command = if ($Command.Count -gt 1) { $Command[1..($Command.Count - 1)] } else { @() }
}

if (-not $Command -or $Command.Count -eq 0) {
    Write-Host '::error::run-step.ps1 called without a command'
    exit 2
}

$log = Join-Path $env:RUNNER_TEMP ('step-' + [guid]::NewGuid().ToString('N') + '.log')
Write-Host "run-step: $($Command -join ' ')"
Write-Host "run-step: log $log"

if ($TimeoutSeconds -le 0) {
    if ($Command.Count -eq 1) {
        & $Command[0] 2>&1 | Tee-Object -FilePath $log
    } else {
        & $Command[0] $Command[1..($Command.Count - 1)] 2>&1 | Tee-Object -FilePath $log
    }
    $code = $LASTEXITCODE
} else {
    # Start-Process needs a resolved path: it failed here before by being handed the bare
    # name 'git', which took the SDK fetch step down in one second.
    $exe = $Command[0]
    $resolved = Get-Command $Command[0] -ErrorAction SilentlyContinue
    if ($resolved) { $exe = $resolved.Source }

    $stdout = "$log.out"
    $stderr = "$log.err"
    $arguments = if ($Command.Count -gt 1) { $Command[1..($Command.Count - 1)] } else { @() }

    $proc = Start-Process -FilePath $exe -ArgumentList $arguments -NoNewWindow -PassThru `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if ($null -eq $proc) {
        Write-Host '::error::run-step.ps1 could not start the command'
        exit 2
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $proc.HasExited) {
        Start-Sleep -Seconds 3
        if (-not $proc.HasExited -and (Get-Date) -gt $deadline) {
            Write-Host "run-step: still running after $TimeoutSeconds s, killing it"
            & taskkill /T /F /PID $proc.Id 2>&1 | Out-Null
            Start-Sleep -Seconds 2
            break
        }
    }

    foreach ($file in @($stdout, $stderr)) {
        foreach ($line in @(Get-Content -Path $file -ErrorAction SilentlyContinue)) {
            Add-Content -Path $log -Value $line
        }
    }

    $stalled = -not $proc.HasExited
    if ($stalled) {
        Write-Host 'run-step: last lines written before the kill'
        Get-Content -Path $log -Tail 45 | ForEach-Object { "::error::$_" }
        exit 124
    }

    $code = $proc.ExitCode
}

if ($null -eq $code) { $code = 0 }

if ($code -ne 0) {
    Write-Host "run-step: failed with exit code $code, annotating the tail of the log"
    Get-Content -Path $log -Tail 40 | ForEach-Object { "::error::$_" }
}

exit $code
