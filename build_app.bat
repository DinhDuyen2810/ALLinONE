@echo off
setlocal
cd /d "%~dp0"
echo ===================================================
echo   BIEN DICH VA DONG GOI ONE FOR ALL (Qt 6.11.1)
echo ===================================================

rem /nopause: cho phep goi script nay tu script khac (vd build_installer.bat) ma khong bi treo o
rem pause cuoi cung cho nguoi dung bam phim - chi dung khi TU goi tu dong hoa, khong anh huong gi khi
rem nguoi dung tu bam dup chuot chay binh thuong (van pause nhu cu).
set "SKIP_PAUSE="
if /i "%~1"=="/nopause" set "SKIP_PAUSE=1"

set "PATH=D:\Qt\Tools\mingw1310_64\bin;D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\Ninja;%PATH%"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"

echo.
echo [1/7] Cau hinh CMake...
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH="D:/Qt/6.11.1/mingw_64" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto error

echo.
echo [2/7] Bien dich voi Ninja...
cmake --build build
if errorlevel 1 goto error

echo.
echo [3/7] Dong goi Qt DLLs bang windeployqt...
"D:\Qt\6.11.1\mingw_64\bin\windeployqt.exe" "build\OneForAll.exe" --no-translations --compiler-runtime
if errorlevel 1 goto error

echo.
echo [4/7] Dong goi adb/scrcpy (tinh nang Android Phone Control) neu co san trong vendor\scrcpy...
if exist "vendor\scrcpy\scrcpy.exe" (
    if not exist "build\scrcpy" mkdir "build\scrcpy"
    xcopy /y /q "vendor\scrcpy\*" "build\scrcpy\" >nul
    echo   Da dong goi vendor\scrcpy vao build\scrcpy.
) else (
    echo   [CANH BAO] Khong thay vendor\scrcpy\scrcpy.exe - tinh nang Android Phone Control se thieu adb/scrcpy.
    echo   Xem THIRD_PARTY.md de biet cach tai ban scrcpy-win64 chinh thuc va dat vao vendor\scrcpy\.
)

echo.
echo [5/7] Dong goi yt-dlp/ffmpeg (tinh nang Downloader - video nen tang) neu co san trong vendor\yt-dlp...
if exist "vendor\yt-dlp\yt-dlp.exe" (
    if not exist "build\yt-dlp" mkdir "build\yt-dlp"
    xcopy /y /q "vendor\yt-dlp\*" "build\yt-dlp\" >nul
    echo   Da dong goi vendor\yt-dlp vao build\yt-dlp.
) else (
    echo   [CANH BAO] Khong thay vendor\yt-dlp\yt-dlp.exe - tinh nang tai video nen tang se thieu yt-dlp/ffmpeg.
    echo   Xem THIRD_PARTY.md de biet cach tai ban yt-dlp/ffmpeg chinh thuc va dat vao vendor\yt-dlp\.
)

echo.
echo [6/7] Dong bo sang OneForAll_Release (ban ma run_app.bat chay)...
if not exist "OneForAll_Release" mkdir "OneForAll_Release"
copy /y "build\OneForAll.exe" "OneForAll_Release\" >nul
if exist "build\*.dll" xcopy /y /q "build\*.dll" "OneForAll_Release\" >nul
if exist "build\scrcpy" xcopy /y /q /s /i "build\scrcpy" "OneForAll_Release\scrcpy\" >nul
if exist "build\yt-dlp" xcopy /y /q /s /i "build\yt-dlp" "OneForAll_Release\yt-dlp\" >nul
for %%D in (generic iconengines imageformats multimedia networkinformation platforms styles tls) do (
    if exist "build\%%D" xcopy /y /q /s /i "build\%%D" "OneForAll_Release\%%D\" >nul
)

echo.
echo [7/7] Chep chuoi Auto Click MAU (profiles\default.json) tu ma nguon vao ban dong goi...
rem Nguon DUY NHAT cua tep mau la profiles\default.json trong Git - truoc day OneForAll_Release\profiles\
rem chi ton tai do tung chep tay mot lan tren may dev (ban CI khong co tep nay). Du lieu nguoi dung luc chay
rem nay nam o %%LOCALAPPDATA%%\OneForAll (xem src\core\AppPaths.h), khong con ghi vao thu muc nay.
if not exist "OneForAll_Release\profiles" mkdir "OneForAll_Release\profiles"
copy /y "profiles\default.json" "OneForAll_Release\profiles\default.json" >nul
if errorlevel 1 goto error

echo.
echo ===================================================
echo   BUILD THANH CONG!
echo   File chay thu (build):      build\OneForAll.exe
echo   Ban dong goi (run_app.bat): OneForAll_Release\OneForAll.exe
echo ===================================================
if not defined SKIP_PAUSE pause
exit /b 0

:error
echo.
echo [LOI] Qua trinh bien dich gap su co. Vui long kiem tra log.
if not defined SKIP_PAUSE pause
exit /b 1
