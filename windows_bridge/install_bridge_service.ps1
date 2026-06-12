param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [string]$TtsVoice = "zh-CN-YunxiNeural",
    [string]$ControlToken = "",
    [string]$ServiceName = "OpenClawStackChanBridge"
)

$ErrorActionPreference = "Stop"
$repo = Resolve-Path $RepoRoot
$python = (Get-Command python -ErrorAction Stop).Source
$bridge = Join-Path $repo "windows_bridge\openclaw_stackchan_bridge.py"
$logs = Join-Path $repo "windows_bridge\logs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null

$args = "-u `"$bridge`" --host 0.0.0.0 --port 8765 --control-host 127.0.0.1 --control-port 8766 --ws-host 0.0.0.0 --ws-port 8767 --tts-voice `"$TtsVoice`" --event-log `"$logs\events.jsonl`""
if ($ControlToken) {
    $args = "$args --control-token `"$ControlToken`""
}

$nssm = Get-Command nssm -ErrorAction SilentlyContinue
if ($nssm) {
    Write-Host "[service] Installing Windows service with NSSM: $ServiceName"
    & $nssm.Source stop $ServiceName 2>$null | Out-Null
    & $nssm.Source remove $ServiceName confirm 2>$null | Out-Null
    & $nssm.Source install $ServiceName $python $args
    & $nssm.Source set $ServiceName AppDirectory $repo
    & $nssm.Source set $ServiceName AppStdout "$logs\bridge.log"
    & $nssm.Source set $ServiceName AppStderr "$logs\bridge.err.log"
    & $nssm.Source set $ServiceName AppEnvironmentExtra "OPENCLAW_TTS_VOICE=$TtsVoice" "OPENCLAW_BRIDGE_TOKEN=$ControlToken"
    & $nssm.Source start $ServiceName
    Write-Host "[service] Started $ServiceName"
    exit 0
}

Write-Host "[service] NSSM was not found; registering a logon scheduled task fallback."
$taskName = $ServiceName
$action = New-ScheduledTaskAction -Execute $python -Argument $args -WorkingDirectory $repo
$trigger = New-ScheduledTaskTrigger -AtLogOn
$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Highest
Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal | Out-Null
Start-ScheduledTask -TaskName $taskName
Write-Host "[service] Started scheduled task $taskName"
