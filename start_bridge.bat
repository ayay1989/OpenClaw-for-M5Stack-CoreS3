@echo off
setlocal EnableExtensions

cd /d "%~dp0"

echo.
echo OpenClaw StackChan Bridge launcher
echo ==================================
echo.

where python >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Python was not found. Install Python 3.10+ and enable "Add python.exe to PATH".
  exit /b 1
)

echo [1/4] Python:
python --version

echo.
echo [2/4] Windows LAN IPv4 candidates:
powershell -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.IPAddress -notlike '127.*' -and $_.PrefixOrigin -ne 'WellKnown'} | Select-Object -ExpandProperty IPAddress"

echo.
echo [3/4] Checking TCP port 8765:
powershell -NoProfile -Command "$c=Get-NetTCPConnection -LocalPort 8765 -State Listen -ErrorAction SilentlyContinue; if ($c) { Write-Host '[WARN] Port 8765 is already listening. Another bridge may already be running.' } else { Write-Host '[OK] Port 8765 is free.' }"

echo.
echo [4/4] Starting Bridge on 0.0.0.0:8765
echo If CoreS3 still shows EHOSTUNREACH, confirm the firmware TCP host matches one of the LAN IPv4 addresses above.
echo Keep this window open.
echo.

python -u windows_bridge\openclaw_stackchan_bridge.py --host 0.0.0.0 --port 8765 --control-host 127.0.0.1 --control-port 8766 --ws-host 0.0.0.0 --ws-port 8767

echo.
echo Bridge stopped with exit code %ERRORLEVEL%.
endlocal
