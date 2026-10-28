@echo off
setlocal
cd /d "%~dp0.."
call eng\msvc-env.cmd
if errorlevel 1 exit /b %errorlevel%
if not exist build\bin mkdir build\bin
if not exist build\obj mkdir build\obj
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /Iinclude /Fobuild\obj\ /Febuild\bin\cdx.exe src\cdx-core\result.c src\cdx-core\process_win32.c src\cdx-workspace\document_win32.c src\cdx-workspace\encoding_win32.c src\cdx-protocol\json.c src\cdx-protocol\operations.c src\cdx-protocol\tools.c src\cdx-cli\main.c /link bcrypt.lib
exit /b %errorlevel%
