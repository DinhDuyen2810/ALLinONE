#include "CredentialStore.h"

#include <windows.h>
#include <wincred.h>

#include <cstring>
#include <string>

namespace CredentialStore
{
namespace
{
QString g_prefix = QStringLiteral("OneForAll/VPN/");
} // namespace

void setTargetPrefixForTest(const QString& prefix)
{
    g_prefix = prefix;
}

QString targetName(const QString& server)
{
    return g_prefix + server.trimmed().toLower();
}

bool save(const QString& server, const QString& username, const QString& password, QString* error)
{
    if (server.trimmed().isEmpty() || username.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Thiếu máy chủ hoặc tên đăng nhập.");
        return false;
    }
    std::wstring target = targetName(server).toStdWString();
    std::wstring user = username.toStdWString();
    std::wstring pass = password.toStdWString();

    CREDENTIALW cred;
    std::memset(&cred, 0, sizeof(cred));
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = target.data();
    cred.UserName = user.data();
    cred.CredentialBlobSize = static_cast<DWORD>(pass.size() * sizeof(wchar_t));
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(pass.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE; // theo tài khoản Windows trên máy này (không đồng bộ lên mạng)
    std::wstring comment = L"One for ALL - tài khoản VPN kết nối trực tiếp";
    cred.Comment = comment.data();
    const BOOL ok = CredWriteW(&cred, 0);
    const DWORD e = GetLastError();
    SecureZeroMemory(pass.data(), pass.size() * sizeof(wchar_t));
    if (!ok)
    {
        if (error)
            *error = QStringLiteral("Không lưu được vào Windows Credential Manager (mã %1).").arg(e);
        return false;
    }
    return true;
}

bool load(const QString& server, QString* username, QString* password)
{
    const std::wstring target = targetName(server).toStdWString();
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || !cred)
        return false;
    if (username)
        *username = cred->UserName ? QString::fromWCharArray(cred->UserName) : QString();
    if (password)
        *password = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(cred->CredentialBlob),
                                            static_cast<int>(cred->CredentialBlobSize / sizeof(wchar_t)));
    if (cred->CredentialBlob && cred->CredentialBlobSize)
        SecureZeroMemory(cred->CredentialBlob, cred->CredentialBlobSize);
    CredFree(cred);
    return !username || !username->isEmpty();
}

bool has(const QString& server)
{
    return load(server, nullptr, nullptr);
}

void remove(const QString& server)
{
    const std::wstring target = targetName(server).toStdWString();
    CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
}
} // namespace CredentialStore
