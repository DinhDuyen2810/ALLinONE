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
$tmp = Join-Path $env:RUNNER_TEMP "vendor_fetch"
New-Item -ItemType Directory -Force -Path $vendorScrcpy, $vendorYtDlp, $tmp | Out-Null

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
# trỏ bản mới nhất) - KHÔNG có checksum riêng công khai cho tag này nên không xác minh SHA-256 được (bản
# thân đây đã là thực tế đã chấp nhận từ trước khi vendor hóa thủ công, xem THIRD_PARTY.md) ----
Write-Host "==> ffmpeg (BtbN latest win64 gpl)"
$ffmpegZip = Join-Path $tmp "ffmpeg-win64-gpl.zip"
Invoke-WebRequest -Uri "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip" -OutFile $ffmpegZip
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

Write-Host "==> Xong - vendor/ da san sang."
