# Runs a command, tees its output to a log file, and on failure re-emits the tail of that
# log as ::error:: annotations.
#
# Why: a workflow's log stream can only be downloaded with an authenticated request, but
# annotations can be read by anyone (GET /repos/{owner}/{repo}/check-runs/{id}/annotations).
# Without this, a failed step reports nothing but "Process completed with exit code 1".
#
# With -Annotate the interesting lines are also surfaced as ::notice:: annotations *while*
# the command runs, which is the only way to see where a step that never finishes has got
# to.
#
# Usage (dot-source it so `exit` ends the step with the command's status):
#   ./.github/scripts/run-step.ps1 -Annotate -- cmake -S . -B build
param(
    [switch] $Annotate,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Command
)

# Arguments after `--` are the command to run; PowerShell would otherwise try to bind
# -DCRATE_FETCH_VST3_SDK=ON as a parameter of this script.
if ($Command -and $Command[0] -eq '--') {
    $Command = if ($Command.Count -gt 1) { $Command[1..($Command.Count - 1)] } else { @() }
}

if (-not $Command -or $Command.Count -eq 0) {
    Write-Host '::error::run-step.ps1 called without a command'
    exit 2
}

$log = Join-Path $env:RUNNER_TEMP ('step-' + [guid]::NewGuid().ToString('N') + '.log')
Write-Host "run-step: $($Command -join ' ')"

$arguments = if ($Command.Count -gt 1) { $Command[1..($Command.Count - 1)] } else { @() }

if ($Annotate) {
    $noticePattern = '^-- |^CMake (Error|Warning)|error:|fatal:|Cloning into|Submodule path|FAILED|ninja:'
    $noticed = 0
    & $Command[0] $arguments 2>&1 | ForEach-Object {
        $line = $_.ToString()
        Add-Content -Path $log -Value $line
        Write-Host $line
        if ($noticed -lt 45 -and $line -match $noticePattern) {
            Write-Host "::notice::$line"
            $noticed++
        }
    }
} elseif ($Command.Count -eq 1) {
    & $Command[0] 2>&1 | Tee-Object -FilePath $log
} else {
    & $Command[0] $arguments 2>&1 | Tee-Object -FilePath $log
}

$code = $LASTEXITCODE
if ($null -eq $code) { $code = 0 }

if ($code -ne 0) {
    Write-Host "run-step: failed with exit code $code, annotating the tail of the log"
    Get-Content -Path $log -Tail 40 | ForEach-Object { "::error::$_" }
}

exit $code
