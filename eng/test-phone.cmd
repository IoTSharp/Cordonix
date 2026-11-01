@echo off
setlocal
cd /d "%~dp0.."
call eng\msvc-env.cmd
if errorlevel 1 exit /b %errorlevel%
if not exist build\phone mkdir build\phone
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /Iinclude /Fobuild\phone\ /Febuild\phone\cdx-phone-tests.exe tests\protocol\test_phone.c src\cdx-core\phone.c src\cdx-core\phone_protocol.c src\cdx-core\phone_win32.c src\cdx-core\phone_pair_win32.c src\cdx-core\phone_notifications_win32.c src\cdx-protocol\json.c src\cdx-workspace\encoding_win32.c src\cdx-core\process_win32.c src\cdx-core\result.c /link bcrypt.lib ws2_32.lib bthprops.lib runtimeobject.lib user32.lib
if errorlevel 1 exit /b %errorlevel%
build\phone\cdx-phone-tests.exe
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /DCDX_PHONE_TEST /Iinclude /Fobuild\phone\ /Febuild\phone\cdx-phone-session-tests.exe tests\protocol\test_phone_session_win32.c src\cdx-core\phone.c src\cdx-core\phone_protocol.c src\cdx-core\phone_win32.c src\cdx-core\phone_pair_win32.c src\cdx-core\phone_notifications_win32.c src\cdx-protocol\json.c src\cdx-workspace\encoding_win32.c src\cdx-core\process_win32.c src\cdx-core\result.c /link bcrypt.lib ws2_32.lib bthprops.lib runtimeobject.lib user32.lib
if errorlevel 1 exit /b %errorlevel%
build\phone\cdx-phone-session-tests.exe
exit /b %errorlevel%
