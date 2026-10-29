@echo off
setlocal
cd /d "%~dp0.."
call eng\msvc-env.cmd
if errorlevel 1 exit /b %errorlevel%
if not defined CDX_VERSION set "CDX_VERSION=0.1.0-dev"
set "CDX_VERSION_DEFINE=/DCDX_VERSION=\"%CDX_VERSION%\""
call eng\build.cmd
if errorlevel 1 exit /b %errorlevel%
call eng\build-process-core.cmd
if errorlevel 1 exit /b %errorlevel%
call eng\build-workspace.cmd
if errorlevel 1 exit /b %errorlevel%
if not exist build\protocol mkdir build\protocol
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 %CDX_VERSION_DEFINE% /DUNICODE /D_UNICODE /Iinclude /Fobuild\protocol\ /Febuild\protocol\cdx-json-tests.exe tests\protocol\test_json.c src\cdx-protocol\json.c
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 %CDX_VERSION_DEFINE% /DUNICODE /D_UNICODE /Iinclude /Fobuild\protocol\ /Febuild\protocol\cdx-edge-tests.exe tests\workspace\test_edge_cases.c src\cdx-protocol\json.c src\cdx-protocol\operations.c src\cdx-workspace\document_common.c src\cdx-workspace\document_win32.c src\cdx-workspace\encoding_win32.c src\cdx-core\process_win32.c src\cdx-core\result.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c /link bcrypt.lib
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 %CDX_VERSION_DEFINE% /DUNICODE /D_UNICODE /Iinclude /Fobuild\protocol\ /Febuild\protocol\cdx-stdio-tests.exe tests\protocol\test_stdio.c src\cdx-protocol\json.c
if errorlevel 1 exit /b %errorlevel%
build\process\cdx-process-tests.exe
if errorlevel 1 exit /b %errorlevel%
build\workspace\workspace-tests.exe "%cd%\build\workspace"
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-json-tests.exe
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-edge-tests.exe "%cd%\build\protocol"
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-stdio-tests.exe "%cd%\build\bin\cdx.exe" "%cd%\build\process\cdx-process-fixture.exe"
exit /b %errorlevel%
