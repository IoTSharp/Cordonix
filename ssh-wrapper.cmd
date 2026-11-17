@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Compatibility entry point for scripts that expect an ssh.exe-shaped command.
if not defined CDX_SSH_EXE set "CDX_SSH_EXE=%SystemRoot%\System32\OpenSSH\ssh.exe"
if not defined CDX_SSH_KEY set "CDX_SSH_KEY=%USERPROFILE%\.ssh\xjetc_toll_remote_ed25519"

if not exist "%CDX_SSH_EXE%" (
    >&2 echo cdx ssh-wrapper: OpenSSH client not found: "%CDX_SSH_EXE%"
    exit /b 9009
)
if not exist "%CDX_SSH_KEY%" (
    >&2 echo cdx ssh-wrapper: private key not found: "%CDX_SSH_KEY%"
    exit /b 2
)

"%CDX_SSH_EXE%" -i "%CDX_SSH_KEY%" -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 -o BatchMode=yes %*
set "CDX_SSH_EXIT=%ERRORLEVEL%"
endlocal & exit /b %CDX_SSH_EXIT%
