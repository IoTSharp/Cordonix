@echo off
setlocal
cd /d "%~dp0.."
if not exist build\process mkdir build\process
call eng\msvc-env.cmd
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /DUNICODE /D_UNICODE /I include /c src\cdx-core\result.c /Fo:build\process\result.obj
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /DUNICODE /D_UNICODE /I include /c src\cdx-core\process_win32.c /Fo:build\process\process_win32.obj
if errorlevel 1 exit /b %errorlevel%
lib.exe /nologo /out:build\process\cdx-core.lib build\process\result.obj build\process\process_win32.obj
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /utf-8 /DUNICODE /D_UNICODE /I include /c tests\process\test_process.c /Fo:build\process\test_process.obj
if errorlevel 1 exit /b %errorlevel%
link.exe /nologo build\process\test_process.obj build\process\cdx-core.lib /out:build\process\cdx-process-tests.exe
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /DUNICODE /D_UNICODE tests\process\process_fixture.c /Fo:build\process\process_fixture.obj /Fe:build\process\cdx-process-fixture.exe
exit /b %errorlevel%
