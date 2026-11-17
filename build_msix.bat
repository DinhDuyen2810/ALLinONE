@echo off
setlocal
cd /d "%~dp0"

rem Dong goi MSIX de nop Microsoft Store: dist\OneForAll_Store.msix (KHONG ky - Store tu ky lai khi phat hanh).
rem Khong phai 1 trong 4 file phat hanh GitHub (build_installer.bat) - chi dung de tai len Partner Center.
rem Danh tinh goi: installer\msix\store_identity.txt. Chi tiet: PROJECT_OVERVIEW.md muc 4zzi.
rem   build_msix.bat            dung lai OneForAll_Release\ (build_app.bat) roi dong goi
rem   build_msix.bat /nobuild   dong goi tu OneForAll_Release\ dang co (vd ngay sau build_installer.bat)
rem   them /nopause de khong dung o cuoi (goi tu cong cu tu dong)

set "SKIP_PAUSE="
set "NO_BUILD="
for %%A in (%*) do (
    if /i "%%~A"=="/nopause" set "SKIP_PAUSE=1"
    if /i "%%~A"=="/nobuild" set "NO_BUILD=1"
)

echo ===================================================
echo   DONG GOI MSIX CHO MICROSOFT STORE
echo ===================================================

if not defined NO_BUILD (
    echo [1/2] Dung lai OneForAll_Release\ moi nhat ^(goi build_app.bat^)...
    call "%~dp0build_app.bat" /nopause
    if errorlevel 1 goto error
) else (
    echo [1/2] Bo qua build - dung OneForAll_Release\ dang co.
)

echo.
echo [2/2] Dong goi MSIX (makeappx)...
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\msix\build_msix.ps1"
if errorlevel 1 goto error

echo.
echo ===================================================
echo   XONG: dist\OneForAll_Store.msix
echo ===================================================
if not defined SKIP_PAUSE pause
exit /b 0

:error
echo.
echo [LOI] Dong goi MSIX gap su co. Xem thong bao phia tren.
if not defined SKIP_PAUSE pause
exit /b 1
