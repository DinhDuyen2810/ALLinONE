#pragma once

#include <QString>

/// Lưu tài khoản VPN (tên + mật khẩu) cho kết nối trực tiếp để lần sau chỉ cần bấm "Kết nối" - theo yêu cầu người
/// dùng. Lưu trong **Windows Credential Manager** (thông tin xác thực kiểu Generic, mục "OneForAll/VPN/<máy chủ>"),
/// nơi Windows mã hóa bằng DPAPI theo TÀI KHOẢN Windows của người dùng - cùng cách FortiClient/trình duyệt lưu mật
/// khẩu; ứng dụng KHÔNG tự ghi mật khẩu ra tệp nào. Người dùng xem/xóa được trong Control Panel > Credential Manager.
namespace CredentialStore
{
bool save(const QString& server, const QString& username, const QString& password, QString* error = nullptr);
/// true nếu có tài khoản đã lưu cho máy chủ này.
bool load(const QString& server, QString* username, QString* password);
bool has(const QString& server);
void remove(const QString& server);

/// CHỈ test: đổi tiền tố tên mục để test không đụng mục thật của người dùng (mặc định "OneForAll/VPN/").
void setTargetPrefixForTest(const QString& prefix);
QString targetName(const QString& server);
} // namespace CredentialStore
