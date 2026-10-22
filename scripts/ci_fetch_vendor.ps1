# Tải và xác minh các công cụ nhị phân bên thứ ba (scrcpy/adb, yt-dlp, ffmpeg) vào vendor/ - CHỈ dùng
# trong CI (GitHub Actions), tái hiện đúng quy trình thủ công đã mô tả trong THIRD_PARTY.md (tải bản
# phát hành CHÍNH THỨC, xác minh checksum SHA-256 trước khi dùng - không tin bất kỳ nguồn nào khác).
#
# vendor/ KHÔNG commit vào Git (xem .gitignore) - đây là lý do script này tồn tại: máy build CI clone
# repo sạch sẽ sẽ không có sẵn vendor/, phải tự tải lại mỗi lần chạy, giống hệt những gì người phát triển
# đã làm một lần bằng tay trên máy cục bộ.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$vendorScrcpy = Join-Path $root "vendor\scrcpy"
$vendorYtDlp = Join-Path $root "vendor\yt-dlp"
$vendorOpenVpn = Join-Path $root "vendor\openvpn"
$tmp = Join-Path $env:RUNNER_TEMP "vendor_fetch"
New-Item -ItemType Directory -Force -Path $vendorScrcpy, $vendorYtDlp, $vendorOpenVpn, $tmp | Out-Null

function Get-Sha256($path) {
    return (Get-FileHash -Path $path -Algorithm SHA256).Hash.ToLower()
}

function Assert-Sha256($path, $expectedHex) {
    $actual = Get-Sha256 $path
    if ($actual -ne $expectedHex.ToLower()) {
        throw "SHA-256 KHONG KHOP cho $path`n  mong doi: $expectedHex`n  thuc te:  $actual"
    }
    Write-Host "  OK sha256 $([System.IO.Path]::GetFileName($path)) = $actual"
}

# ---- 1) scrcpy + adb (Genymobile/scrcpy, Apache-2.0) - pin đúng bản đang dùng, xem THIRD_PARTY.md ----
$scrcpyVersion = "v5.0"
Write-Host "==> scrcpy $scrcpyVersion"
$scrcpyZip = Join-Path $tmp "scrcpy-win64-$scrcpyVersion.zip"
Invoke-WebRequest -Uri "https://github.com/Genymobile/scrcpy/releases/download/$scrcpyVersion/scrcpy-win64-$scrcpyVersion.zip" -OutFile $scrcpyZip
Invoke-WebRequest -Uri "https://github.com/Genymobile/scrcpy/releases/download/$scrcpyVersion/SHA256SUMS.txt" -OutFile (Join-Path $tmp "scrcpy-SHA256SUMS.txt")
$sums = Get-Content (Join-Path $tmp "scrcpy-SHA256SUMS.txt")
$expectedLine = $sums | Where-Object { $_ -match "scrcpy-win64-$scrcpyVersion\.zip$" }
if (-not $expectedLine) { throw "Khong tim thay dong checksum cho scrcpy-win64-$scrcpyVersion.zip trong SHA256SUMS.txt" }
$expectedHash = ($expectedLine -split '\s+')[0]
Assert-Sha256 $scrcpyZip $expectedHash

$scrcpyExtract = Join-Path $tmp "scrcpy-extract"
Expand-Archive -Path $scrcpyZip -DestinationPath $scrcpyExtract -Force
# Zip chính thức của scrcpy bọc toàn bộ nội dung trong MỘT thư mục con (vd "scrcpy-win64-v5.0/") - xác
# nhận THẬT bằng cách tự tải và liệt kê nội dung zip (không đoán) - tìm chính xác thư mục chứa adb.exe
# rồi copy từ đó, không giả định cấu trúc phẳng.
$scrcpyInner = Get-ChildItem -Path $scrcpyExtract -Filter "adb.exe" -Recurse | Select-Object -First 1
if (-not $scrcpyInner) { throw "Khong tim thay adb.exe trong ban scrcpy giai nen" }
Copy-Item -Path (Join-Path $scrcpyInner.DirectoryName "*") -Destination $vendorScrcpy -Recurse -Force
if (-not (Test-Path (Join-Path $vendorScrcpy "adb.exe"))) { throw "Thieu adb.exe sau khi giai nen scrcpy" }
Write-Host "  Da cai dat vendor/scrcpy/"

# ---- 2) yt-dlp.exe (yt-dlp, Unlicense/mã nguồn - bản .exe biên dịch sẵn là GPLv3+ kết hợp) - luôn lấy
# bản MỚI NHẤT có chủ đích (dự án cập nhật gần như hàng tuần, xem THIRD_PARTY.md) ----
Write-Host "==> yt-dlp (latest)"
$ytDlpExe = Join-Path $vendorYtDlp "yt-dlp.exe"
Invoke-WebRequest -Uri "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe" -OutFile $ytDlpExe
Invoke-WebRequest -Uri "https://github.com/yt-dlp/yt-dlp/releases/latest/download/SHA2-256SUMS" -OutFile (Join-Path $vendorYtDlp "SHA2-256SUMS")
$ytSums = Get-Content (Join-Path $vendorYtDlp "SHA2-256SUMS")
$ytExpectedLine = $ytSums | Where-Object { $_ -match "\byt-dlp\.exe$" }
if (-not $ytExpectedLine) { throw "Khong tim thay dong checksum cho yt-dlp.exe trong SHA2-256SUMS" }
$ytExpectedHash = ($ytExpectedLine -split '\s+')[0]
Assert-Sha256 $ytDlpExe $ytExpectedHash
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/yt-dlp/yt-dlp/master/LICENSE" -OutFile (Join-Path $vendorYtDlp "LICENSE.txt")
Write-Host "  Da cai dat vendor/yt-dlp/yt-dlp.exe"

# ---- 3) ffmpeg/ffprobe (BtbN/FFmpeg-Builds, bản tĩnh "gpl") - dùng tag "latest" nổi (URL ổn định luôn
# trỏ bản mới nhất). Bản phát hành "latest" CÓ đính kèm `checksums.sha256` (đã xác nhận qua GitHub API:
# dòng của ffmpeg-master-latest-win64-gpl.zip khớp đúng "digest" API trả về cho asset đó) - xác minh
# giống scrcpy/yt-dlp. Giới hạn nêu rõ: checksum tải từ CÙNG nguồn với tệp nên chỉ bắt được tệp hỏng/tải
# dở, không chống được việc chính kho nguồn bị thay nội dung. ----
Write-Host "==> ffmpeg (BtbN latest win64 gpl)"
$ffmpegZipName = "ffmpeg-master-latest-win64-gpl.zip"
$ffmpegZip = Join-Path $tmp $ffmpegZipName
Invoke-WebRequest -Uri "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/$ffmpegZipName" -OutFile $ffmpegZip
Invoke-WebRequest -Uri "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/checksums.sha256" -OutFile (Join-Path $tmp "ffmpeg-checksums.sha256")
$ffSums = Get-Content (Join-Path $tmp "ffmpeg-checksums.sha256")
$ffExpectedLine = $ffSums | Where-Object { $_ -match "\s\*?$([regex]::Escape($ffmpegZipName))$" }
if (-not $ffExpectedLine) { throw "Khong tim thay dong checksum cho $ffmpegZipName trong checksums.sha256" }
$ffExpectedHash = (($ffExpectedLine | Select-Object -First 1) -split '\s+')[0]
Assert-Sha256 $ffmpegZip $ffExpectedHash
$ffmpegExtract = Join-Path $tmp "ffmpeg-extract"
Expand-Archive -Path $ffmpegZip -DestinationPath $ffmpegExtract -Force
$ffmpegExe = Get-ChildItem -Path $ffmpegExtract -Filter "ffmpeg.exe" -Recurse | Select-Object -First 1
$ffprobeExe = Get-ChildItem -Path $ffmpegExtract -Filter "ffprobe.exe" -Recurse | Select-Object -First 1
$ffmpegLicense = Get-ChildItem -Path $ffmpegExtract -Filter "LICENSE*" -Recurse | Select-Object -First 1
if (-not $ffmpegExe -or -not $ffprobeExe) { throw "Khong tim thay ffmpeg.exe/ffprobe.exe trong ban BtbN giai nen" }
Copy-Item $ffmpegExe.FullName (Join-Path $vendorYtDlp "ffmpeg.exe") -Force
Copy-Item $ffprobeExe.FullName (Join-Path $vendorYtDlp "ffprobe.exe") -Force
if ($ffmpegLicense) {
    # Gộp vào LICENSE.txt đã có (của yt-dlp) thay vì ghi đè - cả hai đều cần được giữ lại.
    Add-Content -Path (Join-Path $vendorYtDlp "LICENSE.txt") -Value "`n`n---- FFmpeg (BtbN/FFmpeg-Builds) ----`n`n"
    Get-Content $ffmpegLicense.FullName | Add-Content -Path (Join-Path $vendorYtDlp "LICENSE.txt")
}
Write-Host "  Da cai dat vendor/yt-dlp/ffmpeg.exe + ffprobe.exe"

# ---- 4) OpenVPN community (openvpn.exe + wintun.dll, GPLv2) - dùng cho tab "Doi IP theo quoc gia" (VPN
# Gate qua OpenVPN, loi thoat khi L2TP native hong). Nguon CHINH THUC build.openvpn.net CHI ky GPG (.asc),
# KHONG co SHA256SUMS - nen xac minh bang CHU KY GPG cua OpenVPN (van dung voi THIRD_PARTY.md: xac minh
# tinh xac thuc truoc khi dung). Khoa ky cua OpenVPN (primary fingerprint, xem THIRD_PARTY.md):
#   F554 A368 7412 CFFE BDEF  E0A3 12F5 F7B4 2F2B 01E7
# Lay tu keys.openpgp.org. .msi duoc ky bang SUBKEY nam duoi primary nay. ----
Write-Host "==> OpenVPN community (latest stable, GPG-verified)"
$ovpnPrimaryFpr = "F554A3687412CFFEBDEFE0A312F5F7B42F2B01E7"
$ovpnMsi = Join-Path $tmp "openvpn-latest-stable-amd64.msi"
$ovpnAsc = Join-Path $tmp "openvpn-latest-stable-amd64.msi.asc"
Invoke-WebRequest -Uri "https://build.openvpn.net/downloads/releases/latest/openvpn-latest-stable-amd64.msi" -OutFile $ovpnMsi
Invoke-WebRequest -Uri "https://build.openvpn.net/downloads/releases/latest/openvpn-latest-stable-amd64.msi.asc" -OutFile $ovpnAsc

# Xac minh chu ky GPG: lay khoa cong khai OpenVPN theo fingerprint tu keys.openpgp.org QUA HTTPS (import tu
# tep - tranh giao thuc keyserver/dirmngr hay hong tren CI) roi gpg --verify. Ket qua phai "Good signature"
# VA phai co primary fingerprint o tren (chu ky qua subkey van quy ve primary nay).
$env:GNUPGHOME = Join-Path $tmp "gnupg"
New-Item -ItemType Directory -Force -Path $env:GNUPGHOME | Out-Null
$ovpnKey = Join-Path $tmp "openvpn-security.asc"
Invoke-WebRequest -Uri "https://keys.openpgp.org/vks/v1/by-fingerprint/$ovpnPrimaryFpr" -OutFile $ovpnKey
& gpg --batch --import $ovpnKey
if ($LASTEXITCODE -ne 0) { throw "Khong import duoc khoa GPG OpenVPN ($ovpnPrimaryFpr) tu keys.openpgp.org" }
$gpgOut = & gpg --batch --status-fd 1 --verify $ovpnAsc $ovpnMsi 2>&1 | Out-String
Write-Host $gpgOut
# status-fd in dong "VALIDSIG <subkey> ... <primaryfpr>" khi chu ky hop le - bat buoc primary fingerprint khop.
if ($gpgOut -notmatch [regex]::Escape($ovpnPrimaryFpr)) {
    throw "Chu ky GPG cua openvpn .msi KHONG quy ve primary fingerprint $ovpnPrimaryFpr - tu choi dung."
}
if ($gpgOut -notmatch "GOODSIG|VALIDSIG") { throw "gpg --verify khong xac nhan chu ky hop le cho openvpn .msi" }
Write-Host "  OK chu ky GPG OpenVPN hop le (primary $ovpnPrimaryFpr)"
Write-Host "  sha256 (tham khao) = $(Get-Sha256 $ovpnMsi)"

# Admin install (/a) chi GIAI NEN tep tu .msi vao thu muc dich - KHONG cai driver/dich vu, khong can quyen
# admin. openvpn.exe can them cac DLL ben canh (OpenSSL + pkcs11-helper + vcruntime) moi chay duoc -> copy
# ca bo. LUU Y: ban .msi 2.6 KHONG bundle wintun.dll duoi dang tep (driver nam trong driver store), nen
# wintun.dll phai lay rieng tu nguon chinh thuc wintun.net o buoc duoi.
$ovpnExtract = Join-Path $tmp "openvpn-extract"
New-Item -ItemType Directory -Force -Path $ovpnExtract | Out-Null
$p = Start-Process msiexec.exe -ArgumentList @("/a", "`"$ovpnMsi`"", "/qn", "TARGETDIR=`"$ovpnExtract`"") -Wait -PassThru -NoNewWindow
if ($p.ExitCode -ne 0) { throw "msiexec /a (admin install extract) that bai, ExitCode=$($p.ExitCode)" }
$ovpnExe = Get-ChildItem -Path $ovpnExtract -Filter "openvpn.exe" -Recurse | Select-Object -First 1
if (-not $ovpnExe) { throw "Khong tim thay openvpn.exe trong ban OpenVPN giai nen" }
# Xac minh them chu ky Authenticode cua openvpn.exe (GPG da xac minh .msi; day la kiem tra chong lot nhi).
$ovpnSig = Get-AuthenticodeSignature $ovpnExe.FullName
if ($ovpnSig.Status -ne "Valid" -or $ovpnSig.SignerCertificate.Subject -notmatch "OpenVPN") {
    throw "openvpn.exe chu ky Authenticode khong hop le / khong phai OpenVPN Inc. (Status=$($ovpnSig.Status))"
}
$ovpnBin = $ovpnExe.DirectoryName
# openvpn.exe phu thuc (objdump -p): libssl-3-x64 + libcrypto-3-x64 + libpkcs11-helper-1 + vcruntime140.
$ovpnDlls = @("libssl-3-x64.dll", "libcrypto-3-x64.dll", "libpkcs11-helper-1.dll", "vcruntime140.dll")
Copy-Item $ovpnExe.FullName (Join-Path $vendorOpenVpn "openvpn.exe") -Force
foreach ($d in $ovpnDlls) {
    $src = Join-Path $ovpnBin $d
    if (-not (Test-Path $src)) { throw "Thieu DLL bat buoc cua openvpn.exe: $d" }
    Copy-Item $src (Join-Path $vendorOpenVpn $d) -Force
}
$ovpnLicense = Get-ChildItem -Path $ovpnExtract -Filter "license.txt" -Recurse | Select-Object -First 1
if ($ovpnLicense) { Copy-Item $ovpnLicense.FullName (Join-Path $vendorOpenVpn "LICENSE-OpenVPN.txt") -Force }
Write-Host "  Da cai dat vendor/openvpn/openvpn.exe + OpenSSL DLLs"

# ---- 4b) wintun.dll (WireGuard LLC, GPLv2/BSD-2 - driver adapter ao cho OpenVPN). Nguon CHINH THUC
# wintun.net. .zip khong co SHA256SUMS rieng, nhung wintun.dll duoc ky Authenticode boi WireGuard LLC ->
# xac minh bang chu ky Authenticode (du voi THIRD_PARTY.md: xac minh tinh xac thuc truoc khi dung). ----
Write-Host "==> wintun 0.14.1 (WireGuard LLC, Authenticode-verified)"
$wintunVersion = "0.14.1"
$wintunZip = Join-Path $tmp "wintun-$wintunVersion.zip"
Invoke-WebRequest -Uri "https://www.wintun.net/builds/wintun-$wintunVersion.zip" -OutFile $wintunZip
Write-Host "  sha256 zip (tham khao) = $(Get-Sha256 $wintunZip)"
$wintunExtract = Join-Path $tmp "wintun-extract"
Expand-Archive -Path $wintunZip -DestinationPath $wintunExtract -Force
$wintunDll = Get-ChildItem -Path (Join-Path $wintunExtract "wintun\bin\amd64") -Filter "wintun.dll" | Select-Object -First 1
if (-not $wintunDll) { throw "Khong tim thay wintun/bin/amd64/wintun.dll trong ban wintun giai nen" }
$wintunSig = Get-AuthenticodeSignature $wintunDll.FullName
if ($wintunSig.Status -ne "Valid" -or $wintunSig.SignerCertificate.Subject -notmatch "WireGuard") {
    throw "wintun.dll chu ky Authenticode khong hop le / khong phai WireGuard LLC (Status=$($wintunSig.Status))"
}
Copy-Item $wintunDll.FullName (Join-Path $vendorOpenVpn "wintun.dll") -Force
Write-Host "  Da cai dat vendor/openvpn/wintun.dll"

Write-Host "==> Xong - vendor/ da san sang."
