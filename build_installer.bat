@echo off
setlocal
cd /d "%~dp0"

rem /nopause: cho phep goi script nay tu cong cu tu dong (khong co nguoi ngoi go phim) ma khong bi treo
rem o lenh "pause" cuoi cung - giong het co che /nopause cua build_app.bat.
set "SKIP_PAUSE="
if /i "%~1"=="/nopause" set "SKIP_PAUSE=1"

echo ===================================================
echo   DONG GOI BO CAI DAT ONE FOR ALL (setup.exe + setup.msi + zip)
echo ===================================================
rem Tu ban ghi nho du an (2026-10-09): MOI lan sua code deu phai dung lai CA BON file phat hanh - setup.exe,
rem setup.msi, VA zip cua ca hai - khong chi rieng setup.exe nhu truoc. Ly do: nguoi tai ve chi double-click
rem chay ngay, khong tu chon dinh dang - can co san ca 4 moi luc (xem PROJECT_OVERVIEW.md muc 4n/4o).

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo [LOI] Khong tim thay Inno Setup tai "%ISCC%".
    echo Tai va cai Inno Setup mien phi tai: https://jrsoftware.org/isdl.php
    if not defined SKIP_PAUSE pause
    exit /b 1
)

rem Tim WiX Toolset (candle.exe/heat.exe/light.exe) - khong phai trinh cai dat, chi la file .exe giai nen
rem tu zip chinh thuc cua wixtoolset/wix3 (xem THIRD_PARTY.md). Uu tien WIX_TOOLSET_BIN (bien moi truong,
rem cho may khac tu dat duong dan rieng), roi %LOCALAPPDATA%\WixToolset (quy uoc khuyen dung cho may moi),
rem roi D:\WixToolset (vi tri da dung tren may dev hien tai).
set "WIXBIN="
if defined WIX_TOOLSET_BIN if exist "%WIX_TOOLSET_BIN%\candle.exe" set "WIXBIN=%WIX_TOOLSET_BIN%"
if not defined WIXBIN if exist "%LOCALAPPDATA%\WixToolset\candle.exe" set "WIXBIN=%LOCALAPPDATA%\WixToolset"
if not defined WIXBIN if exist "D:\WixToolset\candle.exe" set "WIXBIN=D:\WixToolset"
if not defined WIXBIN (
    echo [LOI] Khong tim thay WiX Toolset ^(candle.exe/heat.exe/light.exe^).
    echo Tai ban nhi phan WiX Toolset v3 tai: https://github.com/wixtoolset/wix3/releases
    echo Giai nen vao %%LOCALAPPDATA%%\WixToolset ^(hoac dat bien WIX_TOOLSET_BIN tro toi thu muc chua candle.exe^).
    if not defined SKIP_PAUSE pause
    exit /b 1
)

echo.
echo [1/5] Dung lai OneForAll_Release\ moi nhat (goi build_app.bat)...
call "%~dp0build_app.bat" /nopause
if errorlevel 1 goto error

echo.
echo [2/5] Bien dich bo cai dat .exe bang Inno Setup...
rem Doc so phien ban tu file VERSION o goc du an (nguon DUY NHAT - xem CMakeLists.txt) roi truyen vao
rem Inno Setup qua /D, de AppVersion trong trinh cai dat luon khop voi APP_VERSION ben trong exe.
set "APPVER="
set /p APPVER=<"%~dp0VERSION"
rem Xoa khoang trang thua o CUOI (set /p da tu bo ky tu xuong dong, chi con lo neu ai do lo go them
rem dau cach khi sua file VERSION bang tay) - khac CMakeLists.txt dung string(STRIP ...), batch khong co
rem san ham trim nen phai tu xoa tung ky tu mot. Phat hien khi tu ra soat lai: thieu buoc nay co the
rem nhung dau cach thua lot qua kiem tra rong roi nhung vao dung AppVersion cua trinh cai dat.
:TrimAppVer
if "%APPVER:~-1%"==" " (
    set "APPVER=%APPVER:~0,-1%"
    goto TrimAppVer
)
if "%APPVER%"=="" (
    echo [LOI] Khong doc duoc file VERSION.
    goto error
)
echo   Phien ban: %APPVER%
"%ISCC%" "/DMyAppVersion=%APPVER%" "%~dp0installer\OneForAll.iss"
if errorlevel 1 goto error

echo.
echo [3/5] Don sach OneForAll_Release\ de "gat" (heat.exe) dung bo cai dat .msi...
rem Can mot ban sao RIENG, da loai logs\ va TOAN BO profiles\ (giong het quy tac Excludes cua
rem OneForAll.iss) - vi heat.exe khong co co che loai tru theo mau nhu Inno Setup, phai loc truoc roi moi
rem "gat" thu muc. Chi DUY NHAT profiles\default.json (chuoi Auto Click mau, lay tu ma nguon trong Git) duoc
rem dong goi: thu muc profiles\ cua ban dev co the chua lich su QR, khoa ghep doi Connect Together, nhan VPN
rem ca nhan cua nguoi phat trien - khong bao gio duoc lot vao bo cai dat. exit code 1 cua robocopy la BINH
rem THUONG (nghia la da copy file thanh cong), khong phai loi - chi >=8 moi la loi that su.
set "MSI_STAGE=%~dp0obj\msi_stage\OneForAll_Release"
set "MSI_OBJ=%~dp0obj\msi_obj"
if exist "%~dp0obj" rmdir /s /q "%~dp0obj"
mkdir "%MSI_STAGE%" 2>nul
mkdir "%MSI_OBJ%" 2>nul
robocopy "%~dp0OneForAll_Release" "%MSI_STAGE%" /E /XD logs profiles /NFL /NDL /NJH /NJS >nul
if errorlevel 8 goto error
mkdir "%MSI_STAGE%\profiles" 2>nul
copy /y "%~dp0profiles\default.json" "%MSI_STAGE%\profiles\default.json" >nul
if errorlevel 1 goto error

echo.
echo [4/5] Bien dich bo cai dat .msi bang WiX Toolset (heat + candle + light)...
"%WIXBIN%\heat.exe" dir "%MSI_STAGE%" -cg MainComponents -gg -scom -sreg -sfrag -srd -dr INSTALLFOLDER -var var.StagingDir -out "%MSI_OBJ%\Harvested.wxs"
if errorlevel 1 goto error
rem -out "...\\" (hai dau \ truoc dau ngoac kep dong): bat buoc vi mot dau \ don truoc " bi Windows hieu
rem la ky tu thoat lam hong dau ngoac (da xac nhan that: candle.exe bao loi CNDL0117 "contains a literal
rem quote character" khi chi co mot dau \ - day la quy tac phan tich dong lenh chuan cua Windows, khong
rem phai loi cua candle.exe).
"%WIXBIN%\candle.exe" "-dAppVersion=%APPVER%" "-dStagingDir=%MSI_STAGE%" -out "%MSI_OBJ%\\" "%~dp0installer\Product.wxs" "%MSI_OBJ%\Harvested.wxs"
if errorlevel 1 goto error
rem -sice:ICE38/ICE64/ICE91: ca ba deu la canh bao "sai" CHI VOI goi cai dat co the chuyen doi per-user/
rem per-machine qua thuoc tinh ALLUSERS - du an nay LUON LUON cai dat kieu per-user (InstallScope="perUser"
rem co dinh trong Product.wxs, khong bao gio doi), nen ca ba khong ap dung that su (da xac nhan that: cai
rem dat + go cai dat thu nghiem bang msiexec /i va /x deu don sach hoan toan, khong con rac).
"%WIXBIN%\light.exe" -ext WixUIExtension -ext WixUtilExtension -b "%~dp0installer" -sice:ICE38 -sice:ICE64 -sice:ICE91 -spdb -out "%~dp0dist\OneForAll_Setup.msi" "%MSI_OBJ%\Product.wixobj" "%MSI_OBJ%\Harvested.wixobj"
if errorlevel 1 goto error

echo.
echo [5/5] Nen zip ca hai bo cai dat (cho ai muon tai dang .zip thay vi .exe/.msi truc tiep)...
powershell -NoProfile -Command "Compress-Archive -Path '%~dp0dist\OneForAll_Setup.exe' -DestinationPath '%~dp0dist\OneForAll_Setup_exe.zip' -Force"
if errorlevel 1 goto error
powershell -NoProfile -Command "Compress-Archive -Path '%~dp0dist\OneForAll_Setup.msi' -DestinationPath '%~dp0dist\OneForAll_Setup_msi.zip' -Force"
if errorlevel 1 goto error

echo.
echo ===================================================
echo   DONG GOI THANH CONG! (ca 4 file trong dist\)
echo   - OneForAll_Setup.exe       (trinh cai dat, Inno Setup)
echo   - OneForAll_Setup.msi       (trinh cai dat, Windows Installer)
echo   - OneForAll_Setup_exe.zip   (zip cua file .exe)
echo   - OneForAll_Setup_msi.zip   (zip cua file .msi)
echo ===================================================
if not defined SKIP_PAUSE pause
exit /b 0

:error
echo.
echo [LOI] Qua trinh dong goi gap su co. Vui long kiem tra log.
if not defined SKIP_PAUSE pause
exit /b 1
