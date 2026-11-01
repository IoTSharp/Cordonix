@echo off
set "PATH=%SystemRoot%\System32;%SystemRoot%;%ProgramFiles%\Git\cmd;%LOCALAPPDATA%\Microsoft\WindowsApps;%PATH%"
if defined VSCMD_VER exit /b 0
if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" goto vs18
if exist "%ProgramFiles%\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" goto vs18professional
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" goto vs22enterprise
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" goto vs22community
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" goto vs22professional
echo Run this build from an x64 Native Tools Command Prompt with MSVC C11 support. 1>&2
exit /b 1
:vs18
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
set "PATH=%SystemRoot%\System32;%SystemRoot%;%ProgramFiles%\Git\cmd;%LOCALAPPDATA%\Microsoft\WindowsApps;%PATH%"
exit /b %errorlevel%
:vs18professional
call "%ProgramFiles%\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
set "PATH=%SystemRoot%\System32;%SystemRoot%;%ProgramFiles%\Git\cmd;%LOCALAPPDATA%\Microsoft\WindowsApps;%PATH%"
exit /b %errorlevel%
:vs22enterprise
call "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
set "PATH=%SystemRoot%\System32;%SystemRoot%;%ProgramFiles%\Git\cmd;%LOCALAPPDATA%\Microsoft\WindowsApps;%PATH%"
exit /b %errorlevel%
:vs22community
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
set "PATH=%SystemRoot%\System32;%SystemRoot%;%ProgramFiles%\Git\cmd;%LOCALAPPDATA%\Microsoft\WindowsApps;%PATH%"
exit /b %errorlevel%
:vs22professional
call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
set "PATH=%SystemRoot%\System32;%SystemRoot%;%ProgramFiles%\Git\cmd;%PATH%"
exit /b %errorlevel%
