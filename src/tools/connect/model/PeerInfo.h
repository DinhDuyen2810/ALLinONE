#pragma once

#include <QDateTime>
#include <QHostAddress>
#include <QString>

#include "ScreenSide.h"

/// Trạng thái ghép đôi với một máy khác.
enum class PairState
{
    NotPaired,
    AwaitingCode,   // đã gửi yêu cầu ghép đôi, chờ máy kia xác nhận mã
    Paired,
    Rejected
};

/// Một máy khác tìm thấy trên mạng LAN (qua PeerDiscovery) hoặc đã ghép đôi (lưu lâu dài).
struct PeerInfo
{
    QString id;             // định danh ổn định của máy đó (sinh ngẫu nhiên, lưu lại giữa các lần chạy)
    QString machineName;    // tên hiển thị (mặc định = tên máy Windows)
    QHostAddress address;
    quint16 port{0};

    PairState pairState{PairState::NotPaired};
    ScreenSide side{ScreenSide::None}; // vị trí máy này trong lưới, so với máy hiện tại
    bool online{false};                // có thấy broadcast gần đây không
    QDateTime lastSeen;

    bool isValid() const { return !id.isEmpty(); }
};
