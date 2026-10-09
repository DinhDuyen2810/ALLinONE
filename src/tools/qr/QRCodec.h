#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPolygon>
#include <QString>
#include <vector>

/**
 * @brief Lớp bọc mã hóa/giải mã QR, không phụ thuộc widget.
 *  - Mã hóa: Nayuki QR Code generator (MIT) - src/third_party/qrcodegen
 *  - Giải mã: quirc (ISC)                    - src/third_party/quirc
 */

enum class QREcc
{
    Low = 0,       // ~7%
    Medium = 1,    // ~15%
    Quartile = 2,  // ~25%
    High = 3       // ~30%
};

struct QRMatrix
{
    int size{0};
    int version{0};
    QREcc ecc{QREcc::Medium};
    std::vector<bool> modules; // size * size, true = ô tối

    bool isValid() const { return size > 0; }
    bool at(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < size && y < size && modules[static_cast<size_t>(y) * size + x];
    }
};

struct QRStyle
{
    int targetSize{512};       // kích thước ảnh mong muốn (px); thực tế làm tròn xuống bội số của số ô
    int quietZone{4};          // viền trắng (số ô), chuẩn QR là 4
    QColor foreground{Qt::black};
    QColor background{Qt::white};
    bool roundedModules{false};
    QImage logo;               // null = không có logo
    double logoRatio{0.20};    // tỷ lệ cạnh logo / cạnh vùng mã (tối đa 0.30)
};

struct QRDecoded
{
    QString text;
    QString format;       // Numeric / Alphanumeric / Byte / Kanji
    int version{0};
    QREcc ecc{QREcc::Medium};
    QPolygon corners;     // 4 góc trong tọa độ ảnh gốc
};

class QRCodec
{
public:
    /// Mã hóa văn bản (UTF-8). Trả về ma trận không hợp lệ và điền error nếu thất bại.
    static QRMatrix encode(const QString& text, QREcc ecc, QString* error = nullptr);

    /// Số byte tối đa của QR (v40) theo mức sửa lỗi Ở CHẾ ĐỘ BYTE (văn bản thường/UTF-8). Chuỗi chỉ gồm
    /// chữ số hoặc chữ HOA/số được thư viện mã hóa gọn hơn nên encode() chấp nhận dài hơn con số này.
    static int maxBytes(QREcc ecc);

    static QImage render(const QRMatrix& matrix, const QRStyle& style);
    static QString toSvg(const QRMatrix& matrix, const QRStyle& style);

    /// Tìm và giải mã mọi mã QR trong ảnh (thử nhiều lần: gốc, đảo màu, phóng to/thu nhỏ; mã bị lật
    /// gương cũng được thử lại). Dừng ở lượt đầu tiên đọc được HẾT các mã nó nhìn thấy; nếu lượt đó còn
    /// mã chưa đọc được thì chạy thêm các lượt cùng cực tính và gộp kết quả (không trùng nội dung).
    static QList<QRDecoded> decode(const QImage& image);

    /// Độ tương phản WCAG giữa hai màu (1..21). Quét tốt khi >= 3 và nền sáng hơn mã.
    static double contrastRatio(const QColor& a, const QColor& b);

    static QString eccName(QREcc ecc);
};
