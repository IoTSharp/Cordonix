@echo off
setlocal
cd /d "%~dp0.."
call eng\msvc-env.cmd
if errorlevel 1 exit /b %errorlevel%
if not exist build\capability-gaps mkdir build\capability-gaps
set "CDX_GAP_FLAGS=/nologo /TC /std:c11 /W4 /WX /O2 /MT /utf-8 /DUNICODE /D_UNICODE /Iinclude /Fobuild\capability-gaps\"
cl.exe %CDX_GAP_FLAGS% /Febuild\capability-gaps\cdx-data-query-tests.exe tests\protocol\test_data_query.c src\cdx-protocol\data_query.c src\cdx-protocol\json.c src\cdx-workspace\document_common.c src\cdx-workspace\document_win32.c src\cdx-workspace\encoding_win32.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c /link bcrypt.lib
if errorlevel 1 exit /b %errorlevel%
build\capability-gaps\cdx-data-query-tests.exe "%cd%\build\capability-gaps"
if errorlevel 1 exit /b %errorlevel%
cl.exe %CDX_GAP_FLAGS% /Febuild\capability-gaps\cdx-file-stat-tests.exe tests\workspace\test_file_stat.c src\cdx-core\file_stat.c src\cdx-core\file_stat_win32.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c /link bcrypt.lib
if errorlevel 1 exit /b %errorlevel%
build\capability-gaps\cdx-file-stat-tests.exe "%cd%\build\capability-gaps"
if errorlevel 1 exit /b %errorlevel%
cl.exe %CDX_GAP_FLAGS% /Febuild\capability-gaps\cdx-program-discovery-tests.exe tests\protocol\test_program_discovery.c src\cdx-protocol\program_discovery.c src\cdx-protocol\json.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c src\cdx-workspace\encoding_win32.c /link bcrypt.lib
if errorlevel 1 exit /b %errorlevel%
build\capability-gaps\cdx-program-discovery-tests.exe
if errorlevel 1 exit /b %errorlevel%
cl.exe %CDX_GAP_FLAGS% /Febuild\capability-gaps\cdx-file-ops-tests.exe tests\workspace\test_file_ops.c src\cdx-core\file_ops.c src\cdx-core\file_ops_win32.c src\cdx-protocol\file_operations.c src\cdx-protocol\json.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c src\cdx-workspace\encoding_win32.c /link bcrypt.lib
if errorlevel 1 exit /b %errorlevel%
build\capability-gaps\cdx-file-ops-tests.exe "%cd%\build\capability-gaps"
if errorlevel 1 exit /b %errorlevel%
cl.exe %CDX_GAP_FLAGS% /Febuild\capability-gaps\cdx-discovery-tests.exe tests\protocol\test_discovery.c src\cdx-protocol\discovery.c src\cdx-protocol\json.c src\cdx-core\process_win32.c src\cdx-core\result.c src\cdx-core\filesystem.c src\cdx-core\filesystem_win32.c src\cdx-workspace\encoding_win32.c /link bcrypt.lib
if errorlevel 1 exit /b %errorlevel%
if not "%~1"=="" set "CDX_RG=%~1"
if not defined CDX_RG for /f "delims=" %%G in ('where rg.exe 2^>nul') do if not defined CDX_RG set "CDX_RG=%%G"
if not defined CDX_RG (
  echo find_files real-rg tests NOT_RUN: set CDX_RG to an absolute ripgrep executable.
  exit /b 0
)
build\capability-gaps\cdx-discovery-tests.exe "%CDX_RG%" "%cd%\build\capability-gaps"
exit /b %errorlevel%
