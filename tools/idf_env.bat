@echo off
REM Activate the ESP-IDF environment and run the command passed in.
REM
REM The MSYSTEM guard is cleared first: ESP-IDF's scripts refuse to run when
REM they detect an MSys/Mingw environment, and Git Bash sets MSYSTEM for every
REM child process -- including this one.
REM
REM Usage from Git Bash:
REM   cmd.exe //c "tools\idf_env.bat idf.py --version"
REM   cmd.exe //c "tools\idf_env.bat idf.py -p COM3 flash monitor"
setlocal

set MSYSTEM=

for %%i in ("%~dp0..") do set ROOT=%%~fi
if not defined IDF_PATH       set IDF_PATH=%ROOT%\esp-idf
if not defined IDF_TOOLS_PATH set IDF_TOOLS_PATH=%USERPROFILE%\.espressif

call "%IDF_PATH%\export.bat" >nul 2>&1

%*

endlocal
