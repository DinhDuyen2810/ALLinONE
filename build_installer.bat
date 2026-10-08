@echo off
setlocal
cd /d "%~dp0"

rem /nopause: cho phep goi script nay tu cong cu tu dong (khong co nguoi ngoi go phim) ma khong bi treo
rem o lenh "pause" cuoi cung - giong het co che /nopause cua build_app.bat.
set "SKIP_PAUSE="
if /i "%~1"=="/nopause" set "SKIP_PAUSE=1"

echo ===================================================
echo   DONG GOI BO CAI DAT ONE FOR ALL (setup.exe)
echo ===================================================

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo [LOI] Khong tim thay Inno Setup tai "%ISCC%".
    echo Tai va cai Inno Setup mien phi tai: https://jrsoftware.org/isdl.php
    if not defined SKIP_PAUSE pause
    exit /b 1
)

echo.
echo [1/2] Dung lai OneForAll_Release\ moi nhat (goi build_app.bat)...
call "%~dp0build_app.bat" /nopause
if errorlevel 1 goto error

echo.
echo [2/2] Bien dich bo cai dat bang Inno Setup...
rem Doc so phien ban tu file VERSION o goc du an (nguon DUY NHAT - xem CMakeLists.txt) roi truyen vao
rem Inno Setup qua /D, de AppVersion trong trinh cai dat luon khop voi APP_VERSION ben trong exe.
set "APPVER="
set /p APPVER=<"%~dp0VERSION"
if "%APPVER%"=="" (
    echo [LOI] Khong doc duoc file VERSION.
    goto error
)
echo   Phien ban: %APPVER%
"%ISCC%" "/DMyAppVersion=%APPVER%" "%~dp0installer\OneForAll.iss"
if errorlevel 1 goto error

echo.
echo ===================================================
echo   DONG GOI THANH CONG!
echo   Bo cai dat: dist\OneForAll_Setup.exe
echo ===================================================
if not defined SKIP_PAUSE pause
exit /b 0

:error
echo.
echo [LOI] Qua trinh dong goi gap su co. Vui long kiem tra log.
if not defined SKIP_PAUSE pause
exit /b 1
