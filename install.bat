@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

echo.
echo OpenClaw StackChan CoreS3 installer
echo ===================================
echo.

where python >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Python was not found. Install Python 3.10+ and enable "Add python.exe to PATH".
  exit /b 1
)

python --version
python -c "import sys; raise SystemExit(0 if sys.version_info >= (3,10) else 1)"
if errorlevel 1 (
  echo [ERROR] Python 3.10 or newer is required.
  exit /b 1
)

echo.
echo [1/6] Checking Python bridge dependencies...
python -m edge_tts --list-voices >nul 2>nul
if errorlevel 1 (
  echo [INFO] edge-tts is missing. Installing it now...
  python -m pip install edge-tts
  if errorlevel 1 exit /b 1
) else (
  echo [OK] edge-tts is installed.
)

echo.
echo [2/6] WiFi and Windows bridge address
set /p WIFI_SSID=WiFi SSID [CMCC-4DNk]: 
if "%WIFI_SSID%"=="" set WIFI_SSID=CMCC-4DNk
set /p WIFI_PASSWORD=WiFi password: 
set /p BRIDGE_HOST=Windows LAN IPv4 for CoreS3 to connect, for example 192.168.1.6: 
if "%BRIDGE_HOST%"=="" (
  echo [ERROR] Windows LAN IPv4 is required. Do not use 127.0.0.1.
  exit /b 1
)

echo.
echo [3/6] Edge-TTS voice
echo Recommended male voices:
echo   1. zh-CN-YunxiNeural
echo   2. zh-CN-YunjianNeural
set /p TTS_VOICE=Voice [zh-CN-YunxiNeural]: 
if "%TTS_VOICE%"=="" set TTS_VOICE=zh-CN-YunxiNeural

echo.
echo [4/6] Writing local installer config...
if not exist windows_bridge\logs mkdir windows_bridge\logs
for /f %%i in ('powershell -NoProfile -Command "[guid]::NewGuid().ToString(''N'')"') do set OPENCLAW_BRIDGE_TOKEN=%%i
> windows_bridge\install.local.env echo OPENCLAW_TTS_VOICE=%TTS_VOICE%
>> windows_bridge\install.local.env echo OPENCLAW_BRIDGE_HOST=0.0.0.0
>> windows_bridge\install.local.env echo OPENCLAW_BRIDGE_PORT=8765
>> windows_bridge\install.local.env echo OPENCLAW_WS_HOST=0.0.0.0
>> windows_bridge\install.local.env echo OPENCLAW_WS_PORT=8767
>> windows_bridge\install.local.env echo OPENCLAW_BRIDGE_TOKEN=%OPENCLAW_BRIDGE_TOKEN%

echo.
echo [5/6] Flashing firmware with ESP-IDF...
python windows_bridge\tools\write_firmware_config.py --ssid "%WIFI_SSID%" --password "%WIFI_PASSWORD%" --host "%BRIDGE_HOST%" --port 8765
if errorlevel 1 exit /b 1
where idf.py >nul 2>nul
if errorlevel 1 (
  echo [WARN] idf.py was not found in PATH. Start an ESP-IDF terminal and run install.bat again for flashing.
) else (
  idf.py set-target esp32s3
  if errorlevel 1 exit /b 1
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;windows_bridge/generated_sdkconfig.defaults" reconfigure
  if errorlevel 1 exit /b 1
  idf.py build
  if errorlevel 1 (
    echo [WARN] Build failed. Opening menuconfig so you can inspect OpenClaw Stackchan values manually.
    idf.py menuconfig
    if errorlevel 1 exit /b 1
    idf.py build
    if errorlevel 1 exit /b 1
  )
  idf.py flash
  if errorlevel 1 exit /b 1
)

echo.
echo [6/6] Installing and starting Windows Bridge...
powershell -NoProfile -ExecutionPolicy Bypass -File windows_bridge\install_bridge_service.ps1 -RepoRoot "%CD%" -TtsVoice "%TTS_VOICE%" -ControlToken "%OPENCLAW_BRIDGE_TOKEN%"
if errorlevel 1 exit /b 1

echo.
echo Running bridge smoke test and saying hello...
python windows_bridge\tools\bridge_smoke_test.py --url http://127.0.0.1:8766 --tts-text "你好" --voice "%TTS_VOICE%" --token "%OPENCLAW_BRIDGE_TOKEN%"
if errorlevel 1 (
  echo [WARN] Smoke test failed. Check windows_bridge\logs\bridge.log and Windows Firewall.
  exit /b 1
)

echo.
echo [OK] Installation flow completed.
echo If CoreS3 still shows EHOSTUNREACH, confirm Windows IP, firewall, and that the bridge listens on 0.0.0.0:8765.
endlocal
