@echo off
REM Install the ESP-IDF toolchain for the esp32s3 target.
REM
REM Mirrors the steps of ESP-IDF's own install.bat but skips its MSYSTEM guard,
REM so it can be driven from Git Bash (which sets MSYSTEM for child processes).
REM Downloads are routed through the proxy below if one is set.
REM
REM Usage:
REM   cmd.exe //c "tools\idf_install_steps.bat"
setlocal

for %%i in ("%~dp0..") do set ROOT=%%~fi
if not defined IDF_PATH       set IDF_PATH=%ROOT%\esp-idf
if not defined IDF_TOOLS_PATH set IDF_TOOLS_PATH=%USERPROFILE%\.espressif

REM Clear the guard variables, then optionally configure a proxy.
set MSYSTEM=
if defined HTTP_PROXY  set http_proxy=%HTTP_PROXY%
if defined HTTPS_PROXY set https_proxy=%HTTPS_PROXY%

cd /d "%IDF_PATH%"

echo === [1/3] Install tools for target esp32s3 ===
python.exe "%IDF_PATH%\tools\idf_tools.py" install --targets=esp32s3
if errorlevel 1 goto fail

echo === [2/3] Extract features ===
for /f "delims=" %%i in ('python.exe "%IDF_PATH%\tools\install_util.py" extract features esp32s3') do set FEATURES=%%i
echo FEATURES=%FEATURES%

echo === [3/3] Set up Python environment ===
python.exe "%IDF_PATH%\tools\idf_tools.py" install-python-env --features=%FEATURES%
if errorlevel 1 goto fail

echo === ALL_DONE ===
goto end

:fail
echo === FAILED errorlevel=%errorlevel% ===

:end
endlocal
