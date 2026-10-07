@echo off
setlocal
echo ===================================================
echo   BIEN DICH VA DONG GOI ONE FOR ALL (Qt 6.11.1)
echo ===================================================

set "PATH=D:\Qt\Tools\mingw1310_64\bin;D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\Ninja;%PATH%"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"

echo.
echo [1/5] Cau hinh CMake...
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH="D:/Qt/6.11.1/mingw_64" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto error

echo.
echo [2/5] Bien dich voi Ninja...
cmake --build build
if errorlevel 1 goto error

echo.
echo [3/5] Dong goi Qt DLLs bang windeployqt...
"D:\Qt\6.11.1\mingw_64\bin\windeployqt.exe" "build\OneForAll.exe" --no-translations --compiler-runtime
if errorlevel 1 goto error

echo.
echo [4/6] Dong goi adb/scrcpy (tinh nang Android Phone Control) neu co san trong vendor\scrcpy...
if exist "vendor\scrcpy\scrcpy.exe" (
    if not exist "build\scrcpy" mkdir "build\scrcpy"
    xcopy /y /q "vendor\scrcpy\*" "build\scrcpy\" >nul
    echo   Da dong goi vendor\scrcpy vao build\scrcpy.
) else (
    echo   [CANH BAO] Khong thay vendor\scrcpy\scrcpy.exe - tinh nang Android Phone Control se thieu adb/scrcpy.
    echo   Xem THIRD_PARTY.md de biet cach tai ban scrcpy-win64 chinh thuc va dat vao vendor\scrcpy\.
)

echo.
echo [5/6] Dong goi yt-dlp/ffmpeg (tinh nang Downloader - video nen tang) neu co san trong vendor\yt-dlp...
if exist "vendor\yt-dlp\yt-dlp.exe" (
    if not exist "build\yt-dlp" mkdir "build\yt-dlp"
    xcopy /y /q "vendor\yt-dlp\*" "build\yt-dlp\" >nul
    echo   Da dong goi vendor\yt-dlp vao build\yt-dlp.
) else (
    echo   [CANH BAO] Khong thay vendor\yt-dlp\yt-dlp.exe - tinh nang tai video nen tang se thieu yt-dlp/ffmpeg.
    echo   Xem THIRD_PARTY.md de biet cach tai ban yt-dlp/ffmpeg chinh thuc va dat vao vendor\yt-dlp\.
)

echo.
echo [6/6] Dong bo sang OneForAll_Release (ban ma run_app.bat chay)...
if not exist "OneForAll_Release" mkdir "OneForAll_Release"
copy /y "build\OneForAll.exe" "OneForAll_Release\" >nul
if exist "build\*.dll" xcopy /y /q "build\*.dll" "OneForAll_Release\" >nul
if exist "build\scrcpy" xcopy /y /q /s /i "build\scrcpy" "OneForAll_Release\scrcpy\" >nul
if exist "build\yt-dlp" xcopy /y /q /s /i "build\yt-dlp" "OneForAll_Release\yt-dlp\" >nul
for %%D in (generic iconengines imageformats multimedia networkinformation platforms styles tls) do (
    if exist "build\%%D" xcopy /y /q /s /i "build\%%D" "OneForAll_Release\%%D\" >nul
)

echo.
echo ===================================================
echo   BUILD THANH CONG!
echo   File chay thu (build):      build\OneForAll.exe
echo   Ban dong goi (run_app.bat): OneForAll_Release\OneForAll.exe
echo ===================================================
pause
exit /b 0

:error
echo.
echo [LOI] Qua trinh bien dich gap su co. Vui long kiem tra log.
pause
exit /b 1
