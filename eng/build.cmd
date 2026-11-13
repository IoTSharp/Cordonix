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
call eng\build-ssh.cmd
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 %CDX_VERSION_DEFINE% /DUNICODE /D_UNICODE /DLIBSSH2_STATIC /Iinclude /Ithird_party\libssh2\include /Fobuild\obj\ /Febuild\bin\cdx.exe src\cdx-core\result.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c src\cdx-core\file_stat.c src\cdx-core\file_stat_win32.c src\cdx-core\process_win32.c src\cdx-core\process_query_win32.c src\cdx-core\query_processes_win32.c src\cdx-core\phone.c src\cdx-core\phone_protocol.c src\cdx-core\phone_win32.c src\cdx-core\phone_pair_win32.c src\cdx-core\phone_notifications_win32.c src\cdx-core\ssh_win32.c src\cdx-core\proxy_win32.c src\cdx-core\http_win32.c src\cdx-core\http_proxy_win32.c src\cdx-core\serial_win32.c src\cdx-core\network_win32.c src\cdx-core\screenshot_win32.c src\cdx-core\vnc_win32.c src\cdx-workspace\document_common.c src\cdx-workspace\document_win32.c src\cdx-workspace\encoding_win32.c src\cdx-workspace\create_file.c src\cdx-workspace\create_file_win32.c src\cdx-protocol\json.c src\cdx-protocol\presentation.c src\cdx-protocol\operations.c src\cdx-protocol\data_query.c src\cdx-protocol\discovery.c src\cdx-protocol\program_discovery.c src\cdx-protocol\file_operations.c src\cdx-core\file_ops.c src\cdx-core\file_ops_win32.c src\cdx-protocol\adapters.c src\cdx-protocol\process_query.c src\cdx-protocol\ssh.c src\cdx-protocol\http.c src\cdx-protocol\serial.c src\cdx-protocol\network.c src\cdx-protocol\screenshot.c src\cdx-protocol\vnc.c src\cdx-protocol\tools.c src\cdx-protocol\tool_profiles.c src\cdx-cli\main.c src\cdx-cli\phone_ui_win32.c /link /Brepro /MANIFEST:EMBED build\ssh\libssh2.lib bcrypt.lib crypt32.lib ws2_32.lib advapi32.lib winhttp.lib bthprops.lib runtimeobject.lib user32.lib gdi32.lib shell32.lib ole32.lib windowscodecs.lib credui.lib uuid.lib
exit /b %errorlevel%
