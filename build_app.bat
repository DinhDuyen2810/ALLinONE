@echo off
setlocal
echo ===================================================
echo   BIEN DICH VA DONG GOI ONE FOR ALL (Qt 6.11.1)
echo ===================================================

set "PATH=D:\Qt\Tools\mingw1310_64\bin;D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\Ninja;%PATH%"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"

echo.
echo [1/3] Cau hinh CMake...
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH="D:/Qt/6.11.1/mingw_64" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto error

echo.
echo [2/3] Bien dich voi Ninja...
cmake --build build
if errorlevel 1 goto error

echo.
echo [3/3] Dong goi Qt DLLs bang windeployqt...
"D:\Qt\6.11.1\mingw_64\bin\windeployqt.exe" "build\OneForAll.exe" --no-translations --compiler-runtime
if errorlevel 1 goto error

echo.
echo ===================================================
echo   BUILD THANH CONG! File exe: build\OneForAll.exe
echo ===================================================
pause
exit /b 0

:error
echo.
echo [LOI] Qua trinh bien dich gap su co. Vui long kiem tra log.
pause
exit /b 1
