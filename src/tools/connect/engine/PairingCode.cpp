#include "PairingCode.h"

#include <QRandomGenerator>
#include <QRegularExpression>

QString PairingCode::generate()
{
    QString code;
    code.reserve(kLength);
    for (int i = 0; i < kLength; ++i)
        code += QChar('0' + QRandomGenerator::system()->bounded(10));
    return code;
}

QString PairingCode::normalize(const QString& input)
{
    QString digits;
    for (const QChar c : input)
        if (c.isDigit())
            digits += c;
    return isValidFormat(digits) ? digits : QString();
}

bool PairingCode::isValidFormat(const QString& normalized)
{
    if (normalized.size() != kLength)
        return false;
    for (const QChar c : normalized)
        if (!c.isDigit())
            return false;
    return true;
}

QString PairingCode::formatForDisplay(const QString& normalized)
{
    if (!isValidFormat(normalized))
        return normalized;
    return normalized.left(3) + " " + normalized.mid(3, 3) + " " + normalized.mid(6, 3);
}
