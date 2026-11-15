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
cl.exe /nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 %CDX_VERSION_DEFINE% /DUNICODE /D_UNICODE /DLIBSSH2_STATIC /Iinclude /Ithird_party\libssh2\include /Fobuild\obj\ /Febuild\bin\cdx.exe @eng\build-sources.rsp /link /Brepro /MANIFEST:EMBED build\ssh\libssh2.lib bcrypt.lib crypt32.lib ws2_32.lib advapi32.lib winhttp.lib bthprops.lib runtimeobject.lib user32.lib gdi32.lib shell32.lib ole32.lib windowscodecs.lib credui.lib uuid.lib
exit /b %errorlevel%
