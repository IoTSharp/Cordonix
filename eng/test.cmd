@echo off
setlocal
if not "%~1"=="" set "CDX_RG=%~1"
cd /d "%~dp0.."
call eng\msvc-env.cmd
if errorlevel 1 exit /b %errorlevel%
if not defined CDX_VERSION set "CDX_VERSION=0.1.0-dev"
set "CDX_VERSION_DEFINE=/DCDX_VERSION=\"%CDX_VERSION%\""
call eng\build.cmd
if errorlevel 1 exit /b %errorlevel%
call eng\test-screenshot.cmd
if errorlevel 1 exit /b %errorlevel%
call eng\test-capability-gaps.cmd
if errorlevel 1 exit /b %errorlevel%
build\bin\cdx.exe --phone-ui-smoke
if errorlevel 1 exit /b %errorlevel%
call eng\build-process-core.cmd
if errorlevel 1 exit /b %errorlevel%
call eng\build-workspace.cmd
if errorlevel 1 exit /b %errorlevel%
call eng\test-phone.cmd
if errorlevel 1 exit /b %errorlevel%
if not exist build\protocol mkdir build\protocol
if not exist build\serial-tests mkdir build\serial-tests
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 /Iinclude /Fobuild\protocol\ /Febuild\protocol\cdx-serial-tests.exe tests\protocol\test_serial.c src\cdx-protocol\serial.c src\cdx-core\serial_win32.c src\cdx-protocol\json.c /link advapi32.lib
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-serial-tests.exe "%cd%\build\serial-tests" --pilot
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-serial-tests.exe "%cd%\build\serial-tests"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 /Iinclude /Fobuild\protocol\ /Febuild\protocol\cdx-presentation-tests.exe tests\protocol\test_presentation.c src\cdx-protocol\presentation.c src\cdx-protocol\json.c
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-presentation-tests.exe --pilot
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-presentation-tests.exe
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 %CDX_VERSION_DEFINE% /DUNICODE /D_UNICODE /Iinclude /Fobuild\protocol\ /Febuild\protocol\cdx-json-tests.exe tests\protocol\test_json.c src\cdx-protocol\json.c
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 %CDX_VERSION_DEFINE% /DUNICODE /D_UNICODE /Iinclude /Fobuild\workspace\ /Febuild\workspace\create-file-tests.exe tests\workspace\test_create_file.c src\cdx-workspace\create_file.c src\cdx-workspace\create_file_win32.c src\cdx-workspace\document_common.c src\cdx-workspace\document_win32.c src\cdx-workspace\encoding_win32.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c /link bcrypt.lib
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 %CDX_VERSION_DEFINE% /DUNICODE /D_UNICODE /DLIBSSH2_STATIC /Iinclude /Ithird_party\libssh2\include /Fobuild\protocol\ /Febuild\protocol\cdx-edge-tests.exe tests\workspace\test_edge_cases.c src\cdx-protocol\json.c src\cdx-protocol\operations.c src\cdx-protocol\data_query.c src\cdx-protocol\discovery.c src\cdx-protocol\file_operations.c src\cdx-core\file_ops.c src\cdx-core\file_ops_win32.c src\cdx-protocol\adapters.c src\cdx-protocol\process_query.c src\cdx-protocol\ssh.c src\cdx-protocol\http.c src\cdx-protocol\serial.c src\cdx-protocol\network.c src\cdx-protocol\screenshot.c src\cdx-core\ssh_win32.c src\cdx-core\proxy_win32.c src\cdx-core\http_win32.c src\cdx-core\http_proxy_win32.c src\cdx-core\serial_win32.c src\cdx-core\network_win32.c src\cdx-core\screenshot_win32.c src\cdx-core\process_query_win32.c src\cdx-core\query_processes_win32.c src\cdx-core\phone.c src\cdx-core\phone_protocol.c src\cdx-core\phone_win32.c src\cdx-core\phone_pair_win32.c src\cdx-core\phone_notifications_win32.c src\cdx-workspace\document_common.c src\cdx-workspace\document_win32.c src\cdx-workspace\encoding_win32.c src\cdx-workspace\create_file.c src\cdx-workspace\create_file_win32.c src\cdx-core\process_win32.c src\cdx-core\result.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c /link build\ssh\libssh2.lib bcrypt.lib crypt32.lib ws2_32.lib advapi32.lib winhttp.lib bthprops.lib runtimeobject.lib user32.lib gdi32.lib ole32.lib windowscodecs.lib uuid.lib
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 %CDX_VERSION_DEFINE% /DUNICODE /D_UNICODE /Iinclude /Fobuild\protocol\ /Febuild\protocol\cdx-stdio-tests.exe tests\protocol\test_stdio.c src\cdx-protocol\json.c
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /Iinclude /Fobuild\protocol\ /Febuild\protocol\cdx-process-query-tests-native.exe tests\process\test_process_query.c src\cdx-protocol\process_query.c src\cdx-protocol\json.c /link build\process\cdx-core.lib
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-process-query-tests-native.exe --pilot
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-process-query-tests-native.exe "$(literal)" "two words"
if errorlevel 1 exit /b %errorlevel%
build\process\cdx-process-tests.exe
if errorlevel 1 exit /b %errorlevel%
build\workspace\workspace-tests.exe "%cd%\build\workspace"
if errorlevel 1 exit /b %errorlevel%
build\workspace\create-file-tests.exe "%cd%\build\workspace" --pilot
if errorlevel 1 exit /b %errorlevel%
build\workspace\create-file-tests.exe "%cd%\build\workspace"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /Iinclude /Fobuild\workspace\ /Febuild\workspace\create-directory-tests.exe tests\workspace\test_create_directory.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c
if errorlevel 1 exit /b %errorlevel%
build\workspace\create-directory-tests.exe "%cd%\build\workspace" --pilot
if errorlevel 1 exit /b %errorlevel%
build\workspace\create-directory-tests.exe "%cd%\build\workspace"
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-json-tests.exe
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-edge-tests.exe "%cd%\build\protocol"
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-stdio-tests.exe "%cd%\build\bin\cdx.exe" "%cd%\build\process\cdx-process-fixture.exe"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /Iinclude /Fobuild\protocol\ /Febuild\protocol\cdx-adapter-tests.exe tests\protocol\test_adapters.c src\cdx-protocol\adapters.c src\cdx-protocol\json.c src\cdx-core\process_win32.c src\cdx-core\result.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c src\cdx-workspace\encoding_win32.c /link bcrypt.lib
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 %CDX_VERSION_DEFINE% /DUNICODE /D_UNICODE /DLIBSSH2_STATIC /Iinclude /Ithird_party\libssh2\include /Fobuild\protocol\ /Febuild\protocol\cdx-process-query-tests.exe tests\protocol\test_process_query.c src\cdx-protocol\json.c src\cdx-protocol\operations.c src\cdx-protocol\data_query.c src\cdx-protocol\discovery.c src\cdx-protocol\file_operations.c src\cdx-core\file_ops.c src\cdx-core\file_ops_win32.c src\cdx-protocol\adapters.c src\cdx-protocol\process_query.c src\cdx-protocol\ssh.c src\cdx-protocol\http.c src\cdx-protocol\serial.c src\cdx-protocol\network.c src\cdx-protocol\screenshot.c src\cdx-core\ssh_win32.c src\cdx-core\proxy_win32.c src\cdx-core\http_win32.c src\cdx-core\http_proxy_win32.c src\cdx-core\serial_win32.c src\cdx-core\network_win32.c src\cdx-core\screenshot_win32.c src\cdx-core\process_query_win32.c src\cdx-core\query_processes_win32.c src\cdx-core\phone.c src\cdx-core\phone_protocol.c src\cdx-core\phone_win32.c src\cdx-core\phone_pair_win32.c src\cdx-core\phone_notifications_win32.c src\cdx-workspace\document_common.c src\cdx-workspace\document_win32.c src\cdx-workspace\encoding_win32.c src\cdx-workspace\create_file.c src\cdx-workspace\create_file_win32.c src\cdx-core\process_win32.c src\cdx-core\result.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c /link build\ssh\libssh2.lib bcrypt.lib crypt32.lib ws2_32.lib advapi32.lib winhttp.lib bthprops.lib runtimeobject.lib user32.lib gdi32.lib ole32.lib windowscodecs.lib uuid.lib
if errorlevel 1 exit /b %errorlevel%
set "CDX_PWSH=%ProgramFiles%\PowerShell\7\pwsh.exe"
if not exist "%CDX_PWSH%" set "CDX_PWSH=pwsh.exe"
"%CDX_PWSH%" -NoProfile -File eng\test-adapters-windows.ps1
if errorlevel 1 exit /b %errorlevel%
"%CDX_PWSH%" -NoProfile -File eng\test-pwsh-windows.ps1
if errorlevel 1 exit /b %errorlevel%
build\protocol\cdx-process-query-tests.exe
exit /b %errorlevel%
