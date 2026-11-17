# Dong goi OneForAll_Release\ thanh goi MSIX de nop Microsoft Store: dist\OneForAll_Store.msix
# Goi qua build_msix.bat (bat do dung lai OneForAll_Release\ truoc). Ly do va gioi han: PROJECT_OVERVIEW.md muc 4zzi.
#
# Goi KHONG KY (dung quy tac 3 CLAUDE.md): Microsoft Store tu ky lai goi khi phat hanh. Goi khong ky khong cai
# truc tiep duoc bang double-click - chi de tai len Partner Center (hoac cai thu, xem muc 4zzi).
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$OutFile = ''
)
$ErrorActionPreference = 'Stop'

function Fail([string]$msg) { Write-Host "[LOI] $msg"; exit 1 }

# ---- 1. So phien ban: VERSION (nguon DUY NHAT) "X.Y.Z" -> "X.Y.Z.0". Store BAT BUOC phan thu tu = 0 (danh rieng cho Store).
$verRaw = ([IO.File]::ReadAllText((Join-Path $Root 'VERSION'))).Trim()
$verCore = ($verRaw -split '[-+]')[0]
if ($verCore -notmatch '^\d+\.\d+\.\d+$') { Fail "VERSION khong dung dang X.Y.Z: '$verRaw'" }
foreach ($p in $verCore.Split('.')) { if ([int]$p -gt 65535) { Fail "Moi phan cua VERSION phai <= 65535: '$verRaw'" } }
$version4 = "$verCore.0"

# ---- 2. Danh tinh goi tu installer\msix\store_identity.txt (de trong = danh tinh thu nghiem cuc bo).
$identity = @{ IdentityName = ''; Publisher = ''; PublisherDisplayName = '' }
$idFile = Join-Path $PSScriptRoot 'store_identity.txt'
foreach ($line in [IO.File]::ReadAllLines($idFile, [Text.Encoding]::UTF8)) {
    $t = $line.Trim()
    if ($t -eq '' -or $t.StartsWith('#')) { continue }
    $eq = $t.IndexOf('=')
    if ($eq -lt 1) { continue }
    $k = $t.Substring(0, $eq).Trim(); $v = $t.Substring($eq + 1).Trim()
    if ($identity.ContainsKey($k)) { $identity[$k] = $v }
}
$filled = @($identity.Values | Where-Object { $_ -ne '' }).Count
if ($filled -ne 0 -and $filled -ne 3) { Fail "store_identity.txt phai dien DU ca 3 gia tri (hoac de trong ca 3)." }
$localTest = ($filled -eq 0)
if ($localTest) {
    $identity.IdentityName = 'OneForAll.LocalTest'
    $identity.Publisher = 'CN=OneForAll Local Test'
    $identity.PublisherDisplayName = 'One for ALL (local test)'
}
# Quy tac cua Identity Name trong schema AppxManifest: 3-50 ky tu, chu/so/dau cham/gach ngang.
if ($identity.IdentityName -notmatch '^[A-Za-z0-9.\-]{3,50}$') { Fail "IdentityName khong hop le: '$($identity.IdentityName)'" }
if ($identity.Publisher -notmatch '^CN=') { Fail "Publisher phai bat dau bang 'CN=': '$($identity.Publisher)'" }

# ---- 3. Thu muc tam: ban sao OneForAll_Release\ (bo logs\ va profiles\ ca nhan, chi giu profiles\default.json -
# cung quy tac voi bo cai .msi trong build_installer.bat) + logo + manifest.
$release = Join-Path $Root 'OneForAll_Release'
if (-not (Test-Path (Join-Path $release 'OneForAll.exe'))) { Fail 'Chua co OneForAll_Release\OneForAll.exe - chay build_app.bat truoc.' }
$stage = Join-Path $Root 'obj\msix_stage'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
# Bo 2 script tien ich di kem ban phat hanh scrcpy (ung dung khong goi toi, chi de nguoi dung tu mo terminal) -
# goi Store khong mang theo .bat/.vbs khong dung den.
& robocopy $release $stage /E /XD logs profiles /XF open_a_terminal_here.bat scrcpy-noconsole.vbs /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { Fail "robocopy loi (ma $LASTEXITCODE)" }
New-Item -ItemType Directory -Force (Join-Path $stage 'profiles') | Out-Null
Copy-Item (Join-Path $Root 'profiles\default.json') (Join-Path $stage 'profiles\default.json')
Copy-Item -Recurse (Join-Path $PSScriptRoot 'Assets') (Join-Path $stage 'Assets')

function XmlEscape([string]$s) { return [Security.SecurityElement]::Escape($s) }
$manifest = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'AppxManifest.xml.in'), [Text.Encoding]::UTF8)
$manifest = $manifest.Replace('@IDENTITY_NAME@', (XmlEscape $identity.IdentityName))
$manifest = $manifest.Replace('@PUBLISHER@', (XmlEscape $identity.Publisher))
$manifest = $manifest.Replace('@PUBLISHER_DISPLAY_NAME@', (XmlEscape $identity.PublisherDisplayName))
$manifest = $manifest.Replace('@VERSION4@', $version4)
if ($manifest -match '@[A-Z0-9_]+@') { Fail "Manifest con chuoi chua thay: $($Matches[0])" }
[IO.File]::WriteAllText((Join-Path $stage 'AppxManifest.xml'), $manifest, (New-Object Text.UTF8Encoding($false)))

# ---- 4. Tim makeappx.exe: MAKEAPPX_EXE -> vendor\sdk-buildtools (goi NuGet Microsoft.Windows.SDK.BuildTools,
# xem THIRD_PARTY.md) -> Windows SDK da cai (may CI GitHub co san).
$makeappx = $null
if ($env:MAKEAPPX_EXE -and (Test-Path $env:MAKEAPPX_EXE)) { $makeappx = $env:MAKEAPPX_EXE }
if (-not $makeappx) {
    $v = Join-Path $Root 'vendor\sdk-buildtools\x64\makeappx.exe'
    if (Test-Path $v) { $makeappx = $v }
}
if (-not $makeappx) {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path $kits) {
        $makeappx = Get-ChildItem $kits -Directory | Where-Object { $_.Name -match '^10\.' } |
            Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName 'x64\makeappx.exe' } |
            Where-Object { Test-Path $_ } | Select-Object -First 1
    }
}
if (-not $makeappx) {
    Fail ('Khong tim thay makeappx.exe. Tai goi NuGet Microsoft.Windows.SDK.BuildTools, chep thu muc bin\<ban>\x64 vao ' +
          'vendor\sdk-buildtools\x64 (hoac dat MAKEAPPX_EXE) - xem THIRD_PARTY.md.')
}

# ---- 5. Dong goi. makeappx kiem manifest theo schema that (sai khai bao/thieu logo/sai phien ban -> loi ngay).
if (-not $OutFile) { $OutFile = Join-Path $Root 'dist\OneForAll_Store.msix' }
New-Item -ItemType Directory -Force (Split-Path $OutFile) | Out-Null
Write-Host "  makeappx: $makeappx"
Write-Host "  Phien ban goi: $version4 | Identity: $($identity.IdentityName) | $($identity.Publisher)"
# Bo dong "Processing ... as a payload file" (moi tep mot dong); giu nguyen moi dong loi/canh bao.
# (Khong dung 2>&1: PowerShell 5.1 + ErrorActionPreference=Stop bien dong stderr dau tien thanh loi dung script
# va nuot mat thong bao that cua makeappx - stderr cu de hien thang ra man hinh.)
& $makeappx pack /o /h SHA256 /d $stage /p $OutFile | Where-Object { $_ -notmatch '^Processing ' }
if ($LASTEXITCODE -ne 0) { Fail "makeappx pack loi (ma $LASTEXITCODE)" }
Remove-Item -Recurse -Force $stage

$size = [math]::Round((Get-Item $OutFile).Length / 1MB, 1)
Write-Host "  Da tao: $OutFile ($size MB)"
if ($localTest) {
    Write-Host '  CANH BAO: dang dung danh tinh THU NGHIEM (store_identity.txt de trong) - Partner Center se tu choi goi nay.'
    Write-Host '  Dien 3 gia tri tu Partner Center > Product identity vao installer\msix\store_identity.txt roi dung lai.'
}
exit 0
