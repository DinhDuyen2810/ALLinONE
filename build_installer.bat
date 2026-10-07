@echo off
setlocal
cd /d "%~dp0"
echo ===================================================
echo   DONG GOI BO CAI DAT ONE FOR ALL (setup.exe)
echo ===================================================

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo [LOI] Khong tim thay Inno Setup tai "%ISCC%".
    echo Tai va cai Inno Setup mien phi tai: https://jrsoftware.org/isdl.php
    pause
    exit /b 1
)

echo.
echo [1/2] Dung lai OneForAll_Release\ moi nhat (goi build_app.bat)...
call "%~dp0build_app.bat" /nopause
if errorlevel 1 goto error

echo.
echo [2/2] Bien dich bo cai dat bang Inno Setup...
"%ISCC%" "%~dp0installer\OneForAll.iss"
if errorlevel 1 goto error

echo.
echo ===================================================
echo   DONG GOI THANH CONG!
echo   Bo cai dat: dist\OneForAll_Setup.exe
echo ===================================================
pause
exit /b 0

:error
echo.
echo [LOI] Qua trinh dong goi gap su co. Vui long kiem tra log.
pause
exit /b 1
