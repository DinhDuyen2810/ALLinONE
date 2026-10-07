#pragma once

#include <QString>

/// Một liên kết media/tài liệu tìm thấy khi quét HTML một trang web (PageMediaScanner) - struct thuần.
struct MediaLink
{
    enum class Type { Image, Video, Audio, Document, Other };

    QString url;    ///< Đã phân giải thành URL tuyệt đối (resolved theo URL trang gốc)
    Type type{Type::Other};

    QString fileName() const
    {
        const QString path = url.section('?', 0, 0).section('#', 0, 0);
        const int slash = path.lastIndexOf('/');
        QString name = slash >= 0 ? path.mid(slash + 1) : path;
        return name.isEmpty() ? "tep-khong-ten" : name;
    }

    static QString typeLabel(Type t)
    {
        switch (t)
        {
            case Type::Image: return "Ảnh";
            case Type::Video: return "Video";
            case Type::Audio: return "Âm thanh";
            case Type::Document: return "Tài liệu";
            case Type::Other: return "Khác";
        }
        return {};
    }
};
