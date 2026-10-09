#pragma once

// Dùng chung cho các bộ stress test có dựng widget (core_stress_tests, qrwifi_stress_tests): chọn nền tảng
// Qt "offscreen" để KHÔNG BAO GIỜ vẽ cửa sổ thật lên màn hình của máy đang chạy test.
//
// Lỗi thật đã gặp khi chạy tích hợp: thư mục build\ đã qua windeployqt chỉ có build\platforms\qwindows.dll
// (ứng dụng thật không cần plugin offscreen). Ép QT_QPA_PLATFORM=offscreen ở đó khiến Qt bật hộp thoại "no
// Qt platform plugin could be initialized" lên màn hình người dùng và TREO chờ bấm OK - bộ test đứng hơn
// 10 phút rồi thoát mã 127 mà không in gì. Vì vậy phải tự tìm nơi có qoffscreen.dll trước khi ép; không
// tìm thấy thì báo rõ ra stderr và thoát ngay bằng mã lỗi, không để Qt hiện hộp thoại.

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace TestPlatform
{
/// Gọi TRƯỚC khi tạo QApplication. Trả về false (đã in lý do) nếu không thể chạy không màn hình.
/// Người chạy tự đặt QT_QPA_PLATFORM thì giữ nguyên lựa chọn đó.
inline bool forceOffscreen(const char* argv0)
{
    if (!qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        return true;

    const QString pluginFile = QStringLiteral("qoffscreen.dll");
    const QString exeDir = QFileInfo(QString::fromLocal8Bit(argv0)).absolutePath();

    QStringList candidates;
    const QString overridePath = qEnvironmentVariable("QT_QPA_PLATFORM_PLUGIN_PATH");
    if (!overridePath.isEmpty())
        candidates << overridePath;
    candidates << exeDir + "/platforms";
    // Thư mục bin của Qt nằm trong PATH lúc chạy test (xem CLAUDE.md) - plugin nằm ở ../plugins/platforms.
    const QStringList pathDirs = qEnvironmentVariable("PATH").split(';', Qt::SkipEmptyParts);
    for (const QString& dir : pathDirs)
        candidates << QDir::cleanPath(dir + "/../plugins/platforms");

    for (const QString& dir : std::as_const(candidates))
    {
        if (QFileInfo::exists(dir + "/" + pluginFile))
        {
            qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QDir::toNativeSeparators(dir).toLocal8Bit());
            qputenv("QT_QPA_PLATFORM", "offscreen");
            return true;
        }
    }

    std::fprintf(stderr,
                 "KHONG CHAY: khong tim thay plugin Qt 'offscreen' (qoffscreen.dll). Them thu muc bin cua Qt vao PATH "
                 "(vd D:\\Qt\\6.11.1\\mingw_64\\bin) hoac dat QT_QPA_PLATFORM_PLUGIN_PATH, roi chay lai.\n");
    return false;
}
} // namespace TestPlatform
