#include "VpnConnector.h"

#include <QElapsedTimer>

#include <windows.h>
#include <ras.h>
#include <raserror.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace
{
constexpr int kDialTimeoutMs = 45000; // như giới hạn cũ của rasdial.exe: máy chủ chậm/xa có thể bắt tay rất lâu
constexpr int kPollMs = 200;

/// Xóa trắng nội dung một QString chứa bí mật rồi làm rỗng. fill() ghi đè đúng vùng nhớ đang giữ khi
/// chuỗi không còn bản dùng chung nào khác (đúng với các chỗ gọi ở đây: bản của nơi gọi đã bị hủy).
void wipe(QString& secret)
{
    secret.fill(QChar(0));
    secret.clear();
}

// Trạng thái do hàm gọi lại của RasDial báo về (chạy trên luồng của RAS). Hàm gọi lại kiểu 1 không có
// tham số ngữ cảnh nên phải dùng biến toàn cục; mỗi giá trị được gắn với HRASCONN của nó và CHỈ được
// dùng khi trùng với phiên đang chờ - một lần gọi lại muộn của phiên trước không làm hỏng phiên sau.
std::atomic<quintptr> g_callbackConn{0};
std::atomic<quint32> g_callbackError{0};
std::atomic<bool> g_callbackConnected{false};

VOID WINAPI dialCallback(HRASCONN conn, UINT, RASCONNSTATE state, DWORD error, DWORD)
{
    g_callbackConn = reinterpret_cast<quintptr>(conn);
    if (error != 0)
        g_callbackError = error;
    if (state == RASCS_Connected)
        g_callbackConnected = true;
}

/// Gác máy rồi CHỜ tới khi Windows thật sự giải phóng phiên (RasGetConnectStatus trả ERROR_INVALID_HANDLE)
/// - tài liệu RasHangUp yêu cầu điều này trước khi tiến trình đi tiếp/thoát, nếu không trạng thái máy
/// của RAS có thể bị bỏ dở. Giới hạn 5 giây để không bao giờ treo luồng.
void hangUpAndWait(HRASCONN conn)
{
    if (!conn)
        return;
    RasHangUpW(conn);
    for (int waited = 0; waited < 5000; waited += 50)
    {
        RASCONNSTATUSW status;
        std::memset(&status, 0, sizeof(status));
        status.dwSize = sizeof(status);
        if (RasGetConnectStatusW(conn, &status) == ERROR_INVALID_HANDLE)
            break;
        Sleep(50);
    }
}

/// Chép chuỗi vào mảng wchar_t cố định của RASDIALPARAMS; false nếu KHÔNG VỪA (không cắt ngắn âm thầm -
/// một tên/mật khẩu bị cắt sẽ thành tên/mật khẩu KHÁC).
template <size_t N>
bool copyTo(wchar_t (&dest)[N], const QString& value)
{
    if (static_cast<size_t>(value.size()) >= N)
        return false;
    const int written = value.toWCharArray(dest);
    dest[written] = L'\0';
    return true;
}
} // namespace

VpnConnector::VpnConnector(QObject* parent)
    : QThread(parent)
{
}

VpnConnector::~VpnConnector()
{
    wipe(m_password);
}

void VpnConnector::setConnectTarget(const QString& connectionName, const QString& username, const QString& password)
{
    // Đặt lại cờ hủy ở ĐÂY (trước start()), không phải trong run(): cờ từng không bao giờ được đặt lại,
    // nên sau MỘT lần đóng cửa sổ giữa lúc đang kết nối (cửa sổ chỉ ẩn rồi được dùng lại), mọi lần kết
    // nối/ngắt kết nối sau đó đều tự "Đã hủy." trong vòng 200ms.
    m_cancelRequested = false;
    m_mode = Mode::Connect;
    m_connectionName = connectionName;
    m_username = username;
    wipe(m_password);
    m_password = password;
}

void VpnConnector::setDisconnectTarget(const QString& connectionName)
{
    m_cancelRequested = false;
    m_mode = Mode::Disconnect;
    m_connectionName = connectionName;
    m_username.clear();
    wipe(m_password);
}

QString VpnConnector::describeRasError(quint32 code)
{
    QString meaning;
    switch (code)
    {
        case 623: meaning = "Không tìm thấy hồ sơ VPN này trong Windows (có thể đã bị xóa hoặc đổi tên)."; break;
        case 691: meaning = "Máy chủ từ chối đăng nhập: sai tên đăng nhập hoặc mật khẩu."; break;
        case 756: meaning = "Hồ sơ VPN này đang được kết nối bởi một thao tác khác."; break;
        case 800:
        case 807:
        case 809:
        case 868:
            meaning = "Không liên lạc được với máy chủ VPN (sai địa chỉ, máy chủ tắt hoặc tường lửa chặn).";
            break;
        case 787:
        case 788:
        case 789:
            meaning = "Bắt tay bảo mật L2TP/IPsec thất bại (thiếu hoặc sai khóa chia sẻ trước/chứng chỉ).";
            break;
        case 734:
        case 812:
            meaning = "Máy chủ không chấp nhận phương thức xác thực/giao thức của hồ sơ này.";
            break;
        case 13801:
        case 13806:
        case 13868:
            meaning = "Xác thực IKEv2 thất bại (thông tin đăng nhập bị từ chối hoặc chứng chỉ máy chủ không được tin cậy).";
            break;
        default: meaning = "Thao tác VPN thất bại."; break;
    }

    // Mô tả gốc của Windows (theo ngôn ngữ hệ điều hành) - giữ lại để người dùng tra cứu được.
    wchar_t buffer[512] = {};
    QString systemText;
    if (RasGetErrorStringW(code, buffer, 512) == ERROR_SUCCESS)
        systemText = QString::fromWCharArray(buffer).trimmed();
    else if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, buffer, 512, nullptr) > 0)
        systemText = QString::fromWCharArray(buffer).trimmed();

    return systemText.isEmpty() ? QString("%1 (lỗi %2)").arg(meaning).arg(code)
                                : QString("%1 (lỗi %2: %3)").arg(meaning).arg(code).arg(systemText);
}

void VpnConnector::run()
{
    // Lấy mật khẩu ra khỏi đối tượng ngay: từ đây m_password rỗng, bản duy nhất nằm trong biến cục bộ và
    // bị xóa trắng trước khi run() kết thúc dù thành công hay thất bại.
    QString password;
    password.swap(m_password);

    QString message;
    const bool ok = (m_mode == Mode::Disconnect) ? doDisconnect(&message) : doConnect(password, &message);
    wipe(password);
    emit operationFinished(ok, message);
}

bool VpnConnector::doConnect(QString& password, QString* message)
{
    RASDIALPARAMSW params;
    SecureZeroMemory(&params, sizeof(params));
    params.dwSize = sizeof(params);

    if (m_connectionName.isEmpty() || !copyTo(params.szEntryName, m_connectionName))
    {
        *message = "Tên hồ sơ VPN rỗng hoặc quá dài.";
        return false;
    }
    if (m_username.isEmpty())
    {
        // Không nhập tên đăng nhập: nạp thông tin Windows đã nhớ cho kết nối này (như `rasdial <tên>`).
        // Lỗi ở đây (vd hồ sơ không tồn tại) bỏ qua - RasDial bên dưới sẽ báo mã lỗi chính xác.
        BOOL hasSavedPassword = FALSE;
        RasGetEntryDialParamsW(nullptr, &params, &hasSavedPassword);
    }
    else if (!copyTo(params.szUserName, m_username) || !copyTo(params.szPassword, password))
    {
        SecureZeroMemory(&params, sizeof(params));
        *message = "Tên đăng nhập hoặc mật khẩu quá dài (tối đa 256 ký tự).";
        return false;
    }
    wipe(password); // đã chép sang params - bản QString không còn cần nữa

    g_callbackConn = 0;
    g_callbackError = 0;
    g_callbackConnected = false;

    // Kiểu thông báo 1 (RasDialFunc1) = KHÔNG ĐỒNG BỘ: RasDial trả về ngay, tiến độ/lỗi báo qua hàm gọi
    // lại. Sổ danh bạ nullptr = sổ mặc định của người dùng (nơi Add-VpnConnection tạo kết nối per-user).
    HRASCONN conn = nullptr;
    const DWORD dialResult = RasDialW(nullptr, nullptr, &params, 1, reinterpret_cast<LPVOID>(dialCallback), &conn);

    bool success = false;
    if (dialResult != ERROR_SUCCESS)
    {
        *message = describeRasError(dialResult);
    }
    else
    {
        QElapsedTimer timer;
        timer.start();
        while (true)
        {
            const bool callbackIsOurs = g_callbackConn.load() == reinterpret_cast<quintptr>(conn);
            quint32 error = callbackIsOurs ? g_callbackError.load() : 0;
            bool connected = callbackIsOurs && g_callbackConnected.load();

            RASCONNSTATUSW status;
            std::memset(&status, 0, sizeof(status));
            status.dwSize = sizeof(status);
            const DWORD statusResult = RasGetConnectStatusW(conn, &status);
            if (statusResult == ERROR_SUCCESS)
            {
                connected = connected || status.rasconnstate == RASCS_Connected;
                if (error == 0)
                    error = status.dwError;
            }
            else if (statusResult == ERROR_INVALID_HANDLE && error == 0 && !connected)
            {
                error = statusResult; // phiên đã bị đóng từ phía Windows
            }
            // Mọi mã khác (vd không đọc được trạng thái lúc này): không kết luận gì - tiếp tục dựa vào hàm
            // gọi lại, và giới hạn 45 giây bên dưới bảo đảm vòng lặp luôn kết thúc.

            if (connected)
            {
                success = true;
                *message = QString("Đã kết nối \"%1\".").arg(m_connectionName);
                break;
            }
            if (error != 0)
            {
                *message = describeRasError(error);
                break;
            }
            if (m_cancelRequested)
            {
                *message = "Đã hủy.";
                break;
            }
            if (timer.elapsed() >= kDialTimeoutMs)
            {
                *message = "Hết thời gian chờ kết nối VPN.";
                break;
            }
            msleep(kPollMs);
        }
    }

    // Mọi đường KHÔNG thành công đều phải gác máy: ở chế độ không đồng bộ, một phiên lỗi/dở vẫn giữ
    // HRASCONN (và có thể vẫn đang quay số) cho tới khi RasHangUp được gọi.
    if (!success)
        hangUpAndWait(conn);

    SecureZeroMemory(&params, sizeof(params)); // gồm cả szPassword
    return success;
}

bool VpnConnector::doDisconnect(QString* message)
{
    std::vector<RASCONNW> connections(8);
    DWORD bytes = static_cast<DWORD>(connections.size() * sizeof(RASCONNW));
    DWORD count = 0;
    std::memset(connections.data(), 0, bytes);
    connections[0].dwSize = sizeof(RASCONNW);
    DWORD result = RasEnumConnectionsW(connections.data(), &bytes, &count);
    if (result == ERROR_BUFFER_TOO_SMALL)
    {
        connections.resize(bytes / sizeof(RASCONNW) + 1);
        bytes = static_cast<DWORD>(connections.size() * sizeof(RASCONNW));
        std::memset(connections.data(), 0, bytes);
        connections[0].dwSize = sizeof(RASCONNW);
        result = RasEnumConnectionsW(connections.data(), &bytes, &count);
    }
    if (result != ERROR_SUCCESS)
    {
        *message = describeRasError(result);
        return false;
    }

    for (DWORD i = 0; i < count && i < connections.size(); ++i)
    {
        if (QString::fromWCharArray(connections[i].szEntryName).compare(m_connectionName, Qt::CaseInsensitive) != 0)
            continue;
        hangUpAndWait(connections[i].hrasconn);
        *message = QString("Đã ngắt kết nối \"%1\".").arg(m_connectionName);
        return true;
    }

    *message = QString("\"%1\" hiện không kết nối.").arg(m_connectionName);
    return true;
}
