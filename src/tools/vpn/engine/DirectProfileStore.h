#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "../model/DirectVpnProfile.h"
#include "../model/ThirdPartyVpnProfile.h"

/// Kho các hồ sơ VPN kết nối trực tiếp (GlobalProtect / FortiClient) do người dùng tự thêm - lưu thành JSON trong thư mục
/// dữ liệu của ứng dụng (`vpn_direct_profiles.json`, qua AppPaths). KHÔNG chứa bí mật nào (chỉ tên + máy chủ + giao thức).
namespace DirectProfileStore
{
/// Tên hiển thị của "phần mềm" tương ứng giao thức - khớp ThirdPartyVpnScanner để mọi chỗ dùng chung một cách gọi
/// ("GlobalProtect", "FortiClient" cho SSL-VPN, "FortiClient (IPsec)").
QString clientName(DirectProtocol p);

/// Kiểm tra một hồ sơ trước khi lưu: tên không rỗng/không quá dài, giao thức thuộc 3 loại hỗ trợ, địa chỉ máy chủ hợp lệ
/// (host[:cổng]; FortiClient IPsec chỉ nhận host - cổng cố định 500/4500). Trả false + *error (tiếng Việt) nếu sai.
bool validate(const DirectVpnProfile& p, QString* error = nullptr);

/// Chuẩn hóa địa chỉ nhập tay: bỏ khoảng trắng, tiền tố https:// và đường dẫn phía sau (vd "https://vpn.cty.vn/portal" ->
/// "vpn.cty.vn"). Giữ lại ":cổng" nếu có.
QString normalizeServer(const QString& raw);

/// Đọc toàn bộ hồ sơ (tệp thiếu/hỏng -> danh sách rỗng; mục sai bị bỏ qua).
QList<DirectVpnProfile> load();
/// Thêm một hồ sơ: validate + chặn trùng (cùng giao thức và tên) rồi ghi tệp. false + *error nếu không thêm được.
bool add(const DirectVpnProfile& p, QString* error = nullptr);
/// Xóa hồ sơ theo giao thức + tên. true nếu có xóa.
bool remove(DirectProtocol protocol, const QString& name);

/// Chuyển hồ sơ thành dạng dùng chung với danh sách hồ sơ bên thứ ba (clientName/name/server/direct, custom=true).
ThirdPartyVpnProfile toThirdParty(const DirectVpnProfile& p);

// ---- Lõi THUẦN (không đụng tệp) - tách để kiểm thử bằng dữ liệu mẫu ----
namespace internal
{
QByteArray serialize(const QList<DirectVpnProfile>& profiles);
QList<DirectVpnProfile> parse(const QByteArray& json);
} // namespace internal
} // namespace DirectProfileStore
