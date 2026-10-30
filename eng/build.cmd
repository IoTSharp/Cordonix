@echo off
setlocal
cd /d "%~dp0.."
call eng\msvc-env.cmd
if errorlevel 1 exit /b %errorlevel%
if not defined CDX_VERSION set "CDX_VERSION=0.1.0-dev"
set "CDX_VERSION_DEFINE=/DCDX_VERSION=\"%CDX_VERSION%\""
if not exist build\obj mkdir build\obj
git rev-parse --verify HEAD > build\obj\build-revision.txt
if errorlevel 1 exit /b %errorlevel%
set /p CDX_BUILD_REVISION=<build\obj\build-revision.txt
git status --porcelain > build\obj\build-status.txt
if errorlevel 1 exit /b %errorlevel%
set "CDX_BUILD_DIRTY=0"
for %%S in (build\obj\build-status.txt) do if %%~zS GTR 0 set "CDX_BUILD_DIRTY=1"
set "CDX_VERSION_DEFINE=%CDX_VERSION_DEFINE% /DCDX_BUILD_REVISION=\"%CDX_BUILD_REVISION%\" /DCDX_BUILD_DIRTY=%CDX_BUILD_DIRTY%"
if not exist build\bin mkdir build\bin
if not exist build\obj mkdir build\obj
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 %CDX_VERSION_DEFINE% /DUNICODE /D_UNICODE /Iinclude /Fobuild\obj\ /Febuild\bin\cdx.exe src\cdx-core\result.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c src\cdx-core\process_win32.c src\cdx-workspace\document_common.c src\cdx-workspace\document_win32.c src\cdx-workspace\encoding_win32.c src\cdx-workspace\create_file.c src\cdx-workspace\create_file_win32.c src\cdx-protocol\json.c src\cdx-protocol\operations.c src\cdx-protocol\adapters.c src\cdx-protocol\tools.c src\cdx-cli\main.c /link /Brepro bcrypt.lib
exit /b %errorlevel%
