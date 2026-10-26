@echo off
rem ===================================================================================================
rem  Don dep cac tep .exe thua - chi giu MOT ban chay that: OneForAll_Release\OneForAll.exe (run_app.bat chay no).
rem
rem  Xoa: moi .exe trong build\ (bo test, cong cu kiem tra tay, ban dev OneForAll.exe), ban sao cong cu ben
rem  thu ba trong build\ (scrcpy, yt-dlp - build_app.bat tu chep lai khi can), thu muc tam dung MSI (obj\).
rem  KHONG dong vao: OneForAll_Release\ (ban chay that + adb/scrcpy/yt-dlp/ffmpeg no can), vendor\ (nguon tai
rem  san, build lai khong phai tai), ma nguon, build\ cache/objects.
rem
rem    clean_exes.bat          don exe test/cong cu + thu muc tam
rem    clean_exes.bat /dist    don them bo cai trong dist\ (setup.exe, .msi, 2 tep zip - da co tren GitHub Release)
rem
rem  Sau khi don: muon chay test thi build lai target do (cmake --build build --target <ten>_tests);
rem  vpn_direct_tests can build\OneForAll.exe -> chay build_app.bat truoc.
rem ===================================================================================================
setlocal
cd /d "%~dp0"

del /q build\*.exe 2>nul
if exist build\scrcpy rd /s /q build\scrcpy
if exist build\yt-dlp rd /s /q build\yt-dlp
if exist obj\msi_stage rd /s /q obj\msi_stage
if exist obj\msi_obj rd /s /q obj\msi_obj

if /i "%~1"=="/dist" (
    del /q dist\OneForAll_Setup.exe dist\OneForAll_Setup.msi dist\OneForAll_Setup_exe.zip dist\OneForAll_Setup_msi.zip 2>nul
    echo Da don bo cai trong dist\.
)

echo Da don xong. Ban chay that con lai: OneForAll_Release\OneForAll.exe
if not exist OneForAll_Release\OneForAll.exe echo CANH BAO: chua co OneForAll_Release\OneForAll.exe - chay build_app.bat de dung lai.
endlocal
