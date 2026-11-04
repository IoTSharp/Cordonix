@echo off
setlocal
cd /d "%~dp0.."
call eng\msvc-env.cmd
if errorlevel 1 exit /b %errorlevel%
if not exist build\ssh mkdir build\ssh
set "ssh_sources=agent bcrypt_pbkdf channel comp chacha cipher-chachapoly crypt crypto global hostkey keepalive kex knownhost mac misc packet pem poly1305 publickey scp session sftp transport userauth userauth_kbd_packet version wincng"
for %%S in (%ssh_sources%) do (
  cl.exe /nologo /TC /std:c11 /W3 /WX /O2 /MT /utf-8 /DLIBSSH2_STATIC /DLIBSSH2_WINCNG /DLIBSSH2_ECDSA_WINCNG /DLIBSSH2_NO_ZLIB /DWIN32_LEAN_AND_MEAN /Ithird_party\libssh2\include /Ithird_party\libssh2\src /Fobuild\ssh\%%S.obj /c third_party\libssh2\src\%%S.c
  if errorlevel 1 exit /b 1
)
lib.exe /nologo /OUT:build\ssh\libssh2.lib build\ssh\*.obj
exit /b %errorlevel%
