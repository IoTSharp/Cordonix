@echo off
setlocal
cd /d "%~dp0.."
call eng\msvc-env.cmd
if errorlevel 1 exit /b %errorlevel%
if not exist build\screenshot mkdir build\screenshot
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /Iinclude /Fobuild\screenshot\ /Febuild\screenshot\cdx-screenshot-tests.exe tests\protocol\test_screenshot.c src\cdx-core\screenshot_win32.c src\cdx-protocol\screenshot.c src\cdx-protocol\json.c src\cdx-core\process_win32.c src\cdx-core\result.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c /link user32.lib gdi32.lib ole32.lib windowscodecs.lib uuid.lib
if errorlevel 1 exit /b %errorlevel%
build\screenshot\cdx-screenshot-tests.exe "%cd%\build\screenshot"
exit /b %errorlevel%
