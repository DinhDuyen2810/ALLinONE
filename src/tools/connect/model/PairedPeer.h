#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include "ScreenSide.h"

/// Một máy đã ghép đôi thành công, lưu lâu dài để tự kết nối lại mà không cần nhập lại mã.
struct PairedPeer
{
    QString id;             // định danh ổn định của máy đó
    QString machineName;
    QString lastAddress;    // IP gần nhất thấy được - chỉ là gợi ý, PeerDiscovery sẽ cập nhật lại
    quint16 lastPort{0};
    QByteArray longTermKey; // 256-bit, sinh ngẫu nhiên lúc ghép đôi (không phải mã ghép đôi gốc)
    ScreenSide side{ScreenSide::None};
    bool autoConnect{true};
    QDateTime pairedAt;

    bool isValid() const { return !id.isEmpty() && longTermKey.size() == 32; }
};
