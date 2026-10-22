#include "OpenVpnRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <memory>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace
{
QString bundleDir()
{
    return QCoreApplication::applicationDirPath() + "/openvpn";
}

quint16 freeLocalPort()
{
    QTcpServer s;
    return s.listen(QHostAddress::LocalHost, 0) ? s.serverPort() : 0;
}
} // namespace

QString OpenVpnRunner::exePath()
{
    const QString p = bundleDir() + "/openvpn.exe";
    return QFileInfo::exists(p) ? p : QString();
}

QString OpenVpnRunner::wintunPath()
{
    return bundleDir() + "/wintun.dll";
}

bool OpenVpnRunner::isAvailable(QString* missing)
{
    const bool hasExe = !exePath().isEmpty();
    const bool hasWintun = QFileInfo::exists(wintunPath());
    if (missing)
    {
        if (!hasExe && !hasWintun) *missing = "openvpn.exe và wintun.dll";
        else if (!hasExe) *missing = "openvpn.exe";
        else if (!hasWintun) *missing = "wintun.dll";
    }
    return hasExe && hasWintun;
}

QString OpenVpnRunner::Internal::buildConfig(const QString& ovpnConfig, quint16 mgmtPort, const QString& authFileName,
                                             const QString& mgmtPwFileName)
{
    QStringList out;
    bool hadAuthUserPass = false;
    const QStringList lines = ovpnConfig.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    for (const QString& raw : lines)
    {
        const QString t = raw.trimmed();
        const QString lower = t.toLower();
        // Bỏ các chỉ thị sẽ tự thêm lại / tương tác / không dùng được khi chạy nền:
        if (lower.startsWith("auth-user-pass")) { hadAuthUserPass = true; continue; } // thay bằng tệp NẾU cần
        if (lower.startsWith("management")) continue;            // ta tự đặt management
        if (lower.startsWith("dev-node")) continue;
        if (lower.startsWith("windows-driver")) continue;        // ta ép wintun
        if (lower.startsWith("block-outside-dns")) continue;     // cần quyền/registry, bỏ cho chắc
        out << raw;
    }
    // CHỈ thêm auth-user-pass nếu cấu hình GỐC có (server yêu cầu user/pass). Cấu hình OpenVPN chuẩn của VPN
    // Gate xác thực bằng CHỨNG CHỈ client NHÚNG SẴN (<cert>/<key>) và KHÔNG có auth-user-pass - thêm thừa sẽ
    // khiến openvpn gửi thông tin đăng nhập mà server không chờ, có thể làm hỏng bắt tay.
    if (hadAuthUserPass)
        out << QString("auth-user-pass %1").arg(authFileName);
    out << QString("management 127.0.0.1 %1 %2").arg(mgmtPort).arg(mgmtPwFileName);
    out << "management-hold";       // chờ lệnh "hold release" rồi mới quay số -> đảm bảo ta kịp nối management
    out << "windows-driver wintun"; // dùng wintun.dll đóng gói kèm (không cần cài TAP riêng)
    out << "auth-nocache";
    out << "";
    return out.join("\n");
}

QString OpenVpnRunner::Internal::stateName(const QString& line)
{
    // ">STATE:<time>,<NAME>,<desc>,..." - lấy trường thứ 2.
    if (!line.startsWith(">STATE:"))
        return {};
    const QStringList parts = line.mid(7).split(QChar(','));
    return parts.size() >= 2 ? parts[1].trimmed() : QString();
}

OpenVpnRunner::OpenVpnRunner(QObject* parent) : QObject(parent) {}

OpenVpnRunner::~OpenVpnRunner()
{
    disconnectVpn();
    cleanup();
}

void OpenVpnRunner::connectConfig(const QString& ovpnConfig, const QString& user, const QString& pass)
{
    if (m_active)
        return;
    QString missing;
    if (!isAvailable(&missing))
    {
        fail("Thiếu " + missing + " trong thư mục \"openvpn\" cạnh file chạy.");
        return;
    }
    if (!m_dir.isValid())
    {
        fail("Không tạo được thư mục tạm cho OpenVPN.");
        return;
    }
    m_port = freeLocalPort();
    if (m_port == 0)
    {
        fail("Không xin được cổng cục bộ cho management OpenVPN.");
        return;
    }

    // Mật khẩu management ngẫu nhiên: chỉ tiến trình biết mới điều khiển được openvpn nâng quyền (dù cổng
    // chỉ ở 127.0.0.1).
    m_mgmtPassword = QString::number(QRandomGenerator::system()->generate64(), 16);

    const QString dir = m_dir.path();
    auto write = [&](const QString& name, const QString& content) {
        QFile f(dir + "/" + name);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(content.toUtf8());
        return true;
    };
    if (!write("auth.txt", user + "\n" + pass + "\n") || !write("mgmt.txt", m_mgmtPassword + "\n") ||
        !write("config.ovpn", Internal::buildConfig(ovpnConfig, m_port, "auth.txt", "mgmt.txt")))
    {
        fail("Không ghi được tệp cấu hình tạm cho OpenVPN.");
        return;
    }

#ifdef Q_OS_WIN
    // openvpn.exe cần quyền Administrator (tạo adapter wintun) -> chạy qua UAC.
    const std::wstring exeW = QDir::toNativeSeparators(exePath()).toStdWString();
    const QString args = "--config config.ovpn --log openvpn.log --verb 3";
    const std::wstring argsW = args.toStdWString();
    const std::wstring dirW = QDir::toNativeSeparators(dir).toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exeW.c_str();
    info.lpParameters = argsW.c_str();
    info.lpDirectory = dirW.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info))
    {
        const DWORD e = GetLastError();
        fail(e == ERROR_CANCELLED ? "Bạn đã từ chối cấp quyền Administrator (UAC)."
                                  : QString("Không chạy được openvpn.exe (mã lỗi %1).").arg(e));
        return;
    }
    m_process = info.hProcess;
#else
    fail("Chỉ hỗ trợ trên Windows.");
    return;
#endif

    m_active = true;
    m_authSent = false;
    m_everConnected = false;
    m_mgmtBuf.clear();
    emit stateChanged("⏳ Đang khởi động OpenVPN...");

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        if (!m_everConnected)
            fail("Hết thời gian chờ OpenVPN kết nối (60 giây). Thử máy chủ khác.");
    });
    m_timeout->start(60000);

    // openvpn mở cổng management sau ~1-2s - thử nối lại nhiều lần trong ~15s.
    auto tryConnect = std::make_shared<std::function<void(int)>>();
    *tryConnect = [this, tryConnect](int attempt) {
        if (!m_active)
            return;
        auto* sock = new QTcpSocket(this);
        m_mgmt = sock;
        connect(sock, &QTcpSocket::readyRead, this, &OpenVpnRunner::onMgmtReadyRead);
        connect(sock, &QTcpSocket::errorOccurred, this, [this, sock, attempt, tryConnect](QAbstractSocket::SocketError) {
            if (!m_active || m_mgmt != sock)
                return;
            sock->deleteLater();
            m_mgmt = nullptr;
            if (attempt < 30)
                QTimer::singleShot(500, this, [tryConnect, attempt]() { (*tryConnect)(attempt + 1); });
            else
                fail("Không nối được tới management của OpenVPN (openvpn.exe không khởi động được?).");
        });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
            if (m_mgmt != sock)
                return;
            // Management đóng = openvpn đã thoát.
            if (m_everConnected)
            {
                m_active = false;
                emit disconnected();
            }
            else if (m_active)
                fail("OpenVPN đã thoát trước khi kết nối (xem máy chủ không nhận / cấu hình lỗi).");
        });
        sock->connectToHost(QHostAddress::LocalHost, m_port);
    };
    (*tryConnect)(0);
}

void OpenVpnRunner::onMgmtReadyRead()
{
    if (!m_mgmt)
        return;
    m_mgmtBuf += m_mgmt->readAll();

    // Lời nhắc mật khẩu "ENTER PASSWORD:" KHÔNG có ký tự xuống dòng nên không đọc theo dòng được - dò thẳng
    // trong bộ đệm.
    if (!m_authSent && m_mgmtBuf.contains("ENTER PASSWORD"))
    {
        m_mgmt->write((m_mgmtPassword + "\n").toUtf8());
        m_authSent = true;
    }

    // Xử lý các dòng ĐÃ TRỌN (kết thúc bằng \n) - mọi thông điệp >INFO/SUCCESS/>STATE/>FATAL đều có \n.
    int nl;
    while ((nl = m_mgmtBuf.indexOf('\n')) >= 0)
    {
        const QString line = QString::fromUtf8(m_mgmtBuf.left(nl)).trimmed();
        m_mgmtBuf.remove(0, nl + 1);
        if (line.isEmpty())
            continue;

        if (line.startsWith("SUCCESS: password"))
        {
            // Đã qua xác thực management: bật thông báo trạng thái rồi thả "hold" để openvpn quay số.
            m_mgmt->write("state on\n");
            m_mgmt->write("hold release\n");
            continue;
        }
        if (line.startsWith(">PASSWORD:Verification Failed") || line.contains("auth-failure"))
        {
            fail("Máy chủ từ chối tài khoản (VPN Gate dùng vpn/vpn - có thể máy chủ này đang quá tải/hỏng).");
            return;
        }
        if (line.startsWith(">FATAL:"))
        {
            fail("OpenVPN lỗi: " + line.mid(7).trimmed());
            return;
        }

        const QString st = Internal::stateName(line);
        if (!st.isEmpty())
        {
            if (st == "CONNECTED")
            {
                if (m_timeout)
                    m_timeout->stop();
                m_everConnected = true;
                emit stateChanged("✓ OpenVPN đã kết nối.");
                emit connected();
            }
            else if (st == "EXITING")
            {
                // xử lý ở disconnected() của socket
            }
            else
            {
                static const QHash<QString, QString> human = {
                    {"CONNECTING", "⏳ Đang kết nối tới máy chủ..."}, {"WAIT", "⏳ Đang chờ máy chủ phản hồi..."},
                    {"AUTH", "⏳ Đang xác thực..."},                   {"GET_CONFIG", "⏳ Đang nhận cấu hình..."},
                    {"ASSIGN_IP", "⏳ Đang gán địa chỉ IP..."},        {"ADD_ROUTES", "⏳ Đang thêm tuyến..."},
                    {"RECONNECTING", "⏳ Đang kết nối lại..."}};
                emit stateChanged(human.value(st, "⏳ " + st));
            }
        }
    }
}

void OpenVpnRunner::disconnectVpn()
{
    if (m_mgmt && m_mgmt->state() == QAbstractSocket::ConnectedState)
    {
        m_mgmt->write("signal SIGTERM\n");
        m_mgmt->flush();
        m_mgmt->waitForBytesWritten(1000);
    }
    m_active = false;
    cleanup();
}

void OpenVpnRunner::fail(const QString& reason)
{
    const bool wasActive = m_active;
    m_active = false;
    cleanup();
    if (wasActive || !m_everConnected)
        emit failed(reason);
}

void OpenVpnRunner::cleanup()
{
    if (m_timeout)
    {
        m_timeout->stop();
        m_timeout->deleteLater();
        m_timeout = nullptr;
    }
    if (m_mgmt)
    {
        m_mgmt->disconnect(this);
        m_mgmt->abort();
        m_mgmt->deleteLater();
        m_mgmt = nullptr;
    }
#ifdef Q_OS_WIN
    if (m_process)
    {
        CloseHandle(static_cast<HANDLE>(m_process));
        m_process = nullptr;
    }
#endif
}
