@echo off
setlocal
cd /d "%~dp0.."
call eng\msvc-env.cmd
if errorlevel 1 exit /b %errorlevel%
if not exist "build\workspace" mkdir "build\workspace"
cl.exe /nologo /TC /std:c11 /W4 /WX /DUNICODE /D_UNICODE /O2 /Iinclude /Febuild\workspace\workspace-tests.exe /Fobuild\workspace\ src\cdx-workspace\document_common.c src\cdx-workspace\document_win32.c src\cdx-workspace\encoding_win32.c tests\workspace\test_document.c /link bcrypt.lib
exit /b %errorlevel%
