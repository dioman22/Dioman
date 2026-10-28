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

    # Kill the command after this many seconds and annotate where it got to. A step that
    # never finishes produces no annotations at all, so without this a stall stays opaque.
    [int] $TimeoutSeconds = 0,

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

$stdout = "$log.out"
$stderr = "$log.err"
$pattern = '^-- |^CMake (Error|Warning)|error:|fatal:|FAILED|ninja:'
$noticed = 0

$proc = Start-Process -FilePath $Command[0] -ArgumentList $arguments -NoNewWindow -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
if ($null -eq $proc) {
    Write-Host '::error::run-step.ps1 could not start the command'
    exit 2
}

$deadline = if ($TimeoutSeconds -gt 0) { (Get-Date).AddSeconds($TimeoutSeconds) } else { [datetime]::MaxValue }
$seen = 0
$killed = $false

while (-not $proc.HasExited) {
    Start-Sleep -Seconds 3
    $all = @(Get-Content -Path $stdout -ErrorAction SilentlyContinue)
    for ($i = $seen; $i -lt $all.Count; $i++) {
        Add-Content -Path $log -Value $all[$i]
        Write-Host $all[$i]
        if ($Annotate -and $noticed -lt 45 -and $all[$i] -match $pattern) {
            Write-Host "::notice::$($all[$i])"
            $noticed++
        }
    }
    $seen = $all.Count
    if ((Get-Date) -gt $deadline) { $killed = $true; break }
}

if ($killed) {
    Write-Host "run-step: still running after $TimeoutSeconds s, killing it"
    & taskkill /T /F /PID $proc.Id 2>&1 | Out-Null
    Start-Sleep -Seconds 2
    foreach ($line in @(Get-Content -Path $stderr -ErrorAction SilentlyContinue)) {
        Add-Content -Path $log -Value $line
    }
    Write-Host 'run-step: last lines written before the kill'
    Get-Content -Path $log -Tail 45 | ForEach-Object { "::error::$_" }
    exit 124
}

$proc.WaitForExit()
$all = @(Get-Content -Path $stdout -ErrorAction SilentlyContinue)
for ($i = $seen; $i -lt $all.Count; $i++) {
    Add-Content -Path $log -Value $all[$i]
    Write-Host $all[$i]
}
foreach ($line in @(Get-Content -Path $stderr -ErrorAction SilentlyContinue)) {
    Add-Content -Path $log -Value $line
    Write-Host $line
}

$code = $proc.ExitCode
if ($null -eq $code) { $code = 0 }

if ($code -ne 0) {
    Write-Host "run-step: failed with exit code $code, annotating the tail of the log"
    Get-Content -Path $log -Tail 40 | ForEach-Object { "::error::$_" }
}

exit $code
